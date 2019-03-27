/*
 * Copyright (c) 2009-2012 Niels Provos and Nick Mathewson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef IOCP_INTERNAL_H_INCLUDED_
#define IOCP_INTERNAL_H_INCLUDED_

#ifdef __cplusplus
extern "C" {
#endif

struct event_overlapped;
struct event_iocp_port;
struct evbuffer;
typedef void (*iocp_callback)(struct event_overlapped *, ev_uintptr_t, ev_ssize_t, int success);

/* This whole file is actually win32 only. We wrap the structures in a win32
 * ifdef so that we can test-compile code that uses these interfaces on
 * non-win32 platforms. */
#ifdef _WIN32

/**
   Internal use only.  Wraps an OVERLAPPED that we're using for libevent
   functionality.  Whenever an event_iocp_port gets an event for a given
   OVERLAPPED*, it upcasts the pointer to an event_overlapped, and calls the
   iocp_callback function with the event_overlapped, the iocp key, and the
   number of bytes transferred as arguments.
 */
struct event_overlapped {
	OVERLAPPED overlapped;
	iocp_callback cb;
};

/* Mingw's headers don't define LPFN_ACCEPTEX. */

typedef BOOL (WINAPI *AcceptExPtr)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
typedef BOOL (WINAPI *ConnectExPtr)(SOCKET, const struct sockaddr *, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);
typedef void (WINAPI *GetAcceptExSockaddrsPtr)(PVOID, DWORD, DWORD, DWORD, LPSOCKADDR *, LPINT, LPSOCKADDR *, LPINT);

/** Internal use only. Holds pointers to functions that only some versions of
    Windows provide.
 */
struct win32_extension_fns {
	AcceptExPtr AcceptEx;
	ConnectExPtr ConnectEx;
	GetAcceptExSockaddrsPtr GetAcceptExSockaddrs;
};

/**
    Internal use only. Stores a Windows IO Completion port, along with
    related data.
 */
struct event_iocp_port {
	/** The port itself */
	HANDLE port;
	/* A lock to cover internal structures. */
	CRITICAL_SECTION lock;
	/** Number of threads ever open on the port. */
	short n_threads;
	/** True iff we're shutting down all the threads on this port */
	short shutdown;
	/** How often the threads on this port check for shutdown and other
	 * conditions */
	long ms;
	/* The threads that are waiting for events. */
	HANDLE *threads;
	/** Number of threads currently open on this port. */
	short n_live_threads;
	/** A semaphore to signal when we are done shutting down. */
	HANDLE *shutdownSemaphore;
};

const struct win32_extension_fns *event_get_win32_extension_fns_(void);
#else
/* Dummy definition so we can test-compile more things on unix. */
struct event_overlapped {
	iocp_callback cb;
};
#endif

/** Initialize the fields in an event_overlapped.

    @param overlapped The struct event_overlapped to initialize
    @param cb The callback that should be invoked once the IO operation has
	finished.
 */
void event_overlapped_init_(struct event_overlapped *, iocp_callback cb);

/** Allocate and return a new evbuffer that supports overlapped IO on a given
    socket.  The socket must be associated with an IO completion port using
    event_iocp_port_associate_.
*/
struct evbuffer *evbuffer_overlapped_new_(evutil_socket_t fd);

/** XXXX Document (nickm) */
evutil_socket_t evbuffer_overlapped_get_fd_(struct evbuffer *buf);

void evbuffer_overlapped_set_fd_(struct evbuffer *buf, evutil_socket_t fd);

/** Start reading data onto the end of an overlapped evbuffer.

    An evbuffer can only have one read pending at a time.  While the read
    is in progress, no other data may be added to the end of the buffer.
    The buffer must be created with event_overlapped_init_().
    evbuffer_commit_read_() must be called in the completion callback.

    @param buf The buffer to read onto
    @param n The number of bytes to try to read.
    @param ol Overlapped object with associated completion callback.
    @return 0 on success, -1 on error.
 */
int evbuffer_launch_read_(struct evbuffer *buf, size_t n, struct event_overlapped *ol);

/** Start writing data from the start of an evbuffer.

    An evbuffer can only have one write pending at a time.  While the write is
    in progress, no other data may be removed from the front of the buffer.
    The buffer must be created with event_overlapped_init_().
    evbuffer_commit_write_() must be called in the completion callback.

    @param buf The buffer to read onto
    @param n The number of bytes to try to read.
    @param ol Overlapped object with associated completion callback.
    @return 0 on success, -1 on error.
 */
int evbuffer_launch_write_(struct evbuffer *buf, ev_ssize_t n, struct event_overlapped *ol);

/** XXX document */
void evbuffer_commit_read_(struct evbuffer *, ev_ssize_t);
void evbuffer_commit_write_(struct evbuffer *, ev_ssize_t);

/** Create an IOCP, and launch its worker threads.  Internal use only.

    This interface is unstable, and will change.
 */
struct event_iocp_port *event_iocp_port_launch_(int n_cpus);

/** Associate a file descriptor with an iocp, such that overlapped IO on the
    fd will happen on one of the iocp's worker threads.
*/
int event_iocp_port_associate_(struct event_iocp_port *port, evutil_socket_t fd,
    ev_uintptr_t key);

/** Tell all threads serving an iocp to stop.  Wait for up to waitMsec for all
    the threads to finish whatever they're doing.  If waitMsec is -1, wait
    as long as required.  If all the threads are done, free the port and return
    0. Otherwise, return -1.  If you get a -1 return value, it is safe to call
    this function again.
*/
int event_iocp_shutdown_(struct event_iocp_port *port, long waitMsec);

/* FIXME document. */
int event_iocp_activate_overlapped_(struct event_iocp_port *port,
    struct event_overlapped *o,
    ev_uintptr_t key, ev_uint32_t n_bytes);

struct event_base;
/* FIXME document. */
struct event_iocp_port *event_base_get_iocp_(struct event_base *base);

/* FIXME document. */
int event_base_start_iocp_(struct event_base *base, int n_cpus);
void event_base_stop_iocp_(struct event_base *base);

/* FIXME document. */
struct bufferevent *bufferevent_async_new_(struct event_base *base,
    evutil_socket_t fd, int options);

/* FIXME document. */
void bufferevent_async_set_connected_(struct bufferevent *bev);
int bufferevent_async_can_connect_(struct bufferevent *bev);
int bufferevent_async_connect_(struct bufferevent *bev, evutil_socket_t fd,
	const struct sockaddr *sa, int socklen);

#ifdef __cplusplus
}
#endif

#endif
