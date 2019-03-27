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

#ifndef APR_QUEUE_H
#define APR_QUEUE_H

/**
 * @file apr_queue.h
 * @brief Thread Safe FIFO bounded queue
 * @note Since most implementations of the queue are backed by a condition
 * variable implementation, it isn't available on systems without threads.
 * Although condition variables are sometimes available without threads.
 */

#include "apu.h"
#include "apr_errno.h"
#include "apr_pools.h"

#if APR_HAS_THREADS

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @defgroup APR_Util_FIFO Thread Safe FIFO bounded queue
 * @ingroup APR_Util
 * @{
 */

/**
 * opaque structure
 */
typedef struct apr_queue_t apr_queue_t;

/** 
 * create a FIFO queue
 * @param queue The new queue
 * @param queue_capacity maximum size of the queue
 * @param a pool to allocate queue from
 */
APU_DECLARE(apr_status_t) apr_queue_create(apr_queue_t **queue, 
                                           unsigned int queue_capacity, 
                                           apr_pool_t *a);

/**
 * push/add an object to the queue, blocking if the queue is already full
 *
 * @param queue the queue
 * @param data the data
 * @returns APR_EINTR the blocking was interrupted (try again)
 * @returns APR_EOF the queue has been terminated
 * @returns APR_SUCCESS on a successful push
 */
APU_DECLARE(apr_status_t) apr_queue_push(apr_queue_t *queue, void *data);

/**
 * pop/get an object from the queue, blocking if the queue is already empty
 *
 * @param queue the queue
 * @param data the data
 * @returns APR_EINTR the blocking was interrupted (try again)
 * @returns APR_EOF if the queue has been terminated
 * @returns APR_SUCCESS on a successful pop
 */
APU_DECLARE(apr_status_t) apr_queue_pop(apr_queue_t *queue, void **data);

/**
 * push/add an object to the queue, returning immediately if the queue is full
 *
 * @param queue the queue
 * @param data the data
 * @returns APR_EINTR the blocking operation was interrupted (try again)
 * @returns APR_EAGAIN the queue is full
 * @returns APR_EOF the queue has been terminated
 * @returns APR_SUCCESS on a successful push
 */
APU_DECLARE(apr_status_t) apr_queue_trypush(apr_queue_t *queue, void *data);

/**
 * pop/get an object to the queue, returning immediately if the queue is empty
 *
 * @param queue the queue
 * @param data the data
 * @returns APR_EINTR the blocking operation was interrupted (try again)
 * @returns APR_EAGAIN the queue is empty
 * @returns APR_EOF the queue has been terminated
 * @returns APR_SUCCESS on a successful pop
 */
APU_DECLARE(apr_status_t) apr_queue_trypop(apr_queue_t *queue, void **data);

/**
 * returns the size of the queue.
 *
 * @warning this is not threadsafe, and is intended for reporting/monitoring
 * of the queue.
 * @param queue the queue
 * @returns the size of the queue
 */
APU_DECLARE(unsigned int) apr_queue_size(apr_queue_t *queue);

/**
 * interrupt all the threads blocking on this queue.
 *
 * @param queue the queue
 */
APU_DECLARE(apr_status_t) apr_queue_interrupt_all(apr_queue_t *queue);

/**
 * terminate the queue, sending an interrupt to all the
 * blocking threads
 *
 * @param queue the queue
 */
APU_DECLARE(apr_status_t) apr_queue_term(apr_queue_t *queue);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* APR_HAS_THREADS */

#endif /* APRQUEUE_H */
