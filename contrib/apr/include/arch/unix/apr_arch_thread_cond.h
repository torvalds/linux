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

#ifndef THREAD_COND_H
#define THREAD_COND_H

#include "apr.h"
#include "apr_private.h"
#include "apr_general.h"
#include "apr_lib.h"
#include "apr_thread_mutex.h"
#include "apr_thread_cond.h"
#include "apr_pools.h"

#if APR_HAVE_PTHREAD_H
#include <pthread.h>
#endif

/* XXX: Should we have a better autoconf search, something like
 * APR_HAS_PTHREAD_COND? -aaron */
#if APR_HAS_THREADS
struct apr_thread_cond_t {
    apr_pool_t *pool;
    pthread_cond_t cond;
};
#endif

#endif  /* THREAD_COND_H */

