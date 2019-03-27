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

#ifndef THREAD_MUTEX_H
#define THREAD_MUTEX_H

#include "apr.h"
#include "apr_private.h"
#include "apr_general.h"
#include "apr_thread_mutex.h"
#include "apr_portable.h"
#include "apr_atomic.h"

#if APR_HAVE_PTHREAD_H
#include <pthread.h>
#endif

#if APR_HAS_THREADS
struct apr_thread_mutex_t {
    apr_pool_t *pool;
    pthread_mutex_t mutex;
};
#endif

#endif  /* THREAD_MUTEX_H */

