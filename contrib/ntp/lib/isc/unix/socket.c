/*
 * Copyright (C) 2004-2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <isc/buffer.h>
#include <isc/bufferlist.h>
#include <isc/condition.h>
#include <isc/formatcheck.h>
#include <isc/list.h>
#include <isc/log.h>
#include <isc/mem.h>
#include <isc/msgs.h>
#include <isc/mutex.h>
#include <isc/net.h>
#include <isc/once.h>
#include <isc/platform.h>
#include <isc/print.h>
#include <isc/region.h>
#include <isc/socket.h>
#include <isc/stats.h>
#include <isc/strerror.h>
#include <isc/task.h>
#include <isc/thread.h>
#include <isc/util.h>
#include <isc/xml.h>

#ifdef ISC_PLATFORM_HAVESYSUNH
#include <sys/un.h>
#endif
#ifdef ISC_PLATFORM_HAVEKQUEUE
#include <sys/event.h>
#endif
#ifdef ISC_PLATFORM_HAVEEPOLL
#include <sys/epoll.h>
#endif
#ifdef ISC_PLATFORM_HAVEDEVPOLL
#if defined(HAVE_SYS_DEVPOLL_H)
#include <sys/devpoll.h>
#elif defined(HAVE_DEVPOLL_H)
#include <devpoll.h>
#endif
#endif

#include "errno2result.h"

/* See task.c about the following definition: */
#ifdef BIND9
#ifdef ISC_PLATFORM_USETHREADS
#define USE_WATCHER_THREAD
#else
#define USE_SHARED_MANAGER
#endif	/* ISC_PLATFORM_USETHREADS */
#endif	/* BIND9 */

#ifndef USE_WATCHER_THREAD
#include "socket_p.h"
#include "../task_p.h"
#endif /* USE_WATCHER_THREAD */

#if defined(SO_BSDCOMPAT) && defined(__linux__)
#include <sys/utsname.h>
#endif

/*%
 * Choose the most preferable multiplex method.
 */
#ifdef ISC_PLATFORM_HAVEKQUEUE
#define USE_KQUEUE
#elif defined (ISC_PLATFORM_HAVEEPOLL)
#define USE_EPOLL
#elif defined (ISC_PLATFORM_HAVEDEVPOLL)
#define USE_DEVPOLL
typedef struct {
	unsigned int want_read : 1,
		want_write : 1;
} pollinfo_t;
#else
#define USE_SELECT
#endif	/* ISC_PLATFORM_HAVEKQUEUE */

#ifndef USE_WATCHER_THREAD
#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_DEVPOLL)
struct isc_socketwait {
	int nevents;
};
#elif defined (USE_SELECT)
struct isc_socketwait {
	fd_set *readset;
	fd_set *writeset;
	int nfds;
	int maxfd;
};
#endif	/* USE_KQUEUE */
#endif /* !USE_WATCHER_THREAD */

/*%
 * Maximum number of allowable open sockets.  This is also the maximum
 * allowable socket file descriptor.
 *
 * Care should be taken before modifying this value for select():
 * The API standard doesn't ensure select() accept more than (the system default
 * of) FD_SETSIZE descriptors, and the default size should in fact be fine in
 * the vast majority of cases.  This constant should therefore be increased only
 * when absolutely necessary and possible, i.e., the server is exhausting all
 * available file descriptors (up to FD_SETSIZE) and the select() function
 * and FD_xxx macros support larger values than FD_SETSIZE (which may not
 * always by true, but we keep using some of them to ensure as much
 * portability as possible).  Note also that overall server performance
 * may be rather worsened with a larger value of this constant due to
 * inherent scalability problems of select().
 *
 * As a special note, this value shouldn't have to be touched if
 * this is a build for an authoritative only DNS server.
 */
#ifndef ISC_SOCKET_MAXSOCKETS
#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_DEVPOLL)
#define ISC_SOCKET_MAXSOCKETS 4096
#elif defined(USE_SELECT)
#define ISC_SOCKET_MAXSOCKETS FD_SETSIZE
#endif	/* USE_KQUEUE... */
#endif	/* ISC_SOCKET_MAXSOCKETS */

#ifdef USE_SELECT
/*%
 * Mac OS X needs a special definition to support larger values in select().
 * We always define this because a larger value can be specified run-time.
 */
#ifdef __APPLE__
#define _DARWIN_UNLIMITED_SELECT
#endif	/* __APPLE__ */
#endif	/* USE_SELECT */

#ifdef ISC_SOCKET_USE_POLLWATCH
/*%
 * If this macro is defined, enable workaround for a Solaris /dev/poll kernel
 * bug: DP_POLL ioctl could keep sleeping even if socket I/O is possible for
 * some of the specified FD.  The idea is based on the observation that it's
 * likely for a busy server to keep receiving packets.  It specifically works
 * as follows: the socket watcher is first initialized with the state of
 * "poll_idle".  While it's in the idle state it keeps sleeping until a socket
 * event occurs.  When it wakes up for a socket I/O event, it moves to the
 * poll_active state, and sets the poll timeout to a short period
 * (ISC_SOCKET_POLLWATCH_TIMEOUT msec).  If timeout occurs in this state, the
 * watcher goes to the poll_checking state with the same timeout period.
 * In this state, the watcher tries to detect whether this is a break
 * during intermittent events or the kernel bug is triggered.  If the next
 * polling reports an event within the short period, the previous timeout is
 * likely to be a kernel bug, and so the watcher goes back to the active state.
 * Otherwise, it moves to the idle state again.
 *
 * It's not clear whether this is a thread-related bug, but since we've only
 * seen this with threads, this workaround is used only when enabling threads.
 */

typedef enum { poll_idle, poll_active, poll_checking } pollstate_t;

#ifndef ISC_SOCKET_POLLWATCH_TIMEOUT
#define ISC_SOCKET_POLLWATCH_TIMEOUT 10
#endif	/* ISC_SOCKET_POLLWATCH_TIMEOUT */
#endif	/* ISC_SOCKET_USE_POLLWATCH */

/*%
 * Size of per-FD lock buckets.
 */
#ifdef ISC_PLATFORM_USETHREADS
#define FDLOCK_COUNT		1024
#define FDLOCK_ID(fd)		((fd) % FDLOCK_COUNT)
#else
#define FDLOCK_COUNT		1
#define FDLOCK_ID(fd)		0
#endif	/* ISC_PLATFORM_USETHREADS */

/*%
 * Maximum number of events communicated with the kernel.  There should normally
 * be no need for having a large number.
 */
#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_DEVPOLL)
#ifndef ISC_SOCKET_MAXEVENTS
#define ISC_SOCKET_MAXEVENTS	64
#endif
#endif

/*%
 * Some systems define the socket length argument as an int, some as size_t,
 * some as socklen_t.  This is here so it can be easily changed if needed.
 */
#ifndef ISC_SOCKADDR_LEN_T
#define ISC_SOCKADDR_LEN_T unsigned int
#endif

/*%
 * Define what the possible "soft" errors can be.  These are non-fatal returns
 * of various network related functions, like recv() and so on.
 *
 * For some reason, BSDI (and perhaps others) will sometimes return <0
 * from recv() but will have errno==0.  This is broken, but we have to
 * work around it here.
 */
#define SOFT_ERROR(e)	((e) == EAGAIN || \
			 (e) == EWOULDBLOCK || \
			 (e) == EINTR || \
			 (e) == 0)

#define DLVL(x) ISC_LOGCATEGORY_GENERAL, ISC_LOGMODULE_SOCKET, ISC_LOG_DEBUG(x)

/*!<
 * DLVL(90)  --  Function entry/exit and other tracing.
 * DLVL(70)  --  Socket "correctness" -- including returning of events, etc.
 * DLVL(60)  --  Socket data send/receive
 * DLVL(50)  --  Event tracing, including receiving/sending completion events.
 * DLVL(20)  --  Socket creation/destruction.
 */
#define TRACE_LEVEL		90
#define CORRECTNESS_LEVEL	70
#define IOEVENT_LEVEL		60
#define EVENT_LEVEL		50
#define CREATION_LEVEL		20

#define TRACE		DLVL(TRACE_LEVEL)
#define CORRECTNESS	DLVL(CORRECTNESS_LEVEL)
#define IOEVENT		DLVL(IOEVENT_LEVEL)
#define EVENT		DLVL(EVENT_LEVEL)
#define CREATION	DLVL(CREATION_LEVEL)

typedef isc_event_t intev_t;

#define SOCKET_MAGIC		ISC_MAGIC('I', 'O', 'i', 'o')
#define VALID_SOCKET(s)		ISC_MAGIC_VALID(s, SOCKET_MAGIC)

/*!
 * IPv6 control information.  If the socket is an IPv6 socket we want
 * to collect the destination address and interface so the client can
 * set them on outgoing packets.
 */
#ifdef ISC_PLATFORM_HAVEIN6PKTINFO
#ifndef USE_CMSG
#define USE_CMSG	1
#endif
#endif

/*%
 * NetBSD and FreeBSD can timestamp packets.  XXXMLG Should we have
 * a setsockopt() like interface to request timestamps, and if the OS
 * doesn't do it for us, call gettimeofday() on every UDP receive?
 */
#ifdef SO_TIMESTAMP
#ifndef USE_CMSG
#define USE_CMSG	1
#endif
#endif

/*%
 * The size to raise the receive buffer to (from BIND 8).
 */
#define RCVBUFSIZE (32*1024)

/*%
 * The number of times a send operation is repeated if the result is EINTR.
 */
#define NRETRIES 10

typedef struct isc__socket isc__socket_t;
typedef struct isc__socketmgr isc__socketmgr_t;

#define NEWCONNSOCK(ev) ((isc__socket_t *)(ev)->newsocket)

struct isc__socket {
	/* Not locked. */
	isc_socket_t		common;
	isc__socketmgr_t	*manager;
	isc_mutex_t		lock;
	isc_sockettype_t	type;
	const isc_statscounter_t	*statsindex;

	/* Locked by socket lock. */
	ISC_LINK(isc__socket_t)	link;
	unsigned int		references;
	int			fd;
	int			pf;
	char				name[16];
	void *				tag;

	ISC_LIST(isc_socketevent_t)		send_list;
	ISC_LIST(isc_socketevent_t)		recv_list;
	ISC_LIST(isc_socket_newconnev_t)	accept_list;
	isc_socket_connev_t		       *connect_ev;

	/*
	 * Internal events.  Posted when a descriptor is readable or
	 * writable.  These are statically allocated and never freed.
	 * They will be set to non-purgable before use.
	 */
	intev_t			readable_ev;
	intev_t			writable_ev;

	isc_sockaddr_t		peer_address;  /* remote address */

	unsigned int		pending_recv : 1,
				pending_send : 1,
				pending_accept : 1,
				listener : 1, /* listener socket */
				connected : 1,
				connecting : 1, /* connect pending */
				bound : 1, /* bound to local addr */
				dupped : 1;

#ifdef ISC_NET_RECVOVERFLOW
	unsigned char		overflow; /* used for MSG_TRUNC fake */
#endif

	char			*recvcmsgbuf;
	ISC_SOCKADDR_LEN_T	recvcmsgbuflen;
	char			*sendcmsgbuf;
	ISC_SOCKADDR_LEN_T	sendcmsgbuflen;

	void			*fdwatcharg;
	isc_sockfdwatch_t	fdwatchcb;
	int			fdwatchflags;
	isc_task_t		*fdwatchtask;
};

#define SOCKET_MANAGER_MAGIC	ISC_MAGIC('I', 'O', 'm', 'g')
#define VALID_MANAGER(m)	ISC_MAGIC_VALID(m, SOCKET_MANAGER_MAGIC)

struct isc__socketmgr {
	/* Not locked. */
	isc_socketmgr_t		common;
	isc_mem_t	       *mctx;
	isc_mutex_t		lock;
	isc_mutex_t		*fdlock;
	isc_stats_t		*stats;
#ifdef USE_KQUEUE
	int			kqueue_fd;
	int			nevents;
	struct kevent		*events;
#endif	/* USE_KQUEUE */
#ifdef USE_EPOLL
	int			epoll_fd;
	int			nevents;
	struct epoll_event	*events;
#endif	/* USE_EPOLL */
#ifdef USE_DEVPOLL
	int			devpoll_fd;
	int			nevents;
	struct pollfd		*events;
#endif	/* USE_DEVPOLL */
#ifdef USE_SELECT
	int			fd_bufsize;
#endif	/* USE_SELECT */
	unsigned int		maxsocks;
#ifdef ISC_PLATFORM_USETHREADS
	int			pipe_fds[2];
#endif

	/* Locked by fdlock. */
	isc__socket_t	       **fds;
	int			*fdstate;
#ifdef USE_DEVPOLL
	pollinfo_t		*fdpollinfo;
#endif

	/* Locked by manager lock. */
	ISC_LIST(isc__socket_t)	socklist;
#ifdef USE_SELECT
	fd_set			*read_fds;
	fd_set			*read_fds_copy;
	fd_set			*write_fds;
	fd_set			*write_fds_copy;
	int			maxfd;
#endif	/* USE_SELECT */
	int			reserved;	/* unlocked */
#ifdef USE_WATCHER_THREAD
	isc_thread_t		watcher;
	isc_condition_t		shutdown_ok;
#else /* USE_WATCHER_THREAD */
	unsigned int		refs;
#endif /* USE_WATCHER_THREAD */
	int			maxudp;
};

#ifdef USE_SHARED_MANAGER
static isc__socketmgr_t *socketmgr = NULL;
#endif /* USE_SHARED_MANAGER */

#define CLOSED			0	/* this one must be zero */
#define MANAGED			1
#define CLOSE_PENDING		2

/*
 * send() and recv() iovec counts
 */
#define MAXSCATTERGATHER_SEND	(ISC_SOCKET_MAXSCATTERGATHER)
#ifdef ISC_NET_RECVOVERFLOW
# define MAXSCATTERGATHER_RECV	(ISC_SOCKET_MAXSCATTERGATHER + 1)
#else
# define MAXSCATTERGATHER_RECV	(ISC_SOCKET_MAXSCATTERGATHER)
#endif

static isc_result_t socket_create(isc_socketmgr_t *manager0, int pf,
				  isc_sockettype_t type,
				  isc_socket_t **socketp,
				  isc_socket_t *dup_socket);
static void send_recvdone_event(isc__socket_t *, isc_socketevent_t **);
static void send_senddone_event(isc__socket_t *, isc_socketevent_t **);
static void free_socket(isc__socket_t **);
static isc_result_t allocate_socket(isc__socketmgr_t *, isc_sockettype_t,
				    isc__socket_t **);
static void destroy(isc__socket_t **);
static void internal_accept(isc_task_t *, isc_event_t *);
static void internal_connect(isc_task_t *, isc_event_t *);
static void internal_recv(isc_task_t *, isc_event_t *);
static void internal_send(isc_task_t *, isc_event_t *);
static void internal_fdwatch_write(isc_task_t *, isc_event_t *);
static void internal_fdwatch_read(isc_task_t *, isc_event_t *);
static void process_cmsg(isc__socket_t *, struct msghdr *, isc_socketevent_t *);
static void build_msghdr_send(isc__socket_t *, isc_socketevent_t *,
			      struct msghdr *, struct iovec *, size_t *);
static void build_msghdr_recv(isc__socket_t *, isc_socketevent_t *,
			      struct msghdr *, struct iovec *, size_t *);
#ifdef USE_WATCHER_THREAD
static isc_boolean_t process_ctlfd(isc__socketmgr_t *manager);
#endif

/*%
 * The following can be either static or public, depending on build environment.
 */

#ifdef BIND9
#define ISC_SOCKETFUNC_SCOPE
#else
#define ISC_SOCKETFUNC_SCOPE static
#endif

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_create(isc_socketmgr_t *manager, int pf, isc_sockettype_t type,
		   isc_socket_t **socketp);
ISC_SOCKETFUNC_SCOPE void
isc__socket_attach(isc_socket_t *sock, isc_socket_t **socketp);
ISC_SOCKETFUNC_SCOPE void
isc__socket_detach(isc_socket_t **socketp);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socketmgr_create(isc_mem_t *mctx, isc_socketmgr_t **managerp);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socketmgr_create2(isc_mem_t *mctx, isc_socketmgr_t **managerp,
		       unsigned int maxsocks);
ISC_SOCKETFUNC_SCOPE void
isc__socketmgr_destroy(isc_socketmgr_t **managerp);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_recvv(isc_socket_t *sock, isc_bufferlist_t *buflist,
		 unsigned int minimum, isc_task_t *task,
		  isc_taskaction_t action, const void *arg);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_recv(isc_socket_t *sock, isc_region_t *region,
		 unsigned int minimum, isc_task_t *task,
		 isc_taskaction_t action, const void *arg);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_recv2(isc_socket_t *sock, isc_region_t *region,
		  unsigned int minimum, isc_task_t *task,
		  isc_socketevent_t *event, unsigned int flags);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_send(isc_socket_t *sock, isc_region_t *region,
		 isc_task_t *task, isc_taskaction_t action, const void *arg);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendto(isc_socket_t *sock, isc_region_t *region,
		   isc_task_t *task, isc_taskaction_t action, const void *arg,
		   isc_sockaddr_t *address, struct in6_pktinfo *pktinfo);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendv(isc_socket_t *sock, isc_bufferlist_t *buflist,
		  isc_task_t *task, isc_taskaction_t action, const void *arg);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendtov(isc_socket_t *sock, isc_bufferlist_t *buflist,
		    isc_task_t *task, isc_taskaction_t action, const void *arg,
		    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendto2(isc_socket_t *sock, isc_region_t *region,
		    isc_task_t *task,
		    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo,
		    isc_socketevent_t *event, unsigned int flags);
ISC_SOCKETFUNC_SCOPE void
isc__socket_cleanunix(isc_sockaddr_t *sockaddr, isc_boolean_t active);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_permunix(isc_sockaddr_t *sockaddr, isc_uint32_t perm,
		     isc_uint32_t owner, isc_uint32_t group);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_bind(isc_socket_t *sock, isc_sockaddr_t *sockaddr,
		 unsigned int options);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_filter(isc_socket_t *sock, const char *filter);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_listen(isc_socket_t *sock, unsigned int backlog);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_accept(isc_socket_t *sock,
		   isc_task_t *task, isc_taskaction_t action, const void *arg);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_connect(isc_socket_t *sock, isc_sockaddr_t *addr,
		    isc_task_t *task, isc_taskaction_t action,
		    const void *arg);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_getpeername(isc_socket_t *sock, isc_sockaddr_t *addressp);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_getsockname(isc_socket_t *sock, isc_sockaddr_t *addressp);
ISC_SOCKETFUNC_SCOPE void
isc__socket_cancel(isc_socket_t *sock, isc_task_t *task, unsigned int how);
ISC_SOCKETFUNC_SCOPE isc_sockettype_t
isc__socket_gettype(isc_socket_t *sock);
ISC_SOCKETFUNC_SCOPE isc_boolean_t
isc__socket_isbound(isc_socket_t *sock);
ISC_SOCKETFUNC_SCOPE void
isc__socket_ipv6only(isc_socket_t *sock, isc_boolean_t yes);
#if defined(HAVE_LIBXML2) && defined(BIND9)
ISC_SOCKETFUNC_SCOPE void
isc__socketmgr_renderxml(isc_socketmgr_t *mgr0, xmlTextWriterPtr writer);
#endif

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_fdwatchcreate(isc_socketmgr_t *manager, int fd, int flags,
			  isc_sockfdwatch_t callback, void *cbarg,
			  isc_task_t *task, isc_socket_t **socketp);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_fdwatchpoke(isc_socket_t *sock, int flags);
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_dup(isc_socket_t *sock, isc_socket_t **socketp);
ISC_SOCKETFUNC_SCOPE int
isc__socket_getfd(isc_socket_t *sock);

static struct {
	isc_socketmethods_t methods;

	/*%
	 * The following are defined just for avoiding unused static functions.
	 */
#ifndef BIND9
	void *recvv, *send, *sendv, *sendto2, *cleanunix, *permunix, *filter,
		*listen, *accept, *getpeername, *isbound;
#endif
} socketmethods = {
	{
		isc__socket_attach,
		isc__socket_detach,
		isc__socket_bind,
		isc__socket_sendto,
		isc__socket_connect,
		isc__socket_recv,
		isc__socket_cancel,
		isc__socket_getsockname,
		isc__socket_gettype,
		isc__socket_ipv6only,
		isc__socket_fdwatchpoke,
		isc__socket_dup,
		isc__socket_getfd
	}
#ifndef BIND9
	,
	(void *)isc__socket_recvv, (void *)isc__socket_send,
	(void *)isc__socket_sendv, (void *)isc__socket_sendto2,
	(void *)isc__socket_cleanunix, (void *)isc__socket_permunix,
	(void *)isc__socket_filter, (void *)isc__socket_listen,
	(void *)isc__socket_accept, (void *)isc__socket_getpeername,
	(void *)isc__socket_isbound
#endif
};

static isc_socketmgrmethods_t socketmgrmethods = {
	isc__socketmgr_destroy,
	isc__socket_create,
	isc__socket_fdwatchcreate
};

#define SELECT_POKE_SHUTDOWN		(-1)
#define SELECT_POKE_NOTHING		(-2)
#define SELECT_POKE_READ		(-3)
#define SELECT_POKE_ACCEPT		(-3) /*%< Same as _READ */
#define SELECT_POKE_WRITE		(-4)
#define SELECT_POKE_CONNECT		(-4) /*%< Same as _WRITE */
#define SELECT_POKE_CLOSE		(-5)

#define SOCK_DEAD(s)			((s)->references == 0)

/*%
 * Shortcut index arrays to get access to statistics counters.
 */
enum {
	STATID_OPEN = 0,
	STATID_OPENFAIL = 1,
	STATID_CLOSE = 2,
	STATID_BINDFAIL = 3,
	STATID_CONNECTFAIL = 4,
	STATID_CONNECT = 5,
	STATID_ACCEPTFAIL = 6,
	STATID_ACCEPT = 7,
	STATID_SENDFAIL = 8,
	STATID_RECVFAIL = 9
};
static const isc_statscounter_t upd4statsindex[] = {
	isc_sockstatscounter_udp4open,
	isc_sockstatscounter_udp4openfail,
	isc_sockstatscounter_udp4close,
	isc_sockstatscounter_udp4bindfail,
	isc_sockstatscounter_udp4connectfail,
	isc_sockstatscounter_udp4connect,
	-1,
	-1,
	isc_sockstatscounter_udp4sendfail,
	isc_sockstatscounter_udp4recvfail
};
static const isc_statscounter_t upd6statsindex[] = {
	isc_sockstatscounter_udp6open,
	isc_sockstatscounter_udp6openfail,
	isc_sockstatscounter_udp6close,
	isc_sockstatscounter_udp6bindfail,
	isc_sockstatscounter_udp6connectfail,
	isc_sockstatscounter_udp6connect,
	-1,
	-1,
	isc_sockstatscounter_udp6sendfail,
	isc_sockstatscounter_udp6recvfail
};
static const isc_statscounter_t tcp4statsindex[] = {
	isc_sockstatscounter_tcp4open,
	isc_sockstatscounter_tcp4openfail,
	isc_sockstatscounter_tcp4close,
	isc_sockstatscounter_tcp4bindfail,
	isc_sockstatscounter_tcp4connectfail,
	isc_sockstatscounter_tcp4connect,
	isc_sockstatscounter_tcp4acceptfail,
	isc_sockstatscounter_tcp4accept,
	isc_sockstatscounter_tcp4sendfail,
	isc_sockstatscounter_tcp4recvfail
};
static const isc_statscounter_t tcp6statsindex[] = {
	isc_sockstatscounter_tcp6open,
	isc_sockstatscounter_tcp6openfail,
	isc_sockstatscounter_tcp6close,
	isc_sockstatscounter_tcp6bindfail,
	isc_sockstatscounter_tcp6connectfail,
	isc_sockstatscounter_tcp6connect,
	isc_sockstatscounter_tcp6acceptfail,
	isc_sockstatscounter_tcp6accept,
	isc_sockstatscounter_tcp6sendfail,
	isc_sockstatscounter_tcp6recvfail
};
static const isc_statscounter_t unixstatsindex[] = {
	isc_sockstatscounter_unixopen,
	isc_sockstatscounter_unixopenfail,
	isc_sockstatscounter_unixclose,
	isc_sockstatscounter_unixbindfail,
	isc_sockstatscounter_unixconnectfail,
	isc_sockstatscounter_unixconnect,
	isc_sockstatscounter_unixacceptfail,
	isc_sockstatscounter_unixaccept,
	isc_sockstatscounter_unixsendfail,
	isc_sockstatscounter_unixrecvfail
};
static const isc_statscounter_t fdwatchstatsindex[] = {
	-1,
	-1,
	isc_sockstatscounter_fdwatchclose,
	isc_sockstatscounter_fdwatchbindfail,
	isc_sockstatscounter_fdwatchconnectfail,
	isc_sockstatscounter_fdwatchconnect,
	-1,
	-1,
	isc_sockstatscounter_fdwatchsendfail,
	isc_sockstatscounter_fdwatchrecvfail
};

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_DEVPOLL) || \
    defined(USE_WATCHER_THREAD)
