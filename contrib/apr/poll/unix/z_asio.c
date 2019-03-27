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
 *
 *
 ******************************************************************************
 *
 * This implementation is based on a design by John Brooks (IBM Pok) which uses
 * the z/OS sockets async i/o facility.  When a
 * socket is added to the pollset, an async poll is issued for that individual
 * socket.  It specifies that the kernel should send an IPC message when the
 * socket becomes ready.  The IPC messages are sent to a single message queue
 * that is part of the pollset.  apr_pollset_poll waits on the arrival of IPC
 * messages or the specified timeout.
 *
 * Since z/OS does not support async i/o for pipes or files at present, this
 * implementation falls back to using ordinary poll() when
 * APR_POLLSET_THREADSAFE is unset.
 *
 * Greg Ames
 * April 2012
 */

#include "apr.h"
#include "apr_hash.h"
#include "apr_poll.h"
#include "apr_time.h"
#include "apr_portable.h"
#include "apr_arch_inherit.h"
#include "apr_arch_file_io.h"
#include "apr_arch_networkio.h"
#include "apr_arch_poll_private.h"

#ifdef HAVE_AIO_MSGQ

#include <sys/msg.h>  	/* msgget etc   */
#include <time.h>     	/* timestruct   */
#include <poll.h>     	/* pollfd       */
#include <limits.h>     /* MAX_INT      */

struct apr_pollset_private_t
{
    int             msg_q;              /* IPC message queue. The z/OS kernel sends messages
                                         * to this queue when our async polls on individual
                                         * file descriptors complete
                                         */
    apr_pollfd_t    *result_set;
    apr_uint32_t    size;

#if APR_HAS_THREADS
    /* A thread mutex to protect operations on the rings and the hash */
    apr_thread_mutex_t *ring_lock;
#endif

    /* A hash of all active elements used for O(1) _remove operations */
    apr_hash_t      *elems;

    APR_RING_HEAD(ready_ring_t,       asio_elem_t)      ready_ring;
    APR_RING_HEAD(prior_ready_ring_t, asio_elem_t)      prior_ready_ring;
    APR_RING_HEAD(free_ring_t,        asio_elem_t)      free_ring;

    /* for pipes etc with no asio */
    struct pollfd   *pollset;
    apr_pollfd_t    *query_set;
};

typedef enum {
    ASIO_INIT = 0,
    ASIO_REMOVED,
    ASIO_COMPLETE
} asio_state_e;

typedef struct asio_elem_t asio_elem_t;

struct asio_msgbuf_t {
    long         msg_type;       /* must be > 0 */
    asio_elem_t *msg_elem;
};

struct asio_elem_t
{
    APR_RING_ENTRY(asio_elem_t) link;
    apr_pollfd_t                pfd;
    struct pollfd               os_pfd;
    struct aiocb                a;
    asio_state_e                state;
    struct asio_msgbuf_t        msg;
};

#define DEBUG 0

/* DEBUG settings: 0 - no debug messages at all,
 *                 1 - should not occur messages,
 *                 2 - apr_pollset_* entry and exit messages,
 *                 3 - state changes, memory usage,
 *                 4 - z/OS, APR, and internal calls,
 *                 5 - everything else except the timer pop path,
 *                 6 - everything, including the Event 1 sec timer pop path
 *
 *  each DEBUG level includes all messages produced by lower numbered levels
 */

#if DEBUG

#include <assert.h>
#include <unistd.h>	/* getpid       */

#define DBG_BUFF char dbg_msg_buff[256];

#define DBG_TEST(lvl) if (lvl <= DEBUG) {

#define DBG_CORE(msg)               sprintf(dbg_msg_buff, "% 8d " __FUNCTION__ \
                                        " "  msg, getpid()),                   \
                                    fprintf(stderr, "%s", dbg_msg_buff);
#define DBG_CORE1(msg, var1)        sprintf(dbg_msg_buff, "% 8d " __FUNCTION__ \
                                        " " msg, getpid(), var1),              \
                                    fprintf(stderr, "%s", dbg_msg_buff);
#define DBG_CORE2(msg, var1, var2)  sprintf(dbg_msg_buff, "% 8d " __FUNCTION__ \
                                        " " msg, getpid(), var1, var2),        \
                                    fprintf(stderr, "%s", dbg_msg_buff);
