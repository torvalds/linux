/*
 * Copyright (C) 2004-2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000-2003  Internet Software Consortium.
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

/* This code uses functions which are only available on Server 2003 and
 * higher, and Windows XP and higher.
 *
 * This code is by nature multithreaded and takes advantage of various
 * features to pass on information through the completion port for
 * when I/O is completed.  All sends, receives, accepts, and connects are
 * completed through the completion port.
 *
 * The number of Completion Port Worker threads used is the total number
 * of CPU's + 1. This increases the likelihood that a Worker Thread is
 * available for processing a completed request.
 *
 * XXXPDM 5 August, 2002
 */

#define MAKE_EXTERNAL 1
#include <config.h>

#include <sys/types.h>

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_   /* Prevent inclusion of winsock.h in windows.h */
#endif

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>

#include <isc/buffer.h>
#include <isc/bufferlist.h>
#include <isc/condition.h>
#include <isc/list.h>
#include <isc/log.h>
#include <isc/mem.h>
#include <isc/msgs.h>
#include <isc/mutex.h>
#include <isc/net.h>
#include <isc/once.h>
#include <isc/os.h>
#include <isc/platform.h>
#include <isc/print.h>
#include <isc/region.h>
#include <isc/socket.h>
#include <isc/stats.h>
#include <isc/strerror.h>
#include <isc/syslog.h>
#include <isc/task.h>
#include <isc/thread.h>
#include <isc/util.h>
#include <isc/win32os.h>

#include <mswsock.h>

#include "errno2result.h"

/*
 * How in the world can Microsoft exist with APIs like this?
 * We can't actually call this directly, because it turns out
 * no library exports this function.  Instead, we need to
 * issue a runtime call to get the address.
 */
LPFN_CONNECTEX ISCConnectEx;
LPFN_ACCEPTEX ISCAcceptEx;
LPFN_GETACCEPTEXSOCKADDRS ISCGetAcceptExSockaddrs;

/*
 * Run expensive internal consistency checks.
 */
#ifdef ISC_SOCKET_CONSISTENCY_CHECKS
#define CONSISTENT(sock) consistent(sock)
#else
#define CONSISTENT(sock) do {} while (0)
#endif
static void consistent(isc_socket_t *sock);

/*
 * Define this macro to control the behavior of connection
 * resets on UDP sockets.  See Microsoft KnowledgeBase Article Q263823
 * for details.
 * NOTE: This requires that Windows 2000 systems install Service Pack 2
 * or later.
 */
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
#endif

/*
 * Some systems define the socket length argument as an int, some as size_t,
 * some as socklen_t.  This is here so it can be easily changed if needed.
 */
#ifndef ISC_SOCKADDR_LEN_T
#define ISC_SOCKADDR_LEN_T unsigned int
#endif

/*
 * Define what the possible "soft" errors can be.  These are non-fatal returns
 * of various network related functions, like recv() and so on.
 */
#define SOFT_ERROR(e)	((e) == WSAEINTR || \
			 (e) == WSAEWOULDBLOCK || \
			 (e) == EWOULDBLOCK || \
			 (e) == EINTR || \
			 (e) == EAGAIN || \
			 (e) == 0)

/*
 * Pending errors are not really errors and should be
 * kept separate
 */
#define PENDING_ERROR(e) ((e) == WSA_IO_PENDING || (e) == 0)

#define DOIO_SUCCESS	  0       /* i/o ok, event sent */
#define DOIO_SOFT	  1       /* i/o ok, soft error, no event sent */
#define DOIO_HARD	  2       /* i/o error, event sent */
#define DOIO_EOF	  3       /* EOF, no event sent */
#define DOIO_PENDING	  4       /* status when i/o is in process */
#define DOIO_NEEDMORE	  5       /* IO was processed, but we need more due to minimum */

#define DLVL(x) ISC_LOGCATEGORY_GENERAL, ISC_LOGMODULE_SOCKET, ISC_LOG_DEBUG(x)

/*
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

/*
 * Socket State
 */
enum {
  SOCK_INITIALIZED,	/* Socket Initialized */
  SOCK_OPEN,		/* Socket opened but nothing yet to do */
  SOCK_DATA,		/* Socket sending or receiving data */
  SOCK_LISTEN,		/* TCP Socket listening for connects */
  SOCK_ACCEPT,		/* TCP socket is waiting to accept */
  SOCK_CONNECT,		/* TCP Socket connecting */
  SOCK_CLOSED,		/* Socket has been closed */
};

#define SOCKET_MAGIC		ISC_MAGIC('I', 'O', 'i', 'o')
#define VALID_SOCKET(t)		ISC_MAGIC_VALID(t, SOCKET_MAGIC)

/*
 * IPv6 control information.  If the socket is an IPv6 socket we want
 * to collect the destination address and interface so the client can
 * set them on outgoing packets.
 */
#ifdef ISC_PLATFORM_HAVEIPV6
#ifndef USE_CMSG
#define USE_CMSG	1
#endif
#endif

/*
 * We really  don't want to try and use these control messages. Win32
 * doesn't have this mechanism before XP.
 */
#undef USE_CMSG

/*
 * Message header for recvmsg and sendmsg calls.
 * Used value-result for recvmsg, value only for sendmsg.
 */
struct msghdr {
	SOCKADDR_STORAGE to_addr;	/* UDP send/recv address */
	int      to_addr_len;		/* length of the address */
	WSABUF  *msg_iov;		/* scatter/gather array */
	u_int   msg_iovlen;             /* # elements in msg_iov */
	void	*msg_control;           /* ancillary data, see below */
	u_int   msg_controllen;         /* ancillary data buffer len */
	int	msg_totallen;		/* total length of this message */
} msghdr;

/*
 * The size to raise the receive buffer to.
 */
#define RCVBUFSIZE (32*1024)

/*
 * The number of times a send operation is repeated if the result
 * is WSAEINTR.
 */
#define NRETRIES 10

struct isc_socket {
	/* Not locked. */
	unsigned int		magic;
	isc_socketmgr_t	       *manager;
	isc_mutex_t		lock;
	isc_sockettype_t	type;

	/* Pointers to scatter/gather buffers */
	WSABUF			iov[ISC_SOCKET_MAXSCATTERGATHER];

	/* Locked by socket lock. */
	ISC_LINK(isc_socket_t)	link;
	unsigned int		references; /* EXTERNAL references */
	SOCKET			fd;	/* file handle */
	int			pf;	/* protocol family */
	char			name[16];
	void *			tag;

	/*
	 * Each recv() call uses this buffer.  It is a per-socket receive
	 * buffer that allows us to decouple the system recv() from the
	 * recv_list done events.  This means the items on the recv_list
	 * can be removed without having to cancel pending system recv()
	 * calls.  It also allows us to read-ahead in some cases.
	 */
	struct {
		SOCKADDR_STORAGE	from_addr;	   // UDP send/recv address
		int		from_addr_len;	   // length of the address
		char		*base;		   // the base of the buffer
		char		*consume_position; // where to start copying data from next
		unsigned int	len;		   // the actual size of this buffer
		unsigned int	remaining;	   // the number of bytes remaining
	} recvbuf;

	ISC_LIST(isc_socketevent_t)		send_list;
	ISC_LIST(isc_socketevent_t)		recv_list;
	ISC_LIST(isc_socket_newconnev_t)	accept_list;
	isc_socket_connev_t		       *connect_ev;

	isc_sockaddr_t		address;  /* remote address */

	unsigned int		listener : 1,	/* listener socket */
				connected : 1,
				pending_connect : 1, /* connect pending */
				bound : 1,	/* bound to local addr */
				dupped : 1;     /* created by isc_socket_dup() */
	unsigned int		pending_iocp;	/* Should equal the counters below. Debug. */
	unsigned int		pending_recv;  /* Number of outstanding recv() calls. */
	unsigned int		pending_send;  /* Number of outstanding send() calls. */
	unsigned int		pending_accept; /* Number of outstanding accept() calls. */
	unsigned int		state; /* Socket state. Debugging and consistency checking. */
	int			state_lineno;  /* line which last touched state */
};

#define _set_state(sock, _state) do { (sock)->state = (_state); (sock)->state_lineno = __LINE__; } while (0)

/*
 * Buffer structure
 */
typedef struct buflist buflist_t;

struct buflist {
	void			*buf;
	unsigned int		buflen;
	ISC_LINK(buflist_t)	link;
};

/*
 * I/O Completion ports Info structures
 */

static HANDLE hHeapHandle = NULL;
typedef struct IoCompletionInfo {
	OVERLAPPED		overlapped;
	isc_socketevent_t	*dev;  /* send()/recv() done event */
	isc_socket_connev_t	*cdev; /* connect() done event */
	isc_socket_newconnev_t	*adev; /* accept() done event */
	void			*acceptbuffer;
	DWORD			received_bytes;
	int			request_type;
	struct msghdr		messagehdr;
	ISC_LIST(buflist_t)	bufferlist;	/*%< list of buffers */
} IoCompletionInfo;

/*
 * Define a maximum number of I/O Completion Port worker threads
 * to handle the load on the Completion Port. The actual number
 * used is the number of CPU's + 1.
 */
#define MAX_IOCPTHREADS 20

#define SOCKET_MANAGER_MAGIC	ISC_MAGIC('I', 'O', 'm', 'g')
#define VALID_MANAGER(m)	ISC_MAGIC_VALID(m, SOCKET_MANAGER_MAGIC)

struct isc_socketmgr {
	/* Not locked. */
	unsigned int			magic;
	isc_mem_t		       *mctx;
	isc_mutex_t			lock;
	isc_stats_t		       *stats;

	/* Locked by manager lock. */
	ISC_LIST(isc_socket_t)		socklist;
	isc_boolean_t			bShutdown;
	isc_condition_t			shutdown_ok;
	HANDLE				hIoCompletionPort;
	int				maxIOCPThreads;
	HANDLE				hIOCPThreads[MAX_IOCPTHREADS];
	DWORD				dwIOCPThreadIds[MAX_IOCPTHREADS];

	/*
	 * Debugging.
	 * Modified by InterlockedIncrement() and InterlockedDecrement()
	 */
	LONG				totalSockets;
	LONG				iocp_total;
};

enum {
	SOCKET_RECV,
	SOCKET_SEND,
	SOCKET_ACCEPT,
	SOCKET_CONNECT
};

/*
 * send() and recv() iovec counts
 */
#define MAXSCATTERGATHER_SEND	(ISC_SOCKET_MAXSCATTERGATHER)
#define MAXSCATTERGATHER_RECV	(ISC_SOCKET_MAXSCATTERGATHER)

static isc_result_t socket_create(isc_socketmgr_t *manager0, int pf,
				  isc_sockettype_t type,
				  isc_socket_t **socketp,
				  isc_socket_t *dup_socket);
static isc_threadresult_t WINAPI SocketIoThread(LPVOID ThreadContext);
static void maybe_free_socket(isc_socket_t **, int);
static void free_socket(isc_socket_t **, int);
static isc_boolean_t senddone_is_active(isc_socket_t *sock, isc_socketevent_t *dev);
static isc_boolean_t acceptdone_is_active(isc_socket_t *sock, isc_socket_newconnev_t *dev);
static isc_boolean_t connectdone_is_active(isc_socket_t *sock, isc_socket_connev_t *dev);
static void send_recvdone_event(isc_socket_t *sock, isc_socketevent_t **dev);
static void send_senddone_event(isc_socket_t *sock, isc_socketevent_t **dev);
static void send_acceptdone_event(isc_socket_t *sock, isc_socket_newconnev_t **adev);
static void send_connectdone_event(isc_socket_t *sock, isc_socket_connev_t **cdev);
static void send_recvdone_abort(isc_socket_t *sock, isc_result_t result);
static void queue_receive_event(isc_socket_t *sock, isc_task_t *task, isc_socketevent_t *dev);
static void queue_receive_request(isc_socket_t *sock);

/*
 * This is used to dump the contents of the sock structure
 * You should make sure that the sock is locked before
 * dumping it. Since the code uses simple printf() statements
 * it should only be used interactively.
 */
void
sock_dump(isc_socket_t *sock) {
	isc_socketevent_t *ldev;
	isc_socket_newconnev_t *ndev;

#if 0
	isc_sockaddr_t addr;
	char socktext[256];

	isc_socket_getpeername(sock, &addr);
	isc_sockaddr_format(&addr, socktext, sizeof(socktext));
	printf("Remote Socket: %s\n", socktext);
	isc_socket_getsockname(sock, &addr);
	isc_sockaddr_format(&addr, socktext, sizeof(socktext));
	printf("This Socket: %s\n", socktext);
#endif

	printf("\n\t\tSock Dump\n");
	printf("\t\tfd: %u\n", sock->fd);
	printf("\t\treferences: %d\n", sock->references);
	printf("\t\tpending_accept: %d\n", sock->pending_accept);
	printf("\t\tconnecting: %d\n", sock->pending_connect);
	printf("\t\tconnected: %d\n", sock->connected);
	printf("\t\tbound: %d\n", sock->bound);
	printf("\t\tpending_iocp: %d\n", sock->pending_iocp);
	printf("\t\tsocket type: %d\n", sock->type);

	printf("\n\t\tSock Recv List\n");
	ldev = ISC_LIST_HEAD(sock->recv_list);
	while (ldev != NULL) {
		printf("\t\tdev: %p\n", ldev);
		ldev = ISC_LIST_NEXT(ldev, ev_link);
	}

	printf("\n\t\tSock Send List\n");
	ldev = ISC_LIST_HEAD(sock->send_list);
	while (ldev != NULL) {
		printf("\t\tdev: %p\n", ldev);
		ldev = ISC_LIST_NEXT(ldev, ev_link);
	}

	printf("\n\t\tSock Accept List\n");
	ndev = ISC_LIST_HEAD(sock->accept_list);
	while (ndev != NULL) {
		printf("\t\tdev: %p\n", ldev);
		ndev = ISC_LIST_NEXT(ndev, ev_link);
	}
}

