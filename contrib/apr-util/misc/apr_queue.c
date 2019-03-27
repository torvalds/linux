/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"

#if APR_HAVE_STDIO_H
#include <stdio.h>
#endif
#if APR_HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "apu.h"
#include "apr_portable.h"
#include "apr_thread_mutex.h"
#include "apr_thread_cond.h"
#include "apr_errno.h"
#include "apr_queue.h"

#if APR_HAS_THREADS
/* 
 * define this to get debug messages
 *
#define QUEUE_DEBUG
 */

struct apr_queue_t {
    void              **data;
    unsigned int        nelts; /**< # elements */
    unsigned int        in;    /**< next empty location */
    unsigned int        out;   /**< next filled location */
    unsigned int        bounds;/**< max size of queue */
    unsigned int        full_waiters;
    unsigned int        empty_waiters;
    apr_thread_mutex_t *one_big_mutex;
    apr_thread_cond_t  *not_empty;
    apr_thread_cond_t  *not_full;
    int                 terminated;
};

#ifdef QUEUE_DEBUG
static void Q_DBG(char*msg, apr_queue_t *q) {
    fprintf(stderr, "%ld\t#%d in %d out %d\t%s\n", 
                    apr_os_thread_current(),
                    q->nelts, q->in, q->out,
                    msg
                    );
}
#else
#define Q_DBG(x,y) 
#endif

/**
 * Detects when the apr_queue_t is full. This utility function is expected
 * to be called from within critical sections, and is not threadsafe.
 */
#define apr_queue_full(queue) ((queue)->nelts == (queue)->bounds)

/**
 * Detects when the apr_queue_t is empty. This utility function is expected
 * to be called from within critical sections, and is not threadsafe.
 */
#define apr_queue_empty(queue) ((queue)->nelts == 0)

/**
 * Callback routine that is called to destroy this
 * apr_queue_t when its pool is destroyed.
 */
static apr_status_t queue_destroy(void *data) 
{
    apr_queue_t *queue = data;

    /* Ignore errors here, we can't do anything about them anyway. */

    apr_thread_cond_destroy(queue->not_empty);
    apr_thread_cond_destroy(queue->not_full);
    apr_thread_mutex_destroy(queue->one_big_mutex);

    return APR_SUCCESS;
}

/**
 * Initialize the apr_queue_t.
 */
APU_DECLARE(apr_status_t) apr_queue_create(apr_queue_t **q, 
                                           unsigned int queue_capacity, 
                                           apr_pool_t *a)
{
    apr_status_t rv;
    apr_queue_t *queue;
    queue = apr_palloc(a, sizeof(apr_queue_t));
    *q = queue;

    /* nested doesn't work ;( */
    rv = apr_thread_mutex_create(&queue->one_big_mutex,
                                 APR_THREAD_MUTEX_UNNESTED,
                                 a);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_thread_cond_create(&queue->not_empty, a);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    rv = apr_thread_cond_create(&queue->not_full, a);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    /* Set all the data in the queue to NULL */
    queue->data = apr_pcalloc(a, queue_capacity * sizeof(void*));
    queue->bounds = queue_capacity;
    queue->nelts = 0;
    queue->in = 0;
    queue->out = 0;
    queue->terminated = 0;
    queue->full_waiters = 0;
    queue->empty_waiters = 0;

    apr_pool_cleanup_register(a, queue, queue_destroy, apr_pool_cleanup_null);

    return APR_SUCCESS;
}

/**
 * Push new data onto the queue. Blocks if the queue is full. Once
 * the push operation has completed, it signals other threads waiting
 * in apr_queue_pop() that they may continue consuming sockets.
 */
APU_DECLARE(apr_status_t) apr_queue_push(apr_queue_t *queue, void *data)
{
    apr_status_t rv;

    if (queue->terminated) {
        return APR_EOF; /* no more elements ever again */
    }

    rv = apr_thread_mutex_lock(queue->one_big_mutex);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    if (apr_queue_full(queue)) {
        if (!queue->terminated) {
            queue->full_waiters++;
            rv = apr_thread_cond_wait(queue->not_full, queue->one_big_mutex);
            queue->full_waiters--;
            if (rv != APR_SUCCESS) {
                apr_thread_mutex_unlock(queue->one_big_mutex);
                return rv;
            }
        }
        /* If we wake up and it's still empty, then we were interrupted */
        if (apr_queue_full(queue)) {
            Q_DBG("queue full (intr)", queue);
            rv = apr_thread_mutex_unlock(queue->one_big_mutex);
            if (rv != APR_SUCCESS) {
                return rv;
            }
            if (queue->terminated) {
                return APR_EOF; /* no more elements ever again */
            }
            else {
                return APR_EINTR;
            }
        }
    }

    queue->data[queue->in] = data;
    queue->in++;
    if (queue->in >= queue->bounds)
        queue->in -= queue->bounds;
    queue->nelts++;

    if (queue->empty_waiters) {
        Q_DBG("sig !empty", queue);
        rv = apr_thread_cond_signal(queue->not_empty);
        if (rv != APR_SUCCESS) {
            apr_thread_mutex_unlock(queue->one_big_mutex);
            return rv;
        }
    }

    rv = apr_thread_mutex_unlock(queue->one_big_mutex);
    return rv;
}

/**
 * Push new data onto the queue. If the queue is full, return APR_EAGAIN. If
 * the push operation completes successfully, it signals other threads
 * waiting in apr_queue_pop() that they may continue consuming sockets.
 */
