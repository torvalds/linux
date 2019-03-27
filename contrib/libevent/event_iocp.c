/*
 * Copyright (c) 2009-2012 Niels Provos, Nick Mathewson
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
#include "evconfig-private.h"

#ifndef _WIN32_WINNT
/* Minimum required for InitializeCriticalSectionAndSpinCount */
#define _WIN32_WINNT 0x0403
#endif
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <mswsock.h>

#include "event2/util.h"
#include "util-internal.h"
#include "iocp-internal.h"
#include "log-internal.h"
#include "mm-internal.h"
#include "event-internal.h"
#include "evthread-internal.h"

#define NOTIFICATION_KEY ((ULONG_PTR)-1)

void
event_overlapped_init_(struct event_overlapped *o, iocp_callback cb)
{
	memset(o, 0, sizeof(struct event_overlapped));
	o->cb = cb;
}

static void
handle_entry(OVERLAPPED *o, ULONG_PTR completion_key, DWORD nBytes, int ok)
{
	struct event_overlapped *eo =
	    EVUTIL_UPCAST(o, struct event_overlapped, overlapped);
	eo->cb(eo, completion_key, nBytes, ok);
}

static void
loop(void *port_)
{
	struct event_iocp_port *port = port_;
	long ms = port->ms;
	HANDLE p = port->port;

	if (ms <= 0)
		ms = INFINITE;

	while (1) {
		OVERLAPPED *overlapped=NULL;
		ULONG_PTR key=0;
		DWORD bytes=0;
		int ok = GetQueuedCompletionStatus(p, &bytes, &key,
			&overlapped, ms);
		EnterCriticalSection(&port->lock);
		if (port->shutdown) {
			if (--port->n_live_threads == 0)
				ReleaseSemaphore(port->shutdownSemaphore, 1,
						NULL);
			LeaveCriticalSection(&port->lock);
			return;
		}
		LeaveCriticalSection(&port->lock);

		if (key != NOTIFICATION_KEY && overlapped)
			handle_entry(overlapped, key, bytes, ok);
		else if (!overlapped)
			break;
	}
	event_warnx("GetQueuedCompletionStatus exited with no event.");
	EnterCriticalSection(&port->lock);
	if (--port->n_live_threads == 0)
		ReleaseSemaphore(port->shutdownSemaphore, 1, NULL);
	LeaveCriticalSection(&port->lock);
}

int
event_iocp_port_associate_(struct event_iocp_port *port, evutil_socket_t fd,
    ev_uintptr_t key)
{
	HANDLE h;
	h = CreateIoCompletionPort((HANDLE)fd, port->port, key, port->n_threads);
	if (!h)
		return -1;
	return 0;
}

static void *
get_extension_function(SOCKET s, const GUID *which_fn)
{
	void *ptr = NULL;
	DWORD bytes=0;
	WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
	    (GUID*)which_fn, sizeof(*which_fn),
	    &ptr, sizeof(ptr),
	    &bytes, NULL, NULL);

	/* No need to detect errors here: if ptr is set, then we have a good
	   function pointer.  Otherwise, we should behave as if we had no
	   function pointer.
	*/
	return ptr;
}

/* Mingw doesn't have these in its mswsock.h.  The values are copied from
   wine.h.   Perhaps if we copy them exactly, the cargo will come again.
*/
#ifndef WSAID_ACCEPTEX
#define WSAID_ACCEPTEX \
	{0xb5367df1,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif
#ifndef WSAID_CONNECTEX
#define WSAID_CONNECTEX \
	{0x25a207b9,0xddf3,0x4660,{0x8e,0xe9,0x76,0xe5,0x8c,0x74,0x06,0x3e}}
#endif
#ifndef WSAID_GETACCEPTEXSOCKADDRS
#define WSAID_GETACCEPTEXSOCKADDRS \
	{0xb5367df2,0xcbac,0x11cf,{0x95,0xca,0x00,0x80,0x5f,0x48,0xa1,0x92}}
#endif

static int extension_fns_initialized = 0;

static void
init_extension_functions(struct win32_extension_fns *ext)
{
	const GUID acceptex = WSAID_ACCEPTEX;
	const GUID connectex = WSAID_CONNECTEX;
	const GUID getacceptexsockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == INVALID_SOCKET)
		return;
	ext->AcceptEx = get_extension_function(s, &acceptex);
	ext->ConnectEx = get_extension_function(s, &connectex);
	ext->GetAcceptExSockaddrs = get_extension_function(s,
	    &getacceptexsockaddrs);
	closesocket(s);

	extension_fns_initialized = 1;
}