static void
socket_log(int lineno, isc_socket_t *sock, isc_sockaddr_t *address,
	   isc_logcategory_t *category, isc_logmodule_t *module, int level,
	   isc_msgcat_t *msgcat, int msgset, int message,
	   const char *fmt, ...) ISC_FORMAT_PRINTF(9, 10);

/*  This function will add an entry to the I/O completion port
 *  that will signal the I/O thread to exit (gracefully)
 */
static void
signal_iocompletionport_exit(isc_socketmgr_t *manager) {
	int i;
	int errval;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_MANAGER(manager));
	for (i = 0; i < manager->maxIOCPThreads; i++) {
		if (!PostQueuedCompletionStatus(manager->hIoCompletionPort,
						0, 0, 0)) {
			errval = GetLastError();
			isc__strerror(errval, strbuf, sizeof(strbuf));
			FATAL_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
				ISC_MSG_FAILED,
				"Can't request service thread to exit: %s"),
				strbuf);
		}
	}
}

/*
 * Create the worker threads for the I/O Completion Port
 */
void
iocompletionport_createthreads(int total_threads, isc_socketmgr_t *manager) {
	int errval;
	char strbuf[ISC_STRERRORSIZE];
	int i;

	INSIST(total_threads > 0);
	REQUIRE(VALID_MANAGER(manager));
	/*
	 * We need at least one
	 */
	for (i = 0; i < total_threads; i++) {
		manager->hIOCPThreads[i] = CreateThread(NULL, 0, SocketIoThread,
						manager, 0,
						&manager->dwIOCPThreadIds[i]);
		if (manager->hIOCPThreads[i] == NULL) {
			errval = GetLastError();
			isc__strerror(errval, strbuf, sizeof(strbuf));
			FATAL_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
				ISC_MSG_FAILED,
				"Can't create IOCP thread: %s"),
				strbuf);
			exit(1);
		}
	}
}

/*
 *  Create/initialise the I/O completion port
 */
void
iocompletionport_init(isc_socketmgr_t *manager) {
	int errval;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_MANAGER(manager));
	/*
	 * Create a private heap to handle the socket overlapped structure
	 * The minimum number of structures is 10, there is no maximum
	 */
	hHeapHandle = HeapCreate(0, 10 * sizeof(IoCompletionInfo), 0);
	if (hHeapHandle == NULL) {
		errval = GetLastError();
		isc__strerror(errval, strbuf, sizeof(strbuf));
		FATAL_ERROR(__FILE__, __LINE__,
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_FAILED,
					   "HeapCreate() failed during "
					   "initialization: %s"),
			    strbuf);
		exit(1);
	}

	manager->maxIOCPThreads = min(isc_os_ncpus() + 1, MAX_IOCPTHREADS);

	/* Now Create the Completion Port */
	manager->hIoCompletionPort = CreateIoCompletionPort(
			INVALID_HANDLE_VALUE, NULL,
			0, manager->maxIOCPThreads);
	if (manager->hIoCompletionPort == NULL) {
		errval = GetLastError();
		isc__strerror(errval, strbuf, sizeof(strbuf));
		FATAL_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
				ISC_MSG_FAILED,
				"CreateIoCompletionPort() failed "
				"during initialization: %s"),
				strbuf);
		exit(1);
	}

	/*
	 * Worker threads for servicing the I/O
	 */
	iocompletionport_createthreads(manager->maxIOCPThreads, manager);
}

/*
 * Associate a socket with an IO Completion Port.  This allows us to queue events for it
 * and have our worker pool of threads process them.
 */
void
iocompletionport_update(isc_socket_t *sock) {
	HANDLE hiocp;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_SOCKET(sock));

	hiocp = CreateIoCompletionPort((HANDLE)sock->fd,
		sock->manager->hIoCompletionPort, (ULONG_PTR)sock, 0);

	if (hiocp == NULL) {
		DWORD errval = GetLastError();
		isc__strerror(errval, strbuf, sizeof(strbuf));
		isc_log_iwrite(isc_lctx,
				ISC_LOGCATEGORY_GENERAL,
				ISC_LOGMODULE_SOCKET, ISC_LOG_ERROR,
				isc_msgcat, ISC_MSGSET_SOCKET,
				ISC_MSG_TOOMANYHANDLES,
				"iocompletionport_update: failed to open"
				" io completion port: %s",
				strbuf);

		/* XXXMLG temporary hack to make failures detected.
		 * This function should return errors to the caller, not
		 * exit here.
		 */
		FATAL_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
				ISC_MSG_FAILED,
				"CreateIoCompletionPort() failed "
				"during initialization: %s"),
				strbuf);
		exit(1);
	}

	InterlockedIncrement(&sock->manager->iocp_total);
}

/*
 * Routine to cleanup and then close the socket.
 * Only close the socket here if it is NOT associated
 * with an event, otherwise the WSAWaitForMultipleEvents
 * may fail due to the fact that the Wait should not
 * be running while closing an event or a socket.
 * The socket is locked before calling this function
 */
void
socket_close(isc_socket_t *sock) {

	REQUIRE(sock != NULL);

	if (sock->fd != INVALID_SOCKET) {
		closesocket(sock->fd);
		sock->fd = INVALID_SOCKET;
		_set_state(sock, SOCK_CLOSED);
		InterlockedDecrement(&sock->manager->totalSockets);
	}
}

static isc_once_t initialise_once = ISC_ONCE_INIT;
static isc_boolean_t initialised = ISC_FALSE;

static void
initialise(void) {
	WORD wVersionRequested;
	WSADATA wsaData;
	int err;
	SOCKET sock;
	GUID GUIDConnectEx = WSAID_CONNECTEX;
	GUID GUIDAcceptEx = WSAID_ACCEPTEX;
	GUID GUIDGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD dwBytes;

	/* Need Winsock 2.2 or better */
	wVersionRequested = MAKEWORD(2, 2);

	err = WSAStartup(wVersionRequested, &wsaData);
	if (err != 0) {
		char strbuf[ISC_STRERRORSIZE];
		isc__strerror(err, strbuf, sizeof(strbuf));
		FATAL_ERROR(__FILE__, __LINE__, "WSAStartup() %s: %s",
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
					   ISC_MSG_FAILED, "failed"),
			    strbuf);
		exit(1);
	}
	/*
	 * The following APIs do not exist as functions in a library, but we must
	 * ask winsock for them.  They are "extensions" -- but why they cannot be
	 * actual functions is beyond me.  So, ask winsock for the pointers to the
	 * functions we need.
	 */
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	INSIST(sock != INVALID_SOCKET);
	err = WSAIoctl(sock,  SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &GUIDConnectEx, sizeof(GUIDConnectEx),
		 &ISCConnectEx, sizeof(ISCConnectEx),
		 &dwBytes, NULL, NULL);
	INSIST(err == 0);

	err = WSAIoctl(sock,  SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &GUIDAcceptEx, sizeof(GUIDAcceptEx),
		 &ISCAcceptEx, sizeof(ISCAcceptEx),
		 &dwBytes, NULL, NULL);
	INSIST(err == 0);

	err = WSAIoctl(sock,  SIO_GET_EXTENSION_FUNCTION_POINTER,
		 &GUIDGetAcceptExSockaddrs, sizeof(GUIDGetAcceptExSockaddrs),
		 &ISCGetAcceptExSockaddrs, sizeof(ISCGetAcceptExSockaddrs),
		 &dwBytes, NULL, NULL);
	INSIST(err == 0);

	closesocket(sock);

	initialised = ISC_TRUE;
}

/*
 * Initialize socket services
 */
void
InitSockets(void) {
	RUNTIME_CHECK(isc_once_do(&initialise_once,
				  initialise) == ISC_R_SUCCESS);
	if (!initialised)
		exit(1);
}

int
internal_sendmsg(isc_socket_t *sock, IoCompletionInfo *lpo,
		 struct msghdr *messagehdr, int flags, int *Error)
{
	int Result;
	DWORD BytesSent;
	DWORD Flags = flags;
	int total_sent;

	*Error = 0;
	Result = WSASendTo(sock->fd, messagehdr->msg_iov,
			   messagehdr->msg_iovlen, &BytesSent,
			   Flags, (SOCKADDR *)&messagehdr->to_addr,
			   messagehdr->to_addr_len, (LPWSAOVERLAPPED)lpo,
			   NULL);

	total_sent = (int)BytesSent;

	/* Check for errors.*/
	if (Result == SOCKET_ERROR) {
		*Error = WSAGetLastError();

		switch (*Error) {
		case WSA_IO_INCOMPLETE:
		case WSA_WAIT_IO_COMPLETION:
		case WSA_IO_PENDING:
		case NO_ERROR:		/* Strange, but okay */
			sock->pending_iocp++;
			sock->pending_send++;
			break;

		default:
			return (-1);
			break;
		}
	} else {
		sock->pending_iocp++;
		sock->pending_send++;
	}

	if (lpo != NULL)
		return (0);
	else
		return (total_sent);
}

static void
queue_receive_request(isc_socket_t *sock) {
	DWORD Flags = 0;
	DWORD NumBytes = 0;
	int total_bytes = 0;
	int Result;
	int Error;
	int need_retry;
	WSABUF iov[1];
	IoCompletionInfo *lpo = NULL;
	isc_result_t isc_result;

 retry:
	need_retry = ISC_FALSE;

	/*
	 * If we already have a receive pending, do nothing.
	 */
	if (sock->pending_recv > 0) {
		if (lpo != NULL)
			HeapFree(hHeapHandle, 0, lpo);
		return;
	}

	/*
	 * If no one is waiting, do nothing.
	 */
	if (ISC_LIST_EMPTY(sock->recv_list)) {
		if (lpo != NULL)
			HeapFree(hHeapHandle, 0, lpo);
		return;
	}

	INSIST(sock->recvbuf.remaining == 0);
	INSIST(sock->fd != INVALID_SOCKET);

	iov[0].len = sock->recvbuf.len;
	iov[0].buf = sock->recvbuf.base;

	if (lpo == NULL) {
		lpo = (IoCompletionInfo *)HeapAlloc(hHeapHandle,
						    HEAP_ZERO_MEMORY,
						    sizeof(IoCompletionInfo));
		RUNTIME_CHECK(lpo != NULL);
	} else
		ZeroMemory(lpo, sizeof(IoCompletionInfo));
	lpo->request_type = SOCKET_RECV;

	sock->recvbuf.from_addr_len = sizeof(sock->recvbuf.from_addr);

	Error = 0;
	Result = WSARecvFrom((SOCKET)sock->fd, iov, 1,
			     &NumBytes, &Flags,
			     (SOCKADDR *)&sock->recvbuf.from_addr,
			     &sock->recvbuf.from_addr_len,
			     (LPWSAOVERLAPPED)lpo, NULL);

	/* Check for errors. */
	if (Result == SOCKET_ERROR) {
		Error = WSAGetLastError();

		switch (Error) {
		case WSA_IO_PENDING:
			sock->pending_iocp++;
			sock->pending_recv++;
			break;

		/* direct error: no completion event */
		case ERROR_HOST_UNREACHABLE:
		case WSAENETRESET:
		case WSAECONNRESET:
			if (!sock->connected) {
				/* soft error */
				need_retry = ISC_TRUE;
				break;
			}
			/* FALLTHROUGH */

		default:
			isc_result = isc__errno2result(Error);
			if (isc_result == ISC_R_UNEXPECTED)
				UNEXPECTED_ERROR(__FILE__, __LINE__,
					"WSARecvFrom: Windows error code: %d, isc result %d",
					Error, isc_result);
			send_recvdone_abort(sock, isc_result);
			HeapFree(hHeapHandle, 0, lpo);
			lpo = NULL;
			break;
		}
	} else {
		/*
		 * The recv() finished immediately, but we will still get
		 * a completion event.  Rather than duplicate code, let
		 * that thread handle sending the data along its way.
		 */
		sock->pending_iocp++;
		sock->pending_recv++;
	}

	socket_log(__LINE__, sock, NULL, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET,
		   ISC_MSG_DOIORECV,
		   "queue_io_request: fd %d result %d error %d",
		   sock->fd, Result, Error);

	CONSISTENT(sock);

	if (need_retry)
		goto retry;
}

static void
manager_log(isc_socketmgr_t *sockmgr, isc_logcategory_t *category,
	    isc_logmodule_t *module, int level, const char *fmt, ...)
{
	char msgbuf[2048];
	va_list ap;

	if (!isc_log_wouldlog(isc_lctx, level))
		return;

	va_start(ap, fmt);
	vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
	va_end(ap);

	isc_log_write(isc_lctx, category, module, level,
		      "sockmgr %p: %s", sockmgr, msgbuf);
}

static void
socket_log(int lineno, isc_socket_t *sock, isc_sockaddr_t *address,
	   isc_logcategory_t *category, isc_logmodule_t *module, int level,
	   isc_msgcat_t *msgcat, int msgset, int message,
	   const char *fmt, ...)
{
	char msgbuf[2048];
	char peerbuf[256];
	va_list ap;


	if (!isc_log_wouldlog(isc_lctx, level))
		return;

	va_start(ap, fmt);
	vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
	va_end(ap);

	if (address == NULL) {
		isc_log_iwrite(isc_lctx, category, module, level,
			       msgcat, msgset, message,
			       "socket %p line %d: %s", sock, lineno, msgbuf);
	} else {
		isc_sockaddr_format(address, peerbuf, sizeof(peerbuf));
		isc_log_iwrite(isc_lctx, category, module, level,
			       msgcat, msgset, message,
				   "socket %p line %d peer %s: %s", sock, lineno,
				   peerbuf, msgbuf);
	}

}