static void
manager_log(isc__socketmgr_t *sockmgr,
	    isc_logcategory_t *category, isc_logmodule_t *module, int level,
	    const char *fmt, ...) ISC_FORMAT_PRINTF(5, 6);
static void
manager_log(isc__socketmgr_t *sockmgr,
	    isc_logcategory_t *category, isc_logmodule_t *module, int level,
	    const char *fmt, ...)
{
	char msgbuf[2048];
	va_list ap;

	if (! isc_log_wouldlog(isc_lctx, level))
		return;

	va_start(ap, fmt);
	vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
	va_end(ap);

	isc_log_write(isc_lctx, category, module, level,
		      "sockmgr %p: %s", sockmgr, msgbuf);
}
#endif

static void
socket_log(isc__socket_t *sock, isc_sockaddr_t *address,
	   isc_logcategory_t *category, isc_logmodule_t *module, int level,
	   isc_msgcat_t *msgcat, int msgset, int message,
	   const char *fmt, ...) ISC_FORMAT_PRINTF(9, 10);
static void
socket_log(isc__socket_t *sock, isc_sockaddr_t *address,
	   isc_logcategory_t *category, isc_logmodule_t *module, int level,
	   isc_msgcat_t *msgcat, int msgset, int message,
	   const char *fmt, ...)
{
	char msgbuf[2048];
	char peerbuf[ISC_SOCKADDR_FORMATSIZE];
	va_list ap;

	if (! isc_log_wouldlog(isc_lctx, level))
		return;

	va_start(ap, fmt);
	vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
	va_end(ap);

	if (address == NULL) {
		isc_log_iwrite(isc_lctx, category, module, level,
			       msgcat, msgset, message,
			       "socket %p: %s", sock, msgbuf);
	} else {
		isc_sockaddr_format(address, peerbuf, sizeof(peerbuf));
		isc_log_iwrite(isc_lctx, category, module, level,
			       msgcat, msgset, message,
			       "socket %p %s: %s", sock, peerbuf, msgbuf);
	}
}

#if defined(_AIX) && defined(ISC_NET_BSD44MSGHDR) && \
    defined(USE_CMSG) && defined(IPV6_RECVPKTINFO)
/*
 * AIX has a kernel bug where IPV6_RECVPKTINFO gets cleared by
 * setting IPV6_V6ONLY.
 */
static void
FIX_IPV6_RECVPKTINFO(isc__socket_t *sock)
{
	char strbuf[ISC_STRERRORSIZE];
	int on = 1;

	if (sock->pf != AF_INET6 || sock->type != isc_sockettype_udp)
		return;

	if (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_RECVPKTINFO,
		       (void *)&on, sizeof(on)) < 0) {

		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "setsockopt(%d, IPV6_RECVPKTINFO) "
				 "%s: %s", sock->fd,
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED,
						"failed"),
				 strbuf);
	}
}
#else
#define FIX_IPV6_RECVPKTINFO(sock) (void)0
#endif

/*%
 * Increment socket-related statistics counters.
 */
static inline void
inc_stats(isc_stats_t *stats, isc_statscounter_t counterid) {
	REQUIRE(counterid != -1);

	if (stats != NULL)
		isc_stats_increment(stats, counterid);
}

static inline isc_result_t
watch_fd(isc__socketmgr_t *manager, int fd, int msg) {
	isc_result_t result = ISC_R_SUCCESS;

#ifdef USE_KQUEUE
	struct kevent evchange;

	memset(&evchange, 0, sizeof(evchange));
	if (msg == SELECT_POKE_READ)
		evchange.filter = EVFILT_READ;
	else
		evchange.filter = EVFILT_WRITE;
	evchange.flags = EV_ADD;
	evchange.ident = fd;
	if (kevent(manager->kqueue_fd, &evchange, 1, NULL, 0, NULL) != 0)
		result = isc__errno2result(errno);

	return (result);
#elif defined(USE_EPOLL)
	struct epoll_event event;

	if (msg == SELECT_POKE_READ)
		event.events = EPOLLIN;
	else
		event.events = EPOLLOUT;
	memset(&event.data, 0, sizeof(event.data));
	event.data.fd = fd;
	if (epoll_ctl(manager->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1 &&
	    errno != EEXIST) {
		result = isc__errno2result(errno);
	}

	return (result);
#elif defined(USE_DEVPOLL)
	struct pollfd pfd;
	int lockid = FDLOCK_ID(fd);

	memset(&pfd, 0, sizeof(pfd));
	if (msg == SELECT_POKE_READ)
		pfd.events = POLLIN;
	else
		pfd.events = POLLOUT;
	pfd.fd = fd;
	pfd.revents = 0;
	LOCK(&manager->fdlock[lockid]);
	if (write(manager->devpoll_fd, &pfd, sizeof(pfd)) == -1)
		result = isc__errno2result(errno);
	else {
		if (msg == SELECT_POKE_READ)
			manager->fdpollinfo[fd].want_read = 1;
		else
			manager->fdpollinfo[fd].want_write = 1;
	}
	UNLOCK(&manager->fdlock[lockid]);

	return (result);
#elif defined(USE_SELECT)
	LOCK(&manager->lock);
	if (msg == SELECT_POKE_READ)
		FD_SET(fd, manager->read_fds);
	if (msg == SELECT_POKE_WRITE)
		FD_SET(fd, manager->write_fds);
	UNLOCK(&manager->lock);

	return (result);
#endif
}

static inline isc_result_t
unwatch_fd(isc__socketmgr_t *manager, int fd, int msg) {
	isc_result_t result = ISC_R_SUCCESS;

#ifdef USE_KQUEUE
	struct kevent evchange;

	memset(&evchange, 0, sizeof(evchange));
	if (msg == SELECT_POKE_READ)
		evchange.filter = EVFILT_READ;
	else
		evchange.filter = EVFILT_WRITE;
	evchange.flags = EV_DELETE;
	evchange.ident = fd;
	if (kevent(manager->kqueue_fd, &evchange, 1, NULL, 0, NULL) != 0)
		result = isc__errno2result(errno);

	return (result);
#elif defined(USE_EPOLL)
	struct epoll_event event;

	if (msg == SELECT_POKE_READ)
		event.events = EPOLLIN;
	else
		event.events = EPOLLOUT;
	memset(&event.data, 0, sizeof(event.data));
	event.data.fd = fd;
	if (epoll_ctl(manager->epoll_fd, EPOLL_CTL_DEL, fd, &event) == -1 &&
	    errno != ENOENT) {
		char strbuf[ISC_STRERRORSIZE];
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "epoll_ctl(DEL), %d: %s", fd, strbuf);
		result = ISC_R_UNEXPECTED;
	}
	return (result);
#elif defined(USE_DEVPOLL)
	struct pollfd pfds[2];
	size_t writelen = sizeof(pfds[0]);
	int lockid = FDLOCK_ID(fd);

	memset(pfds, 0, sizeof(pfds));
	pfds[0].events = POLLREMOVE;
	pfds[0].fd = fd;

	/*
	 * Canceling read or write polling via /dev/poll is tricky.  Since it
	 * only provides a way of canceling per FD, we may need to re-poll the
	 * socket for the other operation.
	 */
	LOCK(&manager->fdlock[lockid]);
	if (msg == SELECT_POKE_READ &&
	    manager->fdpollinfo[fd].want_write == 1) {
		pfds[1].events = POLLOUT;
		pfds[1].fd = fd;
		writelen += sizeof(pfds[1]);
	}
	if (msg == SELECT_POKE_WRITE &&
	    manager->fdpollinfo[fd].want_read == 1) {
		pfds[1].events = POLLIN;
		pfds[1].fd = fd;
		writelen += sizeof(pfds[1]);
	}

	if (write(manager->devpoll_fd, pfds, writelen) == -1)
		result = isc__errno2result(errno);
	else {
		if (msg == SELECT_POKE_READ)
			manager->fdpollinfo[fd].want_read = 0;
		else
			manager->fdpollinfo[fd].want_write = 0;
	}
	UNLOCK(&manager->fdlock[lockid]);

	return (result);
#elif defined(USE_SELECT)
	LOCK(&manager->lock);
	if (msg == SELECT_POKE_READ)
		FD_CLR(fd, manager->read_fds);
	else if (msg == SELECT_POKE_WRITE)
		FD_CLR(fd, manager->write_fds);
	UNLOCK(&manager->lock);

	return (result);
#endif
}

static void
wakeup_socket(isc__socketmgr_t *manager, int fd, int msg) {
	isc_result_t result;
	int lockid = FDLOCK_ID(fd);

	/*
	 * This is a wakeup on a socket.  If the socket is not in the
	 * process of being closed, start watching it for either reads
	 * or writes.
	 */

	INSIST(fd >= 0 && fd < (int)manager->maxsocks);

	if (msg == SELECT_POKE_CLOSE) {
		/* No one should be updating fdstate, so no need to lock it */
		INSIST(manager->fdstate[fd] == CLOSE_PENDING);
		manager->fdstate[fd] = CLOSED;
		(void)unwatch_fd(manager, fd, SELECT_POKE_READ);
		(void)unwatch_fd(manager, fd, SELECT_POKE_WRITE);
		(void)close(fd);
		return;
	}

	LOCK(&manager->fdlock[lockid]);
	if (manager->fdstate[fd] == CLOSE_PENDING) {
		UNLOCK(&manager->fdlock[lockid]);

		/*
		 * We accept (and ignore) any error from unwatch_fd() as we are
		 * closing the socket, hoping it doesn't leave dangling state in
		 * the kernel.
		 * Note that unwatch_fd() must be called after releasing the
		 * fdlock; otherwise it could cause deadlock due to a lock order
		 * reversal.
		 */
		(void)unwatch_fd(manager, fd, SELECT_POKE_READ);
		(void)unwatch_fd(manager, fd, SELECT_POKE_WRITE);
		return;
	}
	if (manager->fdstate[fd] != MANAGED) {
		UNLOCK(&manager->fdlock[lockid]);
		return;
	}
	UNLOCK(&manager->fdlock[lockid]);

	/*
	 * Set requested bit.
	 */
	result = watch_fd(manager, fd, msg);
	if (result != ISC_R_SUCCESS) {
		/*
		 * XXXJT: what should we do?  Ignoring the failure of watching
		 * a socket will make the application dysfunctional, but there
		 * seems to be no reasonable recovery process.
		 */
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
			      "failed to start watching FD (%d): %s",
			      fd, isc_result_totext(result));
	}
}

#ifdef USE_WATCHER_THREAD
/*
 * Poke the select loop when there is something for us to do.
 * The write is required (by POSIX) to complete.  That is, we
 * will not get partial writes.
 */
static void
select_poke(isc__socketmgr_t *mgr, int fd, int msg) {
	int cc;
	int buf[2];
	char strbuf[ISC_STRERRORSIZE];

	buf[0] = fd;
	buf[1] = msg;

	do {
		cc = write(mgr->pipe_fds[1], buf, sizeof(buf));
#ifdef ENOSR
		/*
		 * Treat ENOSR as EAGAIN but loop slowly as it is
		 * unlikely to clear fast.
		 */
		if (cc < 0 && errno == ENOSR) {
			sleep(1);
			errno = EAGAIN;
		}
#endif
	} while (cc < 0 && SOFT_ERROR(errno));

	if (cc < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		FATAL_ERROR(__FILE__, __LINE__,
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_WRITEFAILED,
					   "write() failed "
					   "during watcher poke: %s"),
			    strbuf);
	}

	INSIST(cc == sizeof(buf));
}

/*
 * Read a message on the internal fd.
 */
static void
select_readmsg(isc__socketmgr_t *mgr, int *fd, int *msg) {
	int buf[2];
	int cc;
	char strbuf[ISC_STRERRORSIZE];

	cc = read(mgr->pipe_fds[0], buf, sizeof(buf));
	if (cc < 0) {
		*msg = SELECT_POKE_NOTHING;
		*fd = -1;	/* Silence compiler. */
		if (SOFT_ERROR(errno))
			return;

		isc__strerror(errno, strbuf, sizeof(strbuf));
		FATAL_ERROR(__FILE__, __LINE__,
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_READFAILED,
					   "read() failed "
					   "during watcher poke: %s"),
			    strbuf);

		return;
	}
	INSIST(cc == sizeof(buf));

	*fd = buf[0];
	*msg = buf[1];
}
#else /* USE_WATCHER_THREAD */
/*
 * Update the state of the socketmgr when something changes.
 */
static void
select_poke(isc__socketmgr_t *manager, int fd, int msg) {
	if (msg == SELECT_POKE_SHUTDOWN)
		return;
	else if (fd >= 0)
		wakeup_socket(manager, fd, msg);
	return;
}
#endif /* USE_WATCHER_THREAD */

/*
 * Make a fd non-blocking.
 */
static isc_result_t
make_nonblock(int fd) {
	int ret;
	int flags;
	char strbuf[ISC_STRERRORSIZE];
#ifdef USE_FIONBIO_IOCTL
	int on = 1;

	ret = ioctl(fd, FIONBIO, (char *)&on);
#else
	flags = fcntl(fd, F_GETFL, 0);
	flags |= PORT_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
#endif

	if (ret == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
#ifdef USE_FIONBIO_IOCTL
				 "ioctl(%d, FIONBIO, &on): %s", fd,
#else
				 "fcntl(%d, F_SETFL, %d): %s", fd, flags,
#endif
				 strbuf);

		return (ISC_R_UNEXPECTED);
	}

	return (ISC_R_SUCCESS);
}

#ifdef USE_CMSG
/*
 * Not all OSes support advanced CMSG macros: CMSG_LEN and CMSG_SPACE.
 * In order to ensure as much portability as possible, we provide wrapper
 * functions of these macros.
 * Note that cmsg_space() could run slow on OSes that do not have
 * CMSG_SPACE.
 */
static inline ISC_SOCKADDR_LEN_T
cmsg_len(ISC_SOCKADDR_LEN_T len) {
#ifdef CMSG_LEN
	return (CMSG_LEN(len));
#else
	ISC_SOCKADDR_LEN_T hdrlen;

	/*
	 * Cast NULL so that any pointer arithmetic performed by CMSG_DATA
	 * is correct.
	 */
	hdrlen = (ISC_SOCKADDR_LEN_T)CMSG_DATA(((struct cmsghdr *)NULL));
	return (hdrlen + len);
#endif
}

static inline ISC_SOCKADDR_LEN_T
cmsg_space(ISC_SOCKADDR_LEN_T len) {
#ifdef CMSG_SPACE
	return (CMSG_SPACE(len));
#else
	struct msghdr msg;
	struct cmsghdr *cmsgp;
	/*
	 * XXX: The buffer length is an ad-hoc value, but should be enough
	 * in a practical sense.
	 */
	char dummybuf[sizeof(struct cmsghdr) + 1024];

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = dummybuf;
	msg.msg_controllen = sizeof(dummybuf);

	cmsgp = (struct cmsghdr *)dummybuf;
	cmsgp->cmsg_len = cmsg_len(len);

	cmsgp = CMSG_NXTHDR(&msg, cmsgp);
	if (cmsgp != NULL)
		return ((char *)cmsgp - (char *)msg.msg_control);
	else
		return (0);
#endif
}
#endif /* USE_CMSG */

/*
 * Process control messages received on a socket.
 */
static void
process_cmsg(isc__socket_t *sock, struct msghdr *msg, isc_socketevent_t *dev) {
#ifdef USE_CMSG
	struct cmsghdr *cmsgp;
#ifdef ISC_PLATFORM_HAVEIN6PKTINFO
	struct in6_pktinfo *pktinfop;
#endif
#ifdef SO_TIMESTAMP
	struct timeval *timevalp;
#endif
#endif

	/*
	 * sock is used only when ISC_NET_BSD44MSGHDR and USE_CMSG are defined.
	 * msg and dev are used only when ISC_NET_BSD44MSGHDR is defined.
	 * They are all here, outside of the CPP tests, because it is
	 * more consistent with the usual ISC coding style.
	 */
	UNUSED(sock);
	UNUSED(msg);
	UNUSED(dev);

#ifdef ISC_NET_BSD44MSGHDR

#ifdef MSG_TRUNC
	if ((msg->msg_flags & MSG_TRUNC) == MSG_TRUNC)
		dev->attributes |= ISC_SOCKEVENTATTR_TRUNC;
#endif

#ifdef MSG_CTRUNC
	if ((msg->msg_flags & MSG_CTRUNC) == MSG_CTRUNC)
		dev->attributes |= ISC_SOCKEVENTATTR_CTRUNC;
#endif

#ifndef USE_CMSG
	return;
#else
	if (msg->msg_controllen == 0U || msg->msg_control == NULL)
		return;

#ifdef SO_TIMESTAMP
	timevalp = NULL;
#endif
#ifdef ISC_PLATFORM_HAVEIN6PKTINFO
	pktinfop = NULL;
#endif

	cmsgp = CMSG_FIRSTHDR(msg);
	while (cmsgp != NULL) {
		socket_log(sock, NULL, TRACE,
			   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_PROCESSCMSG,
			   "processing cmsg %p", cmsgp);

#ifdef ISC_PLATFORM_HAVEIN6PKTINFO
		if (cmsgp->cmsg_level == IPPROTO_IPV6
		    && cmsgp->cmsg_type == IPV6_PKTINFO) {

			pktinfop = (struct in6_pktinfo *)CMSG_DATA(cmsgp);
			memcpy(&dev->pktinfo, pktinfop,
			       sizeof(struct in6_pktinfo));
			dev->attributes |= ISC_SOCKEVENTATTR_PKTINFO;
			socket_log(sock, NULL, TRACE,
				   isc_msgcat, ISC_MSGSET_SOCKET,
				   ISC_MSG_IFRECEIVED,
				   "interface received on ifindex %u",
				   dev->pktinfo.ipi6_ifindex);
			if (IN6_IS_ADDR_MULTICAST(&pktinfop->ipi6_addr))
				dev->attributes |= ISC_SOCKEVENTATTR_MULTICAST;
			goto next;
		}
#endif

#ifdef SO_TIMESTAMP
		if (cmsgp->cmsg_level == SOL_SOCKET
		    && cmsgp->cmsg_type == SCM_TIMESTAMP) {
			timevalp = (struct timeval *)CMSG_DATA(cmsgp);
			dev->timestamp.seconds = timevalp->tv_sec;
			dev->timestamp.nanoseconds = timevalp->tv_usec * 1000;
			dev->attributes |= ISC_SOCKEVENTATTR_TIMESTAMP;
			goto next;
		}
#endif

	next:
		cmsgp = CMSG_NXTHDR(msg, cmsgp);
	}
#endif /* USE_CMSG */

#endif /* ISC_NET_BSD44MSGHDR */
}

/*
 * Construct an iov array and attach it to the msghdr passed in.  This is
 * the SEND constructor, which will use the used region of the buffer
 * (if using a buffer list) or will use the internal region (if a single
 * buffer I/O is requested).
 *
 * Nothing can be NULL, and the done event must list at least one buffer
 * on the buffer linked list for this function to be meaningful.
 *
 * If write_countp != NULL, *write_countp will hold the number of bytes
 * this transaction can send.
 */
static void
build_msghdr_send(isc__socket_t *sock, isc_socketevent_t *dev,
		  struct msghdr *msg, struct iovec *iov, size_t *write_countp)
{
	unsigned int iovcount;
	isc_buffer_t *buffer;
	isc_region_t used;
	size_t write_count;
	size_t skip_count;

	memset(msg, 0, sizeof(*msg));

	if (!sock->connected) {
		msg->msg_name = (void *)&dev->address.type.sa;
		msg->msg_namelen = dev->address.length;
	} else {
		msg->msg_name = NULL;
		msg->msg_namelen = 0;
	}

	buffer = ISC_LIST_HEAD(dev->bufferlist);
	write_count = 0;
	iovcount = 0;

	/*
	 * Single buffer I/O?  Skip what we've done so far in this region.
	 */
	if (buffer == NULL) {
		write_count = dev->region.length - dev->n;
		iov[0].iov_base = (void *)(dev->region.base + dev->n);
		iov[0].iov_len = write_count;
		iovcount = 1;

		goto config;
	}

	/*
	 * Multibuffer I/O.
	 * Skip the data in the buffer list that we have already written.
	 */
	skip_count = dev->n;
	while (buffer != NULL) {
		REQUIRE(ISC_BUFFER_VALID(buffer));
		if (skip_count < isc_buffer_usedlength(buffer))
			break;
		skip_count -= isc_buffer_usedlength(buffer);
		buffer = ISC_LIST_NEXT(buffer, link);
	}

	while (buffer != NULL) {
		INSIST(iovcount < MAXSCATTERGATHER_SEND);

		isc_buffer_usedregion(buffer, &used);

		if (used.length > 0) {
			iov[iovcount].iov_base = (void *)(used.base
							  + skip_count);
			iov[iovcount].iov_len = used.length - skip_count;
			write_count += (used.length - skip_count);
			skip_count = 0;
			iovcount++;
		}
		buffer = ISC_LIST_NEXT(buffer, link);
	}

	INSIST(skip_count == 0U);

 config:
	msg->msg_iov = iov;
	msg->msg_iovlen = iovcount;

#ifdef ISC_NET_BSD44MSGHDR
	msg->msg_control = NULL;
	msg->msg_controllen = 0;
	msg->msg_flags = 0;
#if defined(USE_CMSG) && defined(ISC_PLATFORM_HAVEIN6PKTINFO)
	if ((sock->type == isc_sockettype_udp)
	    && ((dev->attributes & ISC_SOCKEVENTATTR_PKTINFO) != 0)) {
#if defined(IPV6_USE_MIN_MTU)
		int use_min_mtu = 1;	/* -1, 0, 1 */
#endif
		struct cmsghdr *cmsgp;
		struct in6_pktinfo *pktinfop;

		socket_log(sock, NULL, TRACE,
			   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_SENDTODATA,
			   "sendto pktinfo data, ifindex %u",
			   dev->pktinfo.ipi6_ifindex);

		msg->msg_controllen = cmsg_space(sizeof(struct in6_pktinfo));
		INSIST(msg->msg_controllen <= sock->sendcmsgbuflen);
		msg->msg_control = (void *)sock->sendcmsgbuf;

		cmsgp = (struct cmsghdr *)sock->sendcmsgbuf;
		cmsgp->cmsg_level = IPPROTO_IPV6;
		cmsgp->cmsg_type = IPV6_PKTINFO;
		cmsgp->cmsg_len = cmsg_len(sizeof(struct in6_pktinfo));
		pktinfop = (struct in6_pktinfo *)CMSG_DATA(cmsgp);
		memcpy(pktinfop, &dev->pktinfo, sizeof(struct in6_pktinfo));
#if defined(IPV6_USE_MIN_MTU)
		/*
		 * Set IPV6_USE_MIN_MTU as a per packet option as FreeBSD
		 * ignores setsockopt(IPV6_USE_MIN_MTU) when IPV6_PKTINFO
		 * is used.
		 */
		cmsgp = (struct cmsghdr *)(sock->sendcmsgbuf +
					   msg->msg_controllen);
		msg->msg_controllen += cmsg_space(sizeof(use_min_mtu));
		INSIST(msg->msg_controllen <= sock->sendcmsgbuflen);

		cmsgp->cmsg_level = IPPROTO_IPV6;
		cmsgp->cmsg_type = IPV6_USE_MIN_MTU;
		cmsgp->cmsg_len = cmsg_len(sizeof(use_min_mtu));
		memcpy(CMSG_DATA(cmsgp), &use_min_mtu, sizeof(use_min_mtu));
#endif
	}
#endif /* USE_CMSG && ISC_PLATFORM_HAVEIPV6 */
#else /* ISC_NET_BSD44MSGHDR */
	msg->msg_accrights = NULL;
	msg->msg_accrightslen = 0;
#endif /* ISC_NET_BSD44MSGHDR */

