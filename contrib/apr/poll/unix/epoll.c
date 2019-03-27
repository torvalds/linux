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
#include "apr_arch_poll_private.h"
#include "apr_arch_inherit.h"

#if defined(HAVE_EPOLL)

static apr_int16_t get_epoll_event(apr_int16_t event)
{
    apr_int16_t rv = 0;

    if (event & APR_POLLIN)
        rv |= EPOLLIN;
    if (event & APR_POLLPRI)
        rv |= EPOLLPRI;
    if (event & APR_POLLOUT)
        rv |= EPOLLOUT;
    /* APR_POLLNVAL is not handled by epoll.  EPOLLERR and EPOLLHUP are return-only */

    return rv;
}

static apr_int16_t get_epoll_revent(apr_int16_t event)
{
    apr_int16_t rv = 0;

    if (event & EPOLLIN)
        rv |= APR_POLLIN;
    if (event & EPOLLPRI)
        rv |= APR_POLLPRI;
    if (event & EPOLLOUT)
        rv |= APR_POLLOUT;
    if (event & EPOLLERR)
        rv |= APR_POLLERR;
    if (event & EPOLLHUP)
        rv |= APR_POLLHUP;
    /* APR_POLLNVAL is not handled by epoll. */

    return rv;
}

struct apr_pollset_private_t
{
    int epoll_fd;
    struct epoll_event *pollset;
    apr_pollfd_t *result_set;
#if APR_HAS_THREADS
    /* A thread mutex to protect operations on the rings */
    apr_thread_mutex_t *ring_lock;
#endif
    /* A ring containing all of the pollfd_t that are active */
    APR_RING_HEAD(pfd_query_ring_t, pfd_elem_t) query_ring;
    /* A ring of pollfd_t that have been used, and then _remove()'d */
    APR_RING_HEAD(pfd_free_ring_t, pfd_elem_t) free_ring;
    /* A ring of pollfd_t where rings that have been _remove()`ed but
        might still be inside a _poll() */
    APR_RING_HEAD(pfd_dead_ring_t, pfd_elem_t) dead_ring;
};

static apr_status_t impl_pollset_cleanup(apr_pollset_t *pollset)
{
    close(pollset->p->epoll_fd);
    return APR_SUCCESS;
}


static apr_status_t impl_pollset_create(apr_pollset_t *pollset,
                                        apr_uint32_t size,
                                        apr_pool_t *p,
                                        apr_uint32_t flags)
{
    apr_status_t rv;
    int fd;

#ifdef HAVE_EPOLL_CREATE1
    fd = epoll_create1(EPOLL_CLOEXEC);
#else
    fd = epoll_create(size);
#endif
    if (fd < 0) {
        pollset->p = NULL;
        return apr_get_netos_error();
    }

#ifndef HAVE_EPOLL_CREATE1
    {
        int fd_flags;

        if ((fd_flags = fcntl(fd, F_GETFD)) == -1) {
            rv = errno;
            close(fd);
            pollset->p = NULL;
            return rv;
        }

        fd_flags |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, fd_flags) == -1) {
            rv = errno;
            close(fd);
            pollset->p = NULL;
            return rv;
        }
    }
#endif

    pollset->p = apr_palloc(p, sizeof(apr_pollset_private_t));
#if APR_HAS_THREADS
    if ((flags & APR_POLLSET_THREADSAFE) &&
        !(flags & APR_POLLSET_NOCOPY) &&
        ((rv = apr_thread_mutex_create(&pollset->p->ring_lock,
                                       APR_THREAD_MUTEX_DEFAULT,
                                       p)) != APR_SUCCESS)) {
        close(fd);
        pollset->p = NULL;
        return rv;
    }
#else
    if (flags & APR_POLLSET_THREADSAFE) {
        close(fd);
        pollset->p = NULL;
        return APR_ENOTIMPL;
    }
#endif
    pollset->p->epoll_fd = fd;
    pollset->p->pollset = apr_palloc(p, size * sizeof(struct epoll_event));
    pollset->p->result_set = apr_palloc(p, size * sizeof(apr_pollfd_t));

    if (!(flags & APR_POLLSET_NOCOPY)) {
        APR_RING_INIT(&pollset->p->query_ring, pfd_elem_t, link);
        APR_RING_INIT(&pollset->p->free_ring, pfd_elem_t, link);
        APR_RING_INIT(&pollset->p->dead_ring, pfd_elem_t, link);
    }
    return APR_SUCCESS;
}