/*
 * Make an fd SOCKET non-blocking.
 */
static isc_result_t
make_nonblock(SOCKET fd) {
	int ret;
	unsigned long flags = 1;
	char strbuf[ISC_STRERRORSIZE];

	/* Set the socket to non-blocking */
	ret = ioctlsocket(fd, FIONBIO, &flags);

	if (ret == -1) {
		isc__strerror(errno, strbuf, sizeof(strbuf));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "ioctlsocket(%d, FIOBIO, %d): %s",
				 fd, flags, strbuf);

		return (ISC_R_UNEXPECTED);
	}

	return (ISC_R_SUCCESS);
}

/*
 * Windows 2000 systems incorrectly cause UDP sockets using WSARecvFrom
 * to not work correctly, returning a WSACONNRESET error when a WSASendTo
 * fails with an "ICMP port unreachable" response and preventing the
 * socket from using the WSARecvFrom in subsequent operations.
 * The function below fixes this, but requires that Windows 2000
 * Service Pack 2 or later be installed on the system.  NT 4.0
 * systems are not affected by this and work correctly.
 * See Microsoft Knowledge Base Article Q263823 for details of this.
 */
isc_result_t
connection_reset_fix(SOCKET fd) {
	DWORD dwBytesReturned = 0;
	BOOL  bNewBehavior = FALSE;
	DWORD status;

	if (isc_win32os_majorversion() < 5)
		return (ISC_R_SUCCESS); /*  NT 4.0 has no problem */

	/* disable bad behavior using IOCTL: SIO_UDP_CONNRESET */
	status = WSAIoctl(fd, SIO_UDP_CONNRESET, &bNewBehavior,
			  sizeof(bNewBehavior), NULL, 0,
			  &dwBytesReturned, NULL, NULL);
	if (status != SOCKET_ERROR)
		return (ISC_R_SUCCESS);
	else {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "WSAIoctl(SIO_UDP_CONNRESET, oldBehaviour) %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
		return (ISC_R_UNEXPECTED);
	}
}

/*
 * Construct an iov array and attach it to the msghdr passed in.  This is
 * the SEND constructor, which will use the used region of the buffer
 * (if using a buffer list) or will use the internal region (if a single
 * buffer I/O is requested).
 *
 * Nothing can be NULL, and the done event must list at least one buffer
 * on the buffer linked list for this function to be meaningful.
 */
static void
build_msghdr_send(isc_socket_t *sock, isc_socketevent_t *dev,
		  struct msghdr *msg, char *cmsg, WSABUF *iov,
		  IoCompletionInfo  *lpo)
{
	unsigned int iovcount;
	isc_buffer_t *buffer;
	buflist_t  *cpbuffer;
	isc_region_t used;
	size_t write_count;
	size_t skip_count;

	memset(msg, 0, sizeof(*msg));

	memcpy(&msg->to_addr, &dev->address.type, dev->address.length);
	msg->to_addr_len = dev->address.length;

	buffer = ISC_LIST_HEAD(dev->bufferlist);
	write_count = 0;
	iovcount = 0;

	/*
	 * Single buffer I/O?  Skip what we've done so far in this region.
	 */
	if (buffer == NULL) {
		write_count = dev->region.length - dev->n;
		cpbuffer = HeapAlloc(hHeapHandle, HEAP_ZERO_MEMORY, sizeof(buflist_t));
		RUNTIME_CHECK(cpbuffer != NULL);
		cpbuffer->buf = HeapAlloc(hHeapHandle, HEAP_ZERO_MEMORY, write_count);
		RUNTIME_CHECK(cpbuffer->buf != NULL);

		socket_log(__LINE__, sock, NULL, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTLOCK,
		   "alloc_buffer %p %d %p %d", cpbuffer, sizeof(buflist_t),
		   cpbuffer->buf, write_count);

		memcpy(cpbuffer->buf,(dev->region.base + dev->n), write_count);
		cpbuffer->buflen = write_count;
		ISC_LIST_ENQUEUE(lpo->bufferlist, cpbuffer, link);
		iov[0].buf = cpbuffer->buf;
		iov[0].len = write_count;
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
			int uselen = used.length - skip_count;
			cpbuffer = HeapAlloc(hHeapHandle, HEAP_ZERO_MEMORY, sizeof(buflist_t));
			RUNTIME_CHECK(cpbuffer != NULL);
			cpbuffer->buf = HeapAlloc(hHeapHandle, HEAP_ZERO_MEMORY, uselen);
			RUNTIME_CHECK(cpbuffer->buf != NULL);

			socket_log(__LINE__, sock, NULL, TRACE,
			   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTLOCK,
			   "alloc_buffer %p %d %p %d", cpbuffer, sizeof(buflist_t),
			   cpbuffer->buf, write_count);

			memcpy(cpbuffer->buf,(used.base + skip_count), uselen);
			cpbuffer->buflen = uselen;
			iov[iovcount].buf = cpbuffer->buf;
			iov[iovcount].len = used.length - skip_count;
			write_count += uselen;
			skip_count = 0;
			iovcount++;
		}
		buffer = ISC_LIST_NEXT(buffer, link);
	}

	INSIST(skip_count == 0);

 config:
	msg->msg_iov = iov;
	msg->msg_iovlen = iovcount;
	msg->msg_totallen = write_count;
}

static void
set_dev_address(isc_sockaddr_t *address, isc_socket_t *sock,
		isc_socketevent_t *dev)
{
	if (sock->type == isc_sockettype_udp) {
		if (address != NULL)
			dev->address = *address;
		else
			dev->address = sock->address;
	} else if (sock->type == isc_sockettype_tcp) {
		INSIST(address == NULL);
		dev->address = sock->address;
	}
}

static void
destroy_socketevent(isc_event_t *event) {
	isc_socketevent_t *ev = (isc_socketevent_t *)event;

	INSIST(ISC_LIST_EMPTY(ev->bufferlist));

	(ev->destroy)(event);
}