	if (write_countp != NULL)
		*write_countp = write_count;
}

/*
 * Construct an iov array and attach it to the msghdr passed in.  This is
 * the RECV constructor, which will use the available region of the buffer
 * (if using a buffer list) or will use the internal region (if a single
 * buffer I/O is requested).
 *
 * Nothing can be NULL, and the done event must list at least one buffer
 * on the buffer linked list for this function to be meaningful.
 *
 * If read_countp != NULL, *read_countp will hold the number of bytes
 * this transaction can receive.
 */
static void
build_msghdr_recv(isc__socket_t *sock, isc_socketevent_t *dev,
		  struct msghdr *msg, struct iovec *iov, size_t *read_countp)
{
	unsigned int iovcount;
	isc_buffer_t *buffer;
	isc_region_t available;
	size_t read_count;

	memset(msg, 0, sizeof(struct msghdr));

	if (sock->type == isc_sockettype_udp) {
		memset(&dev->address, 0, sizeof(dev->address));
#ifdef BROKEN_RECVMSG
		if (sock->pf == AF_INET) {
			msg->msg_name = (void *)&dev->address.type.sin;
			msg->msg_namelen = sizeof(dev->address.type.sin6);
		} else if (sock->pf == AF_INET6) {
			msg->msg_name = (void *)&dev->address.type.sin6;
			msg->msg_namelen = sizeof(dev->address.type.sin6);
#ifdef ISC_PLATFORM_HAVESYSUNH
		} else if (sock->pf == AF_UNIX) {
			msg->msg_name = (void *)&dev->address.type.sunix;
			msg->msg_namelen = sizeof(dev->address.type.sunix);
#endif
		} else {
			msg->msg_name = (void *)&dev->address.type.sa;
			msg->msg_namelen = sizeof(dev->address.type);
		}
#else
		msg->msg_name = (void *)&dev->address.type.sa;
		msg->msg_namelen = sizeof(dev->address.type);
#endif
#ifdef ISC_NET_RECVOVERFLOW
		/* If needed, steal one iovec for overflow detection. */
		maxiov--;
#endif
	} else { /* TCP */
		msg->msg_name = NULL;
		msg->msg_namelen = 0;
		dev->address = sock->peer_address;
	}

	buffer = ISC_LIST_HEAD(dev->bufferlist);
	read_count = 0;

	/*
	 * Single buffer I/O?  Skip what we've done so far in this region.
	 */
	if (buffer == NULL) {
		read_count = dev->region.length - dev->n;
		iov[0].iov_base = (void *)(dev->region.base + dev->n);
		iov[0].iov_len = read_count;
		iovcount = 1;

		goto config;
	}

	/*
	 * Multibuffer I/O.
	 * Skip empty buffers.
	 */
	while (buffer != NULL) {
		REQUIRE(ISC_BUFFER_VALID(buffer));
		if (isc_buffer_availablelength(buffer) != 0)
			break;
		buffer = ISC_LIST_NEXT(buffer, link);
	}

	iovcount = 0;
	while (buffer != NULL) {
		INSIST(iovcount < MAXSCATTERGATHER_RECV);

		isc_buffer_availableregion(buffer, &available);

		if (available.length > 0) {
			iov[iovcount].iov_base = (void *)(available.base);
			iov[iovcount].iov_len = available.length;
			read_count += available.length;
			iovcount++;
		}
		buffer = ISC_LIST_NEXT(buffer, link);
	}

 config:

	/*
	 * If needed, set up to receive that one extra byte.  Note that
	 * we know there is at least one iov left, since we stole it
	 * at the top of this function.
	 */
#ifdef ISC_NET_RECVOVERFLOW
	if (sock->type == isc_sockettype_udp) {
		iov[iovcount].iov_base = (void *)(&sock->overflow);
		iov[iovcount].iov_len = 1;
		iovcount++;
	}
#endif

	msg->msg_iov = iov;
	msg->msg_iovlen = iovcount;

#ifdef ISC_NET_BSD44MSGHDR
	msg->msg_control = NULL;
	msg->msg_controllen = 0;
	msg->msg_flags = 0;
#if defined(USE_CMSG)
	if (sock->type == isc_sockettype_udp) {
		msg->msg_control = sock->recvcmsgbuf;
		msg->msg_controllen = sock->recvcmsgbuflen;
	}
#endif /* USE_CMSG */
#else /* ISC_NET_BSD44MSGHDR */
	msg->msg_accrights = NULL;
	msg->msg_accrightslen = 0;
#endif /* ISC_NET_BSD44MSGHDR */

	if (read_countp != NULL)
		*read_countp = read_count;
}

static void
set_dev_address(isc_sockaddr_t *address, isc__socket_t *sock,
		isc_socketevent_t *dev)
{
	if (sock->type == isc_sockettype_udp) {
		if (address != NULL)
			dev->address = *address;
		else
			dev->address = sock->peer_address;
	} else if (sock->type == isc_sockettype_tcp) {
		INSIST(address == NULL);
		dev->address = sock->peer_address;
	}
}

static void
destroy_socketevent(isc_event_t *event) {
	isc_socketevent_t *ev = (isc_socketevent_t *)event;

	INSIST(ISC_LIST_EMPTY(ev->bufferlist));

	(ev->destroy)(event);
}

static isc_socketevent_t *
allocate_socketevent(isc__socket_t *sock, isc_eventtype_t eventtype,
		     isc_taskaction_t action, const void *arg)
{
	isc_socketevent_t *ev;

	ev = (isc_socketevent_t *)isc_event_allocate(sock->manager->mctx,
						     sock, eventtype,
						     action, arg,
						     sizeof(*ev));

	if (ev == NULL)
		return (NULL);

	ev->result = ISC_R_UNSET;
	ISC_LINK_INIT(ev, ev_link);
	ISC_LIST_INIT(ev->bufferlist);
	ev->region.base = NULL;
	ev->n = 0;
	ev->offset = 0;
	ev->attributes = 0;
	ev->destroy = ev->ev_destroy;
	ev->ev_destroy = destroy_socketevent;

	return (ev);
}

#if defined(ISC_SOCKET_DEBUG)
static void
dump_msg(struct msghdr *msg) {
	unsigned int i;

	printf("MSGHDR %p\n", msg);
	printf("\tname %p, namelen %ld\n", msg->msg_name,
	       (long) msg->msg_namelen);
	printf("\tiov %p, iovlen %ld\n", msg->msg_iov,
	       (long) msg->msg_iovlen);
	for (i = 0; i < (unsigned int)msg->msg_iovlen; i++)
		printf("\t\t%d\tbase %p, len %ld\n", i,
		       msg->msg_iov[i].iov_base,
		       (long) msg->msg_iov[i].iov_len);
#ifdef ISC_NET_BSD44MSGHDR
	printf("\tcontrol %p, controllen %ld\n", msg->msg_control,
	       (long) msg->msg_controllen);
#endif
}
#endif

#define DOIO_SUCCESS		0	/* i/o ok, event sent */
#define DOIO_SOFT		1	/* i/o ok, soft error, no event sent */
#define DOIO_HARD		2	/* i/o error, event sent */
#define DOIO_EOF		3	/* EOF, no event sent */

static int
doio_recv(isc__socket_t *sock, isc_socketevent_t *dev) {
	int cc;
	struct iovec iov[MAXSCATTERGATHER_RECV];
	size_t read_count;
	size_t actual_count;
	struct msghdr msghdr;
	isc_buffer_t *buffer;
	int recv_errno;
	char strbuf[ISC_STRERRORSIZE];

	build_msghdr_recv(sock, dev, &msghdr, iov, &read_count);

#if defined(ISC_SOCKET_DEBUG)
	dump_msg(&msghdr);
#endif

	cc = recvmsg(sock->fd, &msghdr, 0);
	recv_errno = errno;

#if defined(ISC_SOCKET_DEBUG)
	dump_msg(&msghdr);
#endif

	if (cc < 0) {
		if (SOFT_ERROR(recv_errno))
			return (DOIO_SOFT);

		if (isc_log_wouldlog(isc_lctx, IOEVENT_LEVEL)) {
			isc__strerror(recv_errno, strbuf, sizeof(strbuf));
			socket_log(sock, NULL, IOEVENT,
				   isc_msgcat, ISC_MSGSET_SOCKET,
				   ISC_MSG_DOIORECV,
				  "doio_recv: recvmsg(%d) %d bytes, err %d/%s",
				   sock->fd, cc, recv_errno, strbuf);
		}

#define SOFT_OR_HARD(_system, _isc) \
	if (recv_errno == _system) { \
		if (sock->connected) { \
			dev->result = _isc; \
			inc_stats(sock->manager->stats, \
				  sock->statsindex[STATID_RECVFAIL]); \
			return (DOIO_HARD); \
		} \
		return (DOIO_SOFT); \
	}
#define ALWAYS_HARD(_system, _isc) \
	if (recv_errno == _system) { \
		dev->result = _isc; \
		inc_stats(sock->manager->stats, \
			  sock->statsindex[STATID_RECVFAIL]); \
		return (DOIO_HARD); \
	}

		SOFT_OR_HARD(ECONNREFUSED, ISC_R_CONNREFUSED);
		SOFT_OR_HARD(ENETUNREACH, ISC_R_NETUNREACH);
		SOFT_OR_HARD(EHOSTUNREACH, ISC_R_HOSTUNREACH);
		SOFT_OR_HARD(EHOSTDOWN, ISC_R_HOSTDOWN);
		/* HPUX 11.11 can return EADDRNOTAVAIL. */
		SOFT_OR_HARD(EADDRNOTAVAIL, ISC_R_ADDRNOTAVAIL);
		ALWAYS_HARD(ENOBUFS, ISC_R_NORESOURCES);
		/*
		 * HPUX returns EPROTO and EINVAL on receiving some ICMP/ICMPv6
		 * errors.
		 */
#ifdef EPROTO
		SOFT_OR_HARD(EPROTO, ISC_R_HOSTUNREACH);
#endif
		SOFT_OR_HARD(EINVAL, ISC_R_HOSTUNREACH);

#undef SOFT_OR_HARD
#undef ALWAYS_HARD

		dev->result = isc__errno2result(recv_errno);
		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_RECVFAIL]);
		return (DOIO_HARD);
	}

	/*
	 * On TCP and UNIX sockets, zero length reads indicate EOF,
	 * while on UDP sockets, zero length reads are perfectly valid,
	 * although strange.
	 */
	switch (sock->type) {
	case isc_sockettype_tcp:
	case isc_sockettype_unix:
		if (cc == 0)
			return (DOIO_EOF);
		break;
	case isc_sockettype_udp:
		break;
	case isc_sockettype_fdwatch:
	default:
		INSIST(0);
	}

	if (sock->type == isc_sockettype_udp) {
		dev->address.length = msghdr.msg_namelen;
		if (isc_sockaddr_getport(&dev->address) == 0) {
			if (isc_log_wouldlog(isc_lctx, IOEVENT_LEVEL)) {
				socket_log(sock, &dev->address, IOEVENT,
					   isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_ZEROPORT,
					   "dropping source port zero packet");
			}
			return (DOIO_SOFT);
		}
		/*
		 * Simulate a firewall blocking UDP responses bigger than
		 * 512 bytes.
		 */
		if (sock->manager->maxudp != 0 && cc > sock->manager->maxudp)
			return (DOIO_SOFT);
	}

	socket_log(sock, &dev->address, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_PKTRECV,
		   "packet received correctly");

	/*
	 * Overflow bit detection.  If we received MORE bytes than we should,
	 * this indicates an overflow situation.  Set the flag in the
	 * dev entry and adjust how much we read by one.
	 */
#ifdef ISC_NET_RECVOVERFLOW
	if ((sock->type == isc_sockettype_udp) && ((size_t)cc > read_count)) {
		dev->attributes |= ISC_SOCKEVENTATTR_TRUNC;
		cc--;
	}
#endif

	/*
	 * If there are control messages attached, run through them and pull
	 * out the interesting bits.
	 */
	if (sock->type == isc_sockettype_udp)
		process_cmsg(sock, &msghdr, dev);

	/*
	 * update the buffers (if any) and the i/o count
	 */
	dev->n += cc;
	actual_count = cc;
	buffer = ISC_LIST_HEAD(dev->bufferlist);
	while (buffer != NULL && actual_count > 0U) {
		REQUIRE(ISC_BUFFER_VALID(buffer));
		if (isc_buffer_availablelength(buffer) <= actual_count) {
			actual_count -= isc_buffer_availablelength(buffer);
			isc_buffer_add(buffer,
				       isc_buffer_availablelength(buffer));
		} else {
			isc_buffer_add(buffer, actual_count);
			actual_count = 0;
			POST(actual_count);
			break;
		}
		buffer = ISC_LIST_NEXT(buffer, link);
		if (buffer == NULL) {
			INSIST(actual_count == 0U);
		}
	}

	/*
	 * If we read less than we expected, update counters,
	 * and let the upper layer poke the descriptor.
	 */
	if (((size_t)cc != read_count) && (dev->n < dev->minimum))
		return (DOIO_SOFT);

	/*
	 * Full reads are posted, or partials if partials are ok.
	 */
	dev->result = ISC_R_SUCCESS;
	return (DOIO_SUCCESS);
}

/*
 * Returns:
 *	DOIO_SUCCESS	The operation succeeded.  dev->result contains
 *			ISC_R_SUCCESS.
 *
 *	DOIO_HARD	A hard or unexpected I/O error was encountered.
 *			dev->result contains the appropriate error.
 *
 *	DOIO_SOFT	A soft I/O error was encountered.  No senddone
 *			event was sent.  The operation should be retried.
 *
 *	No other return values are possible.
 */
static int
doio_send(isc__socket_t *sock, isc_socketevent_t *dev) {
	int cc;
	struct iovec iov[MAXSCATTERGATHER_SEND];
	size_t write_count;
	struct msghdr msghdr;
	char addrbuf[ISC_SOCKADDR_FORMATSIZE];
	int attempts = 0;
	int send_errno;
	char strbuf[ISC_STRERRORSIZE];

	build_msghdr_send(sock, dev, &msghdr, iov, &write_count);

 resend:
	cc = sendmsg(sock->fd, &msghdr, 0);
	send_errno = errno;

	/*
	 * Check for error or block condition.
	 */
	if (cc < 0) {
		if (send_errno == EINTR && ++attempts < NRETRIES)
			goto resend;

		if (SOFT_ERROR(send_errno))
			return (DOIO_SOFT);

#define SOFT_OR_HARD(_system, _isc) \
	if (send_errno == _system) { \
		if (sock->connected) { \
			dev->result = _isc; \
			inc_stats(sock->manager->stats, \
				  sock->statsindex[STATID_SENDFAIL]); \
			return (DOIO_HARD); \
		} \
		return (DOIO_SOFT); \
	}
#define ALWAYS_HARD(_system, _isc) \
	if (send_errno == _system) { \
		dev->result = _isc; \
		inc_stats(sock->manager->stats, \
			  sock->statsindex[STATID_SENDFAIL]); \
		return (DOIO_HARD); \
	}

		SOFT_OR_HARD(ECONNREFUSED, ISC_R_CONNREFUSED);
		ALWAYS_HARD(EACCES, ISC_R_NOPERM);
		ALWAYS_HARD(EAFNOSUPPORT, ISC_R_ADDRNOTAVAIL);
		ALWAYS_HARD(EADDRNOTAVAIL, ISC_R_ADDRNOTAVAIL);
		ALWAYS_HARD(EHOSTUNREACH, ISC_R_HOSTUNREACH);
#ifdef EHOSTDOWN
		ALWAYS_HARD(EHOSTDOWN, ISC_R_HOSTUNREACH);
#endif
		ALWAYS_HARD(ENETUNREACH, ISC_R_NETUNREACH);
		ALWAYS_HARD(ENOBUFS, ISC_R_NORESOURCES);
		ALWAYS_HARD(EPERM, ISC_R_HOSTUNREACH);
		ALWAYS_HARD(EPIPE, ISC_R_NOTCONNECTED);
		ALWAYS_HARD(ECONNRESET, ISC_R_CONNECTIONRESET);

#undef SOFT_OR_HARD
#undef ALWAYS_HARD

		/*
		 * The other error types depend on whether or not the
		 * socket is UDP or TCP.  If it is UDP, some errors
		 * that we expect to be fatal under TCP are merely
		 * annoying, and are really soft errors.
		 *
		 * However, these soft errors are still returned as
		 * a status.
		 */
		isc_sockaddr_format(&dev->address, addrbuf, sizeof(addrbuf));
		isc__strerror(send_errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__, "internal_send: %s: %s",
				 addrbuf, strbuf);
		dev->result = isc__errno2result(send_errno);
		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_SENDFAIL]);
		return (DOIO_HARD);
	}

	if (cc == 0) {
		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_SENDFAIL]);
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "doio_send: send() %s 0",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_RETURNED, "returned"));
	}

	/*
	 * If we write less than we expected, update counters, poke.
	 */
	dev->n += cc;
	if ((size_t)cc != write_count)
		return (DOIO_SOFT);

	/*
	 * Exactly what we wanted to write.  We're done with this
	 * entry.  Post its completion event.
	 */
	dev->result = ISC_R_SUCCESS;
	return (DOIO_SUCCESS);
}

/*
 * Kill.
 *
 * Caller must ensure that the socket is not locked and no external
 * references exist.
 */
static void
closesocket(isc__socketmgr_t *manager, isc__socket_t *sock, int fd) {
	isc_sockettype_t type = sock->type;
	int lockid = FDLOCK_ID(fd);

	/*
	 * No one has this socket open, so the watcher doesn't have to be
	 * poked, and the socket doesn't have to be locked.
	 */
	LOCK(&manager->fdlock[lockid]);
	manager->fds[fd] = NULL;
	if (type == isc_sockettype_fdwatch)
		manager->fdstate[fd] = CLOSED;
	else
		manager->fdstate[fd] = CLOSE_PENDING;
	UNLOCK(&manager->fdlock[lockid]);
	if (type == isc_sockettype_fdwatch) {
		/*
		 * The caller may close the socket once this function returns,
		 * and `fd' may be reassigned for a new socket.  So we do
		 * unwatch_fd() here, rather than defer it via select_poke().
		 * Note: this may complicate data protection among threads and
		 * may reduce performance due to additional locks.  One way to
		 * solve this would be to dup() the watched descriptor, but we
		 * take a simpler approach at this moment.
		 */
		(void)unwatch_fd(manager, fd, SELECT_POKE_READ);
		(void)unwatch_fd(manager, fd, SELECT_POKE_WRITE);
	} else
		select_poke(manager, fd, SELECT_POKE_CLOSE);

	inc_stats(manager->stats, sock->statsindex[STATID_CLOSE]);

	/*
	 * update manager->maxfd here (XXX: this should be implemented more
	 * efficiently)
	 */
#ifdef USE_SELECT
	LOCK(&manager->lock);
	if (manager->maxfd == fd) {
		int i;

		manager->maxfd = 0;
		for (i = fd - 1; i >= 0; i--) {
			lockid = FDLOCK_ID(i);

			LOCK(&manager->fdlock[lockid]);
			if (manager->fdstate[i] == MANAGED) {
				manager->maxfd = i;
				UNLOCK(&manager->fdlock[lockid]);
				break;
			}
			UNLOCK(&manager->fdlock[lockid]);
		}
#ifdef ISC_PLATFORM_USETHREADS
		if (manager->maxfd < manager->pipe_fds[0])
			manager->maxfd = manager->pipe_fds[0];
#endif
	}
	UNLOCK(&manager->lock);
#endif	/* USE_SELECT */
}

static void
destroy(isc__socket_t **sockp) {
	int fd;
	isc__socket_t *sock = *sockp;
	isc__socketmgr_t *manager = sock->manager;

	socket_log(sock, NULL, CREATION, isc_msgcat, ISC_MSGSET_SOCKET,
		   ISC_MSG_DESTROYING, "destroying");

	INSIST(ISC_LIST_EMPTY(sock->accept_list));
	INSIST(ISC_LIST_EMPTY(sock->recv_list));
	INSIST(ISC_LIST_EMPTY(sock->send_list));
	INSIST(sock->connect_ev == NULL);
	REQUIRE(sock->fd == -1 || sock->fd < (int)manager->maxsocks);

	if (sock->fd >= 0) {
		fd = sock->fd;
		sock->fd = -1;
		closesocket(manager, sock, fd);
	}

	LOCK(&manager->lock);

	ISC_LIST_UNLINK(manager->socklist, sock, link);

#ifdef USE_WATCHER_THREAD
	if (ISC_LIST_EMPTY(manager->socklist))
		SIGNAL(&manager->shutdown_ok);
#endif /* USE_WATCHER_THREAD */

	/* can't unlock manager as its memory context is still used */
	free_socket(sockp);

	UNLOCK(&manager->lock);
}

static isc_result_t
allocate_socket(isc__socketmgr_t *manager, isc_sockettype_t type,
		isc__socket_t **socketp)
{
	isc__socket_t *sock;
	isc_result_t result;
	ISC_SOCKADDR_LEN_T cmsgbuflen;

	sock = isc_mem_get(manager->mctx, sizeof(*sock));

	if (sock == NULL)
		return (ISC_R_NOMEMORY);

	sock->common.magic = 0;
	sock->common.impmagic = 0;
	sock->references = 0;

	sock->manager = manager;
	sock->type = type;
	sock->fd = -1;
	sock->dupped = 0;
	sock->statsindex = NULL;

	ISC_LINK_INIT(sock, link);

	sock->recvcmsgbuf = NULL;
	sock->sendcmsgbuf = NULL;

	/*
	 * set up cmsg buffers
	 */
	cmsgbuflen = 0;
#if defined(USE_CMSG) && defined(ISC_PLATFORM_HAVEIN6PKTINFO)
	cmsgbuflen += cmsg_space(sizeof(struct in6_pktinfo));
#endif
#if defined(USE_CMSG) && defined(SO_TIMESTAMP)
	cmsgbuflen += cmsg_space(sizeof(struct timeval));
#endif
	sock->recvcmsgbuflen = cmsgbuflen;
	if (sock->recvcmsgbuflen != 0U) {
		sock->recvcmsgbuf = isc_mem_get(manager->mctx, cmsgbuflen);
		if (sock->recvcmsgbuf == NULL) {
			result = ISC_R_NOMEMORY;
			goto error;
		}
	}

	cmsgbuflen = 0;
#if defined(USE_CMSG) && defined(ISC_PLATFORM_HAVEIN6PKTINFO)
	cmsgbuflen += cmsg_space(sizeof(struct in6_pktinfo));
#if defined(IPV6_USE_MIN_MTU)
	/*
	 * Provide space for working around FreeBSD's broken IPV6_USE_MIN_MTU
	 * support.
	 */
	cmsgbuflen += cmsg_space(sizeof(int));
#endif
#endif
	sock->sendcmsgbuflen = cmsgbuflen;
	if (sock->sendcmsgbuflen != 0U) {
		sock->sendcmsgbuf = isc_mem_get(manager->mctx, cmsgbuflen);
		if (sock->sendcmsgbuf == NULL) {
			result = ISC_R_NOMEMORY;
			goto error;
		}
	}

	memset(sock->name, 0, sizeof(sock->name));
	sock->tag = NULL;

	/*
	 * set up list of readers and writers to be initially empty
	 */
	ISC_LIST_INIT(sock->recv_list);
	ISC_LIST_INIT(sock->send_list);
	ISC_LIST_INIT(sock->accept_list);
	sock->connect_ev = NULL;
	sock->pending_recv = 0;
	sock->pending_send = 0;
	sock->pending_accept = 0;
	sock->listener = 0;
	sock->connected = 0;
	sock->connecting = 0;
	sock->bound = 0;

	/*
	 * initialize the lock
	 */
	result = isc_mutex_init(&sock->lock);
	if (result != ISC_R_SUCCESS) {
		sock->common.magic = 0;
		sock->common.impmagic = 0;
		goto error;
	}

	/*
	 * Initialize readable and writable events
	 */
	ISC_EVENT_INIT(&sock->readable_ev, sizeof(intev_t),
		       ISC_EVENTATTR_NOPURGE, NULL, ISC_SOCKEVENT_INTR,
		       NULL, sock, sock, NULL, NULL);
	ISC_EVENT_INIT(&sock->writable_ev, sizeof(intev_t),
		       ISC_EVENTATTR_NOPURGE, NULL, ISC_SOCKEVENT_INTW,
		       NULL, sock, sock, NULL, NULL);

	sock->common.magic = ISCAPI_SOCKET_MAGIC;
	sock->common.impmagic = SOCKET_MAGIC;
	*socketp = sock;

	return (ISC_R_SUCCESS);

 error:
	if (sock->recvcmsgbuf != NULL)
		isc_mem_put(manager->mctx, sock->recvcmsgbuf,
			    sock->recvcmsgbuflen);
	if (sock->sendcmsgbuf != NULL)
		isc_mem_put(manager->mctx, sock->sendcmsgbuf,
			    sock->sendcmsgbuflen);
	isc_mem_put(manager->mctx, sock, sizeof(*sock));

	return (result);
}

