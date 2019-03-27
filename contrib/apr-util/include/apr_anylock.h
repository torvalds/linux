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

/**
 * @file apr_anylock.h
 * @brief APR-Util transparent any lock flavor wrapper
 */
#ifndef APR_ANYLOCK_H
#define APR_ANYLOCK_H

#include "apr_proc_mutex.h"
#include "apr_thread_mutex.h"
#include "apr_thread_rwlock.h"

/** Structure that may contain any APR lock type */
typedef struct apr_anylock_t {
    /** Indicates what type of lock is in lock */
    enum tm_lock {
        apr_anylock_none,           /**< None */
        apr_anylock_procmutex,      /**< Process-based */
        apr_anylock_threadmutex,    /**< Thread-based */
        apr_anylock_readlock,       /**< Read lock */
        apr_anylock_writelock       /**< Write lock */
    } type;
    /** Union of all possible APR locks */
    union apr_anylock_u_t {
        apr_proc_mutex_t *pm;       /**< Process mutex */
#if APR_HAS_THREADS
        apr_thread_mutex_t *tm;     /**< Thread mutex */
        apr_thread_rwlock_t *rw;    /**< Read-write lock */
#endif
    } lock;
} apr_anylock_t;

#if APR_HAS_THREADS

/** Lock an apr_anylock_t structure */
#define APR_ANYLOCK_LOCK(lck)                \
    (((lck)->type == apr_anylock_none)         \
      ? APR_SUCCESS                              \
      : (((lck)->type == apr_anylock_threadmutex)  \
          ? apr_thread_mutex_lock((lck)->lock.tm)    \
          : (((lck)->type == apr_anylock_procmutex)    \
              ? apr_proc_mutex_lock((lck)->lock.pm)      \
              : (((lck)->type == apr_anylock_readlock)     \
                  ? apr_thread_rwlock_rdlock((lck)->lock.rw) \
                  : (((lck)->type == apr_anylock_writelock)    \
                      ? apr_thread_rwlock_wrlock((lck)->lock.rw) \
                      : APR_EINVAL)))))

#else /* APR_HAS_THREADS */

#define APR_ANYLOCK_LOCK(lck)                \
    (((lck)->type == apr_anylock_none)         \
      ? APR_SUCCESS                              \
          : (((lck)->type == apr_anylock_procmutex)    \
              ? apr_proc_mutex_lock((lck)->lock.pm)      \
                      : APR_EINVAL))

#endif /* APR_HAS_THREADS */

#if APR_HAS_THREADS

/** Try to lock an apr_anylock_t structure */
#define APR_ANYLOCK_TRYLOCK(lck)                \
    (((lck)->type == apr_anylock_none)            \
      ? APR_SUCCESS                                 \
      : (((lck)->type == apr_anylock_threadmutex)     \
          ? apr_thread_mutex_trylock((lck)->lock.tm)    \
          : (((lck)->type == apr_anylock_procmutex)       \
              ? apr_proc_mutex_trylock((lck)->lock.pm)      \
              : (((lck)->type == apr_anylock_readlock)        \
                  ? apr_thread_rwlock_tryrdlock((lck)->lock.rw) \
                  : (((lck)->type == apr_anylock_writelock)       \
                      ? apr_thread_rwlock_trywrlock((lck)->lock.rw) \
                          : APR_EINVAL)))))

#else /* APR_HAS_THREADS */

#define APR_ANYLOCK_TRYLOCK(lck)                \
    (((lck)->type == apr_anylock_none)            \
      ? APR_SUCCESS                                 \
          : (((lck)->type == apr_anylock_procmutex)       \
              ? apr_proc_mutex_trylock((lck)->lock.pm)      \
                          : APR_EINVAL))

#endif /* APR_HAS_THREADS */

#if APR_HAS_THREADS

/** Unlock an apr_anylock_t structure */
#define APR_ANYLOCK_UNLOCK(lck)              \
    (((lck)->type == apr_anylock_none)         \
      ? APR_SUCCESS                              \
      : (((lck)->type == apr_anylock_threadmutex)  \
          ? apr_thread_mutex_unlock((lck)->lock.tm)  \
          : (((lck)->type == apr_anylock_procmutex)    \
              ? apr_proc_mutex_unlock((lck)->lock.pm)    \
              : ((((lck)->type == apr_anylock_readlock) || \
                  ((lck)->type == apr_anylock_writelock))    \
                  ? apr_thread_rwlock_unlock((lck)->lock.rw)   \
                      : APR_EINVAL))))

#else /* APR_HAS_THREADS */

#define APR_ANYLOCK_UNLOCK(lck)              \
    (((lck)->type == apr_anylock_none)         \
      ? APR_SUCCESS                              \
          : (((lck)->type == apr_anylock_procmutex)    \
              ? apr_proc_mutex_unlock((lck)->lock.pm)    \
                      : APR_EINVAL))

#endif /* APR_HAS_THREADS */

#endif /* !APR_ANYLOCK_H */
