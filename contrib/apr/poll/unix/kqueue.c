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

#ifdef HAVE_KQUEUE

static apr_int16_t get_kqueue_revent(apr_int16_t event, apr_int16_t flags)
{
    apr_int16_t rv = 0;

    if (event == EVFILT_READ)
        rv |= APR_POLLIN;
    else if (event == EVFILT_WRITE)
        rv |= APR_POLLOUT;
    if (flags & EV_EOF)
        rv |= APR_POLLHUP;
    /* APR_POLLPRI, APR_POLLERR, and APR_POLLNVAL are not handled by this
     * implementation.
     * TODO: See if EV_ERROR + certain system errors in the returned data field
     * should map to APR_POLLNVAL.
     */
    return rv;
}

struct apr_pollset_private_t
{
    int kqueue_fd;
    struct kevent kevent;
    apr_uint32_t setsize;
    struct kevent *ke_set;
    apr_pollfd_t *result_set;
#if APR_HAS_THREADS
    /* A thread mutex to protect operations on the rings */
    apr_thread_mutex_t *ring_lock;
#endif
    /* A ring containing all of the pollfd_t that are active */
    APR_RING_HEAD(pfd_query_ring_t, pfd_elem_t) query_ring;
    /* A ring of pollfd_t that have been used, and then _remove'd */
    APR_RING_HEAD(pfd_free_ring_t, pfd_elem_t) free_ring;
    /* A ring of pollfd_t where rings that have been _remove'd but
       might still be inside a _poll */
    APR_RING_HEAD(pfd_dead_ring_t, pfd_elem_t) dead_ring;
};

static apr_status_t impl_pollset_cleanup(apr_pollset_t *pollset)
{
    close(pollset->p->kqueue_fd);
    return APR_SUCCESS;
}

static apr_status_t impl_pollset_create(apr_pollset_t *pollset,
                                        apr_uint32_t size,
                                        apr_pool_t *p,
                                        apr_uint32_t flags)
{
    apr_status_t rv;
    pollset->p = apr_palloc(p, sizeof(apr_pollset_private_t));
#if APR_HAS_THREADS
    if (flags & APR_POLLSET_THREADSAFE &&
        ((rv = apr_thread_mutex_create(&pollset->p->ring_lock,
                                       APR_THREAD_MUTEX_DEFAULT,
                                       p)) != APR_SUCCESS)) {
        pollset->p = NULL;
        return rv;
    }
#else
    if (flags & APR_POLLSET_THREADSAFE) {
        pollset->p = NULL;
        return APR_ENOTIMPL;
    }
#endif

    /* POLLIN and POLLOUT are represented in different returned
     * events, so we need 2 entries per descriptor in the result set,
     * both for what is returned by kevent() and what is returned to
     * the caller of apr_pollset_poll() (since it doesn't spend the
     * CPU to coalesce separate APR_POLLIN and APR_POLLOUT events
     * for the same descriptor)
     */
    pollset->p->setsize = 2 * size;

    pollset->p->ke_set =
        (struct kevent *) apr_palloc(p, pollset->p->setsize * sizeof(struct kevent));

    memset(pollset->p->ke_set, 0, pollset->p->setsize * sizeof(struct kevent));

    pollset->p->kqueue_fd = kqueue();

    if (pollset->p->kqueue_fd == -1) {
        pollset->p = NULL;
        return apr_get_netos_error();
    }

    {
        int flags;

        if ((flags = fcntl(pollset->p->kqueue_fd, F_GETFD)) == -1) {
            rv = errno;
            close(pollset->p->kqueue_fd);
            pollset->p = NULL;
            return rv;
        }

        flags |= FD_CLOEXEC;
        if (fcntl(pollset->p->kqueue_fd, F_SETFD, flags) == -1) {
            rv = errno;
            close(pollset->p->kqueue_fd);
            pollset->p = NULL;
            return rv;
        }
    }

    pollset->p->result_set = apr_palloc(p, pollset->p->setsize * sizeof(apr_pollfd_t));

    APR_RING_INIT(&pollset->p->query_ring, pfd_elem_t, link);
    APR_RING_INIT(&pollset->p->free_ring, pfd_elem_t, link);
    APR_RING_INIT(&pollset->p->dead_ring, pfd_elem_t, link);

    return APR_SUCCESS;
}