APU_DECLARE(apr_status_t) apr_queue_trypush(apr_queue_t *queue, void *data)
{
    apr_status_t rv;

    if (queue->terminated) {
        return APR_EOF; /* no more elements ever again */
    }

    rv = apr_thread_mutex_lock(queue->one_big_mutex);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    if (apr_queue_full(queue)) {
        rv = apr_thread_mutex_unlock(queue->one_big_mutex);
        return APR_EAGAIN;
    }
    
    queue->data[queue->in] = data;
    queue->in++;
    if (queue->in >= queue->bounds)
        queue->in -= queue->bounds;
    queue->nelts++;

    if (queue->empty_waiters) {
        Q_DBG("sig !empty", queue);
        rv  = apr_thread_cond_signal(queue->not_empty);
        if (rv != APR_SUCCESS) {
            apr_thread_mutex_unlock(queue->one_big_mutex);
            return rv;
        }
    }

    rv = apr_thread_mutex_unlock(queue->one_big_mutex);
    return rv;
}

/**
 * not thread safe
 */
APU_DECLARE(unsigned int) apr_queue_size(apr_queue_t *queue) {
    return queue->nelts;
}

/**
 * Retrieves the next item from the queue. If there are no
 * items available, it will block until one becomes available.
 * Once retrieved, the item is placed into the address specified by
 * 'data'.
 */
APU_DECLARE(apr_status_t) apr_queue_pop(apr_queue_t *queue, void **data)
{
    apr_status_t rv;

    if (queue->terminated) {
        return APR_EOF; /* no more elements ever again */
    }

    rv = apr_thread_mutex_lock(queue->one_big_mutex);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    /* Keep waiting until we wake up and find that the queue is not empty. */
    if (apr_queue_empty(queue)) {
        if (!queue->terminated) {
            queue->empty_waiters++;
            rv = apr_thread_cond_wait(queue->not_empty, queue->one_big_mutex);
            queue->empty_waiters--;
            if (rv != APR_SUCCESS) {
                apr_thread_mutex_unlock(queue->one_big_mutex);
                return rv;
            }
        }
        /* If we wake up and it's still empty, then we were interrupted */
        if (apr_queue_empty(queue)) {
            Q_DBG("queue empty (intr)", queue);
            rv = apr_thread_mutex_unlock(queue->one_big_mutex);
            if (rv != APR_SUCCESS) {
                return rv;
            }
            if (queue->terminated) {
                return APR_EOF; /* no more elements ever again */
            }
            else {
                return APR_EINTR;
            }
        }
    } 

    *data = queue->data[queue->out];
    queue->nelts--;

    queue->out++;
    if (queue->out >= queue->bounds)
        queue->out -= queue->bounds;
    if (queue->full_waiters) {
        Q_DBG("signal !full", queue);
        rv = apr_thread_cond_signal(queue->not_full);
        if (rv != APR_SUCCESS) {
            apr_thread_mutex_unlock(queue->one_big_mutex);
            return rv;
        }
    }

    rv = apr_thread_mutex_unlock(queue->one_big_mutex);
    return rv;
}

/**
 * Retrieves the next item from the queue. If there are no
 * items available, return APR_EAGAIN.  Once retrieved,
 * the item is placed into the address specified by 'data'.
 */
APU_DECLARE(apr_status_t) apr_queue_trypop(apr_queue_t *queue, void **data)
{
    apr_status_t rv;

    if (queue->terminated) {
        return APR_EOF; /* no more elements ever again */
    }

    rv = apr_thread_mutex_lock(queue->one_big_mutex);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    if (apr_queue_empty(queue)) {
        rv = apr_thread_mutex_unlock(queue->one_big_mutex);
        return APR_EAGAIN;
    } 

    *data = queue->data[queue->out];
    queue->nelts--;

    queue->out++;
    if (queue->out >= queue->bounds)
        queue->out -= queue->bounds;
    if (queue->full_waiters) {
        Q_DBG("signal !full", queue);
        rv = apr_thread_cond_signal(queue->not_full);
        if (rv != APR_SUCCESS) {
            apr_thread_mutex_unlock(queue->one_big_mutex);
            return rv;
        }
    }

    rv = apr_thread_mutex_unlock(queue->one_big_mutex);
    return rv;
}

APU_DECLARE(apr_status_t) apr_queue_interrupt_all(apr_queue_t *queue)
{
    apr_status_t rv;
    Q_DBG("intr all", queue);    
    if ((rv = apr_thread_mutex_lock(queue->one_big_mutex)) != APR_SUCCESS) {
        return rv;
    }
    apr_thread_cond_broadcast(queue->not_empty);
    apr_thread_cond_broadcast(queue->not_full);

    if ((rv = apr_thread_mutex_unlock(queue->one_big_mutex)) != APR_SUCCESS) {
        return rv;
    }

    return APR_SUCCESS;
}

APU_DECLARE(apr_status_t) apr_queue_term(apr_queue_t *queue)
{
    apr_status_t rv;

    if ((rv = apr_thread_mutex_lock(queue->one_big_mutex)) != APR_SUCCESS) {
        return rv;
    }

    /* we must hold one_big_mutex when setting this... otherwise,
     * we could end up setting it and waking everybody up just after a 
     * would-be popper checks it but right before they block
     */
    queue->terminated = 1;
    if ((rv = apr_thread_mutex_unlock(queue->one_big_mutex)) != APR_SUCCESS) {
        return rv;
    }
    return apr_queue_interrupt_all(queue);
}

#endif /* APR_HAS_THREADS */
