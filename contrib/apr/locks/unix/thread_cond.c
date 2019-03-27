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

#if APR_HAS_THREADS

#include "apr_arch_thread_mutex.h"
#include "apr_arch_thread_cond.h"

static apr_status_t thread_cond_cleanup(void *data)
{
    apr_thread_cond_t *cond = (apr_thread_cond_t *)data;
    apr_status_t rv;

    rv = pthread_cond_destroy(&cond->cond);
#ifdef HAVE_ZOS_PTHREADS
    if (rv) {
        rv = errno;
    }
#endif
    return rv;
} 

APR_DECLARE(apr_status_t) apr_thread_cond_create(apr_thread_cond_t **cond,
                                                 apr_pool_t *pool)
{
    apr_thread_cond_t *new_cond;
    apr_status_t rv;

    new_cond = apr_palloc(pool, sizeof(apr_thread_cond_t));

    new_cond->pool = pool;

    if ((rv = pthread_cond_init(&new_cond->cond, NULL))) {
#ifdef HAVE_ZOS_PTHREADS
        rv = errno;
#endif
        return rv;
    }

    apr_pool_cleanup_register(new_cond->pool,
                              (void *)new_cond, thread_cond_cleanup,
                              apr_pool_cleanup_null);

    *cond = new_cond;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_cond_wait(apr_thread_cond_t *cond,
                                               apr_thread_mutex_t *mutex)
{
    apr_status_t rv;

    rv = pthread_cond_wait(&cond->cond, &mutex->mutex);
#ifdef HAVE_ZOS_PTHREADS
    if (rv) {
        rv = errno;
    }
#endif
    return rv;
}

APR_DECLARE(apr_status_t) apr_thread_cond_timedwait(apr_thread_cond_t *cond,
                                                    apr_thread_mutex_t *mutex,
                                                    apr_interval_time_t timeout)
{
    apr_status_t rv;
    apr_time_t then;
    struct timespec abstime;

    then = apr_time_now() + timeout;
    abstime.tv_sec = apr_time_sec(then);
    abstime.tv_nsec = apr_time_usec(then) * 1000; /* nanoseconds */

    rv = pthread_cond_timedwait(&cond->cond, &mutex->mutex, &abstime);
#ifdef HAVE_ZOS_PTHREADS
    if (rv) {
        rv = errno;
    }
#endif
    if (ETIMEDOUT == rv) {
        return APR_TIMEUP;
    }
    return rv;
}


APR_DECLARE(apr_status_t) apr_thread_cond_signal(apr_thread_cond_t *cond)
{
    apr_status_t rv;

    rv = pthread_cond_signal(&cond->cond);
#ifdef HAVE_ZOS_PTHREADS
    if (rv) {
        rv = errno;
    }
#endif
    return rv;
}

APR_DECLARE(apr_status_t) apr_thread_cond_broadcast(apr_thread_cond_t *cond)
{
    apr_status_t rv;

    rv = pthread_cond_broadcast(&cond->cond);
#ifdef HAVE_ZOS_PTHREADS
    if (rv) {
        rv = errno;
    }
#endif
    return rv;
}

APR_DECLARE(apr_status_t) apr_thread_cond_destroy(apr_thread_cond_t *cond)
{
    return apr_pool_cleanup_run(cond->pool, cond, thread_cond_cleanup);
}

APR_POOL_IMPLEMENT_ACCESSOR(thread_cond)

#endif /* APR_HAS_THREADS */