#define DBG_CORE3(msg, var1, var2, var3)                                       \
                                    sprintf(dbg_msg_buff, "% 8d " __FUNCTION__ \
                                        " " msg, getpid(), var1, var2, var3),  \
                                    fprintf(stderr, "%s", dbg_msg_buff);
#define DBG_CORE4(msg, var1, var2, var3, var4)                                 \
                                    sprintf(dbg_msg_buff, "% 8d " __FUNCTION__ \
                                        " " msg, getpid(), var1, var2, var3, var4),\
                                    fprintf(stderr, "%s", dbg_msg_buff);

#define DBG_END }

#define DBG(lvl, msg)   DBG_TEST(lvl)   \
                        DBG_CORE(msg)   \
                        DBG_END

#define DBG1(lvl, msg, var1)    DBG_TEST(lvl)           \
                                DBG_CORE1(msg, var1)    \
                                DBG_END

#define DBG2(lvl, msg, var1, var2)      DBG_TEST(lvl)               \
                                        DBG_CORE2(msg, var1, var2)  \
                                        DBG_END

#define DBG3(lvl, msg, var1, var2, var3)                        \
                        DBG_TEST(lvl)                           \
                        DBG_CORE3(msg, var1, var2, var3)        \
                        DBG_END

#define DBG4(lvl, msg, var1, var2, var3, var4)                  \
                        DBG_TEST(lvl)                           \
                        DBG_CORE4(msg, var1, var2, var3, var4)  \
                        DBG_END

#else  /* DEBUG is 0 */
#define DBG_BUFF
#define DBG(lvl, msg)                            ((void)0)
#define DBG1(lvl, msg, var1)                     ((void)0)
#define DBG2(lvl, msg, var1, var2)               ((void)0)
#define DBG3(lvl, msg, var1, var2, var3)         ((void)0)
#define DBG4(lvl, msg, var1, var2, var3, var4)   ((void)0)

#endif /* DEBUG */

static int asyncio(struct aiocb *a)
{
    DBG_BUFF
    int rv;

#ifdef _LP64
#define AIO BPX4AIO
#else
#define AIO BPX1AIO
#endif

    AIO(sizeof(struct aiocb), a, &rv, &errno, __err2ad());
    DBG2(4, "BPX4AIO aiocb %p rv %d\n",
             a, rv);
#ifdef DEBUG
    if (rv < 0) {
        DBG2(4, "errno %d errnojr %08x\n",
                 errno, *__err2ad());
    }
#endif
    return rv;
}

static apr_int16_t get_event(apr_int16_t event)
{
    DBG_BUFF
    apr_int16_t rv = 0;
    DBG(4, "entered\n");

    if (event & APR_POLLIN)
        rv |= POLLIN;
    if (event & APR_POLLPRI)
        rv |= POLLPRI;
    if (event & APR_POLLOUT)
        rv |= POLLOUT;
    if (event & APR_POLLERR)
        rv |= POLLERR;
    if (event & APR_POLLHUP)
        rv |= POLLHUP;
    if (event & APR_POLLNVAL)
        rv |= POLLNVAL;

    DBG(4, "exiting\n");
    return rv;
}

static apr_int16_t get_revent(apr_int16_t event)
{
    DBG_BUFF
    apr_int16_t rv = 0;
    DBG(4, "entered\n");

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

    DBG(4, "exiting\n");
    return rv;
}

static apr_status_t asio_pollset_cleanup(apr_pollset_t *pollset)
{
    DBG_BUFF
    int rv;

    DBG(4, "entered\n");
    rv = msgctl(pollset->p->msg_q, IPC_RMID, NULL);

    DBG1(4, "exiting, msgctl(IPC_RMID) returned %d\n", rv);
    return rv;
}