static isc_socketevent_t *
allocate_socketevent(isc_socket_t *sock, isc_eventtype_t eventtype,
		     isc_taskaction_t action, const void *arg)
{
	isc_socketevent_t *ev;

	ev = (isc_socketevent_t *)isc_event_allocate(sock->manager->mctx,
						     sock, eventtype,
						     action, arg,
						     sizeof(*ev));
	if (ev == NULL)
		return (NULL);

	ev->result = ISC_R_IOERROR; // XXXMLG temporary change to detect failure to set
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
dump_msg(struct msghdr *msg, isc_socket_t *sock) {
	unsigned int i;

	printf("MSGHDR %p, Socket #: %u\n", msg, sock->fd);
	printf("\tname %p, namelen %d\n", msg->msg_name, msg->msg_namelen);
	printf("\tiov %p, iovlen %d\n", msg->msg_iov, msg->msg_iovlen);
	for (i = 0; i < (unsigned int)msg->msg_iovlen; i++)
		printf("\t\t%d\tbase %p, len %d\n", i,
		       msg->msg_iov[i].buf,
		       msg->msg_iov[i].len);
}
#endif

/*
 * map the error code
 */
int
map_socket_error(isc_socket_t *sock, int windows_errno, int *isc_errno,
		 char *errorstring, size_t bufsize) {

	int doreturn;
	switch (windows_errno) {
	case WSAECONNREFUSED:
		*isc_errno = ISC_R_CONNREFUSED;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case WSAENETUNREACH:
	case ERROR_NETWORK_UNREACHABLE:
		*isc_errno = ISC_R_NETUNREACH;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case ERROR_PORT_UNREACHABLE:
	case ERROR_HOST_UNREACHABLE:
	case WSAEHOSTUNREACH:
		*isc_errno = ISC_R_HOSTUNREACH;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case WSAENETDOWN:
		*isc_errno = ISC_R_NETDOWN;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case WSAEHOSTDOWN:
		*isc_errno = ISC_R_HOSTDOWN;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case WSAEACCES:
		*isc_errno = ISC_R_NOPERM;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case WSAECONNRESET:
	case WSAENETRESET:
	case WSAECONNABORTED:
	case WSAEDISCON:
		*isc_errno = ISC_R_CONNECTIONRESET;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case WSAENOTCONN:
		*isc_errno = ISC_R_NOTCONNECTED;
		if (sock->connected)
			doreturn = DOIO_HARD;
		else
			doreturn = DOIO_SOFT;
		break;
	case ERROR_OPERATION_ABORTED:
	case ERROR_CONNECTION_ABORTED:
	case ERROR_REQUEST_ABORTED:
		*isc_errno = ISC_R_CONNECTIONRESET;
		doreturn = DOIO_HARD;
		break;
	case WSAENOBUFS:
		*isc_errno = ISC_R_NORESOURCES;
		doreturn = DOIO_HARD;
		break;
	case WSAEAFNOSUPPORT:
		*isc_errno = ISC_R_FAMILYNOSUPPORT;
		doreturn = DOIO_HARD;
		break;
	case WSAEADDRNOTAVAIL:
		*isc_errno = ISC_R_ADDRNOTAVAIL;
		doreturn = DOIO_HARD;
		break;
	case WSAEDESTADDRREQ:
		*isc_errno = ISC_R_BADADDRESSFORM;
		doreturn = DOIO_HARD;
		break;
	case ERROR_NETNAME_DELETED:
		*isc_errno = ISC_R_NETDOWN;
		doreturn = DOIO_HARD;
		break;
	default:
		*isc_errno = ISC_R_IOERROR;
		doreturn = DOIO_HARD;
		break;
	}
	if (doreturn == DOIO_HARD) {
		isc__strerror(windows_errno, errorstring, bufsize);
	}
	return (doreturn);
}

static void
fill_recv(isc_socket_t *sock, isc_socketevent_t *dev) {
	isc_region_t r;
	int copylen;
	isc_buffer_t *buffer;

	INSIST(dev->n < dev->minimum);
	INSIST(sock->recvbuf.remaining > 0);
	INSIST(sock->pending_recv == 0);

	if (sock->type == isc_sockettype_udp) {
		dev->address.length = sock->recvbuf.from_addr_len;
		memcpy(&dev->address.type, &sock->recvbuf.from_addr,
		    sock->recvbuf.from_addr_len);
		if (isc_sockaddr_getport(&dev->address) == 0) {
			if (isc_log_wouldlog(isc_lctx, IOEVENT_LEVEL)) {
				socket_log(__LINE__, sock, &dev->address, IOEVENT,
					   isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_ZEROPORT,
					   "dropping source port zero packet");
			}
			sock->recvbuf.remaining = 0;
			return;
		}
	} else if (sock->type == isc_sockettype_tcp) {
		dev->address = sock->address;
	}

	/*
	 * Run through the list of buffers we were given, and find the
	 * first one with space.  Once it is found, loop through, filling
	 * the buffers as much as possible.
	 */
	buffer = ISC_LIST_HEAD(dev->bufferlist);
	if (buffer != NULL) { // Multi-buffer receive
		while (buffer != NULL && sock->recvbuf.remaining > 0) {
			REQUIRE(ISC_BUFFER_VALID(buffer));
			if (isc_buffer_availablelength(buffer) > 0) {
				isc_buffer_availableregion(buffer, &r);
				copylen = min(r.length, sock->recvbuf.remaining);
				memcpy(r.base, sock->recvbuf.consume_position, copylen);
				sock->recvbuf.consume_position += copylen;
				sock->recvbuf.remaining -= copylen;
				isc_buffer_add(buffer, copylen);
				dev->n += copylen;
			}
			buffer = ISC_LIST_NEXT(buffer, link);
		}
	} else { // Single-buffer receive
		copylen = min(dev->region.length - dev->n, sock->recvbuf.remaining);
		memcpy(dev->region.base + dev->n, sock->recvbuf.consume_position, copylen);
		sock->recvbuf.consume_position += copylen;
		sock->recvbuf.remaining -= copylen;
		dev->n += copylen;
	}

	/*
	 * UDP receives are all-consuming.  That is, if we have 4k worth of
	 * data in our receive buffer, and the caller only gave us
	 * 1k of space, we will toss the remaining 3k of data.  TCP
	 * will keep the extra data around and use it for later requests.
	 */
	if (sock->type == isc_sockettype_udp)
		sock->recvbuf.remaining = 0;
}

/*
 * Copy out as much data from the internal buffer to done events.
 * As each done event is filled, send it along its way.
 */
static void
completeio_recv(isc_socket_t *sock)
{
	isc_socketevent_t *dev;

	/*
	 * If we are in the process of filling our buffer, we cannot
	 * touch it yet, so don't.
	 */
	if (sock->pending_recv > 0)
		return;

	while (sock->recvbuf.remaining > 0 && !ISC_LIST_EMPTY(sock->recv_list)) {
		dev = ISC_LIST_HEAD(sock->recv_list);

		/*
		 * See if we have sufficient data in our receive buffer
		 * to handle this.  If we do, copy out the data.
		 */
		fill_recv(sock, dev);

		/*
		 * Did we satisfy it?
		 */
		if (dev->n >= dev->minimum) {
			dev->result = ISC_R_SUCCESS;
			send_recvdone_event(sock, &dev);
		}
	}
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
completeio_send(isc_socket_t *sock, isc_socketevent_t *dev,
		struct msghdr *messagehdr, int cc, int send_errno)
{
	char addrbuf[ISC_SOCKADDR_FORMATSIZE];
	char strbuf[ISC_STRERRORSIZE];

	if (send_errno != 0) {
		if (SOFT_ERROR(send_errno))
			return (DOIO_SOFT);

		return (map_socket_error(sock, send_errno, &dev->result,
			strbuf, sizeof(strbuf)));

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
		UNEXPECTED_ERROR(__FILE__, __LINE__, "completeio_send: %s: %s",
				 addrbuf, strbuf);
		dev->result = isc__errno2result(send_errno);
		return (DOIO_HARD);
	}

	/*
	 * If we write less than we expected, update counters, poke.
	 */
	dev->n += cc;
	if (cc != messagehdr->msg_totallen)
		return (DOIO_SOFT);

	/*
	 * Exactly what we wanted to write.  We're done with this
	 * entry.  Post its completion event.
	 */
	dev->result = ISC_R_SUCCESS;
	return (DOIO_SUCCESS);
}

static int
startio_send(isc_socket_t *sock, isc_socketevent_t *dev, int *nbytes,
	     int *send_errno)
{
	char *cmsg = NULL;
	char strbuf[ISC_STRERRORSIZE];
	IoCompletionInfo *lpo;
	int status;
	struct msghdr *msghdr;

	lpo = (IoCompletionInfo *)HeapAlloc(hHeapHandle,
					    HEAP_ZERO_MEMORY,
					    sizeof(IoCompletionInfo));
	RUNTIME_CHECK(lpo != NULL);
	lpo->request_type = SOCKET_SEND;
	lpo->dev = dev;
	msghdr = &lpo->messagehdr;
	memset(msghdr, 0, sizeof(struct msghdr));
	ISC_LIST_INIT(lpo->bufferlist);

	build_msghdr_send(sock, dev, msghdr, cmsg, sock->iov, lpo);

	*nbytes = internal_sendmsg(sock, lpo, msghdr, 0, send_errno);

	if (*nbytes < 0) {
		/*
		 * I/O has been initiated
		 * completion will be through the completion port
		 */
		if (PENDING_ERROR(*send_errno)) {
			status = DOIO_PENDING;
			goto done;
		}

		if (SOFT_ERROR(*send_errno)) {
			status = DOIO_SOFT;
			goto done;
		}

		/*
		 * If we got this far then something is wrong
		 */
		if (isc_log_wouldlog(isc_lctx, IOEVENT_LEVEL)) {
			isc__strerror(*send_errno, strbuf, sizeof(strbuf));
			socket_log(__LINE__, sock, NULL, IOEVENT,
				   isc_msgcat, ISC_MSGSET_SOCKET,
				   ISC_MSG_INTERNALSEND,
				   "startio_send: internal_sendmsg(%d) %d "
				   "bytes, err %d/%s",
				   sock->fd, *nbytes, *send_errno, strbuf);
		}
		status = DOIO_HARD;
		goto done;
	}
	dev->result = ISC_R_SUCCESS;
	status = DOIO_SOFT;
 done:
	_set_state(sock, SOCK_DATA);
	return (status);
}

static isc_result_t
allocate_socket(isc_socketmgr_t *manager, isc_sockettype_t type,
		isc_socket_t **socketp) {
	isc_socket_t *sock;
	isc_result_t result;

	sock = isc_mem_get(manager->mctx, sizeof(*sock));

	if (sock == NULL)
		return (ISC_R_NOMEMORY);

	sock->magic = 0;
	sock->references = 0;

	sock->manager = manager;
	sock->type = type;
	sock->fd = INVALID_SOCKET;

	ISC_LINK_INIT(sock, link);

	/*
	 * set up list of readers and writers to be initially empty
	 */
	ISC_LIST_INIT(sock->recv_list);
	ISC_LIST_INIT(sock->send_list);
	ISC_LIST_INIT(sock->accept_list);
	sock->connect_ev = NULL;
	sock->pending_accept = 0;
	sock->pending_recv = 0;
	sock->pending_send = 0;
	sock->pending_iocp = 0;
	sock->listener = 0;
	sock->connected = 0;
	sock->pending_connect = 0;
	sock->bound = 0;
	sock->dupped = 0;
	memset(sock->name, 0, sizeof(sock->name));	// zero the name field
	_set_state(sock, SOCK_INITIALIZED);

	sock->recvbuf.len = 65536;
	sock->recvbuf.consume_position = sock->recvbuf.base;
	sock->recvbuf.remaining = 0;
	sock->recvbuf.base = isc_mem_get(manager->mctx, sock->recvbuf.len); // max buffer size
	if (sock->recvbuf.base == NULL) {
		sock->magic = 0;
		goto error;
	}

	/*
	 * initialize the lock
	 */
	result = isc_mutex_init(&sock->lock);
	if (result != ISC_R_SUCCESS) {
		sock->magic = 0;
		isc_mem_put(manager->mctx, sock->recvbuf.base, sock->recvbuf.len);
		sock->recvbuf.base = NULL;
		goto error;
	}

	socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
		   "allocated");

	sock->magic = SOCKET_MAGIC;
	*socketp = sock;

	return (ISC_R_SUCCESS);

 error:
	isc_mem_put(manager->mctx, sock, sizeof(*sock));

	return (result);
}

/*
 * Verify that the socket state is consistent.
 */
static void
consistent(isc_socket_t *sock) {

	isc_socketevent_t *dev;
	isc_socket_newconnev_t *nev;
	unsigned int count;
	char *crash_reason;
	isc_boolean_t crash = ISC_FALSE;

	REQUIRE(sock->pending_iocp == sock->pending_recv + sock->pending_send
		+ sock->pending_accept + sock->pending_connect);

	dev = ISC_LIST_HEAD(sock->send_list);
	count = 0;
	while (dev != NULL) {
		count++;
		dev = ISC_LIST_NEXT(dev, ev_link);
	}
	if (count > sock->pending_send) {
		crash = ISC_TRUE;
		crash_reason = "send_list > sock->pending_send";
	}

	nev = ISC_LIST_HEAD(sock->accept_list);
	count = 0;
	while (nev != NULL) {
		count++;
		nev = ISC_LIST_NEXT(nev, ev_link);
	}
	if (count > sock->pending_accept) {
		crash = ISC_TRUE;
		crash_reason = "send_list > sock->pending_send";
	}

	if (crash) {
		socket_log(__LINE__, sock, NULL, CREATION, isc_msgcat, ISC_MSGSET_SOCKET,
			   ISC_MSG_DESTROYING, "SOCKET INCONSISTENT: %s",
			   crash_reason);
		sock_dump(sock);
		INSIST(crash == ISC_FALSE);
	}
}

/*
 * Maybe free the socket.
 *
 * This function will verify tht the socket is no longer in use in any way,
 * either internally or externally.  This is the only place where this
 * check is to be made; if some bit of code believes that IT is done with
 * the socket (e.g., some reference counter reaches zero), it should call
 * this function.
 *
 * When calling this function, the socket must be locked, and the manager
 * must be unlocked.
 *
 * When this function returns, *socketp will be NULL.  No tricks to try
 * to hold on to this pointer are allowed.
 */
static void
maybe_free_socket(isc_socket_t **socketp, int lineno) {
	isc_socket_t *sock = *socketp;
	*socketp = NULL;

	INSIST(VALID_SOCKET(sock));
	CONSISTENT(sock);

	if (sock->pending_iocp > 0
	    || sock->pending_recv > 0
	    || sock->pending_send > 0
	    || sock->pending_accept > 0
	    || sock->references > 0
	    || sock->pending_connect == 1
	    || !ISC_LIST_EMPTY(sock->recv_list)
	    || !ISC_LIST_EMPTY(sock->send_list)
	    || !ISC_LIST_EMPTY(sock->accept_list)
	    || sock->fd != INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return;
	}
	UNLOCK(&sock->lock);

	free_socket(&sock, lineno);
}

void
free_socket(isc_socket_t **sockp, int lineno) {
	isc_socketmgr_t *manager;
	isc_socket_t *sock = *sockp;
	*sockp = NULL;

	manager = sock->manager;

	/*
	 * Seems we can free the socket after all.
	 */
	manager = sock->manager;
	socket_log(__LINE__, sock, NULL, CREATION, isc_msgcat, ISC_MSGSET_SOCKET,
		   ISC_MSG_DESTROYING, "freeing socket line %d fd %d lock %p semaphore %p",
		   lineno, sock->fd, &sock->lock, sock->lock.LockSemaphore);

	sock->magic = 0;
	DESTROYLOCK(&sock->lock);

	if (sock->recvbuf.base != NULL)
		isc_mem_put(manager->mctx, sock->recvbuf.base, sock->recvbuf.len);

	LOCK(&manager->lock);
	if (ISC_LINK_LINKED(sock, link))
		ISC_LIST_UNLINK(manager->socklist, sock, link);
	isc_mem_put(manager->mctx, sock, sizeof(*sock));

	if (ISC_LIST_EMPTY(manager->socklist))
		SIGNAL(&manager->shutdown_ok);
	UNLOCK(&manager->lock);
}

/*
 * Create a new 'type' socket managed by 'manager'.  Events
 * will be posted to 'task' and when dispatched 'action' will be
 * called with 'arg' as the arg value.  The new socket is returned
 * in 'socketp'.
 */
static isc_result_t
socket_create(isc_socketmgr_t *manager, int pf, isc_sockettype_t type,
	      isc_socket_t **socketp, isc_socket_t *dup_socket)
{
	isc_socket_t *sock = NULL;
	isc_result_t result;
#if defined(USE_CMSG)
	int on = 1;
#endif
#if defined(SO_RCVBUF)
	ISC_SOCKADDR_LEN_T optlen;
	int size;
#endif
	int socket_errno;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(socketp != NULL && *socketp == NULL);
	REQUIRE(type != isc_sockettype_fdwatch);

	if (dup_socket != NULL)
		return (ISC_R_NOTIMPLEMENTED);

	result = allocate_socket(manager, type, &sock);
	if (result != ISC_R_SUCCESS)
		return (result);

	sock->pf = pf;
#if 0
	if (dup_socket == NULL) {
#endif
		switch (type) {
		case isc_sockettype_udp:
			sock->fd = socket(pf, SOCK_DGRAM, IPPROTO_UDP);
			if (sock->fd != INVALID_SOCKET) {
				result = connection_reset_fix(sock->fd);
				if (result != ISC_R_SUCCESS) {
					socket_log(__LINE__, sock,
						NULL, EVENT, NULL, 0, 0,
						"closed %d %d %d "
						"con_reset_fix_failed",
						sock->pending_recv,
						sock->pending_send,
						sock->references);
					closesocket(sock->fd);
					_set_state(sock, SOCK_CLOSED);
					sock->fd = INVALID_SOCKET;
					free_socket(&sock, __LINE__);
					return (result);
				}
			}
			break;
		case isc_sockettype_tcp:
			sock->fd = socket(pf, SOCK_STREAM, IPPROTO_TCP);
			break;
		}
#if 0
	} else {
		/*
		 * XXX: dup() is deprecated in windows, use _dup()
		 * instead.  In future we may want to investigate
		 * WSADuplicateSocket().
		 */
		sock->fd = _dup(dup_socket->fd);
		sock->dupped = 1;
		sock->bound = dup_socket->bound;
	}
#endif

	if (sock->fd == INVALID_SOCKET) {
		socket_errno = WSAGetLastError();
		free_socket(&sock, __LINE__);

		switch (socket_errno) {
		case WSAEMFILE:
		case WSAENOBUFS:
			return (ISC_R_NORESOURCES);

		case WSAEPROTONOSUPPORT:
		case WSAEPFNOSUPPORT:
		case WSAEAFNOSUPPORT:
			return (ISC_R_FAMILYNOSUPPORT);

		default:
			isc__strerror(socket_errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "socket() %s: %s",
					 isc_msgcat_get(isc_msgcat,
							ISC_MSGSET_GENERAL,
							ISC_MSG_FAILED,
							"failed"),
					 strbuf);
			return (ISC_R_UNEXPECTED);
		}
	}

	result = make_nonblock(sock->fd);
	if (result != ISC_R_SUCCESS) {
		socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
			"closed %d %d %d make_nonblock_failed",
			sock->pending_recv, sock->pending_send,
			sock->references);
		closesocket(sock->fd);
		sock->fd = INVALID_SOCKET;
		free_socket(&sock, __LINE__);
		return (result);
	}


#if defined(USE_CMSG) || defined(SO_RCVBUF)
	if (type == isc_sockettype_udp) {

#if defined(USE_CMSG)
#if defined(ISC_PLATFORM_HAVEIPV6)
#ifdef IPV6_RECVPKTINFO
		/* 2292bis */
		if ((pf == AF_INET6)
		    && (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_RECVPKTINFO,
				   (char *)&on, sizeof(on)) < 0)) {
			isc__strerror(WSAGetLastError(), strbuf, sizeof(strbuf));
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
		/* 2292 */
		if ((pf == AF_INET6)
		    && (setsockopt(sock->fd, IPPROTO_IPV6, IPV6_PKTINFO,
				   (char *)&on, sizeof(on)) < 0)) {
			isc__strerror(WSAGetLastError(), strbuf, sizeof(strbuf));
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
#ifdef IPV6_USE_MIN_MTU	/*2292bis, not too common yet*/
		/* use minimum MTU */
		if (pf == AF_INET6) {
			(void)setsockopt(sock->fd, IPPROTO_IPV6,
					 IPV6_USE_MIN_MTU,
					 (char *)&on, sizeof(on));
		}
#endif
#endif /* ISC_PLATFORM_HAVEIPV6 */
#endif /* defined(USE_CMSG) */

#if defined(SO_RCVBUF)
	       optlen = sizeof(size);
	       if (getsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
			      (char *)&size, &optlen) >= 0 &&
		    size < RCVBUFSIZE) {
		       size = RCVBUFSIZE;
		       (void)setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
					(char *)&size, sizeof(size));
	       }
#endif

	}
