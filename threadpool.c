//
// Created by amir on 12/27/22.
//
#include "threadpool.h"
#include <stdio.h>
#include <malloc.h>

threadpool *create_threadpool(int num_threads_in_pool) {
    if (num_threads_in_pool <= 0 || num_threads_in_pool > MAXT_IN_POOL) {
        fprintf(stderr, "num_threads_in_pool error.\n");
        return NULL;
    }

    //allocate memory for the struct pool.
    threadpool *pool = malloc(sizeof(threadpool));
    if (pool == NULL) {
        fprintf(stderr, "Error allocating memory for threadpool.\n");
        return NULL;
    }

    //initialize the pool struct :
    pool->num_threads = num_threads_in_pool;
    pool->qsize = 0;
    pool->dont_accept = 0;
    pool->shutdown = 0;
    pool->qhead = NULL;
    pool->qtail = NULL;

    //initialize the mutex and variables.
    if (pthread_mutex_init(&(pool->qlock), NULL)) {
        fprintf(stderr, "Error initializing mutex.\n");
        free(pool);
        return NULL;
    }
    if (pthread_cond_init(&(pool->q_empty), NULL)) {
        fprintf(stderr, "Error initializing condition variable.\n");
        pthread_mutex_destroy(&(pool->qlock));
        free(pool);
        return NULL;
    }
    if (pthread_cond_init(&(pool->q_not_empty), NULL)) {
        fprintf(stderr, "Error initializing condition variable.\n");
        pthread_cond_destroy(&(pool->q_empty));
        pthread_mutex_destroy(&(pool->qlock));
        free(pool);
        return NULL;
    }

    pool->threads = (pthread_t *) malloc(sizeof(pthread_t) * num_threads_in_pool);
    if (pool->threads == NULL) {
        // if allocation fails, destroy mutex and condition variables and free memory.
        pthread_cond_destroy(&(pool->q_not_empty));
        pthread_cond_destroy(&(pool->q_empty));
        pthread_mutex_destroy(&(pool->qlock));
        free(pool);
        return NULL;
    }
    for (int i = 0; i < num_threads_in_pool; i++) {
        if (pthread_create(&(pool->threads[i]), NULL, do_work, (void *) pool) != 0) {
            // if creation fails, destroy mutex and condition variables, free memory, and return NULL
            pthread_cond_destroy(&(pool->q_not_empty));
            pthread_cond_destroy(&(pool->q_empty));
            pthread_mutex_destroy(&(pool->qlock));
            free(pool->threads);
            free(pool);
            return NULL;
        }
    }
    return pool;

}

void dispatch(threadpool *from_me, dispatch_fn dispatch_to_here, void *arg) {
    // create work_t element.
    work_t *work = (work_t *) malloc(sizeof(work_t));
    work->routine = dispatch_to_here;
    work->arg = arg;
    work->next = NULL;

    //lock mutex.
    if (pthread_mutex_lock(&(from_me->qlock))) {
        fprintf(stderr, "Error locking mutex.\n");
        return;
    }
    if (from_me->dont_accept) {
        printf("Closed\n");
        free(from_me);
        return;
    }

    //add work to queue
    if (from_me->qsize == 0) {
        from_me->qhead = work;
        from_me->qtail = work;
        if (pthread_cond_signal(&(from_me->q_not_empty))) {
            fprintf(stderr, "Error signaling condition variable.\n");
            pthread_mutex_unlock(&(from_me->qlock));
            return;
        }
    } else {
        from_me->qtail->next = work;
        from_me->qtail = work;
    }
    from_me->qsize++;
    //signal
    if (pthread_cond_signal(&(from_me->q_not_empty))) {
        fprintf(stderr, "Error signaling condition variable.\n");
        pthread_mutex_unlock(&(from_me->qlock));
        return;
    }
    if (pthread_mutex_unlock(&(from_me->qlock))) {
        fprintf(stderr, "Error unlocking mutex.\n");
        return;
    }
}

void destroy_threadpool(threadpool* destroyme){

    pthread_mutex_lock(&destroyme->qlock);
    destroyme->dont_accept=1;
    if(destroyme->qsize!=0)
        pthread_cond_wait(&destroyme->q_empty,&destroyme->qlock);

    destroyme->shutdown=1;
    pthread_mutex_unlock(&destroyme->qlock);
    pthread_cond_broadcast(&destroyme->q_not_empty);

    int status;
    for(int i=0;i< destroyme->num_threads; i++) {
        status = pthread_join(destroyme->threads[i],NULL);
        if (status) {
            perror("Error join process");
        }
    }

    free(destroyme->threads);
    free(destroyme);

}
void* do_work(void* p) {
    threadpool *pool = (threadpool *) p;
    work_t *work;
    while (1) {
        //lock mutex
        if (pthread_mutex_lock(&(pool->qlock))) {
            fprintf(stderr, "Error locking mutex.\n");
            return NULL;
        }

        // wait until there is work to do.
        while ((pool->qsize == 0) && !(pool->shutdown)) {
            if (pthread_cond_wait(&(pool->q_not_empty), &(pool->qlock))) {
                fprintf(stderr, "Error waiting on condition variable.\n");
                pthread_mutex_unlock(&(pool->qlock));
                return NULL;
            }
        }

        // If the threadpool is shutting down, exit the loop.
        if (pool->shutdown) {
            if (pthread_mutex_unlock(&(pool->qlock))) {
                fprintf(stderr, "Error unlocking mutex.\n");
                return NULL;
            }
            pthread_exit(NULL);
        }

        // Take the first element from the queue.
        work = pool->qhead;
        pool->qsize--;
        if (pool->qsize == 0) {
            pool->qhead = NULL;
            pool->qtail = NULL;
            if (pthread_cond_signal(&(pool->q_empty))) {
                fprintf(stderr, "Error signaling condition variable.\n");
                pthread_mutex_unlock(&(pool->qlock));
                return NULL;
            }
        } else {
            pool->qhead = work->next;
        }

        // Unlock the mutex.
        if (pthread_mutex_unlock(&(pool->qlock))) {
            fprintf(stderr, "Error unlocking mutex.\n");
            return NULL;
        }

        // Call the thread routine.
        (*(work->routine))(work->arg);
        free(work);
    }
    return NULL;
}