static apr_status_t impl_pollset_add(apr_pollset_t *pollset,
                                     const apr_pollfd_t *descriptor)
{
    struct epoll_event ev = {0};
    int ret = -1;
    pfd_elem_t *elem = NULL;
    apr_status_t rv = APR_SUCCESS;

    ev.events = get_epoll_event(descriptor->reqevents);

    if (pollset->flags & APR_POLLSET_NOCOPY) {
        ev.data.ptr = (void *)descriptor;
    }
    else {
        pollset_lock_rings();

        if (!APR_RING_EMPTY(&(pollset->p->free_ring), pfd_elem_t, link)) {
            elem = APR_RING_FIRST(&(pollset->p->free_ring));
            APR_RING_REMOVE(elem, link);
        }
        else {
            elem = (pfd_elem_t *) apr_palloc(pollset->pool, sizeof(pfd_elem_t));
            APR_RING_ELEM_INIT(elem, link);
        }
        elem->pfd = *descriptor;
        ev.data.ptr = elem;
    }
    if (descriptor->desc_type == APR_POLL_SOCKET) {
        ret = epoll_ctl(pollset->p->epoll_fd, EPOLL_CTL_ADD,
                        descriptor->desc.s->socketdes, &ev);
    }
    else {
        ret = epoll_ctl(pollset->p->epoll_fd, EPOLL_CTL_ADD,
                        descriptor->desc.f->filedes, &ev);
    }

    if (0 != ret) {
        rv = apr_get_netos_error();
    }

    if (!(pollset->flags & APR_POLLSET_NOCOPY)) {
        if (rv != APR_SUCCESS) {
            APR_RING_INSERT_TAIL(&(pollset->p->free_ring), elem, pfd_elem_t, link);
        }
        else {
            APR_RING_INSERT_TAIL(&(pollset->p->query_ring), elem, pfd_elem_t, link);
        }
        pollset_unlock_rings();
    }

    return rv;
}

static apr_status_t impl_pollset_remove(apr_pollset_t *pollset,
                                        const apr_pollfd_t *descriptor)
{
    pfd_elem_t *ep;
    apr_status_t rv = APR_SUCCESS;
    struct epoll_event ev = {0}; /* ignored, but must be passed with
                                  * kernel < 2.6.9
                                  */
    int ret = -1;

    if (descriptor->desc_type == APR_POLL_SOCKET) {
        ret = epoll_ctl(pollset->p->epoll_fd, EPOLL_CTL_DEL,
                        descriptor->desc.s->socketdes, &ev);
    }
    else {
        ret = epoll_ctl(pollset->p->epoll_fd, EPOLL_CTL_DEL,
                        descriptor->desc.f->filedes, &ev);
    }
    if (ret < 0) {
        rv = APR_NOTFOUND;
    }

    if (!(pollset->flags & APR_POLLSET_NOCOPY)) {
        pollset_lock_rings();

        for (ep = APR_RING_FIRST(&(pollset->p->query_ring));
             ep != APR_RING_SENTINEL(&(pollset->p->query_ring),
                                     pfd_elem_t, link);
             ep = APR_RING_NEXT(ep, link)) {
                
            if (descriptor->desc.s == ep->pfd.desc.s) {
                APR_RING_REMOVE(ep, link);
                APR_RING_INSERT_TAIL(&(pollset->p->dead_ring),
                                     ep, pfd_elem_t, link);
                break;
            }
        }

        pollset_unlock_rings();
    }

    return rv;
}

static apr_status_t impl_pollset_poll(apr_pollset_t *pollset,
                                           apr_interval_time_t timeout,
                                           apr_int32_t *num,
                                           const apr_pollfd_t **descriptors)
{
    int ret, i, j;
    apr_status_t rv = APR_SUCCESS;
    apr_pollfd_t *fdptr;

    if (timeout > 0) {
        timeout /= 1000;
    }

    ret = epoll_wait(pollset->p->epoll_fd, pollset->p->pollset, pollset->nalloc,
                     timeout);
    (*num) = ret;

    if (ret < 0) {
        rv = apr_get_netos_error();
    }
    else if (ret == 0) {
        rv = APR_TIMEUP;
    }
    else {
        for (i = 0, j = 0; i < ret; i++) {
            if (pollset->flags & APR_POLLSET_NOCOPY) {
                fdptr = (apr_pollfd_t *)(pollset->p->pollset[i].data.ptr);
            }
            else {
                fdptr = &(((pfd_elem_t *) (pollset->p->pollset[i].data.ptr))->pfd);
            }
            /* Check if the polled descriptor is our
             * wakeup pipe. In that case do not put it result set.
             */
            if ((pollset->flags & APR_POLLSET_WAKEABLE) &&
                fdptr->desc_type == APR_POLL_FILE &&
                fdptr->desc.f == pollset->wakeup_pipe[0]) {
                apr_pollset_drain_wakeup_pipe(pollset);
                rv = APR_EINTR;
            }
            else {
                pollset->p->result_set[j] = *fdptr;
                pollset->p->result_set[j].rtnevents =
                    get_epoll_revent(pollset->p->pollset[i].events);
                j++;
            }
        }
        if (((*num) = j)) { /* any event besides wakeup pipe? */
            rv = APR_SUCCESS;

            if (descriptors) {
                *descriptors = pollset->p->result_set;
            }
        }
    }

    if (!(pollset->flags & APR_POLLSET_NOCOPY)) {
        pollset_lock_rings();

        /* Shift all PFDs in the Dead Ring to the Free Ring */
        APR_RING_CONCAT(&(pollset->p->free_ring), &(pollset->p->dead_ring), pfd_elem_t, link);

        pollset_unlock_rings();
    }

    return rv;
}