static struct win32_extension_fns the_extension_fns;

const struct win32_extension_fns *
event_get_win32_extension_fns_(void)
{
	return &the_extension_fns;
}

#define N_CPUS_DEFAULT 2

struct event_iocp_port *
event_iocp_port_launch_(int n_cpus)
{
	struct event_iocp_port *port;
	int i;

	if (!extension_fns_initialized)
		init_extension_functions(&the_extension_fns);

	if (!(port = mm_calloc(1, sizeof(struct event_iocp_port))))
		return NULL;

	if (n_cpus <= 0)
		n_cpus = N_CPUS_DEFAULT;
	port->n_threads = n_cpus * 2;
	port->threads = mm_calloc(port->n_threads, sizeof(HANDLE));
	if (!port->threads)
		goto err;

	port->port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0,
			n_cpus);
	port->ms = -1;
	if (!port->port)
		goto err;

	port->shutdownSemaphore = CreateSemaphore(NULL, 0, 1, NULL);
	if (!port->shutdownSemaphore)
		goto err;

	for (i=0; i<port->n_threads; ++i) {
		ev_uintptr_t th = _beginthread(loop, 0, port);
		if (th == (ev_uintptr_t)-1)
			goto err;
		port->threads[i] = (HANDLE)th;
		++port->n_live_threads;
	}

	InitializeCriticalSectionAndSpinCount(&port->lock, 1000);

	return port;
err:
	if (port->port)
		CloseHandle(port->port);
	if (port->threads)
		mm_free(port->threads);
	if (port->shutdownSemaphore)
		CloseHandle(port->shutdownSemaphore);
	mm_free(port);
	return NULL;
}

static void
event_iocp_port_unlock_and_free_(struct event_iocp_port *port)
{
	DeleteCriticalSection(&port->lock);
	CloseHandle(port->port);
	CloseHandle(port->shutdownSemaphore);
	mm_free(port->threads);
	mm_free(port);
}

static int
event_iocp_notify_all(struct event_iocp_port *port)
{
	int i, r, ok=1;
	for (i=0; i<port->n_threads; ++i) {
		r = PostQueuedCompletionStatus(port->port, 0, NOTIFICATION_KEY,
		    NULL);
		if (!r)
			ok = 0;
	}
	return ok ? 0 : -1;
}

int
event_iocp_shutdown_(struct event_iocp_port *port, long waitMsec)
{
	DWORD ms = INFINITE;
	int n;

	EnterCriticalSection(&port->lock);
	port->shutdown = 1;
	LeaveCriticalSection(&port->lock);
	event_iocp_notify_all(port);

	if (waitMsec >= 0)
		ms = waitMsec;

	WaitForSingleObject(port->shutdownSemaphore, ms);
	EnterCriticalSection(&port->lock);
	n = port->n_live_threads;
	LeaveCriticalSection(&port->lock);
	if (n == 0) {
		event_iocp_port_unlock_and_free_(port);
		return 0;
	} else {
		return -1;
	}
}

int
event_iocp_activate_overlapped_(
    struct event_iocp_port *port, struct event_overlapped *o,
    ev_uintptr_t key, ev_uint32_t n)
{
	BOOL r;

	r = PostQueuedCompletionStatus(port->port, n, key, &o->overlapped);
	return (r==0) ? -1 : 0;
}

struct event_iocp_port *
event_base_get_iocp_(struct event_base *base)
{
#ifdef _WIN32
	return base->iocp;
#else
	return NULL;
#endif
}