/*
 * This event requires that the various lists be empty, that the reference
 * count be 1, and that the magic number is valid.  The other socket bits,
 * like the lock, must be initialized as well.  The fd associated must be
 * marked as closed, by setting it to -1 on close, or this routine will
 * also close the socket.
 */
static void
free_socket(isc__socket_t **socketp) {
	isc__socket_t *sock = *socketp;

	INSIST(sock->references == 0);
	INSIST(VALID_SOCKET(sock));
	INSIST(!sock->connecting);
	INSIST(!sock->pending_recv);
	INSIST(!sock->pending_send);
	INSIST(!sock->pending_accept);
	INSIST(ISC_LIST_EMPTY(sock->recv_list));
	INSIST(ISC_LIST_EMPTY(sock->send_list));
	INSIST(ISC_LIST_EMPTY(sock->accept_list));
	INSIST(!ISC_LINK_LINKED(sock, link));

	if (sock->recvcmsgbuf != NULL)
		isc_mem_put(sock->manager->mctx, sock->recvcmsgbuf,
			    sock->recvcmsgbuflen);
	if (sock->sendcmsgbuf != NULL)
		isc_mem_put(sock->manager->mctx, sock->sendcmsgbuf,
			    sock->sendcmsgbuflen);

	sock->common.magic = 0;
	sock->common.impmagic = 0;

	DESTROYLOCK(&sock->lock);

	isc_mem_put(sock->manager->mctx, sock, sizeof(*sock));

	*socketp = NULL;
}

#ifdef SO_BSDCOMPAT
/*
 * This really should not be necessary to do.  Having to workout
 * which kernel version we are on at run time so that we don't cause
 * the kernel to issue a warning about us using a deprecated socket option.
 * Such warnings should *never* be on by default in production kernels.
 *
 * We can't do this a build time because executables are moved between
 * machines and hence kernels.
 *
 * We can't just not set SO_BSDCOMAT because some kernels require it.
 */

static isc_once_t         bsdcompat_once = ISC_ONCE_INIT;
isc_boolean_t bsdcompat = ISC_TRUE;

static void
clear_bsdcompat(void) {
#ifdef __linux__
	 struct utsname buf;
	 char *endp;
	 long int major;
	 long int minor;

	 uname(&buf);    /* Can only fail if buf is bad in Linux. */

	 /* Paranoia in parsing can be increased, but we trust uname(). */
	 major = strtol(buf.release, &endp, 10);
	 if (*endp == '.') {
		minor = strtol(endp+1, &endp, 10);
		if ((major > 2) || ((major == 2) && (minor >= 4))) {
			bsdcompat = ISC_FALSE;
		}
	 }
#endif /* __linux __ */
}
#endif

static isc_result_t
opensocket(isc__socketmgr_t *manager, isc__socket_t *sock,
	   isc__socket_t *dup_socket)
{
	isc_result_t result;
	char strbuf[ISC_STRERRORSIZE];
	const char *err = "socket";
	int tries = 0;
#if defined(USE_CMSG) || defined(SO_BSDCOMPAT)
	int on = 1;
#endif
#if defined(SO_RCVBUF)
	ISC_SOCKADDR_LEN_T optlen;
	int size;
#endif

 again:
	if (dup_socket == NULL) {
		switch (sock->type) {
		case isc_sockettype_udp:
			sock->fd = socket(sock->pf, SOCK_DGRAM, IPPROTO_UDP);
			break;
		case isc_sockettype_tcp:
			sock->fd = socket(sock->pf, SOCK_STREAM, IPPROTO_TCP);
			break;
		case isc_sockettype_unix:
			sock->fd = socket(sock->pf, SOCK_STREAM, 0);
			break;
		case isc_sockettype_fdwatch:
			/*
			 * We should not be called for isc_sockettype_fdwatch
			 * sockets.
			 */
			INSIST(0);
			break;
		}
	} else {
		sock->fd = dup(dup_socket->fd);
		sock->dupped = 1;
		sock->bound = dup_socket->bound;
	}
	if (sock->fd == -1 && errno == EINTR && tries++ < 42)
		goto again;

#ifdef F_DUPFD
	/*
	 * Leave a space for stdio and TCP to work in.
	 */
	if (manager->reserved != 0 && sock->type == isc_sockettype_udp &&
	    sock->fd >= 0 && sock->fd < manager->reserved) {
		int new, tmp;
		new = fcntl(sock->fd, F_DUPFD, manager->reserved);
		tmp = errno;
		(void)close(sock->fd);
		errno = tmp;
		sock->fd = new;
		err = "isc_socket_create: fcntl/reserved";
	} else if (sock->fd >= 0 && sock->fd < 20) {
		int new, tmp;
		new = fcntl(sock->fd, F_DUPFD, 20);
		tmp = errno;
		(void)close(sock->fd);
		errno = tmp;
		sock->fd = new;
		err = "isc_socket_create: fcntl";
	}
#endif

	if (sock->fd >= (int)manager->maxsocks) {
		(void)close(sock->fd);
		isc_log_iwrite(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			       ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
			       isc_msgcat, ISC_MSGSET_SOCKET,
			       ISC_MSG_TOOMANYFDS,
			       "socket: file descriptor exceeds limit (%d/%u)",
			       sock->fd, manager->maxsocks);
		return (ISC_R_NORESOURCES);
	}

	if (sock->fd < 0) {
		switch (errno) {
		case EMFILE:
		case ENFILE:
			isc__strerror(errno, strbuf, sizeof(strbuf));
			isc_log_iwrite(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				       ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				       isc_msgcat, ISC_MSGSET_SOCKET,
				       ISC_MSG_TOOMANYFDS,
				       "%s: %s", err, strbuf);
			/* fallthrough */
		case ENOBUFS:
			return (ISC_R_NORESOURCES);

		case EPROTONOSUPPORT:
		case EPFNOSUPPORT:
		case EAFNOSUPPORT:
		/*
		 * Linux 2.2 (and maybe others) return EINVAL instead of
		 * EAFNOSUPPORT.
		 */
		case EINVAL:
			return (ISC_R_FAMILYNOSUPPORT);

		default:
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "%s() %s: %s", err,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strbuf);
			return (ISC_R_UNEXPECTED);
		}
	}

	if (dup_socket != NULL)
		goto setup_done;

	result = make_nonblock(sock->fd);
	if (result != ISC_R_SUCCESS) {
		(void)close(sock->fd);
		return (result);
	}

#ifdef SO_BSDCOMPAT
	RUNTIME_CHECK(isc_once_do(&bsdcompat_once,
				  clear_bsdcompat) == ISC_R_SUCCESS);
	if (sock->type != isc_sockettype_unix && bsdcompat &&
	    setsockopt(sock->fd, SOL_SOCKET, SO_BSDCOMPAT,
		       (void *)&on, sizeof(on)) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "setsockopt(%d, SO_BSDCOMPAT) %s: %s",
				 sock->fd,
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 strbuf);
		/* Press on... */
	}
#endif

#ifdef SO_NOSIGPIPE
	if (setsockopt(sock->fd, SOL_SOCKET, SO_NOSIGPIPE,
		       (void *)&on, sizeof(on)) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "setsockopt(%d, SO_NOSIGPIPE) %s: %s",
				 sock->fd,
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 strbuf);
		/* Press on... */
	}
#endif

#if defined(USE_CMSG) || defined(SO_RCVBUF)
	if (sock->type == isc_sockettype_udp) {

#if defined(USE_CMSG)
#if defined(SO_TIMESTAMP)
		if (setsockopt(sock->fd, SOL_SOCKET, SO_TIMESTAMP,
			       (void *)&on, sizeof(on)) < 0
		    && errno != ENOPROTOOPT) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "setsockopt(%d, SO_TIMESTAMP) %s: %s",
					 sock->fd,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strbuf);
			/* Press on... */
		}
#endif /* SO_TIMESTAMP */

#if defined(ISC_PLATFORM_HAVEIPV6)
		if (sock->pf == AF_INET6 && sock->recvcmsgbuflen == 0U) {
			/*
			 * Warn explicitly because this anomaly can be hidden
			 * in usual operation (and unexpectedly appear later).
			 */
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "No buffer available to receive "
					 "IPv6 destination");
		}
#ifdef ISC_PLATFORM_HAVEIN6PKTINFO
#ifdef IPV6_RECVPKTINFO
		/* RFC 3542 */
		if ((sock->pf == AF_INET6)
		    && (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_RECVPKTINFO,
				   (void *)&on, sizeof(on)) < 0)) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "setsockopt(%d, IPV6_RECVPKTINFO) "
					 "%s: %s", sock->fd,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strbuf);
		}
#else
		/* RFC 2292 */
		if ((sock->pf == AF_INET6)
		    && (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_PKTINFO,
				   (void *)&on, sizeof(on)) < 0)) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "setsockopt(%d, IPV6_PKTINFO) %s: %s",
					 sock->fd,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strbuf);
		}
#endif /* IPV6_RECVPKTINFO */
#endif /* ISC_PLATFORM_HAVEIN6PKTINFO */
#ifdef IPV6_USE_MIN_MTU        /* RFC 3542, not too common yet*/
		/* use minimum MTU */
		if (sock->pf == AF_INET6 &&
		    setsockopt(sock->fd, IPPROTO_IPV6, IPV6_USE_MIN_MTU,
			       (void *)&on, sizeof(on)) < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "setsockopt(%d, IPV6_USE_MIN_MTU) "
					 "%s: %s", sock->fd,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strbuf);
		}
#endif
#if defined(IPV6_MTU)
		/*
		 * Use minimum MTU on IPv6 sockets.
		 */
		if (sock->pf == AF_INET6) {
			int mtu = 1280;
			(void)setsockopt(sock->fd, IPPROTO_IPV6, IPV6_MTU,
					 &mtu, sizeof(mtu));
		}
#endif
#if defined(IPV6_MTU_DISCOVER) && defined(IPV6_PMTUDISC_DONT)
		/*
		 * Turn off Path MTU discovery on IPv6/UDP sockets.
		 */
		if (sock->pf == AF_INET6) {
			int action = IPV6_PMTUDISC_DONT;
			(void)setsockopt(sock->fd, IPPROTO_IPV6,
					 IPV6_MTU_DISCOVER, &action,
					 sizeof(action));
		}
#endif
#endif /* ISC_PLATFORM_HAVEIPV6 */
#endif /* defined(USE_CMSG) */

#if defined(IP_MTU_DISCOVER) && defined(IP_PMTUDISC_DONT)
		/*
		 * Turn off Path MTU discovery on IPv4/UDP sockets.
		 */
		if (sock->pf == AF_INET) {
			int action = IP_PMTUDISC_DONT;
			(void)setsockopt(sock->fd, IPPROTO_IP, IP_MTU_DISCOVER,
					 &action, sizeof(action));
		}
#endif
#if defined(IP_DONTFRAG)
		/*
		 * Turn off Path MTU discovery on IPv4/UDP sockets.
		 */
		if (sock->pf == AF_INET) {
			int off = 0;
			(void)setsockopt(sock->fd, IPPROTO_IP, IP_DONTFRAG,
					 &off, sizeof(off));
		}
#endif

#if defined(SO_RCVBUF)
		optlen = sizeof(size);
		if (getsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
			       (void *)&size, &optlen) >= 0 &&
		     size < RCVBUFSIZE) {
			size = RCVBUFSIZE;
			if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
				       (void *)&size, sizeof(size)) == -1) {
				isc__strerror(errno, strbuf, sizeof(strbuf));
				UNEXPECTED_ERROR(__FILE__, __LINE__,
					"setsockopt(%d, SO_RCVBUF, %d) %s: %s",
					sock->fd, size,
					isc_msgcat_get(isc_msgcat,
						       ISC_MSGSET_GENERAL,
						       ISC_MSG_FAILED,
						       "failed"),
					strbuf);
			}
		}
#endif
	}
#endif /* defined(USE_CMSG) || defined(SO_RCVBUF) */

setup_done:
	inc_stats(manager->stats, sock->statsindex[STATID_OPEN]);

	return (ISC_R_SUCCESS);
}

/*
 * Create a 'type' socket or duplicate an existing socket, managed
 * by 'manager'.  Events will be posted to 'task' and when dispatched
 * 'action' will be called with 'arg' as the arg value.  The new
 * socket is returned in 'socketp'.
 */
static isc_result_t
socket_create(isc_socketmgr_t *manager0, int pf, isc_sockettype_t type,
	      isc_socket_t **socketp, isc_socket_t *dup_socket)
{
	isc__socket_t *sock = NULL;
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;
	isc_result_t result;
	int lockid;

	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(socketp != NULL && *socketp == NULL);
	REQUIRE(type != isc_sockettype_fdwatch);

	result = allocate_socket(manager, type, &sock);
	if (result != ISC_R_SUCCESS)
		return (result);

	switch (sock->type) {
	case isc_sockettype_udp:
		sock->statsindex =
			(pf == AF_INET) ? upd4statsindex : upd6statsindex;
		break;
	case isc_sockettype_tcp:
		sock->statsindex =
			(pf == AF_INET) ? tcp4statsindex : tcp6statsindex;
		break;
	case isc_sockettype_unix:
		sock->statsindex = unixstatsindex;
		break;
	default:
		INSIST(0);
	}

	sock->pf = pf;

	result = opensocket(manager, sock, (isc__socket_t *)dup_socket);
	if (result != ISC_R_SUCCESS) {
		inc_stats(manager->stats, sock->statsindex[STATID_OPENFAIL]);
		free_socket(&sock);
		return (result);
	}

	sock->common.methods = (isc_socketmethods_t *)&socketmethods;
	sock->references = 1;
	*socketp = (isc_socket_t *)sock;

	/*
	 * Note we don't have to lock the socket like we normally would because
	 * there are no external references to it yet.
	 */

	lockid = FDLOCK_ID(sock->fd);
	LOCK(&manager->fdlock[lockid]);
	manager->fds[sock->fd] = sock;
	manager->fdstate[sock->fd] = MANAGED;
#ifdef USE_DEVPOLL
	INSIST(sock->manager->fdpollinfo[sock->fd].want_read == 0 &&
	       sock->manager->fdpollinfo[sock->fd].want_write == 0);
#endif
	UNLOCK(&manager->fdlock[lockid]);

	LOCK(&manager->lock);
	ISC_LIST_APPEND(manager->socklist, sock, link);
#ifdef USE_SELECT
	if (manager->maxfd < sock->fd)
		manager->maxfd = sock->fd;
#endif
	UNLOCK(&manager->lock);

	socket_log(sock, NULL, CREATION, isc_msgcat, ISC_MSGSET_SOCKET,
		   ISC_MSG_CREATED, dup_socket == NULL ? "dupped" : "created");

	return (ISC_R_SUCCESS);
}

/*%
 * Create a new 'type' socket managed by 'manager'.  Events
 * will be posted to 'task' and when dispatched 'action' will be
 * called with 'arg' as the arg value.  The new socket is returned
 * in 'socketp'.
 */
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_create(isc_socketmgr_t *manager0, int pf, isc_sockettype_t type,
		   isc_socket_t **socketp)
{
	return (socket_create(manager0, pf, type, socketp, NULL));
}

/*%
 * Duplicate an existing socket.  The new socket is returned
 * in 'socketp'.
 */
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_dup(isc_socket_t *sock0, isc_socket_t **socketp) {
	isc__socket_t *sock = (isc__socket_t *)sock0;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(socketp != NULL && *socketp == NULL);

	return (socket_create((isc_socketmgr_t *) sock->manager,
			      sock->pf, sock->type, socketp,
			      sock0));
}

#ifdef BIND9
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_open(isc_socket_t *sock0) {
	isc_result_t result;
	isc__socket_t *sock = (isc__socket_t *)sock0;

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	REQUIRE(sock->references == 1);
	REQUIRE(sock->type != isc_sockettype_fdwatch);
	UNLOCK(&sock->lock);
	/*
	 * We don't need to retain the lock hereafter, since no one else has
	 * this socket.
	 */
	REQUIRE(sock->fd == -1);

	result = opensocket(sock->manager, sock, NULL);
	if (result != ISC_R_SUCCESS)
		sock->fd = -1;

	if (result == ISC_R_SUCCESS) {
		int lockid = FDLOCK_ID(sock->fd);

		LOCK(&sock->manager->fdlock[lockid]);
		sock->manager->fds[sock->fd] = sock;
		sock->manager->fdstate[sock->fd] = MANAGED;
#ifdef USE_DEVPOLL
		INSIST(sock->manager->fdpollinfo[sock->fd].want_read == 0 &&
		       sock->manager->fdpollinfo[sock->fd].want_write == 0);
#endif
		UNLOCK(&sock->manager->fdlock[lockid]);

#ifdef USE_SELECT
		LOCK(&sock->manager->lock);
		if (sock->manager->maxfd < sock->fd)
			sock->manager->maxfd = sock->fd;
		UNLOCK(&sock->manager->lock);
#endif
	}

	return (result);
}
#endif	/* BIND9 */

/*
 * Create a new 'type' socket managed by 'manager'.  Events
 * will be posted to 'task' and when dispatched 'action' will be
 * called with 'arg' as the arg value.  The new socket is returned
 * in 'socketp'.
 */
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_fdwatchcreate(isc_socketmgr_t *manager0, int fd, int flags,
			  isc_sockfdwatch_t callback, void *cbarg,
			  isc_task_t *task, isc_socket_t **socketp)
{
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;
	isc__socket_t *sock = NULL;
	isc_result_t result;
	int lockid;

	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(socketp != NULL && *socketp == NULL);

	result = allocate_socket(manager, isc_sockettype_fdwatch, &sock);
	if (result != ISC_R_SUCCESS)
		return (result);

	sock->fd = fd;
	sock->fdwatcharg = cbarg;
	sock->fdwatchcb = callback;
	sock->fdwatchflags = flags;
	sock->fdwatchtask = task;
	sock->statsindex = fdwatchstatsindex;

	sock->common.methods = (isc_socketmethods_t *)&socketmethods;
	sock->references = 1;
	*socketp = (isc_socket_t *)sock;

	/*
	 * Note we don't have to lock the socket like we normally would because
	 * there are no external references to it yet.
	 */

	lockid = FDLOCK_ID(sock->fd);
	LOCK(&manager->fdlock[lockid]);
	manager->fds[sock->fd] = sock;
	manager->fdstate[sock->fd] = MANAGED;
	UNLOCK(&manager->fdlock[lockid]);

	LOCK(&manager->lock);
	ISC_LIST_APPEND(manager->socklist, sock, link);
#ifdef USE_SELECT
	if (manager->maxfd < sock->fd)
		manager->maxfd = sock->fd;
#endif
	UNLOCK(&manager->lock);

	if (flags & ISC_SOCKFDWATCH_READ)
		select_poke(sock->manager, sock->fd, SELECT_POKE_READ);
	if (flags & ISC_SOCKFDWATCH_WRITE)
		select_poke(sock->manager, sock->fd, SELECT_POKE_WRITE);

	socket_log(sock, NULL, CREATION, isc_msgcat, ISC_MSGSET_SOCKET,
		   ISC_MSG_CREATED, "fdwatch-created");

	return (ISC_R_SUCCESS);
}

/*
 * Indicate to the manager that it should watch the socket again.
 * This can be used to restart watching if the previous event handler
 * didn't indicate there was more data to be processed.  Primarily
 * it is for writing but could be used for reading if desired
 */

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_fdwatchpoke(isc_socket_t *sock0, int flags)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;

	REQUIRE(VALID_SOCKET(sock));

	/*
	 * We check both flags first to allow us to get the lock
	 * once but only if we need it.
	 */

	if ((flags & (ISC_SOCKFDWATCH_READ | ISC_SOCKFDWATCH_WRITE)) != 0) {
		LOCK(&sock->lock);
		if (((flags & ISC_SOCKFDWATCH_READ) != 0) &&
		    !sock->pending_recv)
			select_poke(sock->manager, sock->fd,
				    SELECT_POKE_READ);
		if (((flags & ISC_SOCKFDWATCH_WRITE) != 0) &&
		    !sock->pending_send)
			select_poke(sock->manager, sock->fd,
				    SELECT_POKE_WRITE);
		UNLOCK(&sock->lock);
	}

	socket_log(sock, NULL, TRACE, isc_msgcat, ISC_MSGSET_SOCKET,
		   ISC_MSG_POKED, "fdwatch-poked flags: %d", flags);

	return (ISC_R_SUCCESS);
}

/*
 * Attach to a socket.  Caller must explicitly detach when it is done.
 */
ISC_SOCKETFUNC_SCOPE void
isc__socket_attach(isc_socket_t *sock0, isc_socket_t **socketp) {
	isc__socket_t *sock = (isc__socket_t *)sock0;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(socketp != NULL && *socketp == NULL);

	LOCK(&sock->lock);
	sock->references++;
	UNLOCK(&sock->lock);

	*socketp = (isc_socket_t *)sock;
}

/*
 * Dereference a socket.  If this is the last reference to it, clean things
 * up by destroying the socket.
 */
ISC_SOCKETFUNC_SCOPE void
isc__socket_detach(isc_socket_t **socketp) {
	isc__socket_t *sock;
	isc_boolean_t kill_socket = ISC_FALSE;

	REQUIRE(socketp != NULL);
	sock = (isc__socket_t *)*socketp;
	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	REQUIRE(sock->references > 0);
	sock->references--;
	if (sock->references == 0)
		kill_socket = ISC_TRUE;
	UNLOCK(&sock->lock);

	if (kill_socket)
		destroy(&sock);

	*socketp = NULL;
}

#ifdef BIND9
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_close(isc_socket_t *sock0) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
	int fd;
	isc__socketmgr_t *manager;

	fflush(stdout);
	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);

	REQUIRE(sock->references == 1);
	REQUIRE(sock->type != isc_sockettype_fdwatch);
	REQUIRE(sock->fd >= 0 && sock->fd < (int)sock->manager->maxsocks);

	INSIST(!sock->connecting);
	INSIST(!sock->pending_recv);
	INSIST(!sock->pending_send);
	INSIST(!sock->pending_accept);
	INSIST(ISC_LIST_EMPTY(sock->recv_list));
	INSIST(ISC_LIST_EMPTY(sock->send_list));
	INSIST(ISC_LIST_EMPTY(sock->accept_list));
	INSIST(sock->connect_ev == NULL);

	manager = sock->manager;
	fd = sock->fd;
	sock->fd = -1;
	sock->dupped = 0;
	memset(sock->name, 0, sizeof(sock->name));
	sock->tag = NULL;
	sock->listener = 0;
	sock->connected = 0;
	sock->connecting = 0;
	sock->bound = 0;
	isc_sockaddr_any(&sock->peer_address);

	UNLOCK(&sock->lock);

	closesocket(manager, sock, fd);

	return (ISC_R_SUCCESS);
}
#endif	/* BIND9 */

