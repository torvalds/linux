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

/**
   @file buffer_iocp.c

   This module implements overlapped read and write functions for evbuffer
   objects on Windows.
*/
#include "event2/event-config.h"
#include "evconfig-private.h"

#include "event2/buffer.h"
#include "event2/buffer_compat.h"
#include "event2/util.h"
#include "event2/thread.h"
#include "util-internal.h"
#include "evthread-internal.h"
#include "evbuffer-internal.h"
#include "iocp-internal.h"
#include "mm-internal.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>

#define MAX_WSABUFS 16

/** An evbuffer that can handle overlapped IO. */
struct evbuffer_overlapped {
	struct evbuffer buffer;
	/** The socket that we're doing overlapped IO on. */
	evutil_socket_t fd;

	/** pending I/O type */
	unsigned read_in_progress : 1;
	unsigned write_in_progress : 1;

	/** The first pinned chain in the buffer. */
	struct evbuffer_chain *first_pinned;

	/** How many chains are pinned; how many of the fields in buffers
	 * are we using. */
	int n_buffers;
	WSABUF buffers[MAX_WSABUFS];
};

/** Given an evbuffer, return the correponding evbuffer structure, or NULL if
 * the evbuffer isn't overlapped. */
static inline struct evbuffer_overlapped *
upcast_evbuffer(struct evbuffer *buf)
{
	if (!buf || !buf->is_overlapped)
		return NULL;
	return EVUTIL_UPCAST(buf, struct evbuffer_overlapped, buffer);
}

/** Unpin all the chains noted as pinned in 'eo'. */
static void
pin_release(struct evbuffer_overlapped *eo, unsigned flag)
{
	int i;
	struct evbuffer_chain *next, *chain = eo->first_pinned;

	for (i = 0; i < eo->n_buffers; ++i) {
		EVUTIL_ASSERT(chain);
		next = chain->next;
		evbuffer_chain_unpin_(chain, flag);
		chain = next;
	}
}

void
evbuffer_commit_read_(struct evbuffer *evbuf, ev_ssize_t nBytes)
{
	struct evbuffer_overlapped *buf = upcast_evbuffer(evbuf);
	struct evbuffer_chain **chainp;
	size_t remaining, len;
	unsigned i;

	EVBUFFER_LOCK(evbuf);
	EVUTIL_ASSERT(buf->read_in_progress && !buf->write_in_progress);
	EVUTIL_ASSERT(nBytes >= 0); /* XXXX Can this be false? */

	evbuffer_unfreeze(evbuf, 0);

	chainp = evbuf->last_with_datap;
	if (!((*chainp)->flags & EVBUFFER_MEM_PINNED_R))
		chainp = &(*chainp)->next;
	remaining = nBytes;
	for (i = 0; remaining > 0 && i < (unsigned)buf->n_buffers; ++i) {
		EVUTIL_ASSERT(*chainp);
		len = buf->buffers[i].len;
		if (remaining < len)
			len = remaining;
		(*chainp)->off += len;
		evbuf->last_with_datap = chainp;
		remaining -= len;
		chainp = &(*chainp)->next;
	}

	pin_release(buf, EVBUFFER_MEM_PINNED_R);

	buf->read_in_progress = 0;

	evbuf->total_len += nBytes;
	evbuf->n_add_for_cb += nBytes;

	evbuffer_invoke_callbacks_(evbuf);

	evbuffer_decref_and_unlock_(evbuf);
}

void
evbuffer_commit_write_(struct evbuffer *evbuf, ev_ssize_t nBytes)
{
	struct evbuffer_overlapped *buf = upcast_evbuffer(evbuf);

	EVBUFFER_LOCK(evbuf);
	EVUTIL_ASSERT(buf->write_in_progress && !buf->read_in_progress);
	evbuffer_unfreeze(evbuf, 1);
	evbuffer_drain(evbuf, nBytes);
	pin_release(buf,EVBUFFER_MEM_PINNED_W);
	buf->write_in_progress = 0;
	evbuffer_decref_and_unlock_(evbuf);
}

struct evbuffer *
evbuffer_overlapped_new_(evutil_socket_t fd)
{
	struct evbuffer_overlapped *evo;

	evo = mm_calloc(1, sizeof(struct evbuffer_overlapped));
	if (!evo)
		return NULL;

	LIST_INIT(&evo->buffer.callbacks);
	evo->buffer.refcnt = 1;
	evo->buffer.last_with_datap = &evo->buffer.first;

	evo->buffer.is_overlapped = 1;
	evo->fd = fd;

	return &evo->buffer;
}

int
evbuffer_launch_write_(struct evbuffer *buf, ev_ssize_t at_most,
		struct event_overlapped *ol)
{
	struct evbuffer_overlapped *buf_o = upcast_evbuffer(buf);
	int r = -1;
	int i;
	struct evbuffer_chain *chain;
	DWORD bytesSent;

