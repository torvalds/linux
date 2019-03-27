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
#ifndef EVENT2_HTTP_COMPAT_H_INCLUDED_
#define EVENT2_HTTP_COMPAT_H_INCLUDED_

/** @file event2/http_compat.h

  Potentially non-threadsafe versions of the functions in http.h: provided
  only for backwards compatibility.

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

/**
 * Start an HTTP server on the specified address and port
 *
 * @deprecated It does not allow an event base to be specified
 *
 * @param address the address to which the HTTP server should be bound
 * @param port the port number on which the HTTP server should listen
 * @return an struct evhttp object
 */
struct evhttp *evhttp_start(const char *address, ev_uint16_t port);

/**
 * A connection object that can be used to for making HTTP requests.  The
 * connection object tries to establish the connection when it is given an
 * http request object.
 *
 * @deprecated It does not allow an event base to be specified
 */
struct evhttp_connection *evhttp_connection_new(
	const char *address, ev_uint16_t port);

/**
 * Associates an event base with the connection - can only be called
 * on a freshly created connection object that has not been used yet.
 *
 * @deprecated XXXX Why?
 */
void evhttp_connection_set_base(struct evhttp_connection *evcon,
    struct event_base *base);


/** Returns the request URI */
#define evhttp_request_uri evhttp_request_get_uri

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_EVENT_COMPAT_H_INCLUDED_ */