#endif /* defined(USE_CMSG) || defined(SO_RCVBUF) */

	_set_state(sock, SOCK_OPEN);
	sock->references = 1;
	*socketp = sock;

	iocompletionport_update(sock);

	/*
	 * Note we don't have to lock the socket like we normally would because
	 * there are no external references to it yet.
	 */
	LOCK(&manager->lock);
	ISC_LIST_APPEND(manager->socklist, sock, link);
	InterlockedIncrement(&manager->totalSockets);
	UNLOCK(&manager->lock);

	socket_log(__LINE__, sock, NULL, CREATION, isc_msgcat,
		   ISC_MSGSET_SOCKET, ISC_MSG_CREATED,
		   "created %u type %u", sock->fd, type);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc__socket_create(isc_socketmgr_t *manager, int pf, isc_sockettype_t type,
		   isc_socket_t **socketp)
{
	return (socket_create(manager, pf, type, socketp, NULL));
}

isc_result_t
isc__socket_dup(isc_socket_t *sock, isc_socket_t **socketp) {
	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(socketp != NULL && *socketp == NULL);

#if 1
	return (ISC_R_NOTIMPLEMENTED);
#else
	return (socket_create(sock->manager, sock->pf, sock->type,
			      socketp, sock));
#endif
}

isc_result_t
isc_socket_open(isc_socket_t *sock) {
	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(sock->type != isc_sockettype_fdwatch);

	return (ISC_R_NOTIMPLEMENTED);
}

/*
 * Attach to a socket.  Caller must explicitly detach when it is done.
 */
void
isc__socket_attach(isc_socket_t *sock, isc_socket_t **socketp) {
	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(socketp != NULL && *socketp == NULL);

	LOCK(&sock->lock);
	CONSISTENT(sock);
	sock->references++;
	UNLOCK(&sock->lock);

	*socketp = sock;
}

/*
 * Dereference a socket.  If this is the last reference to it, clean things
 * up by destroying the socket.
 */
void
isc__socket_detach(isc_socket_t **socketp) {
	isc_socket_t *sock;
	isc_boolean_t kill_socket = ISC_FALSE;

	REQUIRE(socketp != NULL);
	sock = *socketp;
	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(sock->type != isc_sockettype_fdwatch);

	LOCK(&sock->lock);
	CONSISTENT(sock);
	REQUIRE(sock->references > 0);
	sock->references--;

	socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
		"detach_socket %d %d %d",
		sock->pending_recv, sock->pending_send,
		sock->references);

	if (sock->references == 0 && sock->fd != INVALID_SOCKET) {
		closesocket(sock->fd);
		sock->fd = INVALID_SOCKET;
		_set_state(sock, SOCK_CLOSED);
	}

	maybe_free_socket(&sock, __LINE__);

	*socketp = NULL;
}

isc_result_t
isc_socket_close(isc_socket_t *sock) {
	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(sock->type != isc_sockettype_fdwatch);

	return (ISC_R_NOTIMPLEMENTED);
}

/*
 * Dequeue an item off the given socket's read queue, set the result code
 * in the done event to the one provided, and send it to the task it was
 * destined for.
 *
 * If the event to be sent is on a list, remove it before sending.  If
 * asked to, send and detach from the task as well.
 *
 * Caller must have the socket locked if the event is attached to the socket.
 */
static void
send_recvdone_event(isc_socket_t *sock, isc_socketevent_t **dev) {
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

	CONSISTENT(sock);
}

/*
 * See comments for send_recvdone_event() above.
 */
static void
send_senddone_event(isc_socket_t *sock, isc_socketevent_t **dev) {
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

	CONSISTENT(sock);
}

/*
 * See comments for send_recvdone_event() above.
 */
static void
send_acceptdone_event(isc_socket_t *sock, isc_socket_newconnev_t **adev) {
	isc_task_t *task;

	INSIST(adev != NULL && *adev != NULL);

	task = (*adev)->ev_sender;
	(*adev)->ev_sender = sock;

	if (ISC_LINK_LINKED(*adev, ev_link))
		ISC_LIST_DEQUEUE(sock->accept_list, *adev, ev_link);

	isc_task_sendanddetach(&task, (isc_event_t **)adev);

	CONSISTENT(sock);
}

/*
 * See comments for send_recvdone_event() above.
 */
static void
send_connectdone_event(isc_socket_t *sock, isc_socket_connev_t **cdev) {
	isc_task_t *task;

	INSIST(cdev != NULL && *cdev != NULL);

	task = (*cdev)->ev_sender;
	(*cdev)->ev_sender = sock;

	sock->connect_ev = NULL;

	isc_task_sendanddetach(&task, (isc_event_t **)cdev);

	CONSISTENT(sock);
}

/*
 * On entry to this function, the event delivered is the internal
 * readable event, and the first item on the accept_list should be
 * the done event we want to send.  If the list is empty, this is a no-op,
 * so just close the new connection, unlock, and return.
 *
 * Note the socket is locked before entering here
 */
static void
internal_accept(isc_socket_t *sock, IoCompletionInfo *lpo, int accept_errno) {
	isc_socket_newconnev_t *adev;
	isc_result_t result = ISC_R_SUCCESS;
	isc_socket_t *nsock;
	struct sockaddr *localaddr;
	int localaddr_len = sizeof(*localaddr);
	struct sockaddr *remoteaddr;
	int remoteaddr_len = sizeof(*remoteaddr);

	INSIST(VALID_SOCKET(sock));
	LOCK(&sock->lock);
	CONSISTENT(sock);

	socket_log(__LINE__, sock, NULL, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTLOCK,
		   "internal_accept called");

	INSIST(sock->listener);

	INSIST(sock->pending_iocp > 0);
	sock->pending_iocp--;
	INSIST(sock->pending_accept > 0);
	sock->pending_accept--;

	adev = lpo->adev;

	/*
	 * If the event is no longer in the list we can just return.
	 */
	if (!acceptdone_is_active(sock, adev))
		goto done;

	nsock = adev->newsocket;

	/*
	 * Pull off the done event.
	 */
	ISC_LIST_UNLINK(sock->accept_list, adev, ev_link);

	/*
	 * Extract the addresses from the socket, copy them into the structure,
	 * and return the new socket.
	 */
	ISCGetAcceptExSockaddrs(lpo->acceptbuffer, 0,
		sizeof(SOCKADDR_STORAGE) + 16, sizeof(SOCKADDR_STORAGE) + 16,
		(LPSOCKADDR *)&localaddr, &localaddr_len,
		(LPSOCKADDR *)&remoteaddr, &remoteaddr_len);
	memcpy(&adev->address.type, remoteaddr, remoteaddr_len);
	adev->address.length = remoteaddr_len;
	nsock->address = adev->address;
	nsock->pf = adev->address.type.sa.sa_family;

	socket_log(__LINE__, nsock, &nsock->address, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTLOCK,
		   "internal_accept parent %p", sock);

	result = make_nonblock(adev->newsocket->fd);
	INSIST(result == ISC_R_SUCCESS);

	INSIST(setsockopt(nsock->fd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
			  (char *)&sock->fd, sizeof(sock->fd)) == 0);

	/*
	 * Hook it up into the manager.
	 */
	nsock->bound = 1;
	nsock->connected = 1;
	_set_state(nsock, SOCK_OPEN);

	LOCK(&nsock->manager->lock);
	ISC_LIST_APPEND(nsock->manager->socklist, nsock, link);
	InterlockedIncrement(&nsock->manager->totalSockets);
	UNLOCK(&nsock->manager->lock);

	socket_log(__LINE__, sock, &nsock->address, CREATION,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTEDCXN,
		   "accepted_connection new_socket %p fd %d",
		   nsock, nsock->fd);

	adev->result = result;
	send_acceptdone_event(sock, &adev);

done:
	CONSISTENT(sock);
	UNLOCK(&sock->lock);

	HeapFree(hHeapHandle, 0, lpo->acceptbuffer);
	lpo->acceptbuffer = NULL;
}

/*
 * Called when a socket with a pending connect() finishes.
 * Note that the socket is locked before entering.
 */
static void
internal_connect(isc_socket_t *sock, IoCompletionInfo *lpo, int connect_errno) {
	isc_socket_connev_t *cdev;
	char strbuf[ISC_STRERRORSIZE];

	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);

	INSIST(sock->pending_iocp > 0);
	sock->pending_iocp--;
	INSIST(sock->pending_connect == 1);
	sock->pending_connect = 0;

	/*
	 * Has this event been canceled?
	 */
	cdev = lpo->cdev;
	if (!connectdone_is_active(sock, cdev)) {
		sock->pending_connect = 0;
		if (sock->fd != INVALID_SOCKET) {
			closesocket(sock->fd);
			sock->fd = INVALID_SOCKET;
			_set_state(sock, SOCK_CLOSED);
		}
		CONSISTENT(sock);
		UNLOCK(&sock->lock);
		return;
	}

	/*
	 * Check possible Windows network event error status here.
	 */
	if (connect_errno != 0) {
		/*
		 * If the error is SOFT, just try again on this
		 * fd and pretend nothing strange happened.
		 */
		if (SOFT_ERROR(connect_errno) ||
		    connect_errno == WSAEINPROGRESS) {
			sock->pending_connect = 1;
			CONSISTENT(sock);
			UNLOCK(&sock->lock);
			return;
		}

		/*
		 * Translate other errors into ISC_R_* flavors.
		 */
		switch (connect_errno) {
#define ERROR_MATCH(a, b) case a: cdev->result = b; break;
			ERROR_MATCH(WSAEACCES, ISC_R_NOPERM);
			ERROR_MATCH(WSAEADDRNOTAVAIL, ISC_R_ADDRNOTAVAIL);
			ERROR_MATCH(WSAEAFNOSUPPORT, ISC_R_ADDRNOTAVAIL);
			ERROR_MATCH(WSAECONNREFUSED, ISC_R_CONNREFUSED);
			ERROR_MATCH(WSAEHOSTUNREACH, ISC_R_HOSTUNREACH);
			ERROR_MATCH(WSAEHOSTDOWN, ISC_R_HOSTDOWN);
			ERROR_MATCH(WSAENETUNREACH, ISC_R_NETUNREACH);
			ERROR_MATCH(WSAENETDOWN, ISC_R_NETDOWN);
			ERROR_MATCH(WSAENOBUFS, ISC_R_NORESOURCES);
			ERROR_MATCH(WSAECONNRESET, ISC_R_CONNECTIONRESET);
			ERROR_MATCH(WSAECONNABORTED, ISC_R_CONNECTIONRESET);
			ERROR_MATCH(WSAETIMEDOUT, ISC_R_TIMEDOUT);
#undef ERROR_MATCH
		default:
			cdev->result = ISC_R_UNEXPECTED;
			isc__strerror(connect_errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__,
					 "internal_connect: connect() %s",
					 strbuf);
		}
	} else {
		INSIST(setsockopt(sock->fd, SOL_SOCKET,
				  SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == 0);
		cdev->result = ISC_R_SUCCESS;
		sock->connected = 1;
		socket_log(__LINE__, sock, &sock->address, IOEVENT,
			   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTEDCXN,
			   "internal_connect: success");
	}

	send_connectdone_event(sock, &cdev);

	UNLOCK(&sock->lock);
}

/*
 * Loop through the socket, returning ISC_R_EOF for each done event pending.
 */
static void
send_recvdone_abort(isc_socket_t *sock, isc_result_t result) {
	isc_socketevent_t *dev;

	while (!ISC_LIST_EMPTY(sock->recv_list)) {
		dev = ISC_LIST_HEAD(sock->recv_list);
		dev->result = result;
		send_recvdone_event(sock, &dev);
	}
}

/*
 * Take the data we received in our private buffer, and if any recv() calls on
 * our list are satisfied, send the corresponding done event.
 *
 * If we need more data (there are still items on the recv_list after we consume all
 * our data) then arrange for another system recv() call to fill our buffers.
 */
static void
internal_recv(isc_socket_t *sock, int nbytes)
{
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	CONSISTENT(sock);

	socket_log(__LINE__, sock, NULL, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_INTERNALRECV,
		   "internal_recv: %d bytes received", nbytes);

	/*
	 * If we got here, the I/O operation succeeded.  However, we might still have removed this
	 * event from our notification list (or never placed it on it due to immediate completion.)
	 * Handle the reference counting here, and handle the cancellation event just after.
	 */
	INSIST(sock->pending_iocp > 0);
	sock->pending_iocp--;
	INSIST(sock->pending_recv > 0);
	sock->pending_recv--;

	/*
	 * The only way we could have gotten here is that our I/O has successfully completed.
	 * Update our pointers, and move on.  The only odd case here is that we might not
	 * have received enough data on a TCP stream to satisfy the minimum requirements.  If
	 * this is the case, we will re-issue the recv() call for what we need.
	 *
	 * We do check for a recv() of 0 bytes on a TCP stream.  This means the remote end
	 * has closed.
	 */
	if (nbytes == 0 && sock->type == isc_sockettype_tcp) {
		send_recvdone_abort(sock, ISC_R_EOF);
		maybe_free_socket(&sock, __LINE__);
		return;
	}
	sock->recvbuf.remaining = nbytes;
	sock->recvbuf.consume_position = sock->recvbuf.base;
	completeio_recv(sock);

	/*
	 * If there are more receivers waiting for data, queue another receive
	 * here.
	 */
	queue_receive_request(sock);

	/*
	 * Unlock and/or destroy if we are the last thing this socket has left to do.
	 */
	maybe_free_socket(&sock, __LINE__);
}