	if (!buf) {
		/* No buffer, or it isn't overlapped */
		return -1;
	}

	EVBUFFER_LOCK(buf);
	EVUTIL_ASSERT(!buf_o->read_in_progress);
	if (buf->freeze_start || buf_o->write_in_progress)
		goto done;
	if (!buf->total_len) {
		/* Nothing to write */
		r = 0;
		goto done;
	} else if (at_most < 0 || (size_t)at_most > buf->total_len) {
		at_most = buf->total_len;
	}
	evbuffer_freeze(buf, 1);

	buf_o->first_pinned = NULL;
	buf_o->n_buffers = 0;
	memset(buf_o->buffers, 0, sizeof(buf_o->buffers));

	chain = buf_o->first_pinned = buf->first;

	for (i=0; i < MAX_WSABUFS && chain; ++i, chain=chain->next) {
		WSABUF *b = &buf_o->buffers[i];
		b->buf = (char*)( chain->buffer + chain->misalign );
		evbuffer_chain_pin_(chain, EVBUFFER_MEM_PINNED_W);

		if ((size_t)at_most > chain->off) {
			/* XXXX Cast is safe for now, since win32 has no
			   mmaped chains.  But later, we need to have this
			   add more WSAbufs if chain->off is greater than
			   ULONG_MAX */
			b->len = (unsigned long)chain->off;
			at_most -= chain->off;
		} else {
			b->len = (unsigned long)at_most;
			++i;
			break;
		}
	}

	buf_o->n_buffers = i;
	evbuffer_incref_(buf);
	if (WSASend(buf_o->fd, buf_o->buffers, i, &bytesSent, 0,
		&ol->overlapped, NULL)) {
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING) {
			/* An actual error. */
			pin_release(buf_o, EVBUFFER_MEM_PINNED_W);
			evbuffer_unfreeze(buf, 1);
			evbuffer_free(buf); /* decref */
			goto done;
		}
	}

	buf_o->write_in_progress = 1;
	r = 0;
done:
	EVBUFFER_UNLOCK(buf);
	return r;
}

int
evbuffer_launch_read_(struct evbuffer *buf, size_t at_most,
		struct event_overlapped *ol)
{
	struct evbuffer_overlapped *buf_o = upcast_evbuffer(buf);
	int r = -1, i;
	int nvecs;
	int npin=0;
	struct evbuffer_chain *chain=NULL, **chainp;
	DWORD bytesRead;
	DWORD flags = 0;
	struct evbuffer_iovec vecs[MAX_WSABUFS];

	if (!buf_o)
		return -1;
	EVBUFFER_LOCK(buf);
	EVUTIL_ASSERT(!buf_o->write_in_progress);
	if (buf->freeze_end || buf_o->read_in_progress)
		goto done;

	buf_o->first_pinned = NULL;
	buf_o->n_buffers = 0;
	memset(buf_o->buffers, 0, sizeof(buf_o->buffers));

	if (evbuffer_expand_fast_(buf, at_most, MAX_WSABUFS) == -1)
		goto done;
	evbuffer_freeze(buf, 0);

	nvecs = evbuffer_read_setup_vecs_(buf, at_most,
	    vecs, MAX_WSABUFS, &chainp, 1);
	for (i=0;i<nvecs;++i) {
		WSABUF_FROM_EVBUFFER_IOV(
			&buf_o->buffers[i],
			&vecs[i]);
	}

	buf_o->n_buffers = nvecs;
	buf_o->first_pinned = chain = *chainp;

	npin=0;
	for ( ; chain; chain = chain->next) {
		evbuffer_chain_pin_(chain, EVBUFFER_MEM_PINNED_R);
		++npin;
	}
	EVUTIL_ASSERT(npin == nvecs);

	evbuffer_incref_(buf);
	if (WSARecv(buf_o->fd, buf_o->buffers, nvecs, &bytesRead, &flags,
		    &ol->overlapped, NULL)) {
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING) {
			/* An actual error. */
			pin_release(buf_o, EVBUFFER_MEM_PINNED_R);
			evbuffer_unfreeze(buf, 0);
			evbuffer_free(buf); /* decref */
			goto done;
		}
	}

	buf_o->read_in_progress = 1;
	r = 0;
done:
	EVBUFFER_UNLOCK(buf);
	return r;
}

evutil_socket_t
evbuffer_overlapped_get_fd_(struct evbuffer *buf)
{
	struct evbuffer_overlapped *buf_o = upcast_evbuffer(buf);
	return buf_o ? buf_o->fd : -1;
}

void
evbuffer_overlapped_set_fd_(struct evbuffer *buf, evutil_socket_t fd)
{
	struct evbuffer_overlapped *buf_o = upcast_evbuffer(buf);
	EVBUFFER_LOCK(buf);
	/* XXX is this right?, should it cancel current I/O operations? */
	if (buf_o)
		buf_o->fd = fd;
	EVBUFFER_UNLOCK(buf);
}
