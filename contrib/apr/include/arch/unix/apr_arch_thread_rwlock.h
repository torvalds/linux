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

#ifndef THREAD_RWLOCK_H
#define THREAD_RWLOCK_H

#include "apr.h"
#include "apr_private.h"
#include "apr_general.h"
#include "apr_thread_rwlock.h"
#include "apr_pools.h"

#if APR_HAVE_PTHREAD_H
/* this gives us pthread_rwlock_t */
#include <pthread.h>
#endif

#if APR_HAS_THREADS
#ifdef HAVE_PTHREAD_RWLOCKS

struct apr_thread_rwlock_t {
    apr_pool_t *pool;
    pthread_rwlock_t rwlock;
};

#else

struct apr_thread_rwlock_t {
    apr_pool_t *pool;
};
#endif

#endif

#endif  /* THREAD_RWLOCK_H */