/*
 * I/O is possible on a given socket.  Schedule an event to this task that
 * will call an internal function to do the I/O.  This will charge the
 * task with the I/O operation and let our select loop handler get back
 * to doing something real as fast as possible.
 *
 * The socket and manager must be locked before calling this function.
 */
static void
dispatch_recv(isc__socket_t *sock) {
	intev_t *iev;
	isc_socketevent_t *ev;
	isc_task_t *sender;

	INSIST(!sock->pending_recv);

	if (sock->type != isc_sockettype_fdwatch) {
		ev = ISC_LIST_HEAD(sock->recv_list);
		if (ev == NULL)
			return;
		socket_log(sock, NULL, EVENT, NULL, 0, 0,
			   "dispatch_recv:  event %p -> task %p",
			   ev, ev->ev_sender);
		sender = ev->ev_sender;
	} else {
		sender = sock->fdwatchtask;
	}

	sock->pending_recv = 1;
	iev = &sock->readable_ev;

	sock->references++;
	iev->ev_sender = sock;
	if (sock->type == isc_sockettype_fdwatch)
		iev->ev_action = internal_fdwatch_read;
	else
		iev->ev_action = internal_recv;
	iev->ev_arg = sock;

	isc_task_send(sender, (isc_event_t **)&iev);
}

static void
dispatch_send(isc__socket_t *sock) {
	intev_t *iev;
	isc_socketevent_t *ev;
	isc_task_t *sender;

	INSIST(!sock->pending_send);

	if (sock->type != isc_sockettype_fdwatch) {
		ev = ISC_LIST_HEAD(sock->send_list);
		if (ev == NULL)
			return;
		socket_log(sock, NULL, EVENT, NULL, 0, 0,
			   "dispatch_send:  event %p -> task %p",
			   ev, ev->ev_sender);
		sender = ev->ev_sender;
	} else {
		sender = sock->fdwatchtask;
	}

	sock->pending_send = 1;
	iev = &sock->writable_ev;

	sock->references++;
	iev->ev_sender = sock;
	if (sock->type == isc_sockettype_fdwatch)
		iev->ev_action = internal_fdwatch_write;
	else
		iev->ev_action = internal_send;
	iev->ev_arg = sock;

	isc_task_send(sender, (isc_event_t **)&iev);
}

/*
 * Dispatch an internal accept event.
 */
static void
dispatch_accept(isc__socket_t *sock) {
	intev_t *iev;
	isc_socket_newconnev_t *ev;

	INSIST(!sock->pending_accept);

	/*
	 * Are there any done events left, or were they all canceled
	 * before the manager got the socket lock?
	 */
	ev = ISC_LIST_HEAD(sock->accept_list);
	if (ev == NULL)
		return;

	sock->pending_accept = 1;
	iev = &sock->readable_ev;

	sock->references++;  /* keep socket around for this internal event */
	iev->ev_sender = sock;
	iev->ev_action = internal_accept;
	iev->ev_arg = sock;

	isc_task_send(ev->ev_sender, (isc_event_t **)&iev);
}

static void
dispatch_connect(isc__socket_t *sock) {
	intev_t *iev;
	isc_socket_connev_t *ev;

	iev = &sock->writable_ev;

	ev = sock->connect_ev;
	INSIST(ev != NULL); /* XXX */

	INSIST(sock->connecting);

	sock->references++;  /* keep socket around for this internal event */
	iev->ev_sender = sock;
	iev->ev_action = internal_connect;
	iev->ev_arg = sock;

	isc_task_send(ev->ev_sender, (isc_event_t **)&iev);
}

/*
 * Dequeue an item off the given socket's read queue, set the result code
 * in the done event to the one provided, and send it to the task it was
 * destined for.
 *
 * If the event to be sent is on a list, remove it before sending.  If
 * asked to, send and detach from the socket as well.
 *
 * Caller must have the socket locked if the event is attached to the socket.
 */
static void
send_recvdone_event(isc__socket_t *sock, isc_socketevent_t **dev) {
	isc_task_t *task;

	task = (*dev)->ev_sender;

	(*dev)->ev_sender = sock;

	if (ISC_LINK_LINKED(*dev, ev_link))
		ISC_LIST_DEQUEUE(sock->recv_list, *dev, ev_link);

	if (((*dev)->attributes & ISC_SOCKEVENTATTR_ATTACHED)
	    == ISC_SOCKEVENTATTR_ATTACHED)
		isc_task_sendanddetach(&task, (isc_event_t **)dev);
	else
		isc_task_send(task, (isc_event_t **)dev);
}

/*
 * See comments for send_recvdone_event() above.
 *
 * Caller must have the socket locked if the event is attached to the socket.
 */
static void
send_senddone_event(isc__socket_t *sock, isc_socketevent_t **dev) {
	isc_task_t *task;

	INSIST(dev != NULL && *dev != NULL);

	task = (*dev)->ev_sender;
	(*dev)->ev_sender = sock;

	if (ISC_LINK_LINKED(*dev, ev_link))
		ISC_LIST_DEQUEUE(sock->send_list, *dev, ev_link);

	if (((*dev)->attributes & ISC_SOCKEVENTATTR_ATTACHED)
	    == ISC_SOCKEVENTATTR_ATTACHED)
		isc_task_sendanddetach(&task, (isc_event_t **)dev);
	else
		isc_task_send(task, (isc_event_t **)dev);
}

/*
 * Call accept() on a socket, to get the new file descriptor.  The listen
 * socket is used as a prototype to create a new isc_socket_t.  The new
 * socket has one outstanding reference.  The task receiving the event
 * will be detached from just after the event is delivered.
 *
 * On entry to this function, the event delivered is the internal
 * readable event, and the first item on the accept_list should be
 * the done event we want to send.  If the list is empty, this is a no-op,
 * so just unlock and return.
 */
static void
internal_accept(isc_task_t *me, isc_event_t *ev) {
	isc__socket_t *sock;
	isc__socketmgr_t *manager;
	isc_socket_newconnev_t *dev;
	isc_task_t *task;
	ISC_SOCKADDR_LEN_T addrlen;
	int fd;
	isc_result_t result = ISC_R_SUCCESS;
	char strbuf[ISC_STRERRORSIZE];
	const char *err = "accept";

	UNUSED(me);

	sock = ev->ev_sender;
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	socket_log(sock, NULL, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTLOCK,
		   "internal_accept called, locked socket");

	manager = sock->manager;
	INSIST(VALID_MANAGER(manager));

	INSIST(sock->listener);
	INSIST(sock->pending_accept == 1);
	sock->pending_accept = 0;

	INSIST(sock->references > 0);
	sock->references--;  /* the internal event is done with this socket */
	if (sock->references == 0) {
		UNLOCK(&sock->lock);
		destroy(&sock);
		return;
	}

	/*
	 * Get the first item off the accept list.
	 * If it is empty, unlock the socket and return.
	 */
	dev = ISC_LIST_HEAD(sock->accept_list);
	if (dev == NULL) {
		UNLOCK(&sock->lock);
		return;
	}

	/*
	 * Try to accept the new connection.  If the accept fails with
	 * EAGAIN or EINTR, simply poke the watcher to watch this socket
	 * again.  Also ignore ECONNRESET, which has been reported to
	 * be spuriously returned on Linux 2.2.19 although it is not
	 * a documented error for accept().  ECONNABORTED has been
	 * reported for Solaris 8.  The rest are thrown in not because
	 * we have seen them but because they are ignored by other
	 * daemons such as BIND 8 and Apache.
	 */

	addrlen = sizeof(NEWCONNSOCK(dev)->peer_address.type);
	memset(&NEWCONNSOCK(dev)->peer_address.type, 0, addrlen);
	fd = accept(sock->fd, &NEWCONNSOCK(dev)->peer_address.type.sa,
		    (void *)&addrlen);

#ifdef F_DUPFD
	/*
	 * Leave a space for stdio to work in.
	 */
	if (fd >= 0 && fd < 20) {
		int new, tmp;
		new = fcntl(fd, F_DUPFD, 20);
		tmp = errno;
		(void)close(fd);
		errno = tmp;
		fd = new;
		err = "accept/fcntl";
	}
#endif

	if (fd < 0) {
		if (SOFT_ERROR(errno))
			goto soft_error;
		switch (errno) {
		case ENFILE:
		case EMFILE:
			isc_log_iwrite(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				       ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				       isc_msgcat, ISC_MSGSET_SOCKET,
				       ISC_MSG_TOOMANYFDS,
				       "%s: too many open file descriptors",
				       err);
			goto soft_error;

		case ENOBUFS:
		case ENOMEM:
		case ECONNRESET:
		case ECONNABORTED:
		case EHOSTUNREACH:
		case EHOSTDOWN:
		case ENETUNREACH:
		case ENETDOWN:
		case ECONNREFUSED:
#ifdef EPROTO
		case EPROTO:
#endif
#ifdef ENONET
		case ENONET:
#endif
			goto soft_error;
		default:
			break;
		}
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "internal_accept: %s() %s: %s", err,
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED,
						"failed"),
				 strbuf);
		fd = -1;
		result = ISC_R_UNEXPECTED;
	} else {
		if (addrlen == 0U) {
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "internal_accept(): "
					 "accept() failed to return "
					 "remote address");

			(void)close(fd);
			goto soft_error;
		} else if (NEWCONNSOCK(dev)->peer_address.type.sa.sa_family !=
			   sock->pf)
		{
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "internal_accept(): "
					 "accept() returned peer address "
					 "family %u (expected %u)",
					 NEWCONNSOCK(dev)->peer_address.
					 type.sa.sa_family,
					 sock->pf);
			(void)close(fd);
			goto soft_error;
		} else if (fd >= (int)manager->maxsocks) {
			isc_log_iwrite(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				       ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				       isc_msgcat, ISC_MSGSET_SOCKET,
				       ISC_MSG_TOOMANYFDS,
				       "accept: "
				       "file descriptor exceeds limit (%d/%u)",
				       fd, manager->maxsocks);
			(void)close(fd);
			goto soft_error;
		}
	}

	if (fd != -1) {
		NEWCONNSOCK(dev)->peer_address.length = addrlen;
		NEWCONNSOCK(dev)->pf = sock->pf;
	}

	/*
	 * Pull off the done event.
	 */
	ISC_LIST_UNLINK(sock->accept_list, dev, ev_link);

	/*
	 * Poke watcher if there are more pending accepts.
	 */
	if (!ISC_LIST_EMPTY(sock->accept_list))
		select_poke(sock->manager, sock->fd, SELECT_POKE_ACCEPT);

	UNLOCK(&sock->lock);

	if (fd != -1) {
		result = make_nonblock(fd);
		if (result != ISC_R_SUCCESS) {
			(void)close(fd);
			fd = -1;
		}
	}

	/*
	 * -1 means the new socket didn't happen.
	 */
	if (fd != -1) {
		int lockid = FDLOCK_ID(fd);

		LOCK(&manager->fdlock[lockid]);
		manager->fds[fd] = NEWCONNSOCK(dev);
		manager->fdstate[fd] = MANAGED;
		UNLOCK(&manager->fdlock[lockid]);

		LOCK(&manager->lock);
		ISC_LIST_APPEND(manager->socklist, NEWCONNSOCK(dev), link);

		NEWCONNSOCK(dev)->fd = fd;
		NEWCONNSOCK(dev)->bound = 1;
		NEWCONNSOCK(dev)->connected = 1;

		/*
		 * Save away the remote address
		 */
		dev->address = NEWCONNSOCK(dev)->peer_address;

#ifdef USE_SELECT
		if (manager->maxfd < fd)
			manager->maxfd = fd;
#endif

		socket_log(sock, &NEWCONNSOCK(dev)->peer_address, CREATION,
			   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTEDCXN,
			   "accepted connection, new socket %p",
			   dev->newsocket);

		UNLOCK(&manager->lock);

		inc_stats(manager->stats, sock->statsindex[STATID_ACCEPT]);
	} else {
		inc_stats(manager->stats, sock->statsindex[STATID_ACCEPTFAIL]);
		NEWCONNSOCK(dev)->references--;
		free_socket((isc__socket_t **)&dev->newsocket);
	}

	/*
	 * Fill in the done event details and send it off.
	 */
	dev->result = result;
	task = dev->ev_sender;
	dev->ev_sender = sock;

	isc_task_sendanddetach(&task, ISC_EVENT_PTR(&dev));
	return;

 soft_error:
	select_poke(sock->manager, sock->fd, SELECT_POKE_ACCEPT);
	UNLOCK(&sock->lock);

	inc_stats(manager->stats, sock->statsindex[STATID_ACCEPTFAIL]);
	return;
}

static void
internal_recv(isc_task_t *me, isc_event_t *ev) {
	isc_socketevent_t *dev;
	isc__socket_t *sock;

	INSIST(ev->ev_type == ISC_SOCKEVENT_INTR);

	sock = ev->ev_sender;
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	socket_log(sock, NULL, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_INTERNALRECV,
		   "internal_recv: task %p got event %p", me, ev);

	INSIST(sock->pending_recv == 1);
	sock->pending_recv = 0;

	INSIST(sock->references > 0);
	sock->references--;  /* the internal event is done with this socket */
	if (sock->references == 0) {
		UNLOCK(&sock->lock);
		destroy(&sock);
		return;
	}

	/*
	 * Try to do as much I/O as possible on this socket.  There are no
	 * limits here, currently.
	 */
	dev = ISC_LIST_HEAD(sock->recv_list);
	while (dev != NULL) {
		switch (doio_recv(sock, dev)) {
		case DOIO_SOFT:
			goto poke;

		case DOIO_EOF:
			/*
			 * read of 0 means the remote end was closed.
			 * Run through the event queue and dispatch all
			 * the events with an EOF result code.
			 */
			do {
				dev->result = ISC_R_EOF;
				send_recvdone_event(sock, &dev);
				dev = ISC_LIST_HEAD(sock->recv_list);
			} while (dev != NULL);
			goto poke;

		case DOIO_SUCCESS:
		case DOIO_HARD:
			send_recvdone_event(sock, &dev);
			break;
		}

		dev = ISC_LIST_HEAD(sock->recv_list);
	}

 poke:
	if (!ISC_LIST_EMPTY(sock->recv_list))
		select_poke(sock->manager, sock->fd, SELECT_POKE_READ);

	UNLOCK(&sock->lock);
}

static void
internal_send(isc_task_t *me, isc_event_t *ev) {
	isc_socketevent_t *dev;
	isc__socket_t *sock;

	INSIST(ev->ev_type == ISC_SOCKEVENT_INTW);

	/*
	 * Find out what socket this is and lock it.
	 */
	sock = (isc__socket_t *)ev->ev_sender;
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	socket_log(sock, NULL, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_INTERNALSEND,
		   "internal_send: task %p got event %p", me, ev);

	INSIST(sock->pending_send == 1);
	sock->pending_send = 0;

	INSIST(sock->references > 0);
	sock->references--;  /* the internal event is done with this socket */
	if (sock->references == 0) {
		UNLOCK(&sock->lock);
		destroy(&sock);
		return;
	}

	/*
	 * Try to do as much I/O as possible on this socket.  There are no
	 * limits here, currently.
	 */
	dev = ISC_LIST_HEAD(sock->send_list);
	while (dev != NULL) {
		switch (doio_send(sock, dev)) {
		case DOIO_SOFT:
			goto poke;

		case DOIO_HARD:
		case DOIO_SUCCESS:
			send_senddone_event(sock, &dev);
			break;
		}

		dev = ISC_LIST_HEAD(sock->send_list);
	}

 poke:
	if (!ISC_LIST_EMPTY(sock->send_list))
		select_poke(sock->manager, sock->fd, SELECT_POKE_WRITE);

	UNLOCK(&sock->lock);
}

static void
internal_fdwatch_write(isc_task_t *me, isc_event_t *ev) {
	isc__socket_t *sock;
	int more_data;

	INSIST(ev->ev_type == ISC_SOCKEVENT_INTW);

	/*
	 * Find out what socket this is and lock it.
	 */
	sock = (isc__socket_t *)ev->ev_sender;
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	socket_log(sock, NULL, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_INTERNALSEND,
		   "internal_fdwatch_write: task %p got event %p", me, ev);

	INSIST(sock->pending_send == 1);

	UNLOCK(&sock->lock);
	more_data = (sock->fdwatchcb)(me, (isc_socket_t *)sock,
				      sock->fdwatcharg, ISC_SOCKFDWATCH_WRITE);
	LOCK(&sock->lock);

	sock->pending_send = 0;

	INSIST(sock->references > 0);
	sock->references--;  /* the internal event is done with this socket */
	if (sock->references == 0) {
		UNLOCK(&sock->lock);
		destroy(&sock);
		return;
	}

	if (more_data)
		select_poke(sock->manager, sock->fd, SELECT_POKE_WRITE);

	UNLOCK(&sock->lock);
}

static void
internal_fdwatch_read(isc_task_t *me, isc_event_t *ev) {
	isc__socket_t *sock;
	int more_data;

	INSIST(ev->ev_type == ISC_SOCKEVENT_INTR);

	/*
	 * Find out what socket this is and lock it.
	 */
	sock = (isc__socket_t *)ev->ev_sender;
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	socket_log(sock, NULL, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_INTERNALRECV,
		   "internal_fdwatch_read: task %p got event %p", me, ev);

	INSIST(sock->pending_recv == 1);

	UNLOCK(&sock->lock);
	more_data = (sock->fdwatchcb)(me, (isc_socket_t *)sock,
				      sock->fdwatcharg, ISC_SOCKFDWATCH_READ);
	LOCK(&sock->lock);

	sock->pending_recv = 0;

	INSIST(sock->references > 0);
	sock->references--;  /* the internal event is done with this socket */
	if (sock->references == 0) {
		UNLOCK(&sock->lock);
		destroy(&sock);
		return;
	}

	if (more_data)
		select_poke(sock->manager, sock->fd, SELECT_POKE_READ);

	UNLOCK(&sock->lock);
}

/*
 * Process read/writes on each fd here.  Avoid locking
 * and unlocking twice if both reads and writes are possible.
 */
static void
process_fd(isc__socketmgr_t *manager, int fd, isc_boolean_t readable,
	   isc_boolean_t writeable)
{
	isc__socket_t *sock;
	isc_boolean_t unlock_sock;
	isc_boolean_t unwatch_read = ISC_FALSE, unwatch_write = ISC_FALSE;
	int lockid = FDLOCK_ID(fd);

	/*
	 * If the socket is going to be closed, don't do more I/O.
	 */
	LOCK(&manager->fdlock[lockid]);
	if (manager->fdstate[fd] == CLOSE_PENDING) {
		UNLOCK(&manager->fdlock[lockid]);

		(void)unwatch_fd(manager, fd, SELECT_POKE_READ);
		(void)unwatch_fd(manager, fd, SELECT_POKE_WRITE);
		return;
	}

	sock = manager->fds[fd];
	unlock_sock = ISC_FALSE;
	if (readable) {
		if (sock == NULL) {
			unwatch_read = ISC_TRUE;
			goto check_write;
		}
		unlock_sock = ISC_TRUE;
		LOCK(&sock->lock);
		if (!SOCK_DEAD(sock)) {
			if (sock->listener)
				dispatch_accept(sock);
			else
				dispatch_recv(sock);
		}
		unwatch_read = ISC_TRUE;
	}
check_write:
	if (writeable) {
		if (sock == NULL) {
			unwatch_write = ISC_TRUE;
			goto unlock_fd;
		}
		if (!unlock_sock) {
			unlock_sock = ISC_TRUE;
			LOCK(&sock->lock);
		}
		if (!SOCK_DEAD(sock)) {
			if (sock->connecting)
				dispatch_connect(sock);
			else
				dispatch_send(sock);
		}
		unwatch_write = ISC_TRUE;
	}
	if (unlock_sock)
		UNLOCK(&sock->lock);

 unlock_fd:
	UNLOCK(&manager->fdlock[lockid]);
	if (unwatch_read)
		(void)unwatch_fd(manager, fd, SELECT_POKE_READ);
	if (unwatch_write)
		(void)unwatch_fd(manager, fd, SELECT_POKE_WRITE);

}

#ifdef USE_KQUEUE
static isc_boolean_t
process_fds(isc__socketmgr_t *manager, struct kevent *events, int nevents) {
	int i;
	isc_boolean_t readable, writable;
	isc_boolean_t done = ISC_FALSE;
#ifdef USE_WATCHER_THREAD
	isc_boolean_t have_ctlevent = ISC_FALSE;
#endif

	if (nevents == manager->nevents) {
		/*
		 * This is not an error, but something unexpected.  If this
		 * happens, it may indicate the need for increasing
		 * ISC_SOCKET_MAXEVENTS.
		 */
		manager_log(manager, ISC_LOGCATEGORY_GENERAL,
			    ISC_LOGMODULE_SOCKET, ISC_LOG_INFO,
			    "maximum number of FD events (%d) received",
			    nevents);
	}

	for (i = 0; i < nevents; i++) {
		REQUIRE(events[i].ident < manager->maxsocks);
#ifdef USE_WATCHER_THREAD
		if (events[i].ident == (uintptr_t)manager->pipe_fds[0]) {
			have_ctlevent = ISC_TRUE;
			continue;
		}
#endif
		readable = ISC_TF(events[i].filter == EVFILT_READ);
		writable = ISC_TF(events[i].filter == EVFILT_WRITE);
		process_fd(manager, events[i].ident, readable, writable);
	}

#ifdef USE_WATCHER_THREAD
	if (have_ctlevent)
		done = process_ctlfd(manager);
#endif

	return (done);
}
#elif defined(USE_EPOLL)
static isc_boolean_t
process_fds(isc__socketmgr_t *manager, struct epoll_event *events, int nevents)
{
	int i;
	isc_boolean_t done = ISC_FALSE;
#ifdef USE_WATCHER_THREAD
	isc_boolean_t have_ctlevent = ISC_FALSE;
#endif

	if (nevents == manager->nevents) {
		manager_log(manager, ISC_LOGCATEGORY_GENERAL,
			    ISC_LOGMODULE_SOCKET, ISC_LOG_INFO,
			    "maximum number of FD events (%d) received",
			    nevents);
	}

	for (i = 0; i < nevents; i++) {
		REQUIRE(events[i].data.fd < (int)manager->maxsocks);
#ifdef USE_WATCHER_THREAD
		if (events[i].data.fd == manager->pipe_fds[0]) {
			have_ctlevent = ISC_TRUE;
			continue;
		}
#endif
		if ((events[i].events & EPOLLERR) != 0 ||
		    (events[i].events & EPOLLHUP) != 0) {
			/*
			 * epoll does not set IN/OUT bits on an erroneous
			 * condition, so we need to try both anyway.  This is a
			 * bit inefficient, but should be okay for such rare
			 * events.  Note also that the read or write attempt
			 * won't block because we use non-blocking sockets.
			 */
			events[i].events |= (EPOLLIN | EPOLLOUT);
		}
		process_fd(manager, events[i].data.fd,
			   (events[i].events & EPOLLIN) != 0,
			   (events[i].events & EPOLLOUT) != 0);
	}

#ifdef USE_WATCHER_THREAD
	if (have_ctlevent)
		done = process_ctlfd(manager);
#endif

	return (done);
}
#elif defined(USE_DEVPOLL)
static isc_boolean_t
process_fds(isc__socketmgr_t *manager, struct pollfd *events, int nevents) {
	int i;
	isc_boolean_t done = ISC_FALSE;
#ifdef USE_WATCHER_THREAD
	isc_boolean_t have_ctlevent = ISC_FALSE;
#endif

	if (nevents == manager->nevents) {
		manager_log(manager, ISC_LOGCATEGORY_GENERAL,
			    ISC_LOGMODULE_SOCKET, ISC_LOG_INFO,
			    "maximum number of FD events (%d) received",
			    nevents);
	}

	for (i = 0; i < nevents; i++) {
		REQUIRE(events[i].fd < (int)manager->maxsocks);
#ifdef USE_WATCHER_THREAD
		if (events[i].fd == manager->pipe_fds[0]) {
			have_ctlevent = ISC_TRUE;
			continue;
		}
#endif
		process_fd(manager, events[i].fd,
			   (events[i].events & POLLIN) != 0,
			   (events[i].events & POLLOUT) != 0);
	}

#ifdef USE_WATCHER_THREAD
	if (have_ctlevent)
		done = process_ctlfd(manager);
#endif

	return (done);
}
#elif defined(USE_SELECT)
static void
process_fds(isc__socketmgr_t *manager, int maxfd, fd_set *readfds,
	    fd_set *writefds)
{
	int i;

	REQUIRE(maxfd <= (int)manager->maxsocks);

	for (i = 0; i < maxfd; i++) {
#ifdef USE_WATCHER_THREAD
		if (i == manager->pipe_fds[0] || i == manager->pipe_fds[1])
			continue;
#endif /* USE_WATCHER_THREAD */
		process_fd(manager, i, FD_ISSET(i, readfds),
			   FD_ISSET(i, writefds));
	}
}
#endif