static void
internal_send(isc_socket_t *sock, isc_socketevent_t *dev,
	      struct msghdr *messagehdr, int nbytes, int send_errno, IoCompletionInfo *lpo)
{
	buflist_t *buffer;

	/*
	 * Find out what socket this is and lock it.
	 */
	INSIST(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	CONSISTENT(sock);

	socket_log(__LINE__, sock, NULL, IOEVENT,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_INTERNALSEND,
		   "internal_send: task got socket event %p", dev);

	buffer = ISC_LIST_HEAD(lpo->bufferlist);
	while (buffer != NULL) {
		ISC_LIST_DEQUEUE(lpo->bufferlist, buffer, link);

		socket_log(__LINE__, sock, NULL, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_ACCEPTLOCK,
		   "free_buffer %p %p", buffer, buffer->buf);

		HeapFree(hHeapHandle, 0, buffer->buf);
		HeapFree(hHeapHandle, 0, buffer);
		buffer = ISC_LIST_HEAD(lpo->bufferlist);
	}

	INSIST(sock->pending_iocp > 0);
	sock->pending_iocp--;
	INSIST(sock->pending_send > 0);
	sock->pending_send--;

	/* If the event is no longer in the list we can just return */
	if (!senddone_is_active(sock, dev))
		goto done;

	/*
	 * Set the error code and send things on its way.
	 */
	switch (completeio_send(sock, dev, messagehdr, nbytes, send_errno)) {
	case DOIO_SOFT:
		break;
	case DOIO_HARD:
	case DOIO_SUCCESS:
		send_senddone_event(sock, &dev);
		break;
	}

 done:
	maybe_free_socket(&sock, __LINE__);
}

/*
 * These return if the done event passed in is on the list (or for connect, is
 * the one we're waiting for.  Using these ensures we will not double-send an
 * event.
 */
static isc_boolean_t
senddone_is_active(isc_socket_t *sock, isc_socketevent_t *dev)
{
	isc_socketevent_t *ldev;

	ldev = ISC_LIST_HEAD(sock->send_list);
	while (ldev != NULL && ldev != dev)
		ldev = ISC_LIST_NEXT(ldev, ev_link);

	return (ldev == NULL ? ISC_FALSE : ISC_TRUE);
}

static isc_boolean_t
acceptdone_is_active(isc_socket_t *sock, isc_socket_newconnev_t *dev)
{
	isc_socket_newconnev_t *ldev;

	ldev = ISC_LIST_HEAD(sock->accept_list);
	while (ldev != NULL && ldev != dev)
		ldev = ISC_LIST_NEXT(ldev, ev_link);

	return (ldev == NULL ? ISC_FALSE : ISC_TRUE);
}

static isc_boolean_t
connectdone_is_active(isc_socket_t *sock, isc_socket_connev_t *dev)
{
	return (sock->connect_ev == dev ? ISC_TRUE : ISC_FALSE);
}

//
// The Windows network stack seems to have two very distinct paths depending
// on what is installed.  Specifically, if something is looking at network
// connections (like an anti-virus or anti-malware application, such as
// McAfee products) Windows may return additional error conditions which
// were not previously returned.
//
// One specific one is when a TCP SYN scan is used.  In this situation,
// Windows responds with the SYN-ACK, but the scanner never responds with
// the 3rd packet, the ACK.  Windows consiers this a partially open connection.
// Most Unix networking stacks, and Windows without McAfee installed, will
// not return this to the caller.  However, with this product installed,
// Windows returns this as a failed status on the Accept() call.  Here, we
// will just re-issue the ISCAcceptEx() call as if nothing had happened.
//
// This code should only be called when the listening socket has received
// such an error.  Additionally, the "parent" socket must be locked.
// Additionally, the lpo argument is re-used here, and must not be freed
// by the caller.
//
static isc_result_t
restart_accept(isc_socket_t *parent, IoCompletionInfo *lpo)
{
	isc_socket_t *nsock = lpo->adev->newsocket;
	SOCKET new_fd;

	/*
	 * AcceptEx() requires we pass in a socket.  Note that we carefully
	 * do not close the previous socket in case of an error message returned by
	 * our new socket() call.  If we return an error here, our caller will
	 * clean up.
	 */
	new_fd = socket(parent->pf, SOCK_STREAM, IPPROTO_TCP);
	if (nsock->fd == INVALID_SOCKET) {
		return (ISC_R_FAILURE); // parent will ask windows for error message
	}
	closesocket(nsock->fd);
	nsock->fd = new_fd;

	memset(&lpo->overlapped, 0, sizeof(lpo->overlapped));

	ISCAcceptEx(parent->fd,
		    nsock->fd,				/* Accepted Socket */
		    lpo->acceptbuffer,			/* Buffer for initial Recv */
		    0,					/* Length of Buffer */
		    sizeof(SOCKADDR_STORAGE) + 16,	/* Local address length + 16 */
		    sizeof(SOCKADDR_STORAGE) + 16,	/* Remote address lengh + 16 */
		    (LPDWORD)&lpo->received_bytes,	/* Bytes Recved */
		    (LPOVERLAPPED)lpo			/* Overlapped structure */
		    );

	InterlockedDecrement(&nsock->manager->iocp_total);
	iocompletionport_update(nsock);

	return (ISC_R_SUCCESS);
}

/*
 * This is the I/O Completion Port Worker Function. It loops forever
 * waiting for I/O to complete and then forwards them for further
 * processing. There are a number of these in separate threads.
 */
static isc_threadresult_t WINAPI
SocketIoThread(LPVOID ThreadContext) {
	isc_socketmgr_t *manager = ThreadContext;
	BOOL bSuccess = FALSE;
	DWORD nbytes;
	IoCompletionInfo *lpo = NULL;
	isc_socket_t *sock = NULL;
	int request;
	struct msghdr *messagehdr = NULL;
	int errval;
	char strbuf[ISC_STRERRORSIZE];
	int errstatus;

	REQUIRE(VALID_MANAGER(manager));

	/*
	 * Set the thread priority high enough so I/O will
	 * preempt normal recv packet processing, but not
	 * higher than the timer sync thread.
	 */
	if (!SetThreadPriority(GetCurrentThread(),
			       THREAD_PRIORITY_ABOVE_NORMAL)) {
		errval = GetLastError();
		isc__strerror(errval, strbuf, sizeof(strbuf));
		FATAL_ERROR(__FILE__, __LINE__,
				isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
				ISC_MSG_FAILED,
				"Can't set thread priority: %s"),
				strbuf);
	}

	/*
	 * Loop forever waiting on I/O Completions and then processing them
	 */
	while (TRUE) {
		wait_again:
		bSuccess = GetQueuedCompletionStatus(manager->hIoCompletionPort,
						     &nbytes, (LPDWORD)&sock,
						     (LPWSAOVERLAPPED *)&lpo,
						     INFINITE);
		if (lpo == NULL) /* Received request to exit */
			break;

		REQUIRE(VALID_SOCKET(sock));

		request = lpo->request_type;

		errstatus = 0;
		if (!bSuccess) {
			isc_result_t isc_result;

			/*
			 * Did the I/O operation complete?
			 */
			errstatus = GetLastError();
			isc_result = isc__errno2resultx(errstatus, __FILE__, __LINE__);

			LOCK(&sock->lock);
			CONSISTENT(sock);
			switch (request) {
			case SOCKET_RECV:
				INSIST(sock->pending_iocp > 0);
				sock->pending_iocp--;
				INSIST(sock->pending_recv > 0);
				sock->pending_recv--;
				if (!sock->connected &&
				    ((errstatus == ERROR_HOST_UNREACHABLE) ||
				     (errstatus == WSAENETRESET) ||
				     (errstatus == WSAECONNRESET))) {
					/* ignore soft errors */
					queue_receive_request(sock);
					break;
				}
				send_recvdone_abort(sock, isc_result);
				if (isc_result == ISC_R_UNEXPECTED) {
					UNEXPECTED_ERROR(__FILE__, __LINE__,
						"SOCKET_RECV: Windows error code: %d, returning ISC error %d",
						errstatus, isc_result);
				}
				break;

			case SOCKET_SEND:
				INSIST(sock->pending_iocp > 0);
				sock->pending_iocp--;
				INSIST(sock->pending_send > 0);
				sock->pending_send--;
				if (senddone_is_active(sock, lpo->dev)) {
					lpo->dev->result = isc_result;
					socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
						"canceled_send");
					send_senddone_event(sock, &lpo->dev);
				}
				break;

			case SOCKET_ACCEPT:
				INSIST(sock->pending_iocp > 0);
				INSIST(sock->pending_accept > 0);

				socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
					"Accept: errstatus=%d isc_result=%d", errstatus, isc_result);

				if (acceptdone_is_active(sock, lpo->adev)) {
					if (restart_accept(sock, lpo) == ISC_R_SUCCESS) {
						UNLOCK(&sock->lock);
						goto wait_again;
					} else {
						errstatus = GetLastError();
						isc_result = isc__errno2resultx(errstatus, __FILE__, __LINE__);
						socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
							"restart_accept() failed: errstatus=%d isc_result=%d",
							errstatus, isc_result);
					}
				}

				sock->pending_iocp--;
				sock->pending_accept--;
				if (acceptdone_is_active(sock, lpo->adev)) {
					closesocket(lpo->adev->newsocket->fd);
					lpo->adev->newsocket->fd = INVALID_SOCKET;
					lpo->adev->newsocket->references--;
					free_socket(&lpo->adev->newsocket, __LINE__);
					lpo->adev->result = isc_result;
					socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
						"canceled_accept");
					send_acceptdone_event(sock, &lpo->adev);
				}
				break;

			case SOCKET_CONNECT:
				INSIST(sock->pending_iocp > 0);
				sock->pending_iocp--;
				INSIST(sock->pending_connect == 1);
				sock->pending_connect = 0;
				if (connectdone_is_active(sock, lpo->cdev)) {
					lpo->cdev->result = isc_result;
					socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
						"canceled_connect");
					send_connectdone_event(sock, &lpo->cdev);
				}
				break;
			}
			maybe_free_socket(&sock, __LINE__);

			if (lpo != NULL)
				HeapFree(hHeapHandle, 0, lpo);
			continue;
		}

		messagehdr = &lpo->messagehdr;

		switch (request) {
		case SOCKET_RECV:
			internal_recv(sock, nbytes);
			break;
		case SOCKET_SEND:
			internal_send(sock, lpo->dev, messagehdr, nbytes, errstatus, lpo);
			break;
		case SOCKET_ACCEPT:
			internal_accept(sock, lpo, errstatus);
			break;
		case SOCKET_CONNECT:
			internal_connect(sock, lpo, errstatus);
			break;
		}

		if (lpo != NULL)
			HeapFree(hHeapHandle, 0, lpo);
	}

	/*
	 * Exit Completion Port Thread
	 */
	manager_log(manager, TRACE,
		    isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
				   ISC_MSG_EXITING, "SocketIoThread exiting"));
	return ((isc_threadresult_t)0);
}

/*
 * Create a new socket manager.
 */
isc_result_t
isc__socketmgr_create(isc_mem_t *mctx, isc_socketmgr_t **managerp) {
	return (isc_socketmgr_create2(mctx, managerp, 0));
}

isc_result_t
isc__socketmgr_create2(isc_mem_t *mctx, isc_socketmgr_t **managerp,
		       unsigned int maxsocks)
{
	isc_socketmgr_t *manager;
	isc_result_t result;

	REQUIRE(managerp != NULL && *managerp == NULL);

	if (maxsocks != 0)
		return (ISC_R_NOTIMPLEMENTED);

	manager = isc_mem_get(mctx, sizeof(*manager));
	if (manager == NULL)
		return (ISC_R_NOMEMORY);

	InitSockets();

	manager->magic = SOCKET_MANAGER_MAGIC;
	manager->mctx = NULL;
	manager->stats = NULL;
	ISC_LIST_INIT(manager->socklist);
	result = isc_mutex_init(&manager->lock);
	if (result != ISC_R_SUCCESS) {
		isc_mem_put(mctx, manager, sizeof(*manager));
		return (result);
	}
	if (isc_condition_init(&manager->shutdown_ok) != ISC_R_SUCCESS) {
		DESTROYLOCK(&manager->lock);
		isc_mem_put(mctx, manager, sizeof(*manager));
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_condition_init() %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
		return (ISC_R_UNEXPECTED);
	}

	isc_mem_attach(mctx, &manager->mctx);

	iocompletionport_init(manager);	/* Create the Completion Ports */

	manager->bShutdown = ISC_FALSE;
	manager->totalSockets = 0;
	manager->iocp_total = 0;

	*managerp = manager;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc__socketmgr_getmaxsockets(isc_socketmgr_t *manager, unsigned int *nsockp) {
	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(nsockp != NULL);

	return (ISC_R_NOTIMPLEMENTED);
}

void
isc__socketmgr_setstats(isc_socketmgr_t *manager, isc_stats_t *stats) {
	REQUIRE(VALID_MANAGER(manager));
	REQUIRE(ISC_LIST_EMPTY(manager->socklist));
	REQUIRE(manager->stats == NULL);
	REQUIRE(isc_stats_ncounters(stats) == isc_sockstatscounter_max);

	isc_stats_attach(stats, &manager->stats);
}

void
isc__socketmgr_destroy(isc_socketmgr_t **managerp) {
	isc_socketmgr_t *manager;
	int i;
	isc_mem_t *mctx;

	/*
	 * Destroy a socket manager.
	 */

	REQUIRE(managerp != NULL);
	manager = *managerp;
	REQUIRE(VALID_MANAGER(manager));

	LOCK(&manager->lock);

	/*
	 * Wait for all sockets to be destroyed.
	 */
	while (!ISC_LIST_EMPTY(manager->socklist)) {
		manager_log(manager, CREATION,
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKET,
					   ISC_MSG_SOCKETSREMAIN,
					   "sockets exist"));
		WAIT(&manager->shutdown_ok, &manager->lock);
	}

	UNLOCK(&manager->lock);

	/*
	 * Here, we need to had some wait code for the completion port
	 * thread.
	 */
	signal_iocompletionport_exit(manager);
	manager->bShutdown = ISC_TRUE;

	/*
	 * Wait for threads to exit.
	 */
	for (i = 0; i < manager->maxIOCPThreads; i++) {
		if (isc_thread_join((isc_thread_t) manager->hIOCPThreads[i],
			NULL) != ISC_R_SUCCESS)
			UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_thread_join() for Completion Port %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
	}
	/*
	 * Clean up.
	 */

	CloseHandle(manager->hIoCompletionPort);

	(void)isc_condition_destroy(&manager->shutdown_ok);

	DESTROYLOCK(&manager->lock);
	if (manager->stats != NULL)
		isc_stats_detach(&manager->stats);
	manager->magic = 0;
	mctx= manager->mctx;
	isc_mem_put(mctx, manager, sizeof(*manager));

	isc_mem_detach(&mctx);

	*managerp = NULL;
}

