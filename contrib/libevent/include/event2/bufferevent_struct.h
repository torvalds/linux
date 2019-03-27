/*
 * Copyright (c) 2000-2007 Niels Provos <provos@citi.umich.edu>
 * Copyright (c) 2007-2012 Niels Provos and Nick Mathewson
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
#ifndef EVENT2_BUFFEREVENT_STRUCT_H_INCLUDED_
#define EVENT2_BUFFEREVENT_STRUCT_H_INCLUDED_

/** @file event2/bufferevent_struct.h

  Data structures for bufferevents.  Using these structures may hurt forward
  compatibility with later versions of Libevent: be careful!

  @deprecated Use of bufferevent_struct.h is completely deprecated; these
    structures are only exposed for backward compatibility with programs
    written before Libevent 2.0 that used them.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <event2/event-config.h>
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

/* For int types. */
#include <event2/util.h>
/* For struct event */
#include <event2/event_struct.h>

struct event_watermark {
	size_t low;
	size_t high;
};

/**
  Shared implementation of a bufferevent.

  This type is exposed only because it was exposed in previous versions,
  and some people's code may rely on manipulating it.  Otherwise, you
  should really not rely on the layout, size, or contents of this structure:
  it is fairly volatile, and WILL change in future versions of the code.
**/
struct bufferevent {
	/** Event base for which this bufferevent was created. */
	struct event_base *ev_base;
	/** Pointer to a table of function pointers to set up how this
	    bufferevent behaves. */
	const struct bufferevent_ops *be_ops;

	/** A read event that triggers when a timeout has happened or a socket
	    is ready to read data.  Only used by some subtypes of
	    bufferevent. */
	struct event ev_read;
	/** A write event that triggers when a timeout has happened or a socket
	    is ready to write data.  Only used by some subtypes of
	    bufferevent. */
	struct event ev_write;

	/** An input buffer. Only the bufferevent is allowed to add data to
	    this buffer, though the user is allowed to drain it. */
	struct evbuffer *input;

	/** An input buffer. Only the bufferevent is allowed to drain data
	    from this buffer, though the user is allowed to add it. */
	struct evbuffer *output;

	struct event_watermark wm_read;
	struct event_watermark wm_write;

	bufferevent_data_cb readcb;
	bufferevent_data_cb writecb;
	/* This should be called 'eventcb', but renaming it would break
	 * backward compatibility */
	bufferevent_event_cb errorcb;
	void *cbarg;

	struct timeval timeout_read;
	struct timeval timeout_write;

	/** Events that are currently enabled: currently EV_READ and EV_WRITE
	    are supported. */
	short enabled;
};

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_BUFFEREVENT_STRUCT_H_INCLUDED_ */
