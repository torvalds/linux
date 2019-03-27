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

#ifndef APR_THREAD_RWLOCK_H
#define APR_THREAD_RWLOCK_H

/**
 * @file apr_thread_rwlock.h
 * @brief APR Reader/Writer Lock Routines
 */

#include "apr.h"
#include "apr_pools.h"
#include "apr_errno.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if APR_HAS_THREADS

/**
 * @defgroup apr_thread_rwlock Reader/Writer Lock Routines
 * @ingroup APR 
 * @{
 */

/** Opaque read-write thread-safe lock. */
typedef struct apr_thread_rwlock_t apr_thread_rwlock_t;

/**
 * Note: The following operations have undefined results: unlocking a
 * read-write lock which is not locked in the calling thread; write
 * locking a read-write lock which is already locked by the calling
 * thread; destroying a read-write lock more than once; clearing or
 * destroying the pool from which a <b>locked</b> read-write lock is
 * allocated.
 */

/**
 * Create and initialize a read-write lock that can be used to synchronize
 * threads.
 * @param rwlock the memory address where the newly created readwrite lock
 *        will be stored.
 * @param pool the pool from which to allocate the mutex.
 */
APR_DECLARE(apr_status_t) apr_thread_rwlock_create(apr_thread_rwlock_t **rwlock,
                                                   apr_pool_t *pool);
/**
 * Acquire a shared-read lock on the given read-write lock. This will allow
 * multiple threads to enter the same critical section while they have acquired
 * the read lock.
 * @param rwlock the read-write lock on which to acquire the shared read.
 */
APR_DECLARE(apr_status_t) apr_thread_rwlock_rdlock(apr_thread_rwlock_t *rwlock);

/**
 * Attempt to acquire the shared-read lock on the given read-write lock. This
 * is the same as apr_thread_rwlock_rdlock(), only that the function fails
 * if there is another thread holding the write lock, or if there are any
 * write threads blocking on the lock. If the function fails for this case,
 * APR_EBUSY will be returned. Note: it is important that the
 * APR_STATUS_IS_EBUSY(s) macro be used to determine if the return value was
 * APR_EBUSY, for portability reasons.
 * @param rwlock the rwlock on which to attempt the shared read.
 */
APR_DECLARE(apr_status_t) apr_thread_rwlock_tryrdlock(apr_thread_rwlock_t *rwlock);

/**
 * Acquire an exclusive-write lock on the given read-write lock. This will
 * allow only one single thread to enter the critical sections. If there
 * are any threads currently holding the read-lock, this thread is put to
 * sleep until it can have exclusive access to the lock.
 * @param rwlock the read-write lock on which to acquire the exclusive write.
 */
APR_DECLARE(apr_status_t) apr_thread_rwlock_wrlock(apr_thread_rwlock_t *rwlock);

/**
 * Attempt to acquire the exclusive-write lock on the given read-write lock. 
 * This is the same as apr_thread_rwlock_wrlock(), only that the function fails
 * if there is any other thread holding the lock (for reading or writing),
 * in which case the function will return APR_EBUSY. Note: it is important
 * that the APR_STATUS_IS_EBUSY(s) macro be used to determine if the return
 * value was APR_EBUSY, for portability reasons.
 * @param rwlock the rwlock on which to attempt the exclusive write.
 */
APR_DECLARE(apr_status_t) apr_thread_rwlock_trywrlock(apr_thread_rwlock_t *rwlock);

/**
 * Release either the read or write lock currently held by the calling thread
 * associated with the given read-write lock.
 * @param rwlock the read-write lock to be released (unlocked).
 */
APR_DECLARE(apr_status_t) apr_thread_rwlock_unlock(apr_thread_rwlock_t *rwlock);

/**
 * Destroy the read-write lock and free the associated memory.
 * @param rwlock the rwlock to destroy.
 */
APR_DECLARE(apr_status_t) apr_thread_rwlock_destroy(apr_thread_rwlock_t *rwlock);

/**
 * Get the pool used by this thread_rwlock.
 * @return apr_pool_t the pool
 */
APR_POOL_DECLARE_ACCESSOR(thread_rwlock);

#endif  /* APR_HAS_THREADS */

/** @} */

#ifdef __cplusplus
}
#endif

#endif  /* ! APR_THREAD_RWLOCK_H */