static void
queue_receive_event(isc_socket_t *sock, isc_task_t *task, isc_socketevent_t *dev)
{
	isc_task_t *ntask = NULL;

	isc_task_attach(task, &ntask);
	dev->attributes |= ISC_SOCKEVENTATTR_ATTACHED;

	/*
	 * Enqueue the request.
	 */
	INSIST(!ISC_LINK_LINKED(dev, ev_link));
	ISC_LIST_ENQUEUE(sock->recv_list, dev, ev_link);

	socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
		   "queue_receive_event: event %p -> task %p",
		   dev, ntask);
}

/*
 * Check the pending receive queue, and if we have data pending, give it to this
 * caller.  If we have none, queue an I/O request.  If this caller is not the first
 * on the list, then we will just queue this event and return.
 *
 * Caller must have the socket locked.
 */
static isc_result_t
socket_recv(isc_socket_t *sock, isc_socketevent_t *dev, isc_task_t *task,
	    unsigned int flags)
{
	int cc = 0;
	isc_task_t *ntask = NULL;
	isc_result_t result = ISC_R_SUCCESS;
	int recv_errno = 0;

	dev->ev_sender = task;

	if (sock->fd == INVALID_SOCKET)
		return (ISC_R_EOF);

	/*
	 * Queue our event on the list of things to do.  Call our function to
	 * attempt to fill buffers as much as possible, and return done events.
	 * We are going to lie about our handling of the ISC_SOCKFLAG_IMMEDIATE
	 * here and tell our caller that we could not satisfy it immediately.
	 */
	queue_receive_event(sock, task, dev);
	if ((flags & ISC_SOCKFLAG_IMMEDIATE) != 0)
		result = ISC_R_INPROGRESS;

	completeio_recv(sock);

	/*
	 * If there are more receivers waiting for data, queue another receive
	 * here.  If the
	 */
	queue_receive_request(sock);

	return (result);
}

isc_result_t
isc__socket_recvv(isc_socket_t *sock, isc_bufferlist_t *buflist,
		 unsigned int minimum, isc_task_t *task,
		 isc_taskaction_t action, const void *arg)
{
	isc_socketevent_t *dev;
	isc_socketmgr_t *manager;
	unsigned int iocount;
	isc_buffer_t *buffer;
	isc_result_t ret;

	REQUIRE(VALID_SOCKET(sock));
	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * Make sure that the socket is not closed.  XXXMLG change error here?
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

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
	if (dev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
	}

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

	ret = socket_recv(sock, dev, task, 0);

	UNLOCK(&sock->lock);
	return (ret);
}

isc_result_t
isc__socket_recv(isc_socket_t *sock, isc_region_t *region,
		 unsigned int minimum, isc_task_t *task,
		 isc_taskaction_t action, const void *arg)
{
	isc_socketevent_t *dev;
	isc_socketmgr_t *manager;
	isc_result_t ret;

	REQUIRE(VALID_SOCKET(sock));
	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	INSIST(sock->bound);

	dev = allocate_socketevent(sock, ISC_SOCKEVENT_RECVDONE, action, arg);
	if (dev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
	}

	ret = isc_socket_recv2(sock, region, minimum, task, dev, 0);
	UNLOCK(&sock->lock);
	return (ret);
}

isc_result_t
isc__socket_recv2(isc_socket_t *sock, isc_region_t *region,
		  unsigned int minimum, isc_task_t *task,
		  isc_socketevent_t *event, unsigned int flags)
{
	isc_result_t ret;

	REQUIRE(VALID_SOCKET(sock));
	LOCK(&sock->lock);
	CONSISTENT(sock);

	event->result = ISC_R_UNEXPECTED;
	event->ev_sender = sock;
	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

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

	ret = socket_recv(sock, event, task, flags);
	UNLOCK(&sock->lock);
	return (ret);
}

/*
 * Caller must have the socket locked.
 */
static isc_result_t
socket_send(isc_socket_t *sock, isc_socketevent_t *dev, isc_task_t *task,
	    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo,
	    unsigned int flags)
{
	int io_state;
	int send_errno = 0;
	int cc = 0;
	isc_task_t *ntask = NULL;
	isc_result_t result = ISC_R_SUCCESS;

	dev->ev_sender = task;

	set_dev_address(address, sock, dev);
	if (pktinfo != NULL) {
		socket_log(__LINE__, sock, NULL, TRACE, isc_msgcat, ISC_MSGSET_SOCKET,
			   ISC_MSG_PKTINFOPROVIDED,
			   "pktinfo structure provided, ifindex %u (set to 0)",
			   pktinfo->ipi6_ifindex);

		dev->attributes |= ISC_SOCKEVENTATTR_PKTINFO;
		dev->pktinfo = *pktinfo;
		/*
		 * Set the pktinfo index to 0 here, to let the kernel decide
		 * what interface it should send on.
		 */
		dev->pktinfo.ipi6_ifindex = 0;
	}

	io_state = startio_send(sock, dev, &cc, &send_errno);
	switch (io_state) {
	case DOIO_PENDING:	/* I/O started. Nothing more to do */
	case DOIO_SOFT:
		/*
		 * We couldn't send all or part of the request right now, so
		 * queue it unless ISC_SOCKFLAG_NORETRY is set.
		 */
		if ((flags & ISC_SOCKFLAG_NORETRY) == 0) {
			isc_task_attach(task, &ntask);
			dev->attributes |= ISC_SOCKEVENTATTR_ATTACHED;

			/*
			 * Enqueue the request.
			 */
			INSIST(!ISC_LINK_LINKED(dev, ev_link));
			ISC_LIST_ENQUEUE(sock->send_list, dev, ev_link);

			socket_log(__LINE__, sock, NULL, EVENT, NULL, 0, 0,
				   "socket_send: event %p -> task %p",
				   dev, ntask);

			if ((flags & ISC_SOCKFLAG_IMMEDIATE) != 0)
				result = ISC_R_INPROGRESS;
			break;
		}

	case DOIO_SUCCESS:
		break;
	}

	return (result);
}

isc_result_t
isc__socket_send(isc_socket_t *sock, isc_region_t *region,
		 isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	/*
	 * REQUIRE() checking is performed in isc_socket_sendto().
	 */
	return (isc_socket_sendto(sock, region, task, action, arg, NULL,
				  NULL));
}

isc_result_t
isc__socket_sendto(isc_socket_t *sock, isc_region_t *region,
		   isc_task_t *task, isc_taskaction_t action, const void *arg,
		   isc_sockaddr_t *address, struct in6_pktinfo *pktinfo)
{
	isc_socketevent_t *dev;
	isc_socketmgr_t *manager;
	isc_result_t ret;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(sock->type != isc_sockettype_fdwatch);

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}
	REQUIRE(region != NULL);
	REQUIRE(task != NULL);
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	INSIST(sock->bound);

	dev = allocate_socketevent(sock, ISC_SOCKEVENT_SENDDONE, action, arg);
	if (dev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
	}
	dev->region = *region;

	ret = socket_send(sock, dev, task, address, pktinfo, 0);
	UNLOCK(&sock->lock);
	return (ret);
}

isc_result_t
isc__socket_sendv(isc_socket_t *sock, isc_bufferlist_t *buflist,
		  isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	return (isc_socket_sendtov(sock, buflist, task, action, arg, NULL,
				   NULL));
}

isc_result_t
isc__socket_sendtov(isc_socket_t *sock, isc_bufferlist_t *buflist,
		    isc_task_t *task, isc_taskaction_t action, const void *arg,
		    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo)
{
	isc_socketevent_t *dev;
	isc_socketmgr_t *manager;
	unsigned int iocount;
	isc_buffer_t *buffer;
	isc_result_t ret;

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}
	REQUIRE(buflist != NULL);
	REQUIRE(!ISC_LIST_EMPTY(*buflist));
	REQUIRE(task != NULL);
	REQUIRE(action != NULL);

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	iocount = isc_bufferlist_usedcount(buflist);
	REQUIRE(iocount > 0);

	dev = allocate_socketevent(sock, ISC_SOCKEVENT_SENDDONE, action, arg);
	if (dev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
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

	ret = socket_send(sock, dev, task, address, pktinfo, 0);
	UNLOCK(&sock->lock);
	return (ret);
}

isc_result_t
isc__socket_sendto2(isc_socket_t *sock, isc_region_t *region,
		    isc_task_t *task,
		    isc_sockaddr_t *address, struct in6_pktinfo *pktinfo,
		    isc_socketevent_t *event, unsigned int flags)
{
	isc_result_t ret;

	REQUIRE(VALID_SOCKET(sock));
	LOCK(&sock->lock);
	CONSISTENT(sock);

	REQUIRE((flags & ~(ISC_SOCKFLAG_IMMEDIATE|ISC_SOCKFLAG_NORETRY)) == 0);
	if ((flags & ISC_SOCKFLAG_NORETRY) != 0)
		REQUIRE(sock->type == isc_sockettype_udp);
	event->ev_sender = sock;
	event->result = ISC_R_UNEXPECTED;
	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}
	ISC_LIST_INIT(event->bufferlist);
	event->region = *region;
	event->n = 0;
	event->offset = 0;
	event->attributes = 0;

	ret = socket_send(sock, event, task, address, pktinfo, flags);
	UNLOCK(&sock->lock);
	return (ret);
}

isc_result_t
isc__socket_bind(isc_socket_t *sock, isc_sockaddr_t *sockaddr,
		 unsigned int options) {
	int bind_errno;
	char strbuf[ISC_STRERRORSIZE];
	int on = 1;

	REQUIRE(VALID_SOCKET(sock));
	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

	INSIST(!sock->bound);
	INSIST(!sock->dupped);

	if (sock->pf != sockaddr->type.sa.sa_family) {
		UNLOCK(&sock->lock);
		return (ISC_R_FAMILYMISMATCH);
	}
	/*
	 * Only set SO_REUSEADDR when we want a specific port.
	 */
	if ((options & ISC_SOCKET_REUSEADDRESS) != 0 &&
	    isc_sockaddr_getport(sockaddr) != (in_port_t)0 &&
	    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on,
		       sizeof(on)) < 0) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "setsockopt(%d) %s", sock->fd,
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"));
		/* Press on... */
	}
	if (bind(sock->fd, &sockaddr->type.sa, sockaddr->length) < 0) {
		bind_errno = WSAGetLastError();
		UNLOCK(&sock->lock);
		switch (bind_errno) {
		case WSAEACCES:
			return (ISC_R_NOPERM);
		case WSAEADDRNOTAVAIL:
			return (ISC_R_ADDRNOTAVAIL);
		case WSAEADDRINUSE:
			return (ISC_R_ADDRINUSE);
		case WSAEINVAL:
			return (ISC_R_BOUND);
		default:
			isc__strerror(bind_errno, strbuf, sizeof(strbuf));
			UNEXPECTED_ERROR(__FILE__, __LINE__, "bind: %s",
					 strbuf);
			return (ISC_R_UNEXPECTED);
		}
	}

	socket_log(__LINE__, sock, sockaddr, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_BOUND, "bound");
	sock->bound = 1;

	UNLOCK(&sock->lock);
	return (ISC_R_SUCCESS);
}