static apr_status_t asio_pollset_create(apr_pollset_t *pollset,
                                        apr_uint32_t size,
                                        apr_pool_t *p,
                                        apr_uint32_t flags)
{
    DBG_BUFF
    apr_status_t rv;
    apr_pollset_private_t *priv;

    DBG1(2, "entered, flags: %x\n", flags);

    priv = pollset->p = apr_palloc(p, sizeof(*priv));

    if (flags & APR_POLLSET_THREADSAFE) {
#if APR_HAS_THREADS
        if (rv = apr_thread_mutex_create(&(priv->ring_lock),
                                           APR_THREAD_MUTEX_DEFAULT,
                                           p) != APR_SUCCESS) {
            DBG1(1, "apr_thread_mutex_create returned %d\n", rv);
            pollset->p = NULL;
            return rv;
        }
        rv = msgget(IPC_PRIVATE, S_IWUSR+S_IRUSR); /* user r/w perms */
        if (rv < 0) {
#if DEBUG
            perror(__FUNCTION__ " msgget returned < 0 ");
#endif
            pollset->p = NULL;
            return rv;
        }

        DBG2(4, "pollset %p msgget was OK, rv=%d\n", pollset, rv);
        priv->msg_q = rv;
        priv->elems   = apr_hash_make(p);

        APR_RING_INIT(&priv->free_ring, asio_elem_t, link);
        APR_RING_INIT(&priv->prior_ready_ring, asio_elem_t, link);

#else  /* APR doesn't have threads but caller wants a threadsafe pollset */
        pollset->p = NULL;
        return APR_ENOTIMPL;
#endif

    } else {  /* APR_POLLSET_THREADSAFE not set, i.e. no async i/o,
               * init fields only needed in old style pollset
               */

        priv->pollset = apr_palloc(p, size * sizeof(struct pollfd));
        priv->query_set = apr_palloc(p, size * sizeof(apr_pollfd_t));

        if ((!priv->pollset) || (!priv->query_set)) {
            pollset->p = NULL;
            return APR_ENOMEM;
        }
    }

    pollset->nelts   = 0;
    pollset->flags   = flags;
    pollset->pool    = p;
    priv->size    = size;
    priv->result_set = apr_palloc(p, size * sizeof(apr_pollfd_t));
    if (!priv->result_set) {
        if (flags & APR_POLLSET_THREADSAFE) {
            msgctl(priv->msg_q, IPC_RMID, NULL);
        }
        pollset->p = NULL;
        return APR_ENOMEM;
    }

    DBG2(2, "exiting, pollset: %p, type: %s\n",
             pollset,
             flags & APR_POLLSET_THREADSAFE ? "async" : "POSIX");


    return APR_SUCCESS;

} /* end of asio_pollset_create */

static apr_status_t posix_add(apr_pollset_t      *pollset,
                              const apr_pollfd_t *descriptor)
{
    DBG_BUFF
    int fd;
    apr_pool_t  *p = pollset->pool;
    apr_pollset_private_t *priv = pollset->p;

    DBG(4, "entered\n");

    if (pollset->nelts == priv->size) {
        return APR_ENOMEM;
    }

    priv->query_set[pollset->nelts] = *descriptor;
    if (descriptor->desc_type == APR_POLL_SOCKET) {
        fd = descriptor->desc.s->socketdes;
    }
    else {
        fd = descriptor->desc.f->filedes;
    }

    priv->pollset[pollset->nelts].fd = fd;

    priv->pollset[pollset->nelts].events =
        get_event(descriptor->reqevents);

    pollset->nelts++;

    DBG2(4, "exiting, fd %d added to pollset %p\n", fd, pollset);

    return APR_SUCCESS;
}   /* end of posix_add */


