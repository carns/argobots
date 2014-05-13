/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil ; -*- */
/*
 * See COPYRIGHT in top-level directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "abt.h"

#define DEFAULT_NUM_STREAMS     2
#define DEFAULT_NUM_THREADS     2
#define DEFAULT_NUM_TASKS       2

typedef struct {
    size_t num;
    unsigned long long result;
} task_arg_t;

typedef struct thread_arg {
    int id;
    int num_threads;
    ABT_Thread *threads;
} thread_arg_t;

ABT_Thread pick_one(ABT_Thread *threads, int num_threads)
{
    int i;
    ABT_Thread next;
    do {
        i = rand() % num_threads;
        next = threads[i];
    } while (ABT_Thread_get_state(next) == ABT_THREAD_STATE_TERMINATED);
    return next;
}

void thread_func(void *arg)
{
    thread_arg_t *t_arg = (thread_arg_t *)arg;
    ABT_Thread next;

    printf("[T%d]: brefore yield\n", t_arg->id); fflush(stdout);
    next = pick_one(t_arg->threads, t_arg->num_threads);
    ABT_Thread_yield_to(next);

    printf("[T%d]: doing something ...\n", t_arg->id); fflush(stdout);
    next = pick_one(t_arg->threads, t_arg->num_threads);
    ABT_Thread_yield_to(next);

    printf("[T%d]: after yield\n", t_arg->id); fflush(stdout);
}

void task_func1(void *arg)
{
    int i;
    size_t num = (size_t)arg;
    unsigned long long result = 1;
    for (i = 2; i <= num; i++) {
        result += i;
    }
    printf("task_func1: num=%lu result=%llu\n", num, result);
}

void task_func2(void *arg)
{
    size_t i;
    task_arg_t *my_arg = (task_arg_t *)arg;
    unsigned long long result = 1;
    for (i = 2; i <= my_arg->num; i++) {
        result += i;
    }
    my_arg->result = result;
}

int main(int argc, char *argv[])
{
    int i, j, ret;
    int num_streams = DEFAULT_NUM_STREAMS;
    int num_threads = DEFAULT_NUM_THREADS;
    int num_tasks = DEFAULT_NUM_TASKS;
    if (argc > 1) num_streams = atoi(argv[1]);
    assert(num_streams >= 0);
    if (argc > 2) num_threads = atoi(argv[2]);
    assert(num_threads >= 0);
    if (argc > 3) num_tasks = atoi(argv[3]);
    assert(num_tasks >= 0);

    ABT_Stream *streams;
    ABT_Thread **threads;
    thread_arg_t **thread_args;
    ABT_Task *tasks;
    task_arg_t *task_args;

    streams = (ABT_Stream *)malloc(sizeof(ABT_Stream) * num_streams);
    threads = (ABT_Thread **)malloc(sizeof(ABT_Thread *) * num_streams);
    thread_args = (thread_arg_t **)malloc(sizeof(thread_arg_t*) * num_streams);
    for (i = 0; i < num_streams; i++) {
        threads[i] = (ABT_Thread *)malloc(sizeof(ABT_Thread) * num_threads);
        thread_args[i] = (thread_arg_t *)malloc(sizeof(thread_arg_t) *
                                                num_threads);
    }
    tasks = (ABT_Task *)malloc(sizeof(ABT_Task) * num_tasks);
    task_args = (task_arg_t *)malloc(sizeof(task_arg_t) * num_tasks);

    /* Initialize */
    ret = ABT_Init(argc, argv);
    if (ret != ABT_SUCCESS) {
        fprintf(stderr, "ERROR: ABT_Init\n");
        exit(EXIT_FAILURE);
    }

    /* Create streams */
    streams[0] = ABT_Stream_self();
    for (i = 1; i < num_streams; i++) {
        ret = ABT_Stream_create(ABT_SCHEDULER_NULL, &streams[i]);
        if (ret != ABT_SUCCESS) {
            fprintf(stderr, "ERROR: ABT_Stream_create for ES%d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* Create threads */
    for (i = 0; i < num_streams; i++) {
        for (j = 0; j < num_threads; j++) {
            int tid = i * num_threads + j;
            thread_args[i][j].id = tid;
            thread_args[i][j].num_threads = num_threads;
            thread_args[i][j].threads = &threads[i][0];
            ret = ABT_Thread_create(streams[i],
                    thread_func, (void *)&thread_args[i][j], 0,
                    &threads[i][j]);
            if (ret != ABT_SUCCESS) {
                fprintf(stderr, "ERROR: ABT_Thread_create for ES%d-LUT%d\n",
                        i, j);
                exit(EXIT_FAILURE);
            }
        }
    }

    /* Create tasks with task_func1 */
    for (i = 0; i < num_tasks; i++) {
        size_t num = 100 + i;
        ret = ABT_Task_create(ABT_STREAM_NULL,
                              task_func1, (void *)num,
                              NULL);
        if (ret != ABT_SUCCESS) {
            fprintf(stderr, "ERROR: ABT_Task_create for %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* Create tasks with task_func2 */
    for (i = 0; i < num_tasks; i++) {
        task_args[i].num = 100 + i;
        ret = ABT_Task_create(streams[i % num_streams],
                              task_func2, (void *)&task_args[i],
                              &tasks[i]);
        if (ret != ABT_SUCCESS) {
            fprintf(stderr, "ERROR: ABT_Task_create for %d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* Switch to other work units */
    ABT_Thread_yield();

    /* Results of task_funcs2 */
    for (i = 0; i < num_tasks; i++) {
        while (ABT_Task_get_state(tasks[i]) != ABT_TASK_STATE_COMPLETED)
            ABT_Thread_yield();

        printf("task_func2: num=%lu result=%llu\n",
               task_args[i].num, task_args[i].result);

        /* Free named tasks */
        ret = ABT_Task_free(tasks[i]);
        if (ret != ABT_SUCCESS) {
            fprintf(stderr, "ERROR: ABT_Task_free for TK%d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* Join streams */
    for (i = 1; i < num_streams; i++) {
        ret = ABT_Stream_join(streams[i]);
        if (ret != ABT_SUCCESS) {
            fprintf(stderr, "ERROR: ABT_Stream_join for ES%d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* Free streams */
    for (i = 0; i < num_streams; i++) {
        /* Free threads */
        for (j = 0; j < num_threads; j++) {
            ret = ABT_Thread_free(threads[i][j]);
            if (ret != ABT_SUCCESS) {
                fprintf(stderr, "ERROR: ABT_Thread_free for ES%d-LUT%d\n",
                        i, j);
                exit(EXIT_FAILURE);
            }
        }

        ret = ABT_Stream_free(streams[i]);
        if (ret != ABT_SUCCESS) {
            fprintf(stderr, "ERROR: ABT_Stream_free for ES%d\n", i);
            exit(EXIT_FAILURE);
        }
    }

    /* Finalize */
    ABT_Finalize();

    for (i = 0; i < num_streams; i++) {
        free(thread_args[i]);
        free(threads[i]);
    }
    free(thread_args);
    free(threads);
    free(task_args);
    free(tasks);
    free(streams);

    return EXIT_SUCCESS;
}