static apr_pollset_provider_t impl = {
    impl_pollset_create,
    impl_pollset_add,
    impl_pollset_remove,
    impl_pollset_poll,
    impl_pollset_cleanup,
    "epoll"
};

apr_pollset_provider_t *apr_pollset_provider_epoll = &impl;

static apr_status_t cb_cleanup(void *p_)
{
    apr_pollcb_t *pollcb = (apr_pollcb_t *) p_;
    close(pollcb->fd);
    return APR_SUCCESS;
}

static apr_status_t impl_pollcb_create(apr_pollcb_t *pollcb,
                                       apr_uint32_t size,
                                       apr_pool_t *p,
                                       apr_uint32_t flags)
{
    int fd;
    
#ifdef HAVE_EPOLL_CREATE1
    fd = epoll_create1(EPOLL_CLOEXEC);
#else
    fd = epoll_create(size);
#endif
    
    if (fd < 0) {
        return apr_get_netos_error();
    }

#ifndef HAVE_EPOLL_CREATE1
    {
        int fd_flags;
        apr_status_t rv;

        if ((fd_flags = fcntl(fd, F_GETFD)) == -1) {
            rv = errno;
            close(fd);
            pollcb->fd = -1;
            return rv;
        }

        fd_flags |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, fd_flags) == -1) {
            rv = errno;
            close(fd);
            pollcb->fd = -1;
            return rv;
        }
    }
#endif
    
    pollcb->fd = fd;
    pollcb->pollset.epoll = apr_palloc(p, size * sizeof(struct epoll_event));
    apr_pool_cleanup_register(p, pollcb, cb_cleanup, apr_pool_cleanup_null);

    return APR_SUCCESS;
}

static apr_status_t impl_pollcb_add(apr_pollcb_t *pollcb,
                                    apr_pollfd_t *descriptor)
{
    struct epoll_event ev;
    int ret;
    
    ev.events = get_epoll_event(descriptor->reqevents);
    ev.data.ptr = (void *)descriptor;

    if (descriptor->desc_type == APR_POLL_SOCKET) {
        ret = epoll_ctl(pollcb->fd, EPOLL_CTL_ADD,
                        descriptor->desc.s->socketdes, &ev);
    }
    else {
        ret = epoll_ctl(pollcb->fd, EPOLL_CTL_ADD,
                        descriptor->desc.f->filedes, &ev);
    }
    
    if (ret == -1) {
        return apr_get_netos_error();
    }
    
    return APR_SUCCESS;
}

static apr_status_t impl_pollcb_remove(apr_pollcb_t *pollcb,
                                       apr_pollfd_t *descriptor)
{
    apr_status_t rv = APR_SUCCESS;
    struct epoll_event ev = {0}; /* ignored, but must be passed with
                                  * kernel < 2.6.9
                                  */
    int ret = -1;
    
    if (descriptor->desc_type == APR_POLL_SOCKET) {
        ret = epoll_ctl(pollcb->fd, EPOLL_CTL_DEL,
                        descriptor->desc.s->socketdes, &ev);
    }
    else {
        ret = epoll_ctl(pollcb->fd, EPOLL_CTL_DEL,
                        descriptor->desc.f->filedes, &ev);
    }
    
    if (ret < 0) {
        rv = APR_NOTFOUND;
    }
    
    return rv;
}


static apr_status_t impl_pollcb_poll(apr_pollcb_t *pollcb,
                                     apr_interval_time_t timeout,
                                     apr_pollcb_cb_t func,
                                     void *baton)
{
    int ret, i;
    apr_status_t rv = APR_SUCCESS;
    
    if (timeout > 0) {
        timeout /= 1000;
    }
    
    ret = epoll_wait(pollcb->fd, pollcb->pollset.epoll, pollcb->nalloc,
                     timeout);
    if (ret < 0) {
        rv = apr_get_netos_error();
    }
    else if (ret == 0) {
        rv = APR_TIMEUP;
    }
    else {
        for (i = 0; i < ret; i++) {
            apr_pollfd_t *pollfd = (apr_pollfd_t *)(pollcb->pollset.epoll[i].data.ptr);
            pollfd->rtnevents = get_epoll_revent(pollcb->pollset.epoll[i].events);

            rv = func(baton, pollfd);
            if (rv) {
                return rv;
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
    "epoll"
};

apr_pollcb_provider_t *apr_pollcb_provider_epoll = &impl_cb;

#endif /* HAVE_EPOLL */
