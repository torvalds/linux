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
#include "apr_portable.h"
#include "apr_arch_threadproc.h"

#if APR_HAS_THREADS

#if APR_HAVE_PTHREAD_H

/* Destroy the threadattr object */
static apr_status_t threadattr_cleanup(void *data)
{
    apr_threadattr_t *attr = data;
    apr_status_t rv;

    rv = pthread_attr_destroy(&attr->attr);
#ifdef HAVE_ZOS_PTHREADS
    if (rv) {
        rv = errno;
    }
#endif
    return rv;
}

APR_DECLARE(apr_status_t) apr_threadattr_create(apr_threadattr_t **new,
                                                apr_pool_t *pool)
{
    apr_status_t stat;

    (*new) = apr_palloc(pool, sizeof(apr_threadattr_t));
    (*new)->pool = pool;
    stat = pthread_attr_init(&(*new)->attr);

    if (stat == 0) {
        apr_pool_cleanup_register(pool, *new, threadattr_cleanup,
                                  apr_pool_cleanup_null);
        return APR_SUCCESS;
    }
#ifdef HAVE_ZOS_PTHREADS
    stat = errno;
#endif

    return stat;
}

#if defined(PTHREAD_CREATE_DETACHED)
#define DETACH_ARG(v) ((v) ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE)
#else
#define DETACH_ARG(v) ((v) ? 1 : 0)
#endif