static apr_status_t impl_pollset_add(apr_pollset_t *pollset,
                                     const apr_pollfd_t *descriptor)
{
    apr_os_sock_t fd;
    pfd_elem_t *elem;
    apr_status_t rv = APR_SUCCESS;

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

    if (descriptor->desc_type == APR_POLL_SOCKET) {
        fd = descriptor->desc.s->socketdes;
    }
    else {
        fd = descriptor->desc.f->filedes;
    }

    if (descriptor->reqevents & APR_POLLIN) {
        EV_SET(&pollset->p->kevent, fd, EVFILT_READ, EV_ADD, 0, 0, elem);

        if (kevent(pollset->p->kqueue_fd, &pollset->p->kevent, 1, NULL, 0,
                   NULL) == -1) {
            rv = apr_get_netos_error();
        }
    }

    if (descriptor->reqevents & APR_POLLOUT && rv == APR_SUCCESS) {
        EV_SET(&pollset->p->kevent, fd, EVFILT_WRITE, EV_ADD, 0, 0, elem);

        if (kevent(pollset->p->kqueue_fd, &pollset->p->kevent, 1, NULL, 0,
                   NULL) == -1) {
            rv = apr_get_netos_error();
        }
    }

    if (rv == APR_SUCCESS) {
        APR_RING_INSERT_TAIL(&(pollset->p->query_ring), elem, pfd_elem_t, link);
    }
    else {
        APR_RING_INSERT_TAIL(&(pollset->p->free_ring), elem, pfd_elem_t, link);
    }

    pollset_unlock_rings();

    return rv;
}

static apr_status_t impl_pollset_remove(apr_pollset_t *pollset,
                                        const apr_pollfd_t *descriptor)
{
    pfd_elem_t *ep;
    apr_status_t rv;
    apr_os_sock_t fd;

    pollset_lock_rings();

    if (descriptor->desc_type == APR_POLL_SOCKET) {
        fd = descriptor->desc.s->socketdes;
    }
    else {
        fd = descriptor->desc.f->filedes;
    }

    rv = APR_NOTFOUND; /* unless at least one of the specified conditions is */
    if (descriptor->reqevents & APR_POLLIN) {
        EV_SET(&pollset->p->kevent, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);

        if (kevent(pollset->p->kqueue_fd, &pollset->p->kevent, 1, NULL, 0,
                   NULL) != -1) {
            rv = APR_SUCCESS;
        }
    }

    if (descriptor->reqevents & APR_POLLOUT) {
        EV_SET(&pollset->p->kevent, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);

        if (kevent(pollset->p->kqueue_fd, &pollset->p->kevent, 1, NULL, 0,
                   NULL) != -1) {
            rv = APR_SUCCESS;
        }
    }

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

    return rv;
}

static apr_status_t impl_pollset_poll(apr_pollset_t *pollset,
                                      apr_interval_time_t timeout,
                                      apr_int32_t *num,
                                      const apr_pollfd_t **descriptors)
{
    int ret, i, j;
    struct timespec tv, *tvptr;
    apr_status_t rv = APR_SUCCESS;
    apr_pollfd_t fd;

    if (timeout < 0) {
        tvptr = NULL;
    }
    else {
        tv.tv_sec = (long) apr_time_sec(timeout);
        tv.tv_nsec = (long) apr_time_usec(timeout) * 1000;
        tvptr = &tv;
    }

    ret = kevent(pollset->p->kqueue_fd, NULL, 0, pollset->p->ke_set,
                 pollset->p->setsize, tvptr);
    (*num) = ret;
    if (ret < 0) {
        rv = apr_get_netos_error();
    }
    else if (ret == 0) {
        rv = APR_TIMEUP;
    }
    else {
        for (i = 0, j = 0; i < ret; i++) {
            fd = (((pfd_elem_t*)(pollset->p->ke_set[i].udata))->pfd);
            if ((pollset->flags & APR_POLLSET_WAKEABLE) &&
                fd.desc_type == APR_POLL_FILE &&
                fd.desc.f == pollset->wakeup_pipe[0]) {
                apr_pollset_drain_wakeup_pipe(pollset);
                rv = APR_EINTR;
            }
            else {
                pollset->p->result_set[j] = fd;
                pollset->p->result_set[j].rtnevents =
                        get_kqueue_revent(pollset->p->ke_set[i].filter,
                                          pollset->p->ke_set[i].flags);
                j++;
            }
        }
        if ((*num = j)) { /* any event besides wakeup pipe? */
            rv = APR_SUCCESS;
            if (descriptors) {
                *descriptors = pollset->p->result_set;
            }
        }
    }


    pollset_lock_rings();

    /* Shift all PFDs in the Dead Ring to the Free Ring */
    APR_RING_CONCAT(&(pollset->p->free_ring), &(pollset->p->dead_ring),
                    pfd_elem_t, link);

    pollset_unlock_rings();

    return rv;
}

static apr_pollset_provider_t impl = {
    impl_pollset_create,
    impl_pollset_add,
    impl_pollset_remove,
    impl_pollset_poll,
    impl_pollset_cleanup,
    "kqueue"
};

