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
#include "apr_poll.h"
#include "apr_time.h"
#include "apr_portable.h"
#include "apr_arch_file_io.h"
#include "apr_arch_networkio.h"
#include "apr_arch_misc.h"
#include "apr_arch_poll_private.h"

#if defined(HAVE_POLL)

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

static apr_int16_t get_event(apr_int16_t event)
{
    apr_int16_t rv = 0;

    if (event & APR_POLLIN)
        rv |= POLLIN;
    if (event & APR_POLLPRI)
        rv |= POLLPRI;
    if (event & APR_POLLOUT)
        rv |= POLLOUT;
    /* POLLERR, POLLHUP, and POLLNVAL aren't valid as requested events */

    return rv;
}

static apr_int16_t get_revent(apr_int16_t event)
{
    apr_int16_t rv = 0;

    if (event & POLLIN)
        rv |= APR_POLLIN;
    if (event & POLLPRI)
        rv |= APR_POLLPRI;
    if (event & POLLOUT)
        rv |= APR_POLLOUT;
    if (event & POLLERR)
        rv |= APR_POLLERR;
    if (event & POLLHUP)
        rv |= APR_POLLHUP;
    if (event & POLLNVAL)
        rv |= APR_POLLNVAL;

    return rv;
}

#ifdef POLL_USES_POLL

#define SMALL_POLLSET_LIMIT  8

APR_DECLARE(apr_status_t) apr_poll(apr_pollfd_t *aprset, apr_int32_t num,
                                   apr_int32_t *nsds, 
                                   apr_interval_time_t timeout)
{
    int i, num_to_poll;
#ifdef HAVE_VLA
    /* XXX: I trust that this is a segv when insufficient stack exists? */
    struct pollfd pollset[num];
#elif defined(HAVE_ALLOCA)
    struct pollfd *pollset = alloca(sizeof(struct pollfd) * num);
    if (!pollset)
        return APR_ENOMEM;
#else
    struct pollfd tmp_pollset[SMALL_POLLSET_LIMIT];
    struct pollfd *pollset;

    if (num <= SMALL_POLLSET_LIMIT) {
        pollset = tmp_pollset;
    }
    else {
        /* This does require O(n) to copy the descriptors to the internal
         * mapping.
         */
        pollset = malloc(sizeof(struct pollfd) * num);
        /* The other option is adding an apr_pool_abort() fn to invoke
         * the pool's out of memory handler
         */
        if (!pollset)
            return APR_ENOMEM;
    }
#endif
    for (i = 0; i < num; i++) {
        if (aprset[i].desc_type == APR_POLL_SOCKET) {
            pollset[i].fd = aprset[i].desc.s->socketdes;
        }
        else if (aprset[i].desc_type == APR_POLL_FILE) {
            pollset[i].fd = aprset[i].desc.f->filedes;
        }
        else {
            break;
        }
        pollset[i].events = get_event(aprset[i].reqevents);
    }
    num_to_poll = i;

    if (timeout > 0) {
        timeout /= 1000; /* convert microseconds to milliseconds */
    }

    i = poll(pollset, num_to_poll, timeout);
    (*nsds) = i;

    if (i > 0) { /* poll() sets revents only if an event was signalled;
                  * we don't promise to set rtnevents unless an event
                  * was signalled
                  */
        for (i = 0; i < num; i++) {
            aprset[i].rtnevents = get_revent(pollset[i].revents);
        }
    }
    
#if !defined(HAVE_VLA) && !defined(HAVE_ALLOCA)
    if (num > SMALL_POLLSET_LIMIT) {
        free(pollset);
    }
#endif

    if ((*nsds) < 0) {
        return apr_get_netos_error();
    }
    if ((*nsds) == 0) {
        return APR_TIMEUP;
    }
    return APR_SUCCESS;
}


#endif /* POLL_USES_POLL */

struct apr_pollset_private_t
{
    struct pollfd *pollset;
    apr_pollfd_t *query_set;
    apr_pollfd_t *result_set;
};

static apr_status_t impl_pollset_create(apr_pollset_t *pollset,
                                        apr_uint32_t size,
                                        apr_pool_t *p,
                                        apr_uint32_t flags)
{
    if (flags & APR_POLLSET_THREADSAFE) {                
        return APR_ENOTIMPL;
    }
#ifdef WIN32
    if (!APR_HAVE_LATE_DLL_FUNC(WSAPoll)) {
        return APR_ENOTIMPL;
    }
#endif
    pollset->p = apr_palloc(p, sizeof(apr_pollset_private_t));
    pollset->p->pollset = apr_palloc(p, size * sizeof(struct pollfd));
    pollset->p->query_set = apr_palloc(p, size * sizeof(apr_pollfd_t));
    pollset->p->result_set = apr_palloc(p, size * sizeof(apr_pollfd_t));

    return APR_SUCCESS;
}