APR_DECLARE(apr_status_t) apr_threadattr_detach_set(apr_threadattr_t *attr,
                                                    apr_int32_t on)
{
    apr_status_t stat;
#ifdef HAVE_ZOS_PTHREADS
    int arg = DETACH_ARG(on);

    if ((stat = pthread_attr_setdetachstate(&attr->attr, &arg)) == 0) {
#else
    if ((stat = pthread_attr_setdetachstate(&attr->attr, 
                                            DETACH_ARG(on))) == 0) {
#endif
        return APR_SUCCESS;
    }
    else {
#ifdef HAVE_ZOS_PTHREADS
        stat = errno;
#endif

        return stat;
    }
}

APR_DECLARE(apr_status_t) apr_threadattr_detach_get(apr_threadattr_t *attr)
{
    int state;

#ifdef PTHREAD_ATTR_GETDETACHSTATE_TAKES_ONE_ARG
    state = pthread_attr_getdetachstate(&attr->attr);
#else
    pthread_attr_getdetachstate(&attr->attr, &state);
#endif
    if (state == DETACH_ARG(1))
        return APR_DETACH;
    return APR_NOTDETACH;
}

APR_DECLARE(apr_status_t) apr_threadattr_stacksize_set(apr_threadattr_t *attr,
                                                       apr_size_t stacksize)
{
    int stat;

    stat = pthread_attr_setstacksize(&attr->attr, stacksize);
    if (stat == 0) {
        return APR_SUCCESS;
    }
#ifdef HAVE_ZOS_PTHREADS
    stat = errno;
#endif

    return stat;
}

APR_DECLARE(apr_status_t) apr_threadattr_guardsize_set(apr_threadattr_t *attr,
                                                       apr_size_t size)
{
#ifdef HAVE_PTHREAD_ATTR_SETGUARDSIZE
    apr_status_t rv;

    rv = pthread_attr_setguardsize(&attr->attr, size);
    if (rv == 0) {
        return APR_SUCCESS;
    }
#ifdef HAVE_ZOS_PTHREADS
    rv = errno;
#endif
    return rv;
#else
    return APR_ENOTIMPL;
#endif
}

static void *dummy_worker(void *opaque)
{
    apr_thread_t *thread = (apr_thread_t*)opaque;
    return thread->func(thread, thread->data);
}

APR_DECLARE(apr_status_t) apr_thread_create(apr_thread_t **new,
                                            apr_threadattr_t *attr,
                                            apr_thread_start_t func,
                                            void *data,
                                            apr_pool_t *pool)
{
    apr_status_t stat;
    pthread_attr_t *temp;

    (*new) = (apr_thread_t *)apr_pcalloc(pool, sizeof(apr_thread_t));

    if ((*new) == NULL) {
        return APR_ENOMEM;
    }

    (*new)->td = (pthread_t *)apr_pcalloc(pool, sizeof(pthread_t));

    if ((*new)->td == NULL) {
        return APR_ENOMEM;
    }

    (*new)->data = data;
    (*new)->func = func;

    if (attr)
        temp = &attr->attr;
    else
        temp = NULL;

    stat = apr_pool_create(&(*new)->pool, pool);
    if (stat != APR_SUCCESS) {
        return stat;
    }

    if ((stat = pthread_create((*new)->td, temp, dummy_worker, (*new))) == 0) {
        return APR_SUCCESS;
    }
    else {
#ifdef HAVE_ZOS_PTHREADS
        stat = errno;
#endif

        return stat;
    }
}

APR_DECLARE(apr_os_thread_t) apr_os_thread_current(void)
{
    return pthread_self();
}

APR_DECLARE(int) apr_os_thread_equal(apr_os_thread_t tid1,
                                     apr_os_thread_t tid2)
{
    return pthread_equal(tid1, tid2);
}

APR_DECLARE(apr_status_t) apr_thread_exit(apr_thread_t *thd,
                                          apr_status_t retval)
{
    thd->exitval = retval;
    apr_pool_destroy(thd->pool);
    pthread_exit(NULL);
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_join(apr_status_t *retval,
                                          apr_thread_t *thd)
{
    apr_status_t stat;
    apr_status_t *thread_stat;

    if ((stat = pthread_join(*thd->td,(void *)&thread_stat)) == 0) {
        *retval = thd->exitval;
        return APR_SUCCESS;
    }
    else {
#ifdef HAVE_ZOS_PTHREADS
        stat = errno;
#endif

        return stat;
    }
}

APR_DECLARE(apr_status_t) apr_thread_detach(apr_thread_t *thd)
{
    apr_status_t stat;

#ifdef HAVE_ZOS_PTHREADS
    if ((stat = pthread_detach(thd->td)) == 0) {
#else
    if ((stat = pthread_detach(*thd->td)) == 0) {
#endif

        return APR_SUCCESS;
    }
    else {
#ifdef HAVE_ZOS_PTHREADS
        stat = errno;
#endif

        return stat;
    }
}

APR_DECLARE(void) apr_thread_yield(void)
{
#ifdef HAVE_PTHREAD_YIELD
#ifdef HAVE_ZOS_PTHREADS
    pthread_yield(NULL);
#else
    pthread_yield();
#endif /* HAVE_ZOS_PTHREADS */
#else
#ifdef HAVE_SCHED_YIELD
    sched_yield();
#endif
#endif
}

APR_DECLARE(apr_status_t) apr_thread_data_get(void **data, const char *key,
                                              apr_thread_t *thread)
{
    return apr_pool_userdata_get(data, key, thread->pool);
}

APR_DECLARE(apr_status_t) apr_thread_data_set(void *data, const char *key,
                              apr_status_t (*cleanup)(void *),
                              apr_thread_t *thread)
{
    return apr_pool_userdata_set(data, key, cleanup, thread->pool);
}

APR_DECLARE(apr_status_t) apr_os_thread_get(apr_os_thread_t **thethd,
                                            apr_thread_t *thd)
{
    *thethd = thd->td;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_os_thread_put(apr_thread_t **thd,
                                            apr_os_thread_t *thethd,
                                            apr_pool_t *pool)
{
    if (pool == NULL) {
        return APR_ENOPOOL;
    }

    if ((*thd) == NULL) {
        (*thd) = (apr_thread_t *)apr_pcalloc(pool, sizeof(apr_thread_t));
        (*thd)->pool = pool;
    }

    (*thd)->td = thethd;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_once_init(apr_thread_once_t **control,
                                               apr_pool_t *p)
{
    static const pthread_once_t once_init = PTHREAD_ONCE_INIT;

    *control = apr_palloc(p, sizeof(**control));
    (*control)->once = once_init;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_thread_once(apr_thread_once_t *control,
                                          void (*func)(void))
{
    return pthread_once(&control->once, func);
}

APR_POOL_IMPLEMENT_ACCESSOR(thread)

#endif  /* HAVE_PTHREAD_H */
#endif  /* APR_HAS_THREADS */

#if !APR_HAS_THREADS

/* avoid warning for no prototype */
APR_DECLARE(apr_status_t) apr_os_thread_get(void);

APR_DECLARE(apr_status_t) apr_os_thread_get(void)
{
    return APR_ENOTIMPL;
}

#endif