apr_pollset_provider_t *apr_pollset_provider_kqueue = &impl;

static apr_status_t cb_cleanup(void *b_)
{
    apr_pollcb_t *pollcb = (apr_pollcb_t *) b_;
    close(pollcb->fd);
    return APR_SUCCESS;
}

static apr_status_t impl_pollcb_create(apr_pollcb_t *pollcb,
                                       apr_uint32_t size,
                                       apr_pool_t *p,
                                       apr_uint32_t flags)
{
    int fd;
    
    fd = kqueue();
    if (fd < 0) {
        return apr_get_netos_error();
    }

    {
        int flags;
        apr_status_t rv;

        if ((flags = fcntl(fd, F_GETFD)) == -1) {
            rv = errno;
            close(fd);
            pollcb->fd = -1;
            return rv;
        }

        flags |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, flags) == -1) {
            rv = errno;
            close(fd);
            pollcb->fd = -1;
            return rv;
        }
    }
 
    pollcb->fd = fd;
    pollcb->pollset.ke = (struct kevent *)apr_pcalloc(p, 2 * size * sizeof(struct kevent));
    apr_pool_cleanup_register(p, pollcb, cb_cleanup, apr_pool_cleanup_null);
    
    return APR_SUCCESS;
}

static apr_status_t impl_pollcb_add(apr_pollcb_t *pollcb,
                                    apr_pollfd_t *descriptor)
{
    apr_os_sock_t fd;
    struct kevent ev;
    apr_status_t rv = APR_SUCCESS;
    
    if (descriptor->desc_type == APR_POLL_SOCKET) {
        fd = descriptor->desc.s->socketdes;
    }
    else {
        fd = descriptor->desc.f->filedes;
    }
    
    if (descriptor->reqevents & APR_POLLIN) {
        EV_SET(&ev, fd, EVFILT_READ, EV_ADD, 0, 0, descriptor);
        
        if (kevent(pollcb->fd, &ev, 1, NULL, 0, NULL) == -1) {
            rv = apr_get_netos_error();
        }
    }
    
    if (descriptor->reqevents & APR_POLLOUT && rv == APR_SUCCESS) {
        EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD, 0, 0, descriptor);
        
        if (kevent(pollcb->fd, &ev, 1, NULL, 0, NULL) == -1) {
            rv = apr_get_netos_error();
        }
    }
    
    return rv;
}

static apr_status_t impl_pollcb_remove(apr_pollcb_t *pollcb,
                                       apr_pollfd_t *descriptor)
{
    apr_status_t rv;
    struct kevent ev;
    apr_os_sock_t fd;
    
    if (descriptor->desc_type == APR_POLL_SOCKET) {
        fd = descriptor->desc.s->socketdes;
    }
    else {
        fd = descriptor->desc.f->filedes;
    }

    rv = APR_NOTFOUND; /* unless at least one of the specified conditions is */
    if (descriptor->reqevents & APR_POLLIN) {
        EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        
        if (kevent(pollcb->fd, &ev, 1, NULL, 0, NULL) != -1) {
            rv = APR_SUCCESS;
        }
    }
    
    if (descriptor->reqevents & APR_POLLOUT) {
        EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        
        if (kevent(pollcb->fd, &ev, 1, NULL, 0, NULL) != -1) {
            rv = APR_SUCCESS;
        }
    }
    
    return rv;
}


static apr_status_t impl_pollcb_poll(apr_pollcb_t *pollcb,
                                     apr_interval_time_t timeout,
                                     apr_pollcb_cb_t func,
                                     void *baton)
{
    int ret, i;
    struct timespec tv, *tvptr;
    apr_status_t rv = APR_SUCCESS;
    
    if (timeout < 0) {
        tvptr = NULL;
    }
    else {
        tv.tv_sec = (long) apr_time_sec(timeout);
        tv.tv_nsec = (long) apr_time_usec(timeout) * 1000;
        tvptr = &tv;
    }
    
    ret = kevent(pollcb->fd, NULL, 0, pollcb->pollset.ke, 2 * pollcb->nalloc,
                 tvptr);

    if (ret < 0) {
        rv = apr_get_netos_error();
    }
    else if (ret == 0) {
        rv = APR_TIMEUP;
    }
    else {
        for (i = 0; i < ret; i++) {
            apr_pollfd_t *pollfd = (apr_pollfd_t *)(pollcb->pollset.ke[i].udata);
            
            pollfd->rtnevents = get_kqueue_revent(pollcb->pollset.ke[i].filter,
                                                  pollcb->pollset.ke[i].flags);
            
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
    "kqueue"
};

apr_pollcb_provider_t *apr_pollcb_provider_kqueue = &impl_cb;

#endif /* HAVE_KQUEUE */