#ifdef USE_WATCHER_THREAD
static isc_boolean_t
process_ctlfd(isc__socketmgr_t *manager) {
	int msg, fd;

	for (;;) {
		select_readmsg(manager, &fd, &msg);

		manager_log(manager, IOEVENT,
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_WATCHERMSG,
					   "watcher got message %d "
					   "for socket %d"), msg, fd);

		/*
		 * Nothing to read?
		 */
		if (msg == SELECT_POKE_NOTHING)
			break;

		/*
		 * Handle shutdown message.  We really should
		 * jump out of this loop right away, but
		 * it doesn't matter if we have to do a little
		 * more work first.
		 */
		if (msg == SELECT_POKE_SHUTDOWN)
			return (ISC_TRUE);

		/*
		 * This is a wakeup on a socket.  Look
		 * at the event queue for both read and write,
		 * and decide if we need to watch on it now
		 * or not.
		 */
		wakeup_socket(manager, fd, msg);
	}

	return (ISC_FALSE);
}

/*
 * This is the thread that will loop forever, always in a select or poll
 * call.
 *
 * When select returns something to do, track down what thread gets to do
 * this I/O and post the event to it.
 */
static isc_threadresult_t
watcher(void *uap) {
	isc__socketmgr_t *manager = uap;
	isc_boolean_t done;
	int cc;
#ifdef USE_KQUEUE
	const char *fnname = "kevent()";
#elif defined (USE_EPOLL)
	const char *fnname = "epoll_wait()";
#elif defined(USE_DEVPOLL)
	const char *fnname = "ioctl(DP_POLL)";
	struct dvpoll dvp;
#elif defined (USE_SELECT)
	const char *fnname = "select()";
	int maxfd;
	int ctlfd;
#endif
	char strbuf[ISC_STRERRORSIZE];
#ifdef ISC_SOCKET_USE_POLLWATCH
	pollstate_t pollstate = poll_idle;
#endif

#if defined (USE_SELECT)
	/*
	 * Get the control fd here.  This will never change.
	 */
	ctlfd = manager->pipe_fds[0];
#endif
	done = ISC_FALSE;
	while (!done) {
		do {
#ifdef USE_KQUEUE
			cc = kevent(manager->kqueue_fd, NULL, 0,
				    manager->events, manager->nevents, NULL);
#elif defined(USE_EPOLL)
			cc = epoll_wait(manager->epoll_fd, manager->events,
					manager->nevents, -1);
#elif defined(USE_DEVPOLL)
			dvp.dp_fds = manager->events;
			dvp.dp_nfds = manager->nevents;
#ifndef ISC_SOCKET_USE_POLLWATCH
			dvp.dp_timeout = -1;
#else
			if (pollstate == poll_idle)
				dvp.dp_timeout = -1;
			else
				dvp.dp_timeout = ISC_SOCKET_POLLWATCH_TIMEOUT;
#endif	/* ISC_SOCKET_USE_POLLWATCH */
			cc = ioctl(manager->devpoll_fd, DP_POLL, &dvp);
#elif defined(USE_SELECT)
			LOCK(&manager->lock);
			memcpy(manager->read_fds_copy, manager->read_fds,
			       manager->fd_bufsize);
			memcpy(manager->write_fds_copy, manager->write_fds,
			       manager->fd_bufsize);
			maxfd = manager->maxfd + 1;
			UNLOCK(&manager->lock);

			cc = select(maxfd, manager->read_fds_copy,
				    manager->write_fds_copy, NULL, NULL);
#endif	/* USE_KQUEUE */

			if (cc < 0 && !SOFT_ERROR(errno)) {
				isc__strerror(errno, strbuf, sizeof(strbuf));
				FATAL_ERROR(__FILE__, __LINE__,
					    "%s %s: %s", fnname,
					    isc_msgcat_get(isc_msgcat,
							   ISC_MSGSET_GENERAL,
							   ISC_MSG_FAILED,
							   "failed"), strbuf);
			}

#if defined(USE_DEVPOLL) && defined(ISC_SOCKET_USE_POLLWATCH)
			if (cc == 0) {
				if (pollstate == poll_active)
					pollstate = poll_checking;
				else if (pollstate == poll_checking)
					pollstate = poll_idle;
			} else if (cc > 0) {
				if (pollstate == poll_checking) {
					/*
					 * XXX: We'd like to use a more
					 * verbose log level as it's actually an
					 * unexpected event, but the kernel bug
					 * reportedly happens pretty frequently
					 * (and it can also be a false positive)
					 * so it would be just too noisy.
					 */
					manager_log(manager,
						    ISC_LOGCATEGORY_GENERAL,
						    ISC_LOGMODULE_SOCKET,
						    ISC_LOG_DEBUG(1),
						    "unexpected POLL timeout");
				}
				pollstate = poll_active;
			}
#endif
		} while (cc < 0);

#if defined(USE_KQUEUE) || defined (USE_EPOLL) || defined (USE_DEVPOLL)
		done = process_fds(manager, manager->events, cc);
#elif defined(USE_SELECT)
		process_fds(manager, maxfd, manager->read_fds_copy,
			    manager->write_fds_copy);

		/*
		 * Process reads on internal, control fd.
		 */
		if (FD_ISSET(ctlfd, manager->read_fds_copy))
			done = process_ctlfd(manager);
#endif
	}

	manager_log(manager, TRACE, "%s",
		    isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
				   ISC_MSG_EXITING, "watcher exiting"));

	return ((isc_threadresult_t)0);
}
#endif /* USE_WATCHER_THREAD */

#ifdef BIND9
ISC_SOCKETFUNC_SCOPE void
isc__socketmgr_setreserved(isc_socketmgr_t *manager0, isc_uint32_t reserved) {
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;

	REQUIRE(VALID_MANAGER(manager));

	manager->reserved = reserved;
}

ISC_SOCKETFUNC_SCOPE void
isc___socketmgr_maxudp(isc_socketmgr_t *manager0, int maxudp) {
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;

	REQUIRE(VALID_MANAGER(manager));

	manager->maxudp = maxudp;
}
#endif	/* BIND9 */

/*
 * Create a new socket manager.
 */

static isc_result_t
setup_watcher(isc_mem_t *mctx, isc__socketmgr_t *manager) {
	isc_result_t result;
#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_DEVPOLL)
	char strbuf[ISC_STRERRORSIZE];
#endif

#ifdef USE_KQUEUE
	manager->nevents = ISC_SOCKET_MAXEVENTS;
	manager->events = isc_mem_get(mctx, sizeof(struct kevent) *
				      manager->nevents);
	if (manager->events == NULL)
		return (ISC_R_NOMEMORY);
	manager->kqueue_fd = kqueue();
	if (manager->kqueue_fd == -1) {
		result = isc__errno2result(errno);
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "kqueue %s: %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 strbuf);
		isc_mem_put(mctx, manager->events,
			    sizeof(struct kevent) * manager->nevents);
		return (result);
	}

#ifdef USE_WATCHER_THREAD
	result = watch_fd(manager, manager->pipe_fds[0], SELECT_POKE_READ);
	if (result != ISC_R_SUCCESS) {
		close(manager->kqueue_fd);
		isc_mem_put(mctx, manager->events,
			    sizeof(struct kevent) * manager->nevents);
		return (result);
	}
#endif	/* USE_WATCHER_THREAD */
#elif defined(USE_EPOLL)
	manager->nevents = ISC_SOCKET_MAXEVENTS;
	manager->events = isc_mem_get(mctx, sizeof(struct epoll_event) *
				      manager->nevents);
	if (manager->events == NULL)
		return (ISC_R_NOMEMORY);
	manager->epoll_fd = epoll_create(manager->nevents);
	if (manager->epoll_fd == -1) {
		result = isc__errno2result(errno);
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "epoll_create %s: %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 strbuf);
		isc_mem_put(mctx, manager->events,
			    sizeof(struct epoll_event) * manager->nevents);
		return (result);
	}
#ifdef USE_WATCHER_THREAD
	result = watch_fd(manager, manager->pipe_fds[0], SELECT_POKE_READ);
	if (result != ISC_R_SUCCESS) {
		close(manager->epoll_fd);
		isc_mem_put(mctx, manager->events,
			    sizeof(struct epoll_event) * manager->nevents);
		return (result);
	}
#endif	/* USE_WATCHER_THREAD */
#elif defined(USE_DEVPOLL)
	/*
	 * XXXJT: /dev/poll seems to reject large numbers of events,
	 * so we should be careful about redefining ISC_SOCKET_MAXEVENTS.
	 */
	manager->nevents = ISC_SOCKET_MAXEVENTS;
	manager->events = isc_mem_get(mctx, sizeof(struct pollfd) *
				      manager->nevents);
	if (manager->events == NULL)
		return (ISC_R_NOMEMORY);
	/*
	 * Note: fdpollinfo should be able to support all possible FDs, so
	 * it must have maxsocks entries (not nevents).
	 */
	manager->fdpollinfo = isc_mem_get(mctx, sizeof(pollinfo_t) *
					  manager->maxsocks);
	if (manager->fdpollinfo == NULL) {
		isc_mem_put(mctx, manager->events,
			    sizeof(struct pollfd) * manager->nevents);
		return (ISC_R_NOMEMORY);
	}
	memset(manager->fdpollinfo, 0, sizeof(pollinfo_t) * manager->maxsocks);
	manager->devpoll_fd = open("/dev/poll", O_RDWR);
	if (manager->devpoll_fd == -1) {
		result = isc__errno2result(errno);
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "open(/dev/poll) %s: %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 strbuf);
		isc_mem_put(mctx, manager->events,
			    sizeof(struct pollfd) * manager->nevents);
		isc_mem_put(mctx, manager->fdpollinfo,
			    sizeof(pollinfo_t) * manager->maxsocks);
		return (result);
	}
#ifdef USE_WATCHER_THREAD
	result = watch_fd(manager, manager->pipe_fds[0], SELECT_POKE_READ);
	if (result != ISC_R_SUCCESS) {
		close(manager->devpoll_fd);
		isc_mem_put(mctx, manager->events,
			    sizeof(struct pollfd) * manager->nevents);
		isc_mem_put(mctx, manager->fdpollinfo,
			    sizeof(pollinfo_t) * manager->maxsocks);
		return (result);
	}
#endif	/* USE_WATCHER_THREAD */
#elif defined(USE_SELECT)
	UNUSED(result);

#if ISC_SOCKET_MAXSOCKETS > FD_SETSIZE
	/*
	 * Note: this code should also cover the case of MAXSOCKETS <=
	 * FD_SETSIZE, but we separate the cases to avoid possible portability
	 * issues regarding howmany() and the actual representation of fd_set.
	 */
	manager->fd_bufsize = howmany(manager->maxsocks, NFDBITS) *
		sizeof(fd_mask);
#else
	manager->fd_bufsize = sizeof(fd_set);
#endif

	manager->read_fds = NULL;
	manager->read_fds_copy = NULL;
	manager->write_fds = NULL;
	manager->write_fds_copy = NULL;

	manager->read_fds = isc_mem_get(mctx, manager->fd_bufsize);
	if (manager->read_fds != NULL)
		manager->read_fds_copy = isc_mem_get(mctx, manager->fd_bufsize);
	if (manager->read_fds_copy != NULL)
		manager->write_fds = isc_mem_get(mctx, manager->fd_bufsize);
	if (manager->write_fds != NULL) {
		manager->write_fds_copy = isc_mem_get(mctx,
						      manager->fd_bufsize);
	}
	if (manager->write_fds_copy == NULL) {
		if (manager->write_fds != NULL) {
			isc_mem_put(mctx, manager->write_fds,
				    manager->fd_bufsize);
		}
		if (manager->read_fds_copy != NULL) {
			isc_mem_put(mctx, manager->read_fds_copy,
				    manager->fd_bufsize);
		}
		if (manager->read_fds != NULL) {
			isc_mem_put(mctx, manager->read_fds,
				    manager->fd_bufsize);
		}
		return (ISC_R_NOMEMORY);
	}
	memset(manager->read_fds, 0, manager->fd_bufsize);
	memset(manager->write_fds, 0, manager->fd_bufsize);

#ifdef USE_WATCHER_THREAD
	(void)watch_fd(manager, manager->pipe_fds[0], SELECT_POKE_READ);
	manager->maxfd = manager->pipe_fds[0];
#else /* USE_WATCHER_THREAD */
	manager->maxfd = 0;
#endif /* USE_WATCHER_THREAD */
#endif	/* USE_KQUEUE */

	return (ISC_R_SUCCESS);
}