static apr_status_t asio_pollset_add(apr_pollset_t *pollset,
                                     const apr_pollfd_t *descriptor)
{
    DBG_BUFF
    asio_elem_t *elem;
    apr_status_t rv = APR_SUCCESS;
    apr_pollset_private_t *priv = pollset->p;

    pollset_lock_rings();
    DBG(2, "entered\n");

    if (pollset->flags & APR_POLLSET_THREADSAFE) {

        if (!APR_RING_EMPTY(&(priv->free_ring), asio_elem_t, link)) {
            elem = APR_RING_FIRST(&(priv->free_ring));
            APR_RING_REMOVE(elem, link);
            DBG1(3, "used recycled memory at %08p\n", elem);
            elem->state = ASIO_INIT;
            elem->a.aio_cflags = 0;
        }
        else {
            elem = (asio_elem_t *) apr_pcalloc(pollset->pool, sizeof(asio_elem_t));
            DBG1(3, "alloced new memory at %08p\n", elem);

            elem->a.aio_notifytype = AIO_MSGQ;
            elem->a.aio_msgev_qid  = priv->msg_q;
            DBG1(5, "aio_msgev_quid = %d \n", elem->a.aio_msgev_qid);
            elem->a.aio_msgev_size = sizeof(asio_elem_t *);
            elem->a.aio_msgev_flag = 0;     /* wait if queue is full */
            elem->a.aio_msgev_addr = &(elem->msg);
            elem->a.aio_buf        = &(elem->os_pfd);
            elem->a.aio_nbytes     = 1;     /* number of pfds to poll */
            elem->msg.msg_type     = 1;
            elem->msg.msg_elem     = elem;
        }

        /* z/OS only supports async I/O for sockets for now */
        elem->os_pfd.fd = descriptor->desc.s->socketdes;

        APR_RING_ELEM_INIT(elem, link);
        elem->a.aio_cmd       = AIO_SELPOLL;
        elem->a.aio_cflags    &= ~AIO_OK2COMPIMD; /* not OK to complete inline*/
        elem->pfd             = *descriptor;
        elem->os_pfd.events   = get_event(descriptor->reqevents);

        if (0 != asyncio(&elem->a)) {
            rv = errno;
            DBG3(4, "pollset %p asio failed fd %d, errno %p\n",
                     pollset, elem->os_pfd.fd, rv);
#if DEBUG
            perror(__FUNCTION__ " asio failure");
#endif
        }
        else {
            DBG2(4, "good asio call, adding fd %d to pollset %p\n",
                     elem->os_pfd.fd, pollset);

            pollset->nelts++;
            apr_hash_set(priv->elems, &(elem->os_pfd.fd), sizeof(int), elem);
        }
    }
    else {
        /* APR_POLLSET_THREADSAFE isn't set.  use POSIX poll in case
         * pipes or files are used with this pollset
         */

        rv = posix_add(pollset, descriptor);
    }

    DBG1(2, "exiting, rv = %d\n", rv);

    pollset_unlock_rings();
    return rv;
} /* end of asio_pollset_add */

static posix_remove(apr_pollset_t *pollset, const apr_pollfd_t *descriptor)
{
    DBG_BUFF
    apr_uint32_t i;
    apr_pollset_private_t *priv = pollset->p;

    DBG(4, "entered\n");
    for (i = 0; i < pollset->nelts; i++) {
        if (descriptor->desc.s == priv->query_set[i].desc.s) {
            /* Found an instance of the fd: remove this and any other copies */
            apr_uint32_t dst = i;
            apr_uint32_t old_nelts = pollset->nelts;
            pollset->nelts--;
            for (i++; i < old_nelts; i++) {
                if (descriptor->desc.s == priv->query_set[i].desc.s) {
                    pollset->nelts--;
                }
                else {
                    priv->pollset[dst] = priv->pollset[i];
                    priv->query_set[dst] = priv->query_set[i];
                    dst++;
                }
            }
            DBG(4, "returning OK\n");
            return APR_SUCCESS;
        }
    }

    DBG(1, "returning APR_NOTFOUND\n");
    return APR_NOTFOUND;

}   /* end of posix_remove */

static apr_status_t asio_pollset_remove(apr_pollset_t *pollset,
                                        const apr_pollfd_t *descriptor)
{
    DBG_BUFF
    asio_elem_t *elem;
    apr_status_t rv = APR_SUCCESS;
    apr_pollset_private_t *priv = pollset->p;
    struct aiocb cancel_a;   /* AIO_CANCEL is synchronous, so autodata works fine */

    int fd;

    DBG(2, "entered\n");

    if (!(pollset->flags & APR_POLLSET_THREADSAFE)) {
        return posix_remove(pollset, descriptor);
    }

    pollset_lock_rings();

#if DEBUG
    assert(descriptor->desc_type == APR_POLL_SOCKET);
#endif
    /* zOS 1.12 doesn't support files for async i/o */
    fd = descriptor->desc.s->socketdes;

    elem = apr_hash_get(priv->elems, &(fd), sizeof(int));
    if (elem == NULL) {
        DBG1(1, "couldn't find fd %d\n", fd);
        rv = APR_NOTFOUND;
    } else {
        DBG1(5, "hash found fd %d\n", fd);
        /* delete this fd from the hash */
        apr_hash_set(priv->elems, &(fd), sizeof(int), NULL);

        if (elem->state == ASIO_INIT) {
            /* asyncio call to cancel */
            cancel_a.aio_cmd = AIO_CANCEL;
            cancel_a.aio_buf = &elem->a;   /* point to original aiocb */

            cancel_a.aio_cflags  = 0;
            cancel_a.aio_cflags2 = 0;

            /* we want the original aiocb to show up on the pollset message queue 
             * before recycling its memory to eliminate race conditions
             */

            rv = asyncio(&cancel_a);
            DBG1(4, "asyncio returned %d\n", rv);

#if DEBUG
            assert(rv == 1);
#endif
        }
        elem->state = ASIO_REMOVED;
        rv = APR_SUCCESS;
    }

    DBG1(2, "exiting, rv: %d\n", rv);

    pollset_unlock_rings();

    return rv;
}   /* end of asio_pollset_remove */