static apr_status_t impl_pollset_add(apr_pollset_t *pollset,
                                     const apr_pollfd_t *descriptor)
{
    if (pollset->nelts == pollset->nalloc) {
        return APR_ENOMEM;
    }

    pollset->p->query_set[pollset->nelts] = *descriptor;

    if (descriptor->desc_type == APR_POLL_SOCKET) {
        pollset->p->pollset[pollset->nelts].fd = descriptor->desc.s->socketdes;
    }
    else {
#if APR_FILES_AS_SOCKETS
        pollset->p->pollset[pollset->nelts].fd = descriptor->desc.f->filedes;
#else
        if ((pollset->flags & APR_POLLSET_WAKEABLE) &&
            descriptor->desc.f == pollset->wakeup_pipe[0])
            pollset->p->pollset[pollset->nelts].fd = (SOCKET)descriptor->desc.f->filedes;
        else
            return APR_EBADF;
#endif
    }
    pollset->p->pollset[pollset->nelts].events =
        get_event(descriptor->reqevents);
    pollset->nelts++;

    return APR_SUCCESS;
}

static apr_status_t impl_pollset_remove(apr_pollset_t *pollset,
                                        const apr_pollfd_t *descriptor)
{
    apr_uint32_t i;

    for (i = 0; i < pollset->nelts; i++) {
        if (descriptor->desc.s == pollset->p->query_set[i].desc.s) {
            /* Found an instance of the fd: remove this and any other copies */
            apr_uint32_t dst = i;
            apr_uint32_t old_nelts = pollset->nelts;
            pollset->nelts--;
            for (i++; i < old_nelts; i++) {
                if (descriptor->desc.s == pollset->p->query_set[i].desc.s) {
                    pollset->nelts--;
                }
                else {
                    pollset->p->pollset[dst] = pollset->p->pollset[i];
                    pollset->p->query_set[dst] = pollset->p->query_set[i];
                    dst++;
                }
            }
            return APR_SUCCESS;
        }
    }

    return APR_NOTFOUND;
}

static apr_status_t impl_pollset_poll(apr_pollset_t *pollset,
                                      apr_interval_time_t timeout,
                                      apr_int32_t *num,
                                      const apr_pollfd_t **descriptors)
{
    int ret;
    apr_status_t rv = APR_SUCCESS;

#ifdef WIN32
    /* WSAPoll() requires at least one socket. */
    if (pollset->nelts == 0) {
        *num = 0;
        if (timeout > 0) {
            apr_sleep(timeout);
            return APR_TIMEUP;
        }
        return APR_SUCCESS;
    }
    if (timeout > 0) {
        timeout /= 1000;
    }
    ret = WSAPoll(pollset->p->pollset, pollset->nelts, (int)timeout);
#else
    if (timeout > 0) {
        timeout /= 1000;
    }
    ret = poll(pollset->p->pollset, pollset->nelts, timeout);
#endif
    (*num) = ret;
    if (ret < 0) {
        return apr_get_netos_error();
    }
    else if (ret == 0) {
        return APR_TIMEUP;
    }
    else {
        apr_uint32_t i, j;

        for (i = 0, j = 0; i < pollset->nelts; i++) {
            if (pollset->p->pollset[i].revents != 0) {
                /* Check if the polled descriptor is our
                 * wakeup pipe. In that case do not put it result set.
                 */
                if ((pollset->flags & APR_POLLSET_WAKEABLE) &&
                    pollset->p->query_set[i].desc_type == APR_POLL_FILE &&
                    pollset->p->query_set[i].desc.f == pollset->wakeup_pipe[0]) {
                        apr_pollset_drain_wakeup_pipe(pollset);
                        rv = APR_EINTR;
                }
                else {
                    pollset->p->result_set[j] = pollset->p->query_set[i];
                    pollset->p->result_set[j].rtnevents =
                        get_revent(pollset->p->pollset[i].revents);
                    j++;
                }
            }
        }
        if (((*num) = j) > 0)
            rv = APR_SUCCESS;
    }
    if (descriptors && (*num))
        *descriptors = pollset->p->result_set;
    return rv;
}

static apr_pollset_provider_t impl = {
    impl_pollset_create,
    impl_pollset_add,
    impl_pollset_remove,
    impl_pollset_poll,
    NULL,
    "poll"
};