isc_result_t
isc__socket_filter(isc_socket_t *sock, const char *filter) {
	UNUSED(sock);
	UNUSED(filter);

	REQUIRE(VALID_SOCKET(sock));
	return (ISC_R_NOTIMPLEMENTED);
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
isc_result_t
isc__socket_listen(isc_socket_t *sock, unsigned int backlog) {
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

	REQUIRE(!sock->listener);
	REQUIRE(sock->bound);
	REQUIRE(sock->type == isc_sockettype_tcp);

	if (backlog == 0)
		backlog = SOMAXCONN;

	if (listen(sock->fd, (int)backlog) < 0) {
		UNLOCK(&sock->lock);
		isc__strerror(WSAGetLastError(), strbuf, sizeof(strbuf));

		UNEXPECTED_ERROR(__FILE__, __LINE__, "listen: %s", strbuf);

		return (ISC_R_UNEXPECTED);
	}

	socket_log(__LINE__, sock, NULL, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_BOUND, "listening");
	sock->listener = 1;
	_set_state(sock, SOCK_LISTEN);

	UNLOCK(&sock->lock);
	return (ISC_R_SUCCESS);
}

/*
 * This should try to do aggressive accept() XXXMLG
 */
isc_result_t
isc__socket_accept(isc_socket_t *sock,
		   isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	isc_socket_newconnev_t *adev;
	isc_socketmgr_t *manager;
	isc_task_t *ntask = NULL;
	isc_socket_t *nsock;
	isc_result_t result;
	IoCompletionInfo *lpo;

	REQUIRE(VALID_SOCKET(sock));

	manager = sock->manager;
	REQUIRE(VALID_MANAGER(manager));

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

	REQUIRE(sock->listener);

	/*
	 * Sender field is overloaded here with the task we will be sending
	 * this event to.  Just before the actual event is delivered the
	 * actual ev_sender will be touched up to be the socket.
	 */
	adev = (isc_socket_newconnev_t *)
		isc_event_allocate(manager->mctx, task, ISC_SOCKEVENT_NEWCONN,
				   action, arg, sizeof(*adev));
	if (adev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
	}
	ISC_LINK_INIT(adev, ev_link);

	result = allocate_socket(manager, sock->type, &nsock);
	if (result != ISC_R_SUCCESS) {
		isc_event_free((isc_event_t **)&adev);
		UNLOCK(&sock->lock);
		return (result);
	}

	/*
	 * AcceptEx() requires we pass in a socket.
	 */
	nsock->fd = socket(sock->pf, SOCK_STREAM, IPPROTO_TCP);
	if (nsock->fd == INVALID_SOCKET) {
		free_socket(&nsock, __LINE__);
		isc_event_free((isc_event_t **)&adev);
		UNLOCK(&sock->lock);
		return (ISC_R_FAILURE); // XXXMLG need real error message
	}

	/*
	 * Attach to socket and to task.
	 */
	isc_task_attach(task, &ntask);
	if (isc_task_exiting(ntask)) {
		free_socket(&nsock, __LINE__);
		isc_task_detach(&ntask);
		isc_event_free(ISC_EVENT_PTR(&adev));
		UNLOCK(&sock->lock);
		return (ISC_R_SHUTTINGDOWN);
	}
	nsock->references++;

	adev->ev_sender = ntask;
	adev->newsocket = nsock;
	_set_state(nsock, SOCK_ACCEPT);

	/*
	 * Queue io completion for an accept().
	 */
	lpo = (IoCompletionInfo *)HeapAlloc(hHeapHandle,
					    HEAP_ZERO_MEMORY,
					    sizeof(IoCompletionInfo));
	RUNTIME_CHECK(lpo != NULL);
	lpo->acceptbuffer = (void *)HeapAlloc(hHeapHandle, HEAP_ZERO_MEMORY,
		(sizeof(SOCKADDR_STORAGE) + 16) * 2);
	RUNTIME_CHECK(lpo->acceptbuffer != NULL);

	lpo->adev = adev;
	lpo->request_type = SOCKET_ACCEPT;

	ISCAcceptEx(sock->fd,
		    nsock->fd,				/* Accepted Socket */
		    lpo->acceptbuffer,			/* Buffer for initial Recv */
		    0,					/* Length of Buffer */
		    sizeof(SOCKADDR_STORAGE) + 16,		/* Local address length + 16 */
		    sizeof(SOCKADDR_STORAGE) + 16,		/* Remote address lengh + 16 */
		    (LPDWORD)&lpo->received_bytes,	/* Bytes Recved */
		    (LPOVERLAPPED)lpo			/* Overlapped structure */
		    );
	iocompletionport_update(nsock);

	socket_log(__LINE__, sock, NULL, TRACE,
		   isc_msgcat, ISC_MSGSET_SOCKET, ISC_MSG_BOUND,
		   "accepting for nsock %p fd %d", nsock, nsock->fd);

	/*
	 * Enqueue the event
	 */
	ISC_LIST_ENQUEUE(sock->accept_list, adev, ev_link);
	sock->pending_accept++;
	sock->pending_iocp++;

	UNLOCK(&sock->lock);
	return (ISC_R_SUCCESS);
}

isc_result_t
isc__socket_connect(isc_socket_t *sock, isc_sockaddr_t *addr,
		    isc_task_t *task, isc_taskaction_t action, const void *arg)
{
	char strbuf[ISC_STRERRORSIZE];
	isc_socket_connev_t *cdev;
	isc_task_t *ntask = NULL;
	isc_socketmgr_t *manager;
	IoCompletionInfo *lpo;
	int bind_errno;

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
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

	/*
	 * Windows sockets won't connect unless the socket is bound.
	 */
	if (!sock->bound) {
		isc_sockaddr_t any;

		isc_sockaddr_anyofpf(&any, isc_sockaddr_pf(addr));
		if (bind(sock->fd, &any.type.sa, any.length) < 0) {
			bind_errno = WSAGetLastError();
			UNLOCK(&sock->lock);
			switch (bind_errno) {
			case WSAEACCES:
				return (ISC_R_NOPERM);
			case WSAEADDRNOTAVAIL:
				return (ISC_R_ADDRNOTAVAIL);
			case WSAEADDRINUSE:
				return (ISC_R_ADDRINUSE);
			case WSAEINVAL:
				return (ISC_R_BOUND);
			default:
				isc__strerror(bind_errno, strbuf,
					      sizeof(strbuf));
				UNEXPECTED_ERROR(__FILE__, __LINE__,
						 "bind: %s", strbuf);
				return (ISC_R_UNEXPECTED);
			}
		}
		sock->bound = 1;
	}

	REQUIRE(!sock->pending_connect);

	cdev = (isc_socket_connev_t *)isc_event_allocate(manager->mctx, sock,
							ISC_SOCKEVENT_CONNECT,
							action,	arg,
							sizeof(*cdev));
	if (cdev == NULL) {
		UNLOCK(&sock->lock);
		return (ISC_R_NOMEMORY);
	}
	ISC_LINK_INIT(cdev, ev_link);

	if (sock->type == isc_sockettype_tcp) {
		/*
		 * Queue io completion for an accept().
		 */
		lpo = (IoCompletionInfo *)HeapAlloc(hHeapHandle,
						    HEAP_ZERO_MEMORY,
						    sizeof(IoCompletionInfo));
		lpo->cdev = cdev;
		lpo->request_type = SOCKET_CONNECT;

		sock->address = *addr;
		ISCConnectEx(sock->fd, &addr->type.sa, addr->length,
			NULL, 0, NULL, (LPOVERLAPPED)lpo);

		/*
		 * Attach to task.
		 */
		isc_task_attach(task, &ntask);
		cdev->ev_sender = ntask;

		sock->pending_connect = 1;
		_set_state(sock, SOCK_CONNECT);

		/*
		 * Enqueue the request.
		 */
		sock->connect_ev = cdev;
		sock->pending_iocp++;
	} else {
		WSAConnect(sock->fd, &addr->type.sa, addr->length, NULL, NULL, NULL, NULL);
		cdev->result = ISC_R_SUCCESS;
		isc_task_send(task, (isc_event_t **)&cdev);
	}
	CONSISTENT(sock);
	UNLOCK(&sock->lock);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc__socket_getpeername(isc_socket_t *sock, isc_sockaddr_t *addressp) {
	isc_result_t result;

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(addressp != NULL);

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

	if (sock->connected) {
		*addressp = sock->address;
		result = ISC_R_SUCCESS;
	} else {
		result = ISC_R_NOTCONNECTED;
	}

	UNLOCK(&sock->lock);

	return (result);
}

isc_result_t
isc__socket_getsockname(isc_socket_t *sock, isc_sockaddr_t *addressp) {
	ISC_SOCKADDR_LEN_T len;
	isc_result_t result;
	char strbuf[ISC_STRERRORSIZE];

	REQUIRE(VALID_SOCKET(sock));
	REQUIRE(addressp != NULL);

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

	if (!sock->bound) {
		result = ISC_R_NOTBOUND;
		goto out;
	}

	result = ISC_R_SUCCESS;

	len = sizeof(addressp->type);
	if (getsockname(sock->fd, &addressp->type.sa, (void *)&len) < 0) {
		isc__strerror(WSAGetLastError(), strbuf, sizeof(strbuf));
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
void
isc__socket_cancel(isc_socket_t *sock, isc_task_t *task, unsigned int how) {

	REQUIRE(VALID_SOCKET(sock));

	/*
	 * Quick exit if there is nothing to do.  Don't even bother locking
	 * in this case.
	 */
	if (how == 0)
		return;

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return;
	}

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

	if ((how & ISC_SOCKCANCEL_RECV) == ISC_SOCKCANCEL_RECV) {
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
	how &= ~ISC_SOCKCANCEL_RECV;

	if ((how & ISC_SOCKCANCEL_SEND) == ISC_SOCKCANCEL_SEND) {
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
	how &= ~ISC_SOCKCANCEL_SEND;

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

				dev->newsocket->references--;
				closesocket(dev->newsocket->fd);
				dev->newsocket->fd = INVALID_SOCKET;
				free_socket(&dev->newsocket, __LINE__);

				dev->result = ISC_R_CANCELED;
				send_acceptdone_event(sock, &dev);
			}

			dev = next;
		}
	}
	how &= ~ISC_SOCKCANCEL_ACCEPT;

	/*
	 * Connecting is not a list.
	 */
	if (((how & ISC_SOCKCANCEL_CONNECT) == ISC_SOCKCANCEL_CONNECT)
	    && sock->connect_ev != NULL) {
		isc_socket_connev_t    *dev;
		isc_task_t	       *current_task;

		INSIST(sock->pending_connect);

		dev = sock->connect_ev;
		current_task = dev->ev_sender;

		if ((task == NULL) || (task == current_task)) {
			closesocket(sock->fd);
			sock->fd = INVALID_SOCKET;
			_set_state(sock, SOCK_CLOSED);

			sock->connect_ev = NULL;
			dev->result = ISC_R_CANCELED;
			send_connectdone_event(sock, &dev);
		}
	}
	how &= ~ISC_SOCKCANCEL_CONNECT;

	maybe_free_socket(&sock, __LINE__);
}

isc_sockettype_t
isc__socket_gettype(isc_socket_t *sock) {
	isc_sockettype_t type;

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_R_CONNREFUSED);
	}

	type = sock->type;
	UNLOCK(&sock->lock);
	return (type);
}

isc_boolean_t
isc__socket_isbound(isc_socket_t *sock) {
	isc_boolean_t val;

	REQUIRE(VALID_SOCKET(sock));

	LOCK(&sock->lock);
	CONSISTENT(sock);

	/*
	 * make sure that the socket's not closed
	 */
	if (sock->fd == INVALID_SOCKET) {
		UNLOCK(&sock->lock);
		return (ISC_FALSE);
	}

	val = ((sock->bound) ? ISC_TRUE : ISC_FALSE);
	UNLOCK(&sock->lock);

	return (val);
}

void
isc__socket_ipv6only(isc_socket_t *sock, isc_boolean_t yes) {
#if defined(IPV6_V6ONLY)
	int onoff = yes ? 1 : 0;
#else
	UNUSED(yes);
#endif

	REQUIRE(VALID_SOCKET(sock));

#ifdef IPV6_V6ONLY
	if (sock->pf == AF_INET6) {
		(void)setsockopt(sock->fd, IPPROTO_IPV6, IPV6_V6ONLY,
				 (char *)&onoff, sizeof(onoff));
	}
#endif
}

void
isc__socket_cleanunix(isc_sockaddr_t *addr, isc_boolean_t active) {
	UNUSED(addr);
	UNUSED(active);
}

isc_result_t
isc__socket_permunix(isc_sockaddr_t *addr, isc_uint32_t perm,
		     isc_uint32_t owner,	isc_uint32_t group)
{
	UNUSED(addr);
	UNUSED(perm);
	UNUSED(owner);
	UNUSED(group);
	return (ISC_R_NOTIMPLEMENTED);
}

void
isc__socket_setname(isc_socket_t *socket, const char *name, void *tag) {

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

const char *
isc__socket_getname(isc_socket_t *socket) {
	return (socket->name);
}

void *
isc__socket_gettag(isc_socket_t *socket) {
	return (socket->tag);
}

int
isc__socket_getfd(isc_socket_t *socket) {
	return ((short) socket->fd);
}

void
isc__socketmgr_setreserved(isc_socketmgr_t *manager, isc_uint32_t reserved) {
	UNUSED(manager);
	UNUSED(reserved);
}

void
isc___socketmgr_maxudp(isc_socketmgr_t *manager, int maxudp) {

	UNUSED(manager);
	UNUSED(maxudp);
}

#ifdef HAVE_LIBXML2

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

void
isc_socketmgr_renderxml(isc_socketmgr_t *mgr, xmlTextWriterPtr writer)
{
	isc_socket_t *sock;
	char peerbuf[ISC_SOCKADDR_FORMATSIZE];
	isc_sockaddr_t addr;
	ISC_SOCKADDR_LEN_T len;

	LOCK(&mgr->lock);

#ifndef ISC_PLATFORM_USETHREADS
	xmlTextWriterStartElement(writer, ISC_XMLCHAR "references");
	xmlTextWriterWriteFormatString(writer, "%d", mgr->refs);
	xmlTextWriterEndElement(writer);
#endif

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
			isc_sockaddr_format(&sock->address, peerbuf,
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
		if (sock->pending_connect)
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