static posix_poll(apr_pollset_t *pollset,
                  apr_interval_time_t timeout,
                  apr_int32_t *num,
                  const apr_pollfd_t **descriptors)
{
    DBG_BUFF
    int rv;
    apr_uint32_t i, j;
    apr_pollset_private_t *priv = pollset->p;

    DBG(4, "entered\n");

    if (timeout > 0) {
        timeout /= 1000;
    }
    rv = poll(priv->pollset, pollset->nelts, timeout);
    (*num) = rv;
    if (rv < 0) {
        return apr_get_netos_error();
    }
    if (rv == 0) {
        return APR_TIMEUP;
    }
    j = 0;
    for (i = 0; i < pollset->nelts; i++) {
        if (priv->pollset[i].revents != 0) {
            priv->result_set[j] = priv->query_set[i];
            priv->result_set[j].rtnevents =
                get_revent(priv->pollset[i].revents);
            j++;
        }
    }
    if (descriptors)
        *descriptors = priv->result_set;

    DBG(4, "exiting ok\n");
    return APR_SUCCESS;

}   /* end of posix_poll */

static process_msg(apr_pollset_t *pollset, struct asio_msgbuf_t *msg)
{
    DBG_BUFF
    asio_elem_t *elem = msg->msg_elem;

    switch(elem->state) {
    case ASIO_REMOVED:
        DBG2(5, "for cancelled elem, recycling memory - elem %08p, fd %d\n",
                elem, elem->os_pfd.fd);
        APR_RING_INSERT_TAIL(&(pollset->p->free_ring), elem,
                             asio_elem_t, link);
        break;
    case ASIO_INIT:
        DBG2(4, "adding to ready ring: elem %08p, fd %d\n",
                elem, elem->os_pfd.fd);
        elem->state = ASIO_COMPLETE;
        APR_RING_INSERT_TAIL(&(pollset->p->ready_ring), elem,
                             asio_elem_t, link);
        break;
    default:
        DBG3(1, "unexpected state: elem %08p, fd %d, state %d\n",
            elem, elem->os_pfd.fd, elem->state);
#if DEBUG
        assert(0);
#endif
    }
}

