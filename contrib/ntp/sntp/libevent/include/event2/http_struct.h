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
#ifndef EVENT2_HTTP_STRUCT_H_INCLUDED_
#define EVENT2_HTTP_STRUCT_H_INCLUDED_

/** @file event2/http_struct.h

  Data structures for http.  Using these structures may hurt forward
  compatibility with later versions of Libevent: be careful!

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
 * the request structure that a server receives.
 * WARNING: expect this structure to change.  I will try to provide
 * reasonable accessors.
 */
struct evhttp_request {
#if defined(TAILQ_ENTRY)
	TAILQ_ENTRY(evhttp_request) next;
#else
struct {
	struct evhttp_request *tqe_next;
	struct evhttp_request **tqe_prev;
}       next;
#endif

	/* the connection object that this request belongs to */
	struct evhttp_connection *evcon;
	int flags;
/** The request obj owns the evhttp connection and needs to free it */
#define EVHTTP_REQ_OWN_CONNECTION	0x0001
/** Request was made via a proxy */
#define EVHTTP_PROXY_REQUEST		0x0002
/** The request object is owned by the user; the user must free it */
#define EVHTTP_USER_OWNED		0x0004
/** The request will be used again upstack; freeing must be deferred */
#define EVHTTP_REQ_DEFER_FREE		0x0008
/** The request should be freed upstack */
#define EVHTTP_REQ_NEEDS_FREE		0x0010

	struct evkeyvalq *input_headers;
	struct evkeyvalq *output_headers;

	/* address of the remote host and the port connection came from */
	char *remote_host;
	ev_uint16_t remote_port;

	/* cache of the hostname for evhttp_request_get_host */
	char *host_cache;

	enum evhttp_request_kind kind;
	enum evhttp_cmd_type type;

	size_t headers_size;
	size_t body_size;

	char *uri;			/* uri after HTTP request was parsed */
	struct evhttp_uri *uri_elems;	/* uri elements */

	char major;			/* HTTP Major number */
	char minor;			/* HTTP Minor number */

	int response_code;		/* HTTP Response code */
	char *response_code_line;	/* Readable response */

	struct evbuffer *input_buffer;	/* read data */
	ev_int64_t ntoread;
	unsigned chunked:1,		/* a chunked request */
	    userdone:1;			/* the user has sent all data */

	struct evbuffer *output_buffer;	/* outgoing post or data */

	/* Callback */
	void (*cb)(struct evhttp_request *, void *);
	void *cb_arg;

	/*
	 * Chunked data callback - call for each completed chunk if
	 * specified.  If not specified, all the data is delivered via
	 * the regular callback.
	 */
	void (*chunk_cb)(struct evhttp_request *, void *);

	/*
	 * Callback added for forked-daapd so they can collect ICY
	 * (shoutcast) metadata from the http header. If return
	 * int is negative the connection will be closed.
	 */
	int (*header_cb)(struct evhttp_request *, void *);

	/*
	 * Error callback - called when error is occured.
	 * @see evhttp_request_error for error types.
	 *
	 * @see evhttp_request_set_error_cb()
	 */
	void (*error_cb)(enum evhttp_request_error, void *);

	/*
	 * Send complete callback - called when the request is actually
	 * sent and completed.
	 */
	void (*on_complete_cb)(struct evhttp_request *, void *);
	void *on_complete_cb_arg;
};

#ifdef __cplusplus
}
#endif

#endif /* EVENT2_HTTP_STRUCT_H_INCLUDED_ */

