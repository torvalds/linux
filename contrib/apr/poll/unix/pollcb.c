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

#ifdef WIN32
/* POSIX defines 1024 for the FD_SETSIZE */
#define FD_SETSIZE 1024
#endif

#include "apr.h"
#include "apr_poll.h"
#include "apr_time.h"
#include "apr_portable.h"
#include "apr_arch_file_io.h"
#include "apr_arch_networkio.h"
#include "apr_arch_poll_private.h"

static apr_pollset_method_e pollset_default_method = POLLSET_DEFAULT_METHOD;
#if defined(HAVE_KQUEUE)
extern apr_pollcb_provider_t *apr_pollcb_provider_kqueue;
#endif
#if defined(HAVE_PORT_CREATE)
extern apr_pollcb_provider_t *apr_pollcb_provider_port;
#endif
#if defined(HAVE_EPOLL)
extern apr_pollcb_provider_t *apr_pollcb_provider_epoll;
#endif
#if defined(HAVE_POLL)
extern apr_pollcb_provider_t *apr_pollcb_provider_poll;
#endif

static apr_pollcb_provider_t *pollcb_provider(apr_pollset_method_e method)
{
    apr_pollcb_provider_t *provider = NULL;
    switch (method) {
        case APR_POLLSET_KQUEUE:
#if defined(HAVE_KQUEUE)
            provider = apr_pollcb_provider_kqueue;
#endif
        break;
        case APR_POLLSET_PORT:
#if defined(HAVE_PORT_CREATE)
            provider = apr_pollcb_provider_port;
#endif
        break;
        case APR_POLLSET_EPOLL:
#if defined(HAVE_EPOLL)
            provider = apr_pollcb_provider_epoll;
#endif
        break;
        case APR_POLLSET_POLL:
#if defined(HAVE_POLL)
            provider = apr_pollcb_provider_poll;
#endif
        break;
        case APR_POLLSET_SELECT:
        case APR_POLLSET_AIO_MSGQ:
        case APR_POLLSET_DEFAULT:
        break;
    }
    return provider;
}

APR_DECLARE(apr_status_t) apr_pollcb_create_ex(apr_pollcb_t **ret_pollcb,
                                               apr_uint32_t size,
                                               apr_pool_t *p,
                                               apr_uint32_t flags,
                                               apr_pollset_method_e method)
{
    apr_status_t rv;
    apr_pollcb_t *pollcb;
    apr_pollcb_provider_t *provider = NULL;

    *ret_pollcb = NULL;

 #ifdef WIN32
    /* This will work only if ws2_32.dll has WSAPoll funtion.
     * We could check the presence of the function here,
     * but someone might implement other pollcb method in
     * the future.
     */
    if (method == APR_POLLSET_DEFAULT) {
        method = APR_POLLSET_POLL;
    }
 #endif

    if (method == APR_POLLSET_DEFAULT)
        method = pollset_default_method;
    while (provider == NULL) {
        provider = pollcb_provider(method);
        if (!provider) {
            if ((flags & APR_POLLSET_NODEFAULT) == APR_POLLSET_NODEFAULT)
                return APR_ENOTIMPL;
            if (method == pollset_default_method)
                return APR_ENOTIMPL;
            method = pollset_default_method;
        }
    }

    pollcb = apr_palloc(p, sizeof(*pollcb));
    pollcb->nelts = 0;
    pollcb->nalloc = size;
    pollcb->pool = p;
    pollcb->provider = provider;

    rv = (*provider->create)(pollcb, size, p, flags);
    if (rv == APR_ENOTIMPL) {
        if (method == pollset_default_method) {
            return rv;
        }

        if ((flags & APR_POLLSET_NODEFAULT) == APR_POLLSET_NODEFAULT) {
            return rv;
        }

        /* Try with default provider */
        provider = pollcb_provider(pollset_default_method);
        if (!provider) {
            return APR_ENOTIMPL;
        }
        rv = (*provider->create)(pollcb, size, p, flags);
        if (rv != APR_SUCCESS) {
            return rv;
        }
        pollcb->provider = provider;
    }
    else if (rv != APR_SUCCESS) {
        return rv;
    }

    *ret_pollcb = pollcb;
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_pollcb_create(apr_pollcb_t **pollcb,
                                            apr_uint32_t size,
                                            apr_pool_t *p,
                                            apr_uint32_t flags)
{
    apr_pollset_method_e method = APR_POLLSET_DEFAULT;
    return apr_pollcb_create_ex(pollcb, size, p, flags, method);
}

APR_DECLARE(apr_status_t) apr_pollcb_add(apr_pollcb_t *pollcb,
                                         apr_pollfd_t *descriptor)
{
    return (*pollcb->provider->add)(pollcb, descriptor);
}

APR_DECLARE(apr_status_t) apr_pollcb_remove(apr_pollcb_t *pollcb,
                                            apr_pollfd_t *descriptor)
{
    return (*pollcb->provider->remove)(pollcb, descriptor);
}


APR_DECLARE(apr_status_t) apr_pollcb_poll(apr_pollcb_t *pollcb,
                                          apr_interval_time_t timeout,
                                          apr_pollcb_cb_t func,
                                          void *baton)
{
    return (*pollcb->provider->poll)(pollcb, timeout, func, baton);
}
