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

#ifndef APR_ARCH_POLL_PRIVATE_H
#define APR_ARCH_POLL_PRIVATE_H

#if HAVE_POLL_H
#include <poll.h>
#endif

#if HAVE_SYS_POLL_H
#include <sys/poll.h>
#endif

#ifdef HAVE_PORT_CREATE
#include <port.h>
#include <sys/port_impl.h>
#endif

#ifdef HAVE_KQUEUE
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#endif

#ifdef HAVE_EPOLL
#include <sys/epoll.h>
#endif

#ifdef NETWARE
#define HAS_SOCKETS(dt) (dt == APR_POLL_SOCKET) ? 1 : 0
#define HAS_PIPES(dt) (dt == APR_POLL_FILE) ? 1 : 0
#endif

#if defined(HAVE_AIO_H) && defined(HAVE_AIO_MSGQ)
#define _AIO_OS390	/* enable a bunch of z/OS aio.h definitions */
#include <aio.h>	/* aiocb	*/
#endif

/* Choose the best method platform specific to use in apr_pollset */
#ifdef HAVE_KQUEUE
#define POLLSET_USES_KQUEUE
#define POLLSET_DEFAULT_METHOD APR_POLLSET_KQUEUE
#elif defined(HAVE_PORT_CREATE)
#define POLLSET_USES_PORT
#define POLLSET_DEFAULT_METHOD APR_POLLSET_PORT
#elif defined(HAVE_EPOLL)
#define POLLSET_USES_EPOLL
#define POLLSET_DEFAULT_METHOD APR_POLLSET_EPOLL
#elif defined(HAVE_AIO_MSGQ)
#define POLLSET_USES_AIO_MSGQ
#define POLLSET_DEFAULT_METHOD APR_POLLSET_AIO_MSGQ
#elif defined(HAVE_POLL)
#define POLLSET_USES_POLL
#define POLLSET_DEFAULT_METHOD APR_POLLSET_POLL
#else
#define POLLSET_USES_SELECT
#define POLLSET_DEFAULT_METHOD APR_POLLSET_SELECT
#endif

#ifdef WIN32
#define POLL_USES_SELECT
#undef POLLSET_DEFAULT_METHOD
#define POLLSET_DEFAULT_METHOD APR_POLLSET_SELECT
#else
#ifdef HAVE_POLL
#define POLL_USES_POLL
#else
#define POLL_USES_SELECT
#endif
#endif

#if defined(POLLSET_USES_KQUEUE) || defined(POLLSET_USES_EPOLL) || defined(POLLSET_USES_PORT) || defined(POLLSET_USES_AIO_MSGQ)

#include "apr_ring.h"

#if APR_HAS_THREADS
#include "apr_thread_mutex.h"
#define pollset_lock_rings() \
    if (pollset->flags & APR_POLLSET_THREADSAFE) \
        apr_thread_mutex_lock(pollset->p->ring_lock);
#define pollset_unlock_rings() \
    if (pollset->flags & APR_POLLSET_THREADSAFE) \
        apr_thread_mutex_unlock(pollset->p->ring_lock);
#else
#define pollset_lock_rings()
#define pollset_unlock_rings()
#endif

typedef struct pfd_elem_t pfd_elem_t;

struct pfd_elem_t {
    APR_RING_ENTRY(pfd_elem_t) link;
    apr_pollfd_t pfd;
#ifdef HAVE_PORT_CREATE
   int on_query_ring;
#endif
};

#endif

typedef struct apr_pollset_private_t apr_pollset_private_t;
typedef struct apr_pollset_provider_t apr_pollset_provider_t;
typedef struct apr_pollcb_provider_t apr_pollcb_provider_t;

struct apr_pollset_t
{
    apr_pool_t *pool;
    apr_uint32_t nelts;
    apr_uint32_t nalloc;
    apr_uint32_t flags;
    /* Pipe descriptors used for wakeup */
    apr_file_t *wakeup_pipe[2];
    apr_pollfd_t wakeup_pfd;
    apr_pollset_private_t *p;
    apr_pollset_provider_t *provider;
};

typedef union {
#if defined(HAVE_EPOLL)
    struct epoll_event *epoll;
#endif
#if defined(HAVE_PORT_CREATE)
    port_event_t *port;
#endif
#if defined(HAVE_KQUEUE)
    struct kevent *ke;
#endif
#if defined(HAVE_POLL)
    struct pollfd *ps;
#endif
    void *undef;
} apr_pollcb_pset;

struct apr_pollcb_t {
    apr_pool_t *pool;
    apr_uint32_t nelts;
    apr_uint32_t nalloc;
    int fd;
    apr_pollcb_pset pollset;
    apr_pollfd_t **copyset;
    apr_pollcb_provider_t *provider;
};

struct apr_pollset_provider_t {
    apr_status_t (*create)(apr_pollset_t *, apr_uint32_t, apr_pool_t *, apr_uint32_t);
    apr_status_t (*add)(apr_pollset_t *, const apr_pollfd_t *);
    apr_status_t (*remove)(apr_pollset_t *, const apr_pollfd_t *);
    apr_status_t (*poll)(apr_pollset_t *, apr_interval_time_t, apr_int32_t *, const apr_pollfd_t **);
    apr_status_t (*cleanup)(apr_pollset_t *);
    const char *name;
};

struct apr_pollcb_provider_t {
    apr_status_t (*create)(apr_pollcb_t *, apr_uint32_t, apr_pool_t *, apr_uint32_t);
    apr_status_t (*add)(apr_pollcb_t *, apr_pollfd_t *);
    apr_status_t (*remove)(apr_pollcb_t *, apr_pollfd_t *);
    apr_status_t (*poll)(apr_pollcb_t *, apr_interval_time_t, apr_pollcb_cb_t, void *);
    const char *name;
};

/* Private functions */
void apr_pollset_drain_wakeup_pipe(apr_pollset_t *pollset);

#endif /* APR_ARCH_POLL_PRIVATE_H */