apr_pollset_provider_t *apr_pollset_provider_poll = &impl;

/* Poll method pollcb.
 * This is probably usable only for WIN32 having WSAPoll
 */
static apr_status_t impl_pollcb_create(apr_pollcb_t *pollcb,
                                       apr_uint32_t size,
                                       apr_pool_t *p,
                                       apr_uint32_t flags)
{
#if APR_HAS_THREADS
    return APR_ENOTIMPL;
#else
    pollcb->fd = -1;
#ifdef WIN32
    if (!APR_HAVE_LATE_DLL_FUNC(WSAPoll)) {
        return APR_ENOTIMPL;
    }
#endif

    pollcb->pollset.ps = apr_palloc(p, size * sizeof(struct pollfd));
    pollcb->copyset = apr_palloc(p, size * sizeof(apr_pollfd_t *));

    return APR_SUCCESS;
#endif
}

static apr_status_t impl_pollcb_add(apr_pollcb_t *pollcb,
                                    apr_pollfd_t *descriptor)
{
    if (pollcb->nelts == pollcb->nalloc) {
        return APR_ENOMEM;
    }

    if (descriptor->desc_type == APR_POLL_SOCKET) {
        pollcb->pollset.ps[pollcb->nelts].fd = descriptor->desc.s->socketdes;
    }
    else {
#if APR_FILES_AS_SOCKETS
        pollcb->pollset.ps[pollcb->nelts].fd = descriptor->desc.f->filedes;
#else
        return APR_EBADF;
#endif
    }

    pollcb->pollset.ps[pollcb->nelts].events =
        get_event(descriptor->reqevents);
    pollcb->copyset[pollcb->nelts] = descriptor;
    pollcb->nelts++;
    
    return APR_SUCCESS;
}

static apr_status_t impl_pollcb_remove(apr_pollcb_t *pollcb,
                                       apr_pollfd_t *descriptor)
{
    apr_uint32_t i;

    for (i = 0; i < pollcb->nelts; i++) {
        if (descriptor->desc.s == pollcb->copyset[i]->desc.s) {
            /* Found an instance of the fd: remove this and any other copies */
            apr_uint32_t dst = i;
            apr_uint32_t old_nelts = pollcb->nelts;
            pollcb->nelts--;
            for (i++; i < old_nelts; i++) {
                if (descriptor->desc.s == pollcb->copyset[i]->desc.s) {
                    pollcb->nelts--;
                }
                else {
                    pollcb->pollset.ps[dst] = pollcb->pollset.ps[i];
                    pollcb->copyset[dst] = pollcb->copyset[i];
                    dst++;
                }
            }
            return APR_SUCCESS;
        }
    }

    return APR_NOTFOUND;
}

static apr_status_t impl_pollcb_poll(apr_pollcb_t *pollcb,
                                     apr_interval_time_t timeout,
                                     apr_pollcb_cb_t func,
                                     void *baton)
{
    int ret;
    apr_status_t rv = APR_SUCCESS;
    apr_uint32_t i;

#ifdef WIN32
    /* WSAPoll() requires at least one socket. */
    if (pollcb->nelts == 0) {
        if (timeout > 0) {
            apr_sleep(timeout);
            return APR_TIMEUP;
        }
        return APR_SUCCESS;
    }
    if (timeout > 0) {
        timeout /= 1000;
    }
    ret = WSAPoll(pollcb->pollset.ps, pollcb->nelts, (int)timeout);
#else
    if (timeout > 0) {
        timeout /= 1000;
    }
    ret = poll(pollcb->pollset.ps, pollcb->nelts, timeout);
#endif
    if (ret < 0) {
        return apr_get_netos_error();
    }
    else if (ret == 0) {
        return APR_TIMEUP;
    }
    else {
        for (i = 0; i < pollcb->nelts; i++) {
            if (pollcb->pollset.ps[i].revents != 0) {
                apr_pollfd_t *pollfd = pollcb->copyset[i];
                pollfd->rtnevents = get_revent(pollcb->pollset.ps[i].revents);                    
                rv = func(baton, pollfd);
                if (rv) {
                    return rv;
                }
            }
        }
    }
    return rv;
}

static apr_pollcb_provider_t impl_cb = {
    impl_pollcb_create,
    impl_pollcb_add,
    impl_pollcb_remove,
    impl_pollcb_poll,
    "poll"
};

apr_pollcb_provider_t *apr_pollcb_provider_poll = &impl_cb;

#endif /* HAVE_POLL */