static void
cleanup_watcher(isc_mem_t *mctx, isc__socketmgr_t *manager) {
#ifdef USE_WATCHER_THREAD
	isc_result_t result;

	result = unwatch_fd(manager, manager->pipe_fds[0], SELECT_POKE_READ);
	if (result != ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "epoll_ctl(DEL) %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
	}
#endif	/* USE_WATCHER_THREAD */

#ifdef USE_KQUEUE
	close(manager->kqueue_fd);
	isc_mem_put(mctx, manager->events,
		    sizeof(struct kevent) * manager->nevents);
#elif defined(USE_EPOLL)
	close(manager->epoll_fd);
	isc_mem_put(mctx, manager->events,
		    sizeof(struct epoll_event) * manager->nevents);
#elif defined(USE_DEVPOLL)
	close(manager->devpoll_fd);
	isc_mem_put(mctx, manager->events,
		    sizeof(struct pollfd) * manager->nevents);
	isc_mem_put(mctx, manager->fdpollinfo,
		    sizeof(pollinfo_t) * manager->maxsocks);
#elif defined(USE_SELECT)
	if (manager->read_fds != NULL)
		isc_mem_put(mctx, manager->read_fds, manager->fd_bufsize);
	if (manager->read_fds_copy != NULL)
		isc_mem_put(mctx, manager->read_fds_copy, manager->fd_bufsize);
	if (manager->write_fds != NULL)
		isc_mem_put(mctx, manager->write_fds, manager->fd_bufsize);
	if (manager->write_fds_copy != NULL)
		isc_mem_put(mctx, manager->write_fds_copy, manager->fd_bufsize);
#endif	/* USE_KQUEUE */
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socketmgr_create(isc_mem_t *mctx, isc_socketmgr_t **managerp) {
	return (isc__socketmgr_create2(mctx, managerp, 0));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socketmgr_create2(isc_mem_t *mctx, isc_socketmgr_t **managerp,
		       unsigned int maxsocks)
{
	int i;
	isc__socketmgr_t *manager;
#ifdef USE_WATCHER_THREAD
	char strbuf[ISC_STRERRORSIZE];
#endif
	isc_result_t result;

	REQUIRE(managerp != NULL && *managerp == NULL);

#ifdef USE_SHARED_MANAGER
	if (socketmgr != NULL) {
		/* Don't allow maxsocks to be updated */
		if (maxsocks > 0 && socketmgr->maxsocks != maxsocks)
			return (ISC_R_EXISTS);

		socketmgr->refs++;
		*managerp = (isc_socketmgr_t *)socketmgr;
		return (ISC_R_SUCCESS);
	}
#endif /* USE_SHARED_MANAGER */

	if (maxsocks == 0)
		maxsocks = ISC_SOCKET_MAXSOCKETS;

	manager = isc_mem_get(mctx, sizeof(*manager));
	if (manager == NULL)
		return (ISC_R_NOMEMORY);

	/* zero-clear so that necessary cleanup on failure will be easy */
	memset(manager, 0, sizeof(*manager));
	manager->maxsocks = maxsocks;
	manager->reserved = 0;
	manager->maxudp = 0;
	manager->fds = isc_mem_get(mctx,
				   manager->maxsocks * sizeof(isc__socket_t *));
	if (manager->fds == NULL) {
		result = ISC_R_NOMEMORY;
		goto free_manager;
	}
	manager->fdstate = isc_mem_get(mctx, manager->maxsocks * sizeof(int));
	if (manager->fdstate == NULL) {
		result = ISC_R_NOMEMORY;
		goto free_manager;
	}
	manager->stats = NULL;

	manager->common.methods = &socketmgrmethods;
	manager->common.magic = ISCAPI_SOCKETMGR_MAGIC;
	manager->common.impmagic = SOCKET_MANAGER_MAGIC;
	manager->mctx = NULL;
	memset(manager->fds, 0, manager->maxsocks * sizeof(isc_socket_t *));
	ISC_LIST_INIT(manager->socklist);
	result = isc_mutex_init(&manager->lock);
	if (result != ISC_R_SUCCESS)
		goto free_manager;
	manager->fdlock = isc_mem_get(mctx, FDLOCK_COUNT * sizeof(isc_mutex_t));
	if (manager->fdlock == NULL) {
		result = ISC_R_NOMEMORY;
		goto cleanup_lock;
	}
	for (i = 0; i < FDLOCK_COUNT; i++) {
		result = isc_mutex_init(&manager->fdlock[i]);
		if (result != ISC_R_SUCCESS) {
			while (--i >= 0)
				DESTROYLOCK(&manager->fdlock[i]);
			isc_mem_put(mctx, manager->fdlock,
				    FDLOCK_COUNT * sizeof(isc_mutex_t));
			manager->fdlock = NULL;
			goto cleanup_lock;
		}
	}

#ifdef USE_WATCHER_THREAD
	if (isc_condition_init(&manager->shutdown_ok) != ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_condition_init() %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
		result = ISC_R_UNEXPECTED;
		goto cleanup_lock;
	}

	/*
	 * Create the special fds that will be used to wake up the
	 * select/poll loop when something internal needs to be done.
	 */
	if (pipe(manager->pipe_fds) != 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "pipe() %s: %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 strbuf);
		result = ISC_R_UNEXPECTED;
		goto cleanup_condition;
	}

	RUNTIME_CHECK(make_nonblock(manager->pipe_fds[0]) == ISC_R_SUCCESS);
#if 0
	RUNTIME_CHECK(make_nonblock(manager->pipe_fds[1]) == ISC_R_SUCCESS);
#endif
#endif	/* USE_WATCHER_THREAD */

#ifdef USE_SHARED_MANAGER
	manager->refs = 1;
#endif /* USE_SHARED_MANAGER */

	/*
	 * Set up initial state for the select loop
	 */
	result = setup_watcher(mctx, manager);
	if (result != ISC_R_SUCCESS)
		goto cleanup;
	memset(manager->fdstate, 0, manager->maxsocks * sizeof(int));
#ifdef USE_WATCHER_THREAD
	/*
	 * Start up the select/poll thread.
	 */
	if (isc_thread_create(watcher, manager, &manager->watcher) !=
	    ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_thread_create() %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
		cleanup_watcher(mctx, manager);
		result = ISC_R_UNEXPECTED;
		goto cleanup;
	}
#endif /* USE_WATCHER_THREAD */
	isc_mem_attach(mctx, &manager->mctx);

#ifdef USE_SHARED_MANAGER
	socketmgr = manager;
#endif /* USE_SHARED_MANAGER */
	*managerp = (isc_socketmgr_t *)manager;

	return (ISC_R_SUCCESS);

cleanup:
#ifdef USE_WATCHER_THREAD
	(void)close(manager->pipe_fds[0]);
	(void)close(manager->pipe_fds[1]);
#endif	/* USE_WATCHER_THREAD */

#ifdef USE_WATCHER_THREAD
cleanup_condition:
	(void)isc_condition_destroy(&manager->shutdown_ok);
#endif	/* USE_WATCHER_THREAD */


cleanup_lock:
	if (manager->fdlock != NULL) {
		for (i = 0; i < FDLOCK_COUNT; i++)
			DESTROYLOCK(&manager->fdlock[i]);
	}
	DESTROYLOCK(&manager->lock);

free_manager:
	if (manager->fdlock != NULL) {
		isc_mem_put(mctx, manager->fdlock,
			    FDLOCK_COUNT * sizeof(isc_mutex_t));
	}
	if (manager->fdstate != NULL) {
		isc_mem_put(mctx, manager->fdstate,
			    manager->maxsocks * sizeof(int));
	}
	if (manager->fds != NULL) {
		isc_mem_put(mctx, manager->fds,
			    manager->maxsocks * sizeof(isc_socket_t *));
	}
	isc_mem_put(mctx, manager, sizeof(*manager));

	return (result);
}

#ifdef BIND9
isc_result_t
isc__socketmgr_getmaxsockets(isc_socketmgr_t *manager0, unsigned int *nsockp) {
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;
	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(nsockp != NULL);

	*nsockp = manager->maxsocks;

	return (ISC_R_SUCCESS);
}

void
isc__socketmgr_setstats(isc_socketmgr_t *manager0, isc_stats_t *stats) {
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;

	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(ISC_LIST_EMPTY(manager->socklist));
	REQUIRE(manager->stats == NULL);
	REQUIRE(isc_stats_ncounters(stats) == isc_sockstatscounter_max);

	isc_stats_attach(stats, &manager->stats);
}
#endif

ISC_SOCKETFUNC_SCOPE void
isc__socketmgr_destroy(isc_socketmgr_t **managerp) {
	isc__socketmgr_t *manager;
	int i;
	isc_mem_t *mctx;

	/*
	 * Destroy a socket manager.
	 */

	REQUIRE(managerp != NULL);
	manager = (isc__socketmgr_t *)*managerp;
	REQUIRE(VALID_MANAGER(manager));

#ifdef USE_SHARED_MANAGER
	manager->refs--;
	if (manager->refs > 0) {
		*managerp = NULL;
		return;
	}
	socketmgr = NULL;
#endif /* USE_SHARED_MANAGER */

	LOCK(&manager->lock);

	/*
	 * Wait for all sockets to be destroyed.
	 */
	while (!ISC_LIST_EMPTY(manager->socklist)) {
#ifdef USE_WATCHER_THREAD
		manager_log(manager, CREATION, "%s",
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_SOCKETSREMAIN,
					   "sockets exist"));
		WAIT(&manager->shutdown_ok, &manager->lock);
#else /* USE_WATCHER_THREAD */
		UNLOCK(&manager->lock);
		isc__taskmgr_dispatch(NULL);
		LOCK(&manager->lock);
#endif /* USE_WATCHER_THREAD */
	}

	UNLOCK(&manager->lock);

	/*
	 * Here, poke our select/poll thread.  Do this by closing the write
	 * half of the pipe, which will send EOF to the read half.
	 * This is currently a no-op in the non-threaded case.
	 */
	select_poke(manager, 0, SELECT_POKE_SHUTDOWN);

#ifdef USE_WATCHER_THREAD
	/*
	 * Wait for thread to exit.
	 */
	if (isc_thread_join(manager->watcher, NULL) != ISC_R_SUCCESS)
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_thread_join() %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
#endif /* USE_WATCHER_THREAD */

	/*
	 * Clean up.
	 */
	cleanup_watcher(manager->mctx, manager);

#ifdef USE_WATCHER_THREAD
	(void)close(manager->pipe_fds[0]);
	(void)close(manager->pipe_fds[1]);
	(void)isc_condition_destroy(&manager->shutdown_ok);
#endif /* USE_WATCHER_THREAD */

	for (i = 0; i < (int)manager->maxsocks; i++)
		if (manager->fdstate[i] == CLOSE_PENDING) /* no need to lock */
			(void)close(i);

	isc_mem_put(manager->mctx, manager->fds,
		    manager->maxsocks * sizeof(isc__socket_t *));
	isc_mem_put(manager->mctx, manager->fdstate,
		    manager->maxsocks * sizeof(int));

	if (manager->stats != NULL)
		isc_stats_detach(&manager->stats);

	if (manager->fdlock != NULL) {
		for (i = 0; i < FDLOCK_COUNT; i++)
			DESTROYLOCK(&manager->fdlock[i]);
		isc_mem_put(manager->mctx, manager->fdlock,
			    FDLOCK_COUNT * sizeof(isc_mutex_t));
	}
	DESTROYLOCK(&manager->lock);
	manager->common.magic = 0;
	manager->common.impmagic = 0;
	mctx= manager->mctx;
	isc_mem_put(mctx, manager, sizeof(*manager));

	isc_mem_detach(&mctx);

	*managerp = NULL;

#ifdef USE_SHARED_MANAGER
	socketmgr = NULL;
#endif
}

static isc_result_t
socket_recv(isc__socket_t *sock, isc_socketevent_t *dev, isc_task_t *task,
	    unsigned int flags)
{
	int io_state;
	isc_boolean_t have_lock = ISC_FALSE;
	isc_task_t *ntask = NULL;
	isc_result_t result = ISC_R_SUCCESS;

	dev->ev_sender = task;

	if (sock->type == isc_sockettype_udp) {
		io_state = doio_recv(sock, dev);
	} else {
		LOCK(&sock->lock);
		have_lock = ISC_TRUE;

		if (ISC_LIST_EMPTY(sock->recv_list))
			io_state = doio_recv(sock, dev);
		else
			io_state = DOIO_SOFT;
	}

	switch (io_state) {
	case DOIO_SOFT:
		/*
		 * We couldn't read all or part of the request right now, so
		 * queue it.
		 *
		 * Attach to socket and to task
		 */
		isc_task_attach(task, &ntask);
		dev->attributes |= ISC_SOCKEVENTATTR_ATTACHED;

		if (!have_lock) {
			LOCK(&sock->lock);
			have_lock = ISC_TRUE;
		}

		/*
		 * Enqueue the request.  If the socket was previously not being
		 * watched, poke the watcher to start paying attention to it.
		 */
		if (ISC_LIST_EMPTY(sock->recv_list) && !sock->pending_recv)
			select_poke(sock->manager, sock->fd, SELECT_POKE_READ);
		ISC_LIST_ENQUEUE(sock->recv_list, dev, ev_link);

		socket_log(sock, NULL, EVENT, NULL, 0, 0,
			   "socket_recv: event %p -> task %p",
			   dev, ntask);

		if ((flags & ISC_SOCKFLAG_IMMEDIATE) != 0)
			result = ISC_R_INPROGRESS;
		break;

	case DOIO_EOF:
		dev->result = ISC_R_EOF;
		/* fallthrough */

	case DOIO_HARD:
	case DOIO_SUCCESS:
		if ((flags & ISC_SOCKFLAG_IMMEDIATE) == 0)
			send_recvdone_event(sock, &dev);
		break;
	}

	if (have_lock)
		UNLOCK(&sock->lock);

	return (result);
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_recvv(isc_socket_t *sock0, isc_bufferlist_t *buflist,
		  unsigned int minimum, isc_task_t *task,
		  isc_taskaction_t action, const void *arg)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_socketevent_t *dev;
	isc__socketmgr_t *manager;
	unsigned int iocount;
	isc_buffer_t *buffer;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(buflist != NULL);
	REQUIRE(!ISC_LIST_EMPTY(*buflist));
	REQUIRE(task != NULL);
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	iocount = isc_bufferlist_availablecount(buflist);
	REQUIRE(iocount > 0);

	INSIST(sock->bound);

	dev = allocate_socketevent(sock, ISC_SOCKEVENT_RECVDONE, action, arg);
	if (dev == NULL)
		return (ISC_R_NOMEMORY);

	/*
	 * UDP sockets are always partial read
	 */
	if (sock->type == isc_sockettype_udp)
		dev->minimum = 1;
	else {
		if (minimum == 0)
			dev->minimum = iocount;
		else
			dev->minimum = minimum;
	}

	/*
	 * Move each buffer from the passed in list to our internal one.
	 */
	buffer = ISC_LIST_HEAD(*buflist);
	while (buffer != NULL) {
		ISC_LIST_DEQUEUE(*buflist, buffer, link);
		ISC_LIST_ENQUEUE(dev->bufferlist, buffer, link);
		buffer = ISC_LIST_HEAD(*buflist);
	}

	return (socket_recv(sock, dev, task, 0));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_recv(isc_socket_t *sock0, isc_region_t *region,
		 unsigned int minimum, isc_task_t *task,
		 isc_taskaction_t action, const void *arg)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_socketevent_t *dev;
	isc__socketmgr_t *manager;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	INSIST(sock->bound);

	dev = allocate_socketevent(sock, ISC_SOCKEVENT_RECVDONE, action, arg);
	if (dev == NULL)
		return (ISC_R_NOMEMORY);

	return (isc__socket_recv2(sock0, region, minimum, task, dev, 0));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_recv2(isc_socket_t *sock0, isc_region_t *region,
		  unsigned int minimum, isc_task_t *task,
		  isc_socketevent_t *event, unsigned int flags)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;

	event->ev_sender = sock;
	event->result = ISC_R_UNSET;
	ISC_LIST_INIT(event->bufferlist);
	event->region = *region;
	event->n = 0;
	event->offset = 0;
	event->attributes = 0;

	/*
	 * UDP sockets are always partial read.
	 */
	if (sock->type == isc_sockettype_udp)
		event->minimum = 1;
	else {
		if (minimum == 0)
			event->minimum = region->length;
		else
			event->minimum = minimum;
	}

	return (socket_recv(sock, event, task, flags));
}

static isc_result_t
socket_send(isc__socket_t *sock, isc_socketevent_t *dev, isc_task_t *task,
	    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo,
	    unsigned int flags)
{
	int io_state;
	isc_boolean_t have_lock = ISC_FALSE;
	isc_task_t *ntask = NULL;
	isc_result_t result = ISC_R_SUCCESS;

	dev->ev_sender = task;

	set_dev_address(address, sock, dev);
	if (pktinfo != NULL) {
		dev->attributes |= ISC_SOCKEVENTATTR_PKTINFO;
		dev->pktinfo = *pktinfo;

		if (!isc_sockaddr_issitelocal(&dev->address) &&
		    !isc_sockaddr_islinklocal(&dev->address)) {
			socket_log(sock, NULL, TRACE, isc_msgcat,
				   ISC_MSGSET_SOCKET, ISC_MSG_PKTINFOPROVIDED,
				   "pktinfo structure provided, ifindex %u "
				   "(set to 0)", pktinfo->ipi6_ifindex);

			/*
			 * Set the pktinfo index to 0 here, to let the
			 * kernel decide what interface it should send on.
			 */
			dev->pktinfo.ipi6_ifindex = 0;
		}
	}

	if (sock->type == isc_sockettype_udp)
		io_state = doio_send(sock, dev);
	else {
		LOCK(&sock->lock);
		have_lock = ISC_TRUE;

		if (ISC_LIST_EMPTY(sock->send_list))
			io_state = doio_send(sock, dev);
		else
			io_state = DOIO_SOFT;
	}

	switch (io_state) {
	case DOIO_SOFT:
		/*
		 * We couldn't send all or part of the request right now, so
		 * queue it unless ISC_SOCKFLAG_NORETRY is set.
		 */
		if ((flags & ISC_SOCKFLAG_NORETRY) == 0) {
			isc_task_attach(task, &ntask);
			dev->attributes |= ISC_SOCKEVENTATTR_ATTACHED;

			if (!have_lock) {
				LOCK(&sock->lock);
				have_lock = ISC_TRUE;
			}

			/*
			 * Enqueue the request.  If the socket was previously
			 * not being watched, poke the watcher to start
			 * paying attention to it.
			 */
			if (ISC_LIST_EMPTY(sock->send_list) &&
			    !sock->pending_send)
				select_poke(sock->manager, sock->fd,
					    SELECT_POKE_WRITE);
			ISC_LIST_ENQUEUE(sock->send_list, dev, ev_link);

			socket_log(sock, NULL, EVENT, NULL, 0, 0,
				   "socket_send: event %p -> task %p",
				   dev, ntask);

			if ((flags & ISC_SOCKFLAG_IMMEDIATE) != 0)
				result = ISC_R_INPROGRESS;
			break;
		}

	case DOIO_HARD:
	case DOIO_SUCCESS:
		if ((flags & ISC_SOCKFLAG_IMMEDIATE) == 0)
			send_senddone_event(sock, &dev);
		break;
	}

	if (have_lock)
		UNLOCK(&sock->lock);

	return (result);
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_send(isc_socket_t *sock, isc_region_t *region,
		 isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	/*
	 * REQUIRE() checking is performed in isc_socket_sendto().
	 */
	return (isc__socket_sendto(sock, region, task, action, arg, NULL,
				   NULL));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendto(isc_socket_t *sock0, isc_region_t *region,
		   isc_task_t *task, isc_taskaction_t action, const void *arg,
		   isc_sockaddr_t *address, struct in6_pktinfo *pktinfo)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_socketevent_t *dev;
	isc__socketmgr_t *manager;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(region != NULL);
	REQUIRE(task != NULL);
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	INSIST(sock->bound);

	dev = allocate_socketevent(sock, ISC_SOCKEVENT_SENDDONE, action, arg);
	if (dev == NULL)
		return (ISC_R_NOMEMORY);

	dev->region = *region;

	return (socket_send(sock, dev, task, address, pktinfo, 0));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendv(isc_socket_t *sock, isc_bufferlist_t *buflist,
		  isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	return (isc__socket_sendtov(sock, buflist, task, action, arg, NULL,
				    NULL));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendtov(isc_socket_t *sock0, isc_bufferlist_t *buflist,
		    isc_task_t *task, isc_taskaction_t action, const void *arg,
		    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_socketevent_t *dev;
	isc__socketmgr_t *manager;
	unsigned int iocount;
	isc_buffer_t *buffer;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(buflist != NULL);
	REQUIRE(!ISC_LIST_EMPTY(*buflist));
	REQUIRE(task != NULL);
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	iocount = isc_bufferlist_usedcount(buflist);
	REQUIRE(iocount > 0);

	dev = allocate_socketevent(sock, ISC_SOCKEVENT_SENDDONE, action, arg);
	if (dev == NULL)
		return (ISC_R_NOMEMORY);

	/*
	 * Move each buffer from the passed in list to our internal one.
	 */
	buffer = ISC_LIST_HEAD(*buflist);
	while (buffer != NULL) {
		ISC_LIST_DEQUEUE(*buflist, buffer, link);
		ISC_LIST_ENQUEUE(dev->bufferlist, buffer, link);
		buffer = ISC_LIST_HEAD(*buflist);
	}

	return (socket_send(sock, dev, task, address, pktinfo, 0));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_sendto2(isc_socket_t *sock0, isc_region_t *region,
		    isc_task_t *task,
		    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo,
		    isc_socketevent_t *event, unsigned int flags)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE((flags & ~(ISC_SOCKFLAG_IMMEDIATE|ISC_SOCKFLAG_NORETRY)) == 0);
	if ((flags & ISC_SOCKFLAG_NORETRY) != 0)
		REQUIRE(sock->type == isc_sockettype_udp);
	event->ev_sender = sock;
	event->result = ISC_R_UNSET;
	ISC_LIST_INIT(event->bufferlist);
	event->region = *region;
	event->n = 0;
	event->offset = 0;
	event->attributes = 0;

	return (socket_send(sock, event, task, address, pktinfo, flags));
}

ISC_SOCKETFUNC_SCOPE void
isc__socket_cleanunix(isc_sockaddr_t *sockaddr, isc_boolean_t active) {
#ifdef ISC_PLATFORM_HAVESYSUNH
	int s;
	struct stat sb;
	char strbuf[ISC_STRERRORSIZE];

	if (sockaddr->type.sa.sa_family != AF_UNIX)
		return;

#ifndef S_ISSOCK
#if defined(S_IFMT) && defined(S_IFSOCK)
#define S_ISSOCK(mode) ((mode & S_IFMT)==S_IFSOCK)
#elif defined(_S_IFMT) && defined(S_IFSOCK)
#define S_ISSOCK(mode) ((mode & _S_IFMT)==S_IFSOCK)
#endif
#endif

#ifndef S_ISFIFO
#if defined(S_IFMT) && defined(S_IFIFO)
#define S_ISFIFO(mode) ((mode & S_IFMT)==S_IFIFO)
#elif defined(_S_IFMT) && defined(S_IFIFO)
#define S_ISFIFO(mode) ((mode & _S_IFMT)==S_IFIFO)
#endif
#endif

#if !defined(S_ISFIFO) && !defined(S_ISSOCK)
#error You need to define S_ISFIFO and S_ISSOCK as appropriate for your platform.  See <sys/stat.h>.
#endif

#ifndef S_ISFIFO
#define S_ISFIFO(mode) 0
#endif

#ifndef S_ISSOCK
#define S_ISSOCK(mode) 0
#endif

	if (active) {
		if (stat(sockaddr->type.sunix.sun_path, &sb) < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				      "isc_socket_cleanunix: stat(%s): %s",
				      sockaddr->type.sunix.sun_path, strbuf);
			return;
		}
		if (!(S_ISSOCK(sb.st_mode) || S_ISFIFO(sb.st_mode))) {
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				      "isc_socket_cleanunix: %s: not a socket",
				      sockaddr->type.sunix.sun_path);
			return;
		}
		if (unlink(sockaddr->type.sunix.sun_path) < 0) {
			isc__strerror(errno, strbuf, sizeof(strbuf));
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				      "isc_socket_cleanunix: unlink(%s): %s",
				      sockaddr->type.sunix.sun_path, strbuf);
		}
		return;
	}

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_SOCKET, ISC_LOG_WARNING,
			      "isc_socket_cleanunix: socket(%s): %s",
			      sockaddr->type.sunix.sun_path, strbuf);
		return;
	}

	if (stat(sockaddr->type.sunix.sun_path, &sb) < 0) {
		switch (errno) {
		case ENOENT:    /* We exited cleanly last time */
			break;
		default:
			isc__strerror(errno, strbuf, sizeof(strbuf));
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_WARNING,
				      "isc_socket_cleanunix: stat(%s): %s",
				      sockaddr->type.sunix.sun_path, strbuf);
			break;
		}
		goto cleanup;
	}

	if (!(S_ISSOCK(sb.st_mode) || S_ISFIFO(sb.st_mode))) {
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_SOCKET, ISC_LOG_WARNING,
			      "isc_socket_cleanunix: %s: not a socket",
			      sockaddr->type.sunix.sun_path);
		goto cleanup;
	}

	if (connect(s, (struct sockaddr *)&sockaddr->type.sunix,
		    sizeof(sockaddr->type.sunix)) < 0) {
		switch (errno) {
		case ECONNREFUSED:
		case ECONNRESET:
			if (unlink(sockaddr->type.sunix.sun_path) < 0) {
				isc__strerror(errno, strbuf, sizeof(strbuf));
				isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
					      ISC_LOGMODULE_SOCKET,
					      ISC_LOG_WARNING,
					      "isc_socket_cleanunix: "
					      "unlink(%s): %s",
					      sockaddr->type.sunix.sun_path,
					      strbuf);
			}
			break;
		default:
			isc__strerror(errno, strbuf, sizeof(strbuf));
			isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
				      ISC_LOGMODULE_SOCKET, ISC_LOG_WARNING,
				      "isc_socket_cleanunix: connect(%s): %s",
				      sockaddr->type.sunix.sun_path, strbuf);
			break;
		}
	}
 cleanup:
	close(s);
#else
	UNUSED(sockaddr);
	UNUSED(active);
#endif
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_permunix(isc_sockaddr_t *sockaddr, isc_uint32_t perm,
		    isc_uint32_t owner, isc_uint32_t group)
{
#ifdef ISC_PLATFORM_HAVESYSUNH
	isc_result_t result = ISC_R_SUCCESS;
	char strbuf[ISC_STRERRORSIZE];
	char path[sizeof(sockaddr->type.sunix.sun_path)];
#ifdef NEED_SECURE_DIRECTORY
	char *slash;
#endif

	REQUIRE(sockaddr->type.sa.sa_family == AF_UNIX);
	INSIST(strlen(sockaddr->type.sunix.sun_path) < sizeof(path));
	strcpy(path, sockaddr->type.sunix.sun_path);

#ifdef NEED_SECURE_DIRECTORY
	slash = strrchr(path, '/');
	if (slash != NULL) {
		if (slash != path)
			*slash = '\0';
		else
			strcpy(path, "/");
	} else
		strcpy(path, ".");
#endif

	if (chmod(path, perm) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
			      "isc_socket_permunix: chmod(%s, %d): %s",
			      path, perm, strbuf);
		result = ISC_R_FAILURE;
	}
	if (chown(path, owner, group) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		isc_log_write(isc_lctx, ISC_LOGCATEGORY_GENERAL,
			      ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
			      "isc_socket_permunix: chown(%s, %d, %d): %s",
			      path, owner, group,
			      strbuf);
		result = ISC_R_FAILURE;
	}
	return (result);
#else
	UNUSED(sockaddr);
	UNUSED(perm);
	UNUSED(owner);
	UNUSED(group);
	return (ISC_R_NOTIMPLEMENTED);
#endif
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_bind(isc_socket_t *sock0, isc_sockaddr_t *sockaddr,
		 unsigned int options) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
	char strbuf[ISC_STRERRORSIZE];
	int on = 1;

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);

	INSIST(!sock->bound);
	INSIST(!sock->dupped);

	if (sock->pf != sockaddr->type.sa.sa_family) {
		UNLOCK(&sock->lock);
		return (ISC_R_FAMILYMISMATCH);
	}

	/*
	 * Only set SO_REUSEADDR when we want a specific port.
	 */
#ifdef AF_UNIX
	if (sock->pf == AF_UNIX)
		goto bind_socket;
#endif
	if ((options & ISC_SOCKET_REUSEADDRESS) != 0 &&
	    isc_sockaddr_getport(sockaddr) != (in_port_t)0 &&
	    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (void *)&on,
		       sizeof(on)) < 0) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "setsockopt(%d) %s", sock->fd,
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
		/* Press on... */
	}
#ifdef AF_UNIX
 bind_socket:
#endif
	if (bind(sock->fd, &sockaddr->type.sa, sockaddr->length) < 0) {
		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_BINDFAIL]);

		UNLOCK(&sock->lock);
		switch (errno) {
		case EACCES:
			return (ISC_R_NOPERM);
		case EADDRNOTAVAIL:
			return (ISC_R_ADDRNOTAVAIL);
		case EADDRINUSE:
			return (ISC_R_ADDRINUSE);
		case EINVAL:
			return (ISC_R_BOUND);
		default:
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__, "bind: %s",
					 strbuf);
			return (ISC_R_UNEXPECTED);
		}
	}

	socket_log(sock, sockaddr, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_BOUND, "bound");
	sock->bound = 1;

	UNLOCK(&sock->lock);
	return (ISC_R_SUCCESS);
}

/*
 * Enable this only for specific OS versions, and only when they have repaired
 * their problems with it.  Until then, this is is broken and needs to be
 * diabled by default.  See RT22589 for details.
 */
#undef ENABLE_ACCEPTFILTER

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_filter(isc_socket_t *sock0, const char *filter) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
#if defined(SO_ACCEPTFILTER) && defined(ENABLE_ACCEPTFILTER)
	char strbuf[ISC_STRERRORSIZE];
	struct accept_filter_arg afa;
#else
	UNUSED(sock);
	UNUSED(filter);
#endif

	REQUIRE(VALID_SOCKET(sock));

#if defined(SO_ACCEPTFILTER) && defined(ENABLE_ACCEPTFILTER)
	bzero(&afa, sizeof(afa));
	strncpy(afa.af_name, filter, sizeof(afa.af_name));
	if (setsockopt(sock->fd, SOL_SOCKET, SO_ACCEPTFILTER,
			 &afa, sizeof(afa)) == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		socket_log(sock, NULL, CREATION, isc_msgcat, ISC_MSGSET_SOCKET,
			   ISC_MSG_FILTER, "setsockopt(SO_ACCEPTFILTER): %s",
			   strbuf);
		return (ISC_R_FAILURE);
	}
	return (ISC_R_SUCCESS);
#else
	return (ISC_R_NOTIMPLEMENTED);
#endif
}

/*
 * Set up to listen on a given socket.  We do this by creating an internal
 * event that will be dispatched when the socket has read activity.  The
 * watcher will send the internal event to the task when there is a new
 * connection.
 *
 * Unlike in read, we don't preallocate a done event here.  Every time there
 * is a new connection we'll have to allocate a new one anyway, so we might
 * as well keep things simple rather than having to track them.
 */
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_listen(isc_socket_t *sock0, unsigned int backlog) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);

	REQUIRE(!sock->listener);
	REQUIRE(sock->bound);
	REQUIRE(sock->type == isc_sockettype_tcp ||
		sock->type == isc_sockettype_unix);

	if (backlog == 0)
		backlog = SOMAXCONN;

	if (listen(sock->fd, (int)backlog) < 0) {
		UNLOCK(&sock->lock);
		isc__strerror(errno, strbuf, sizeof(strbuf));

		UNEXPECTED_ERROR(__FILE__, __LINE__, "listen: %s", strbuf);

		return (ISC_R_UNEXPECTED);
	}

	sock->listener = 1;

	UNLOCK(&sock->lock);
	return (ISC_R_SUCCESS);
}