static apr_status_t asio_pollset_poll(apr_pollset_t *pollset,
                                      apr_interval_time_t timeout,
                                      apr_int32_t *num,
                                      const apr_pollfd_t **descriptors)
{
    DBG_BUFF
    int i, ret;
    asio_elem_t *elem, *next_elem;
    struct asio_msgbuf_t msg_buff;
    struct timespec tv;
    apr_status_t rv = APR_SUCCESS;
    apr_pollset_private_t *priv = pollset->p;

    DBG(6, "entered\n"); /* chatty - traces every second w/Event */

    if ((pollset->flags & APR_POLLSET_THREADSAFE) == 0 ) {
        return posix_poll(pollset, timeout, num, descriptors);
    }

    pollset_lock_rings();
    APR_RING_INIT(&(priv->ready_ring), asio_elem_t, link);

    while (!APR_RING_EMPTY(&(priv->prior_ready_ring), asio_elem_t, link)) {
        elem = APR_RING_FIRST(&(priv->prior_ready_ring));
        DBG3(5, "pollset %p elem %p fd %d on prior ready ring\n",
                pollset,
                elem,
                elem->os_pfd.fd);

        APR_RING_REMOVE(elem, link);

        /*
         * since USS does not remember what's in our pollset, we have
         * to re-add fds which have not been apr_pollset_remove'd
         *
         * there may have been too many ready fd's to return in the
         * result set last time. re-poll inline for both cases
         */

        if (elem->state == ASIO_REMOVED) {

            /* 
             * async i/o is done since it was found on prior_ready
             * the state says the caller is done with it too 
             * so recycle the elem 
             */
             
            APR_RING_INSERT_TAIL(&(priv->free_ring), elem,
                                 asio_elem_t, link);
            continue;  /* do not re-add if it has been _removed */
        }

        elem->state = ASIO_INIT;
        elem->a.aio_cflags     = AIO_OK2COMPIMD;

        if (0 != (ret = asyncio(&elem->a))) {
            if (ret == 1) {
                DBG(4, "asyncio() completed inline\n");
                /* it's ready now */
                elem->state = ASIO_COMPLETE;
                APR_RING_INSERT_TAIL(&(priv->ready_ring), elem, asio_elem_t,
                                     link);
            }
            else {
                DBG2(1, "asyncio() failed, ret: %d, errno: %d\n",
                        ret, errno);
                pollset_unlock_rings();
                return errno;
            }
        }
        DBG1(4, "asyncio() completed rc %d\n", ret);
    }

    DBG(6, "after prior ready loop\n"); /* chatty w/timeouts, hence 6 */

    /* Gather async poll completions that have occurred since the last call */
    while (0 < msgrcv(priv->msg_q, &msg_buff, sizeof(asio_elem_t *), 0,
                      IPC_NOWAIT)) {
        process_msg(pollset, &msg_buff);
    }

    /* Suspend if nothing is ready yet. */
    if (APR_RING_EMPTY(&(priv->ready_ring), asio_elem_t, link)) {

        if (timeout >= 0) {
            tv.tv_sec  = apr_time_sec(timeout);
            tv.tv_nsec = apr_time_usec(timeout) * 1000;
        } else {
            tv.tv_sec = INT_MAX;  /* block until something is ready */
        }

        DBG2(6, "nothing on the ready ring "
                "- blocking for %d seconds %d ns\n",
                tv.tv_sec, tv.tv_nsec);

        pollset_unlock_rings();   /* allow other apr_pollset_* calls while blocked */

        if (0 >= (ret = __msgrcv_timed(priv->msg_q, &msg_buff,
                                       sizeof(asio_elem_t *), 0, NULL, &tv))) {
#if DEBUG
            if (errno == EAGAIN) {
                DBG(6, "__msgrcv_timed timed out\n"); /* timeout path, so 6 */
            }
            else {
                DBG(1, "__msgrcv_timed failed!\n");
            }
#endif
            return (errno == EAGAIN) ? APR_TIMEUP : errno;
        }

        pollset_lock_rings();

        process_msg(pollset, &msg_buff);
    }

    APR_RING_INIT(&priv->prior_ready_ring, asio_elem_t, link);

    (*num) = 0;
    elem = APR_RING_FIRST(&(priv->ready_ring));

    for (i = 0;

        i < priv->size
                && elem != APR_RING_SENTINEL(&(priv->ready_ring), asio_elem_t, link);
        i++) {
             DBG2(5, "ready ring: elem %08p, fd %d\n", elem, elem->os_pfd.fd);

             priv->result_set[i] = elem->pfd;
             priv->result_set[i].rtnevents
                                    = get_revent(elem->os_pfd.revents);
             (*num)++;

             elem = APR_RING_NEXT(elem, link);

#if DEBUG
             if (elem == APR_RING_SENTINEL(&(priv->ready_ring), asio_elem_t, link)) {
                 DBG(5, "end of ready ring reached\n");
             }
#endif
    }

    if (descriptors) {
        *descriptors = priv->result_set;
    }

    /* if the result size is too small, remember which descriptors
     * haven't had results reported yet.  we will look
     * at these descriptors on the next apr_pollset_poll call
     */

    APR_RING_CONCAT(&priv->prior_ready_ring, &(priv->ready_ring), asio_elem_t, link);

    DBG1(2, "exiting, rv = %d\n", rv);

    pollset_unlock_rings();

    return rv;
}  /* end of asio_pollset_poll */

static apr_pollset_provider_t impl = {
    asio_pollset_create,
    asio_pollset_add,
    asio_pollset_remove,
    asio_pollset_poll,
    asio_pollset_cleanup,
    "asio"
};

apr_pollset_provider_t *apr_pollset_provider_aio_msgq = &impl;

#endif /* HAVE_AIO_MSGQ */