/*
 * This should try to do aggressive accept() XXXMLG
 */
ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_accept(isc_socket_t *sock0,
		  isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_socket_newconnev_t *dev;
	isc__socketmgr_t *manager;
	isc_task_t *ntask = NULL;
	isc__socket_t *nsock;
	isc_result_t result;
	isc_boolean_t do_poke = ISC_FALSE;

	REQUIRE(VALID_SOCKET(sock));
	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	LOCK(&sock->lock);

	REQUIRE(sock->listener);

	/*
	 * Sender field is overloaded here with the task we will be sending
	 * this event to.  Just before the actual event is delivered the
	 * actual ev_sender will be touched up to be the socket.
	 */
	dev = (isc_socket_newconnev_t *)
		isc_event_allocate(manager->mctx, task, ISC_SOCKEVENT_NEWCONN,
				   action, arg, sizeof(*dev));
	if (dev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
	}
	ISC_LINK_INIT(dev, ev_link);

	result = allocate_socket(manager, sock->type, &nsock);
	if (result != ISC_R_SUCCESS) {
		isc_event_free(ISC_EVENT_PTR(&dev));
		UNLOCK(&sock->lock);
		return (result);
	}

	/*
	 * Attach to socket and to task.
	 */
	isc_task_attach(task, &ntask);
	if (isc_task_exiting(ntask)) {
		free_socket(&nsock);
		isc_task_detach(&ntask);
		isc_event_free(ISC_EVENT_PTR(&dev));
		UNLOCK(&sock->lock);
		return (ISC_R_SHUTTINGDOWN);
	}
	nsock->references++;
	nsock->statsindex = sock->statsindex;

	dev->ev_sender = ntask;
	dev->newsocket = (isc_socket_t *)nsock;

	/*
	 * Poke watcher here.  We still have the socket locked, so there
	 * is no race condition.  We will keep the lock for such a short
	 * bit of time waking it up now or later won't matter all that much.
	 */
	if (ISC_LIST_EMPTY(sock->accept_list))
		do_poke = ISC_TRUE;

	ISC_LIST_ENQUEUE(sock->accept_list, dev, ev_link);

	if (do_poke)
		select_poke(manager, sock->fd, SELECT_POKE_ACCEPT);

	UNLOCK(&sock->lock);
	return (ISC_R_SUCCESS);
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_connect(isc_socket_t *sock0, isc_sockaddr_t *addr,
		   isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_socket_connev_t *dev;
	isc_task_t *ntask = NULL;
	isc__socketmgr_t *manager;
	int cc;
	char strbuf[ISC_STRERRORSIZE];
	char addrbuf[ISC_SOCKADDR_FORMATSIZE];

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(addr != NULL);
	REQUIRE(task != NULL);
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(addr != NULL);

	if (isc_sockaddr_ismulticast(addr))
		return (ISC_R_MULTICAST);

	LOCK(&sock->lock);

	REQUIRE(!sock->connecting);

	dev = (isc_socket_connev_t *)isc_event_allocate(manager->mctx, sock,
							ISC_SOCKEVENT_CONNECT,
							action,	arg,
							sizeof(*dev));
	if (dev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
	}
	ISC_LINK_INIT(dev, ev_link);

	/*
	 * Try to do the connect right away, as there can be only one
	 * outstanding, and it might happen to complete.
	 */
	sock->peer_address = *addr;
	cc = connect(sock->fd, &addr->type.sa, addr->length);
	if (cc < 0) {
		/*
		 * HP-UX "fails" to connect a UDP socket and sets errno to
		 * EINPROGRESS if it's non-blocking.  We'd rather regard this as
		 * a success and let the user detect it if it's really an error
		 * at the time of sending a packet on the socket.
		 */
		if (sock->type == isc_sockettype_udp && errno == EINPROGRESS) {
			cc = 0;
			goto success;
		}
		if (SOFT_ERROR(errno) || errno == EINPROGRESS)
			goto queue;

		switch (errno) {
#define ERROR_MATCH(a, b) case a: dev->result = b; goto err_exit;
			ERROR_MATCH(EACCES, ISC_R_NOPERM);
			ERROR_MATCH(EADDRNOTAVAIL, ISC_R_ADDRNOTAVAIL);
			ERROR_MATCH(EAFNOSUPPORT, ISC_R_ADDRNOTAVAIL);
			ERROR_MATCH(ECONNREFUSED, ISC_R_CONNREFUSED);
			ERROR_MATCH(EHOSTUNREACH, ISC_R_HOSTUNREACH);
#ifdef EHOSTDOWN
			ERROR_MATCH(EHOSTDOWN, ISC_R_HOSTUNREACH);
#endif
			ERROR_MATCH(ENETUNREACH, ISC_R_NETUNREACH);
			ERROR_MATCH(ENOBUFS, ISC_R_NORESOURCES);
			ERROR_MATCH(EPERM, ISC_R_HOSTUNREACH);
			ERROR_MATCH(EPIPE, ISC_R_NOTCONNECTED);
			ERROR_MATCH(ECONNRESET, ISC_R_CONNECTIONRESET);
#undef ERROR_MATCH
		}

		sock->connected = 0;

		isc__strerror(errno, strbuf, sizeof(strbuf));
		isc_sockaddr_format(addr, addrbuf, sizeof(addrbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__, "connect(%s) %d/%s",
				 addrbuf, errno, strbuf);

		UNLOCK(&sock->lock);
		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_CONNECTFAIL]);
		isc_event_free(ISC_EVENT_PTR(&dev));
		return (ISC_R_UNEXPECTED);

	err_exit:
		sock->connected = 0;
		isc_task_send(task, ISC_EVENT_PTR(&dev));

		UNLOCK(&sock->lock);
		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_CONNECTFAIL]);
		return (ISC_R_SUCCESS);
	}

	/*
	 * If connect completed, fire off the done event.
	 */
 success:
	if (cc == 0) {
		sock->connected = 1;
		sock->bound = 1;
		dev->result = ISC_R_SUCCESS;
		isc_task_send(task, ISC_EVENT_PTR(&dev));

		UNLOCK(&sock->lock);

		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_CONNECT]);

		return (ISC_R_SUCCESS);
	}

 queue:

	/*
	 * Attach to task.
	 */
	isc_task_attach(task, &ntask);

	sock->connecting = 1;

	dev->ev_sender = ntask;

	/*
	 * Poke watcher here.  We still have the socket locked, so there
	 * is no race condition.  We will keep the lock for such a short
	 * bit of time waking it up now or later won't matter all that much.
	 */
	if (sock->connect_ev == NULL)
		select_poke(manager, sock->fd, SELECT_POKE_CONNECT);

	sock->connect_ev = dev;

	UNLOCK(&sock->lock);
	return (ISC_R_SUCCESS);
}

/*
 * Called when a socket with a pending connect() finishes.
 */
static void
internal_connect(isc_task_t *me, isc_event_t *ev) {
	isc__socket_t *sock;
	isc_socket_connev_t *dev;
	isc_task_t *task;
	int cc;
	ISC_SOCKADDR_LEN_T optlen;
	char strbuf[ISC_STRERRORSIZE];
	char peerbuf[ISC_SOCKADDR_FORMATSIZE];

	UNUSED(me);
	INSIST(ev->ev_type == ISC_SOCKEVENT_INTW);

	sock = ev->ev_sender;
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);

	/*
	 * When the internal event was sent the reference count was bumped
	 * to keep the socket around for us.  Decrement the count here.
	 */
	INSIST(sock->references > 0);
	sock->references--;
	if (sock->references == 0) {
		UNLOCK(&sock->lock);
		destroy(&sock);
		return;
	}

	/*
	 * Has this event been canceled?
	 */
	dev = sock->connect_ev;
	if (dev == NULL) {
		INSIST(!sock->connecting);
		UNLOCK(&sock->lock);
		return;
	}

	INSIST(sock->connecting);
	sock->connecting = 0;

	/*
	 * Get any possible error status here.
	 */
	optlen = sizeof(cc);
	if (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR,
		       (void *)&cc, (void *)&optlen) < 0)
		cc = errno;
	else
		errno = cc;

	if (errno != 0) {
		/*
		 * If the error is EAGAIN, just re-select on this
		 * fd and pretend nothing strange happened.
		 */
		if (SOFT_ERROR(errno) || errno == EINPROGRESS) {
			sock->connecting = 1;
			select_poke(sock->manager, sock->fd,
				    SELECT_POKE_CONNECT);
			UNLOCK(&sock->lock);

			return;
		}

		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_CONNECTFAIL]);

		/*
		 * Translate other errors into ISC_R_* flavors.
		 */
		switch (errno) {
#define ERROR_MATCH(a, b) case a: dev->result = b; break;
			ERROR_MATCH(EACCES, ISC_R_NOPERM);
			ERROR_MATCH(EADDRNOTAVAIL, ISC_R_ADDRNOTAVAIL);
			ERROR_MATCH(EAFNOSUPPORT, ISC_R_ADDRNOTAVAIL);
			ERROR_MATCH(ECONNREFUSED, ISC_R_CONNREFUSED);
			ERROR_MATCH(EHOSTUNREACH, ISC_R_HOSTUNREACH);
#ifdef EHOSTDOWN
			ERROR_MATCH(EHOSTDOWN, ISC_R_HOSTUNREACH);
#endif
			ERROR_MATCH(ENETUNREACH, ISC_R_NETUNREACH);
			ERROR_MATCH(ENOBUFS, ISC_R_NORESOURCES);
			ERROR_MATCH(EPERM, ISC_R_HOSTUNREACH);
			ERROR_MATCH(EPIPE, ISC_R_NOTCONNECTED);
			ERROR_MATCH(ETIMEDOUT, ISC_R_TIMEDOUT);
			ERROR_MATCH(ECONNRESET, ISC_R_CONNECTIONRESET);
#undef ERROR_MATCH
		default:
			dev->result = ISC_R_UNEXPECTED;
			isc_sockaddr_format(&sock->peer_address, peerbuf,
					    sizeof(peerbuf));
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "internal_connect: connect(%s) %s",
					 peerbuf, strbuf);
		}
	} else {
		inc_stats(sock->manager->stats,
			  sock->statsindex[STATID_CONNECT]);
		dev->result = ISC_R_SUCCESS;
		sock->connected = 1;
		sock->bound = 1;
	}

	sock->connect_ev = NULL;

	UNLOCK(&sock->lock);

	task = dev->ev_sender;
	dev->ev_sender = sock;
	isc_task_sendanddetach(&task, ISC_EVENT_PTR(&dev));
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_getpeername(isc_socket_t *sock0, isc_sockaddr_t *addressp) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_result_t result;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(addressp != NULL);

	LOCK(&sock->lock);

	if (sock->connected) {
		*addressp = sock->peer_address;
		result = ISC_R_SUCCESS;
	} else {
		result = ISC_R_NOTCONNECTED;
	}

	UNLOCK(&sock->lock);

	return (result);
}

ISC_SOCKETFUNC_SCOPE isc_result_t
isc__socket_getsockname(isc_socket_t *sock0, isc_sockaddr_t *addressp) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
	ISC_SOCKADDR_LEN_T len;
	isc_result_t result;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(addressp != NULL);

	LOCK(&sock->lock);

	if (!sock->bound) {
		result = ISC_R_NOTBOUND;
		goto out;
	}

	result = ISC_R_SUCCESS;

	len = sizeof(addressp->type);
	if (getsockname(sock->fd, &addressp->type.sa, (void *)&len) < 0) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__, "getsockname: %s",
				 strbuf);
		result = ISC_R_UNEXPECTED;
		goto out;
	}
	addressp->length = (unsigned int)len;

 out:
	UNLOCK(&sock->lock);

	return (result);
}

/*
 * Run through the list of events on this socket, and cancel the ones
 * queued for task "task" of type "how".  "how" is a bitmask.
 */
ISC_SOCKETFUNC_SCOPE void
isc__socket_cancel(isc_socket_t *sock0, isc_task_t *task, unsigned int how) {
	isc__socket_t *sock = (isc__socket_t *)sock0;

	REQUIRE(VALID_SOCKET(sock));

	/*
	 * Quick exit if there is nothing to do.  Don't even bother locking
	 * in this case.
	 */
	if (how == 0)
		return;

	LOCK(&sock->lock);

	/*
	 * All of these do the same thing, more or less.
	 * Each will:
	 *	o If the internal event is marked as "posted" try to
	 *	  remove it from the task's queue.  If this fails, mark it
	 *	  as canceled instead, and let the task clean it up later.
	 *	o For each I/O request for that task of that type, post
	 *	  its done event with status of "ISC_R_CANCELED".
	 *	o Reset any state needed.
	 */
	if (((how & ISC_SOCKCANCEL_RECV) == ISC_SOCKCANCEL_RECV)
	    && !ISC_LIST_EMPTY(sock->recv_list)) {
		isc_socketevent_t      *dev;
		isc_socketevent_t      *next;
		isc_task_t	       *current_task;

		dev = ISC_LIST_HEAD(sock->recv_list);

		while (dev != NULL) {
			current_task = dev->ev_sender;
			next = ISC_LIST_NEXT(dev, ev_link);

			if ((task == NULL) || (task == current_task)) {
				dev->result = ISC_R_CANCELED;
				send_recvdone_event(sock, &dev);
			}
			dev = next;
		}
	}

	if (((how & ISC_SOCKCANCEL_SEND) == ISC_SOCKCANCEL_SEND)
	    && !ISC_LIST_EMPTY(sock->send_list)) {
		isc_socketevent_t      *dev;
		isc_socketevent_t      *next;
		isc_task_t	       *current_task;

		dev = ISC_LIST_HEAD(sock->send_list);

		while (dev != NULL) {
			current_task = dev->ev_sender;
			next = ISC_LIST_NEXT(dev, ev_link);

			if ((task == NULL) || (task == current_task)) {
				dev->result = ISC_R_CANCELED;
				send_senddone_event(sock, &dev);
			}
			dev = next;
		}
	}

	if (((how & ISC_SOCKCANCEL_ACCEPT) == ISC_SOCKCANCEL_ACCEPT)
	    && !ISC_LIST_EMPTY(sock->accept_list)) {
		isc_socket_newconnev_t *dev;
		isc_socket_newconnev_t *next;
		isc_task_t	       *current_task;

		dev = ISC_LIST_HEAD(sock->accept_list);
		while (dev != NULL) {
			current_task = dev->ev_sender;
			next = ISC_LIST_NEXT(dev, ev_link);

			if ((task == NULL) || (task == current_task)) {

				ISC_LIST_UNLINK(sock->accept_list, dev,
						ev_link);

				NEWCONNSOCK(dev)->references--;
				free_socket((isc__socket_t **)&dev->newsocket);

				dev->result = ISC_R_CANCELED;
				dev->ev_sender = sock;
				isc_task_sendanddetach(&current_task,
						       ISC_EVENT_PTR(&dev));
			}

			dev = next;
		}
	}

	/*
	 * Connecting is not a list.
	 */
	if (((how & ISC_SOCKCANCEL_CONNECT) == ISC_SOCKCANCEL_CONNECT)
	    && sock->connect_ev != NULL) {
		isc_socket_connev_t    *dev;
		isc_task_t	       *current_task;

		INSIST(sock->connecting);
		sock->connecting = 0;

		dev = sock->connect_ev;
		current_task = dev->ev_sender;

		if ((task == NULL) || (task == current_task)) {
			sock->connect_ev = NULL;

			dev->result = ISC_R_CANCELED;
			dev->ev_sender = sock;
			isc_task_sendanddetach(&current_task,
					       ISC_EVENT_PTR(&dev));
		}
	}

	UNLOCK(&sock->lock);
}

ISC_SOCKETFUNC_SCOPE isc_sockettype_t
isc__socket_gettype(isc_socket_t *sock0) {
	isc__socket_t *sock = (isc__socket_t *)sock0;

	REQUIRE(VALID_SOCKET(sock));

	return (sock->type);
}

ISC_SOCKETFUNC_SCOPE isc_boolean_t
isc__socket_isbound(isc_socket_t *sock0) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
	isc_boolean_t val;

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	val = ((sock->bound) ? ISC_TRUE : ISC_FALSE);
	UNLOCK(&sock->lock);

	return (val);
}

ISC_SOCKETFUNC_SCOPE void
isc__socket_ipv6only(isc_socket_t *sock0, isc_boolean_t yes) {
	isc__socket_t *sock = (isc__socket_t *)sock0;
#if defined(IPV6_V6ONLY)
	int onoff = yes ? 1 : 0;
#else
	UNUSED(yes);
	UNUSED(sock);
#endif

	REQUIRE(VALID_SOCKET(sock));
	INSIST(!sock->dupped);

#ifdef IPV6_V6ONLY
	if (sock->pf == AF_INET6) {
		if (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_V6ONLY,
			       (void *)&onoff, sizeof(int)) < 0) {
			char strbuf[ISC_STRERRORSIZE];
			isc__strerror(errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "setsockopt(%d, IPV6_V6ONLY) "
					 "%s: %s", sock->fd,
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strbuf);
		}
	}
	FIX_IPV6_RECVPKTINFO(sock);	/* AIX */
#endif
}

#ifndef USE_WATCHER_THREAD
/*
 * In our assumed scenario, we can simply use a single static object.
 * XXX: this is not true if the application uses multiple threads with
 *      'multi-context' mode.  Fixing this is a future TODO item.
 */
static isc_socketwait_t swait_private;

int
isc__socketmgr_waitevents(isc_socketmgr_t *manager0, struct timeval *tvp,
			  isc_socketwait_t **swaitp)
{
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;


	int n;
#ifdef USE_KQUEUE
	struct timespec ts, *tsp;
#endif
#ifdef USE_EPOLL
	int timeout;
#endif
#ifdef USE_DEVPOLL
	struct dvpoll dvp;
#endif

	REQUIRE(swaitp != NULL && *swaitp == NULL);

#ifdef USE_SHARED_MANAGER
	if (manager == NULL)
		manager = socketmgr;
#endif
	if (manager == NULL)
		return (0);

#ifdef USE_KQUEUE
	if (tvp != NULL) {
		ts.tv_sec = tvp->tv_sec;
		ts.tv_nsec = tvp->tv_usec * 1000;
		tsp = &ts;
	} else
		tsp = NULL;
	swait_private.nevents = kevent(manager->kqueue_fd, NULL, 0,
				       manager->events, manager->nevents,
				       tsp);
	n = swait_private.nevents;
#elif defined(USE_EPOLL)
	if (tvp != NULL)
		timeout = tvp->tv_sec * 1000 + (tvp->tv_usec + 999) / 1000;
	else
		timeout = -1;
	swait_private.nevents = epoll_wait(manager->epoll_fd,
					   manager->events,
					   manager->nevents, timeout);
	n = swait_private.nevents;
#elif defined(USE_DEVPOLL)
	dvp.dp_fds = manager->events;
	dvp.dp_nfds = manager->nevents;
	if (tvp != NULL) {
		dvp.dp_timeout = tvp->tv_sec * 1000 +
			(tvp->tv_usec + 999) / 1000;
	} else
		dvp.dp_timeout = -1;
	swait_private.nevents = ioctl(manager->devpoll_fd, DP_POLL, &dvp);
	n = swait_private.nevents;
#elif defined(USE_SELECT)
	memcpy(manager->read_fds_copy, manager->read_fds,  manager->fd_bufsize);
	memcpy(manager->write_fds_copy, manager->write_fds,
	       manager->fd_bufsize);

	swait_private.readset = manager->read_fds_copy;
	swait_private.writeset = manager->write_fds_copy;
	swait_private.maxfd = manager->maxfd + 1;

	n = select(swait_private.maxfd, swait_private.readset,
		   swait_private.writeset, NULL, tvp);
#endif

	*swaitp = &swait_private;
	return (n);
}

isc_result_t
isc__socketmgr_dispatch(isc_socketmgr_t *manager0, isc_socketwait_t *swait) {
	isc__socketmgr_t *manager = (isc__socketmgr_t *)manager0;

	REQUIRE(swait == &swait_private);

#ifdef USE_SHARED_MANAGER
	if (manager == NULL)
		manager = socketmgr;
#endif
	if (manager == NULL)
		return (ISC_R_NOTFOUND);

#if defined(USE_KQUEUE) || defined(USE_EPOLL) || defined(USE_DEVPOLL)
	(void)process_fds(manager, manager->events, swait->nevents);
	return (ISC_R_SUCCESS);
#elif defined(USE_SELECT)
	process_fds(manager, swait->maxfd, swait->readset, swait->writeset);
	return (ISC_R_SUCCESS);
#endif
}
#endif /* USE_WATCHER_THREAD */

#ifdef BIND9
void
isc__socket_setname(isc_socket_t *socket0, const char *name, void *tag) {
	isc__socket_t *socket = (isc__socket_t *)socket0;

	/*
	 * Name 'socket'.
	 */

	REQUIRE(VALID_SOCKET(socket));

	LOCK(&socket->lock);
	memset(socket->name, 0, sizeof(socket->name));
	strncpy(socket->name, name, sizeof(socket->name) - 1);
	socket->tag = tag;
	UNLOCK(&socket->lock);
}

ISC_SOCKETFUNC_SCOPE const char *
isc__socket_getname(isc_socket_t *socket0) {
	isc__socket_t *socket = (isc__socket_t *)socket0;

	return (socket->name);
}

void *
isc__socket_gettag(isc_socket_t *socket0) {
	isc__socket_t *socket = (isc__socket_t *)socket0;

	return (socket->tag);
}
#endif	/* BIND9 */

#ifdef USE_SOCKETIMPREGISTER
isc_result_t
isc__socket_register() {
	return (isc_socket_register(isc__socketmgr_create));
}
#endif

ISC_SOCKETFUNC_SCOPE int
isc__socket_getfd(isc_socket_t *socket0) {
	isc__socket_t *socket = (isc__socket_t *)socket0;

	return ((short) socket->fd);
}

#if defined(HAVE_LIBXML2) && defined(BIND9)

static const char *
_socktype(isc_sockettype_t type)
{
	if (type == isc_sockettype_udp)
		return ("udp");
	else if (type == isc_sockettype_tcp)
		return ("tcp");
	else if (type == isc_sockettype_unix)
		return ("unix");
	else if (type == isc_sockettype_fdwatch)
		return ("fdwatch");
	else
		return ("not-initialized");
}

ISC_SOCKETFUNC_SCOPE void
isc_socketmgr_renderxml(isc_socketmgr_t *mgr0, xmlTextWriterPtr writer) {
	isc__socketmgr_t *mgr = (isc__socketmgr_t *)mgr0;
	isc__socket_t *sock;
	char peerbuf[ISC_SOCKADDR_FORMATSIZE];
	isc_sockaddr_t addr;
	ISC_SOCKADDR_LEN_T len;

	LOCK(&mgr->lock);

#ifdef USE_SHARED_MANAGER
	xmlTextWriterStartElement(writer, ISC_XMLCHAR "references");
	xmlTextWriterWriteFormatString(writer, "%d", mgr->refs);
	xmlTextWriterEndElement(writer);
#endif	/* USE_SHARED_MANAGER */

	xmlTextWriterStartElement(writer, ISC_XMLCHAR "sockets");
	sock = ISC_LIST_HEAD(mgr->socklist);
	while (sock != NULL) {
		LOCK(&sock->lock);
		xmlTextWriterStartElement(writer, ISC_XMLCHAR "socket");

		xmlTextWriterStartElement(writer, ISC_XMLCHAR "id");
		xmlTextWriterWriteFormatString(writer, "%p", sock);
		xmlTextWriterEndElement(writer);

		if (sock->name[0] != 0) {
			xmlTextWriterStartElement(writer, ISC_XMLCHAR "name");
			xmlTextWriterWriteFormatString(writer, "%s",
						       sock->name);
			xmlTextWriterEndElement(writer); /* name */
		}

		xmlTextWriterStartElement(writer, ISC_XMLCHAR "references");
		xmlTextWriterWriteFormatString(writer, "%d", sock->references);
		xmlTextWriterEndElement(writer);

		xmlTextWriterWriteElement(writer, ISC_XMLCHAR "type",
					  ISC_XMLCHAR _socktype(sock->type));

		if (sock->connected) {
			isc_sockaddr_format(&sock->peer_address, peerbuf,
					    sizeof(peerbuf));
			xmlTextWriterWriteElement(writer,
						  ISC_XMLCHAR "peer-address",
						  ISC_XMLCHAR peerbuf);
		}

		len = sizeof(addr);
		if (getsockname(sock->fd, &addr.type.sa, (void *)&len) == 0) {
			isc_sockaddr_format(&addr, peerbuf, sizeof(peerbuf));
			xmlTextWriterWriteElement(writer,
						  ISC_XMLCHAR "local-address",
						  ISC_XMLCHAR peerbuf);
		}

		xmlTextWriterStartElement(writer, ISC_XMLCHAR "states");
		if (sock->pending_recv)
			xmlTextWriterWriteElement(writer, ISC_XMLCHAR "state",
						ISC_XMLCHAR "pending-receive");
		if (sock->pending_send)
			xmlTextWriterWriteElement(writer, ISC_XMLCHAR "state",
						  ISC_XMLCHAR "pending-send");
		if (sock->pending_accept)
			xmlTextWriterWriteElement(writer, ISC_XMLCHAR "state",
						 ISC_XMLCHAR "pending_accept");
		if (sock->listener)
			xmlTextWriterWriteElement(writer, ISC_XMLCHAR "state",
						  ISC_XMLCHAR "listener");
		if (sock->connected)
			xmlTextWriterWriteElement(writer, ISC_XMLCHAR "state",
						  ISC_XMLCHAR "connected");
		if (sock->connecting)
			xmlTextWriterWriteElement(writer, ISC_XMLCHAR "state",
						  ISC_XMLCHAR "connecting");
		if (sock->bound)
			xmlTextWriterWriteElement(writer, ISC_XMLCHAR "state",
						  ISC_XMLCHAR "bound");

		xmlTextWriterEndElement(writer); /* states */

		xmlTextWriterEndElement(writer); /* socket */

		UNLOCK(&sock->lock);
		sock = ISC_LIST_NEXT(sock, link);
	}
	xmlTextWriterEndElement(writer); /* sockets */

	UNLOCK(&mgr->lock);
}
#endif /* HAVE_LIBXML2 */
