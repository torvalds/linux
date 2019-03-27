/*
 * Copyright (c) 2002-2007 Niels Provos <provos@citi.umich.edu>
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

#include "event2/event-config.h"
#include "evconfig-private.h"

#ifdef EVENT__HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif
#ifdef EVENT__HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_IOCCOM_H
#include <sys/ioccom.h>
#endif
#ifdef EVENT__HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#ifdef EVENT__HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifndef _WIN32
#include <sys/socket.h>
#include <sys/stat.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <sys/queue.h>

#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef EVENT__HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef EVENT__HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <syslog.h>
#endif
#include <signal.h>
#ifdef EVENT__HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef EVENT__HAVE_FCNTL_H
#include <fcntl.h>
#endif

#undef timeout_pending
#undef timeout_initialized

#include "strlcpy-internal.h"
#include "event2/http.h"
#include "event2/event.h"
#include "event2/buffer.h"
#include "event2/bufferevent.h"
#include "event2/http_struct.h"
#include "event2/http_compat.h"
#include "event2/util.h"
#include "event2/listener.h"
#include "log-internal.h"
#include "util-internal.h"
#include "http-internal.h"
#include "mm-internal.h"
#include "bufferevent-internal.h"

#ifndef EVENT__HAVE_GETNAMEINFO
#define NI_MAXSERV 32
#define NI_MAXHOST 1025

#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 1
#endif

#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV 2
#endif

static int
fake_getnameinfo(const struct sockaddr *sa, size_t salen, char *host,
	size_t hostlen, char *serv, size_t servlen, int flags)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)sa;

	if (serv != NULL) {
		char tmpserv[16];
		evutil_snprintf(tmpserv, sizeof(tmpserv),
		    "%d", ntohs(sin->sin_port));
		if (strlcpy(serv, tmpserv, servlen) >= servlen)
			return (-1);
	}

	if (host != NULL) {
		if (flags & NI_NUMERICHOST) {
			if (strlcpy(host, inet_ntoa(sin->sin_addr),
			    hostlen) >= hostlen)
				return (-1);
			else
				return (0);
		} else {
			struct hostent *hp;
			hp = gethostbyaddr((char *)&sin->sin_addr,
			    sizeof(struct in_addr), AF_INET);
			if (hp == NULL)
				return (-2);

			if (strlcpy(host, hp->h_name, hostlen) >= hostlen)
				return (-1);
			else
				return (0);
		}
	}
	return (0);
}

#endif

#define REQ_VERSION_BEFORE(req, major_v, minor_v)			\
	((req)->major < (major_v) ||					\
	    ((req)->major == (major_v) && (req)->minor < (minor_v)))

#define REQ_VERSION_ATLEAST(req, major_v, minor_v)			\
	((req)->major > (major_v) ||					\
	    ((req)->major == (major_v) && (req)->minor >= (minor_v)))

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

extern int debug;

static evutil_socket_t bind_socket_ai(struct evutil_addrinfo *, int reuse);
static evutil_socket_t bind_socket(const char *, ev_uint16_t, int reuse);
static void name_from_addr(struct sockaddr *, ev_socklen_t, char **, char **);
static int evhttp_associate_new_request_with_connection(
	struct evhttp_connection *evcon);
static void evhttp_connection_start_detectclose(
	struct evhttp_connection *evcon);
static void evhttp_connection_stop_detectclose(
	struct evhttp_connection *evcon);
static void evhttp_request_dispatch(struct evhttp_connection* evcon);
static void evhttp_read_firstline(struct evhttp_connection *evcon,
				  struct evhttp_request *req);
static void evhttp_read_header(struct evhttp_connection *evcon,
    struct evhttp_request *req);
static int evhttp_add_header_internal(struct evkeyvalq *headers,
    const char *key, const char *value);
static const char *evhttp_response_phrase_internal(int code);
static void evhttp_get_request(struct evhttp *, evutil_socket_t, struct sockaddr *, ev_socklen_t);
static void evhttp_write_buffer(struct evhttp_connection *,
    void (*)(struct evhttp_connection *, void *), void *);
static void evhttp_make_header(struct evhttp_connection *, struct evhttp_request *);

/* callbacks for bufferevent */
static void evhttp_read_cb(struct bufferevent *, void *);
static void evhttp_write_cb(struct bufferevent *, void *);
static void evhttp_error_cb(struct bufferevent *bufev, short what, void *arg);
static int evhttp_find_vhost(struct evhttp *http, struct evhttp **outhttp,
		  const char *hostname);

#ifndef EVENT__HAVE_STRSEP
/* strsep replacement for platforms that lack it.  Only works if
 * del is one character long. */
static char *
strsep(char **s, const char *del)
{
	char *d, *tok;
	EVUTIL_ASSERT(strlen(del) == 1);
	if (!s || !*s)
		return NULL;
	tok = *s;
	d = strstr(tok, del);
	if (d) {
		*d = '\0';
		*s = d + 1;
	} else
		*s = NULL;
	return tok;
}
#endif

static size_t
html_replace(const char ch, const char **escaped)
{
	switch (ch) {
	case '<':
		*escaped = "&lt;";
		return 4;
	case '>':
		*escaped = "&gt;";
		return 4;
	case '"':
		*escaped = "&quot;";
		return 6;
	case '\'':
		*escaped = "&#039;";
		return 6;
	case '&':
		*escaped = "&amp;";
		return 5;
	default:
		break;
	}

	return 1;
}

/*
 * Replaces <, >, ", ' and & with &lt;, &gt;, &quot;,
 * &#039; and &amp; correspondingly.
 *
 * The returned string needs to be freed by the caller.
 */

char *
evhttp_htmlescape(const char *html)
{
	size_t i;
	size_t new_size = 0, old_size = 0;
	char *escaped_html, *p;

	if (html == NULL)
		return (NULL);

	old_size = strlen(html);
	for (i = 0; i < old_size; ++i) {
		const char *replaced = NULL;
		const size_t replace_size = html_replace(html[i], &replaced);
		if (replace_size > EV_SIZE_MAX - new_size) {
			event_warn("%s: html_replace overflow", __func__);
			return (NULL);
		}
		new_size += replace_size;
	}

	if (new_size == EV_SIZE_MAX)
		return (NULL);
	p = escaped_html = mm_malloc(new_size + 1);
	if (escaped_html == NULL) {
		event_warn("%s: malloc(%lu)", __func__,
		           (unsigned long)(new_size + 1));
		return (NULL);
	}
	for (i = 0; i < old_size; ++i) {
		const char *replaced = &html[i];
		const size_t len = html_replace(html[i], &replaced);
		memcpy(p, replaced, len);
		p += len;
	}

	*p = '\0';

	return (escaped_html);
}

/** Given an evhttp_cmd_type, returns a constant string containing the
 * equivalent HTTP command, or NULL if the evhttp_command_type is
 * unrecognized. */
static const char *
evhttp_method(enum evhttp_cmd_type type)
{
	const char *method;

	switch (type) {
	case EVHTTP_REQ_GET:
		method = "GET";
		break;
	case EVHTTP_REQ_POST:
		method = "POST";
		break;
	case EVHTTP_REQ_HEAD:
		method = "HEAD";
		break;
	case EVHTTP_REQ_PUT:
		method = "PUT";
		break;
	case EVHTTP_REQ_DELETE:
		method = "DELETE";
		break;
	case EVHTTP_REQ_OPTIONS:
		method = "OPTIONS";
		break;
	case EVHTTP_REQ_TRACE:
		method = "TRACE";
		break;
	case EVHTTP_REQ_CONNECT:
		method = "CONNECT";
		break;
	case EVHTTP_REQ_PATCH:
		method = "PATCH";
		break;
	default:
		method = NULL;
		break;
	}

	return (method);
}

/**
 * Determines if a response should have a body.
 * Follows the rules in RFC 2616 section 4.3.
 * @return 1 if the response MUST have a body; 0 if the response MUST NOT have
 *     a body.
 */
static int
evhttp_response_needs_body(struct evhttp_request *req)
{
	return (req->response_code != HTTP_NOCONTENT &&
		req->response_code != HTTP_NOTMODIFIED &&
		(req->response_code < 100 || req->response_code >= 200) &&
		req->type != EVHTTP_REQ_HEAD);
}

/** Helper: called after we've added some data to an evcon's bufferevent's
 * output buffer.  Sets the evconn's writing-is-done callback, and puts
 * the bufferevent into writing mode.
 */
static void
evhttp_write_buffer(struct evhttp_connection *evcon,
    void (*cb)(struct evhttp_connection *, void *), void *arg)
{
	event_debug(("%s: preparing to write buffer\n", __func__));

	/* Set call back */
	evcon->cb = cb;
	evcon->cb_arg = arg;

	/* Disable the read callback: we don't actually care about data;
	 * we only care about close detection.  (We don't disable reading,
	 * since we *do* want to learn about any close events.) */
	bufferevent_setcb(evcon->bufev,
	    NULL, /*read*/
	    evhttp_write_cb,
	    evhttp_error_cb,
	    evcon);

	bufferevent_enable(evcon->bufev, EV_WRITE);
}

static void
evhttp_send_continue_done(struct evhttp_connection *evcon, void *arg)
{
	bufferevent_disable(evcon->bufev, EV_WRITE);
}

static void
evhttp_send_continue(struct evhttp_connection *evcon,
			struct evhttp_request *req)
{
	bufferevent_enable(evcon->bufev, EV_WRITE);
	evbuffer_add_printf(bufferevent_get_output(evcon->bufev),
			"HTTP/%d.%d 100 Continue\r\n\r\n",
			req->major, req->minor);
	evcon->cb = evhttp_send_continue_done;
	evcon->cb_arg = NULL;
	bufferevent_setcb(evcon->bufev,
	    evhttp_read_cb,
	    evhttp_write_cb,
	    evhttp_error_cb,
	    evcon);
}

/** Helper: returns true iff evconn is in any connected state. */
static int
evhttp_connected(struct evhttp_connection *evcon)
{
	switch (evcon->state) {
	case EVCON_DISCONNECTED:
	case EVCON_CONNECTING:
		return (0);
	case EVCON_IDLE:
	case EVCON_READING_FIRSTLINE:
	case EVCON_READING_HEADERS:
	case EVCON_READING_BODY:
	case EVCON_READING_TRAILER:
	case EVCON_WRITING:
	default:
		return (1);
	}
}

/* Create the headers needed for an outgoing HTTP request, adds them to
 * the request's header list, and writes the request line to the
 * connection's output buffer.
 */
static void
evhttp_make_header_request(struct evhttp_connection *evcon,
    struct evhttp_request *req)
{
	const char *method;

	evhttp_remove_header(req->output_headers, "Proxy-Connection");

	/* Generate request line */
	if (!(method = evhttp_method(req->type))) {
		method = "NULL";
	}

	evbuffer_add_printf(bufferevent_get_output(evcon->bufev),
	    "%s %s HTTP/%d.%d\r\n",
	    method, req->uri, req->major, req->minor);

	/* Add the content length on a post or put request if missing */
	if ((req->type == EVHTTP_REQ_POST || req->type == EVHTTP_REQ_PUT) &&
	    evhttp_find_header(req->output_headers, "Content-Length") == NULL){
		char size[22];
		evutil_snprintf(size, sizeof(size), EV_SIZE_FMT,
		    EV_SIZE_ARG(evbuffer_get_length(req->output_buffer)));
		evhttp_add_header(req->output_headers, "Content-Length", size);
	}
}

/** Return true if the list of headers in 'headers', intepreted with respect
 * to flags, means that we should send a "connection: close" when the request
 * is done. */
static int
evhttp_is_connection_close(int flags, struct evkeyvalq* headers)
{
	if (flags & EVHTTP_PROXY_REQUEST) {
		/* proxy connection */
		const char *connection = evhttp_find_header(headers, "Proxy-Connection");
		return (connection == NULL || evutil_ascii_strcasecmp(connection, "keep-alive") != 0);
	} else {
		const char *connection = evhttp_find_header(headers, "Connection");
		return (connection != NULL && evutil_ascii_strcasecmp(connection, "close") == 0);
	}
}
static int
evhttp_is_request_connection_close(struct evhttp_request *req)
{
	return
		evhttp_is_connection_close(req->flags, req->input_headers) ||
		evhttp_is_connection_close(req->flags, req->output_headers);
}

/* Return true iff 'headers' contains 'Connection: keep-alive' */
static int
evhttp_is_connection_keepalive(struct evkeyvalq* headers)
{
	const char *connection = evhttp_find_header(headers, "Connection");
	return (connection != NULL
	    && evutil_ascii_strncasecmp(connection, "keep-alive", 10) == 0);
}

/* Add a correct "Date" header to headers, unless it already has one. */
static void
evhttp_maybe_add_date_header(struct evkeyvalq *headers)
{
	if (evhttp_find_header(headers, "Date") == NULL) {
		char date[50];
		if (sizeof(date) - evutil_date_rfc1123(date, sizeof(date), NULL) > 0) {
			evhttp_add_header(headers, "Date", date);
		}
	}
}

/* Add a "Content-Length" header with value 'content_length' to headers,
 * unless it already has a content-length or transfer-encoding header. */
static void
evhttp_maybe_add_content_length_header(struct evkeyvalq *headers,
    size_t content_length)
{
	if (evhttp_find_header(headers, "Transfer-Encoding") == NULL &&
	    evhttp_find_header(headers,	"Content-Length") == NULL) {
		char len[22];
		evutil_snprintf(len, sizeof(len), EV_SIZE_FMT,
		    EV_SIZE_ARG(content_length));
		evhttp_add_header(headers, "Content-Length", len);
	}
}

/*
 * Create the headers needed for an HTTP reply in req->output_headers,
 * and write the first HTTP response for req line to evcon.
 */
static void
evhttp_make_header_response(struct evhttp_connection *evcon,
    struct evhttp_request *req)
{
	int is_keepalive = evhttp_is_connection_keepalive(req->input_headers);
	evbuffer_add_printf(bufferevent_get_output(evcon->bufev),
	    "HTTP/%d.%d %d %s\r\n",
	    req->major, req->minor, req->response_code,
	    req->response_code_line);

	if (req->major == 1) {
		if (req->minor >= 1)
			evhttp_maybe_add_date_header(req->output_headers);

		/*
		 * if the protocol is 1.0; and the connection was keep-alive
		 * we need to add a keep-alive header, too.
		 */
		if (req->minor == 0 && is_keepalive)
			evhttp_add_header(req->output_headers,
			    "Connection", "keep-alive");

		if ((req->minor >= 1 || is_keepalive) &&
		    evhttp_response_needs_body(req)) {
			/*
			 * we need to add the content length if the
			 * user did not give it, this is required for
			 * persistent connections to work.
			 */
			evhttp_maybe_add_content_length_header(
				req->output_headers,
				evbuffer_get_length(req->output_buffer));
		}
	}

	/* Potentially add headers for unidentified content. */
	if (evhttp_response_needs_body(req)) {
		if (evhttp_find_header(req->output_headers,
			"Content-Type") == NULL
		    && evcon->http_server->default_content_type) {
			evhttp_add_header(req->output_headers,
			    "Content-Type",
			    evcon->http_server->default_content_type);
		}
	}

	/* if the request asked for a close, we send a close, too */
	if (evhttp_is_connection_close(req->flags, req->input_headers)) {
		evhttp_remove_header(req->output_headers, "Connection");
		if (!(req->flags & EVHTTP_PROXY_REQUEST))
		    evhttp_add_header(req->output_headers, "Connection", "close");
		evhttp_remove_header(req->output_headers, "Proxy-Connection");
	}
}

enum expect { NO, CONTINUE, OTHER };
static enum expect evhttp_have_expect(struct evhttp_request *req, int input)
{
	const char *expect;
	struct evkeyvalq *h = input ? req->input_headers : req->output_headers;

	if (!(req->kind == EVHTTP_REQUEST) || !REQ_VERSION_ATLEAST(req, 1, 1))
		return NO;

	expect = evhttp_find_header(h, "Expect");
	if (!expect)
		return NO;

	return !evutil_ascii_strcasecmp(expect, "100-continue") ? CONTINUE : OTHER;
}


/** Generate all headers appropriate for sending the http request in req (or
 * the response, if we're sending a response), and write them to evcon's
 * bufferevent. Also writes all data from req->output_buffer */
static void
evhttp_make_header(struct evhttp_connection *evcon, struct evhttp_request *req)
{
	struct evkeyval *header;
	struct evbuffer *output = bufferevent_get_output(evcon->bufev);

	/*
	 * Depending if this is a HTTP request or response, we might need to
	 * add some new headers or remove existing headers.
	 */
	if (req->kind == EVHTTP_REQUEST) {
		evhttp_make_header_request(evcon, req);
	} else {
		evhttp_make_header_response(evcon, req);
	}

	TAILQ_FOREACH(header, req->output_headers, next) {
		evbuffer_add_printf(output, "%s: %s\r\n",
		    header->key, header->value);
	}
	evbuffer_add(output, "\r\n", 2);

	if (evhttp_have_expect(req, 0) != CONTINUE &&
		evbuffer_get_length(req->output_buffer)) {
		/*
		 * For a request, we add the POST data, for a reply, this
		 * is the regular data.
		 */
		evbuffer_add_buffer(output, req->output_buffer);
	}
}

void
evhttp_connection_set_max_headers_size(struct evhttp_connection *evcon,
    ev_ssize_t new_max_headers_size)
{
	if (new_max_headers_size<0)
		evcon->max_headers_size = EV_SIZE_MAX;
	else
		evcon->max_headers_size = new_max_headers_size;
}
void
evhttp_connection_set_max_body_size(struct evhttp_connection* evcon,
    ev_ssize_t new_max_body_size)
{
	if (new_max_body_size<0)
		evcon->max_body_size = EV_UINT64_MAX;
	else
		evcon->max_body_size = new_max_body_size;
}

static int
evhttp_connection_incoming_fail(struct evhttp_request *req,
    enum evhttp_request_error error)
{
	switch (error) {
		case EVREQ_HTTP_DATA_TOO_LONG:
			req->response_code = HTTP_ENTITYTOOLARGE;
			break;
		default:
			req->response_code = HTTP_BADREQUEST;
	}

	switch (error) {
	case EVREQ_HTTP_TIMEOUT:
	case EVREQ_HTTP_EOF:
		/*
		 * these are cases in which we probably should just
		 * close the connection and not send a reply.  this
		 * case may happen when a browser keeps a persistent
		 * connection open and we timeout on the read.  when
		 * the request is still being used for sending, we
		 * need to disassociated it from the connection here.
		 */
		if (!req->userdone) {
			/* remove it so that it will not be freed */
			TAILQ_REMOVE(&req->evcon->requests, req, next);
			/* indicate that this request no longer has a
			 * connection object
			 */
			req->evcon = NULL;
		}
		return (-1);
	case EVREQ_HTTP_INVALID_HEADER:
	case EVREQ_HTTP_BUFFER_ERROR:
	case EVREQ_HTTP_REQUEST_CANCEL:
	case EVREQ_HTTP_DATA_TOO_LONG:
	default:	/* xxx: probably should just error on default */
		/* the callback looks at the uri to determine errors */
		if (req->uri) {
			mm_free(req->uri);
			req->uri = NULL;
		}
		if (req->uri_elems) {
			evhttp_uri_free(req->uri_elems);
			req->uri_elems = NULL;
		}

		/*
		 * the callback needs to send a reply, once the reply has
		 * been send, the connection should get freed.
		 */
		(*req->cb)(req, req->cb_arg);
	}

	return (0);
}

/* Free connection ownership of which can be acquired by user using
 * evhttp_request_own(). */
static inline void
evhttp_request_free_auto(struct evhttp_request *req)
{
	if (!(req->flags & EVHTTP_USER_OWNED))
		evhttp_request_free(req);
}

static void
evhttp_request_free_(struct evhttp_connection *evcon, struct evhttp_request *req)
{
	TAILQ_REMOVE(&evcon->requests, req, next);
	evhttp_request_free_auto(req);
}

/* Called when evcon has experienced a (non-recoverable? -NM) error, as
 * given in error. If it's an outgoing connection, reset the connection,
 * retry any pending requests, and inform the user.  If it's incoming,
 * delegates to evhttp_connection_incoming_fail(). */
void
evhttp_connection_fail_(struct evhttp_connection *evcon,
    enum evhttp_request_error error)
{
	const int errsave = EVUTIL_SOCKET_ERROR();
	struct evhttp_request* req = TAILQ_FIRST(&evcon->requests);
	void (*cb)(struct evhttp_request *, void *);
	void *cb_arg;
	void (*error_cb)(enum evhttp_request_error, void *);
	void *error_cb_arg;
	EVUTIL_ASSERT(req != NULL);

	bufferevent_disable(evcon->bufev, EV_READ|EV_WRITE);

	if (evcon->flags & EVHTTP_CON_INCOMING) {
		/*
		 * for incoming requests, there are two different
		 * failure cases.  it's either a network level error
		 * or an http layer error. for problems on the network
		 * layer like timeouts we just drop the connections.
		 * For HTTP problems, we might have to send back a
		 * reply before the connection can be freed.
		 */
		if (evhttp_connection_incoming_fail(req, error) == -1)
			evhttp_connection_free(evcon);
		return;
	}

	error_cb = req->error_cb;
	error_cb_arg = req->cb_arg;
	/* when the request was canceled, the callback is not executed */
	if (error != EVREQ_HTTP_REQUEST_CANCEL) {
		/* save the callback for later; the cb might free our object */
		cb = req->cb;
		cb_arg = req->cb_arg;
	} else {
		cb = NULL;
		cb_arg = NULL;
	}

	/* do not fail all requests; the next request is going to get
	 * send over a new connection.   when a user cancels a request,
	 * all other pending requests should be processed as normal
	 */
	evhttp_request_free_(evcon, req);

	/* reset the connection */
	evhttp_connection_reset_(evcon);

	/* We are trying the next request that was queued on us */
	if (TAILQ_FIRST(&evcon->requests) != NULL)
		evhttp_connection_connect_(evcon);

	/* The call to evhttp_connection_reset_ overwrote errno.
	 * Let's restore the original errno, so that the user's
	 * callback can have a better idea of what the error was.
	 */
	EVUTIL_SET_SOCKET_ERROR(errsave);

	/* inform the user */
	if (error_cb != NULL)
		error_cb(error, error_cb_arg);
	if (cb != NULL)
		(*cb)(NULL, cb_arg);
}

/* Bufferevent callback: invoked when any data has been written from an
 * http connection's bufferevent */
static void
evhttp_write_cb(struct bufferevent *bufev, void *arg)
{
	struct evhttp_connection *evcon = arg;

	/* Activate our call back */
	if (evcon->cb != NULL)
		(*evcon->cb)(evcon, evcon->cb_arg);
}

/**
 * Advance the connection state.
 * - If this is an outgoing connection, we've just processed the response;
 *   idle or close the connection.
 * - If this is an incoming connection, we've just processed the request;
 *   respond.
 */
static void
evhttp_connection_done(struct evhttp_connection *evcon)
{
	struct evhttp_request *req = TAILQ_FIRST(&evcon->requests);
	int con_outgoing = evcon->flags & EVHTTP_CON_OUTGOING;
	int free_evcon = 0;

	if (con_outgoing) {
		/* idle or close the connection */
		int need_close = evhttp_is_request_connection_close(req);
		TAILQ_REMOVE(&evcon->requests, req, next);
		req->evcon = NULL;

		evcon->state = EVCON_IDLE;

		/* check if we got asked to close the connection */
		if (need_close)
			evhttp_connection_reset_(evcon);

		if (TAILQ_FIRST(&evcon->requests) != NULL) {
			/*
			 * We have more requests; reset the connection
			 * and deal with the next request.
			 */
			if (!evhttp_connected(evcon))
				evhttp_connection_connect_(evcon);
			else
				evhttp_request_dispatch(evcon);
		} else if (!need_close) {
			/*
			 * The connection is going to be persistent, but we
			 * need to detect if the other side closes it.
			 */
			evhttp_connection_start_detectclose(evcon);
		} else if ((evcon->flags & EVHTTP_CON_AUTOFREE)) {
			/*
			 * If we have no more requests that need completion
			 * and we're not waiting for the connection to close
			 */
			 free_evcon = 1;
		}
	} else {
		/*
		 * incoming connection - we need to leave the request on the
		 * connection so that we can reply to it.
		 */
		evcon->state = EVCON_WRITING;
	}

	/* notify the user of the request */
	(*req->cb)(req, req->cb_arg);

	/* if this was an outgoing request, we own and it's done. so free it. */
	if (con_outgoing) {
		evhttp_request_free_auto(req);
	}

	/* If this was the last request of an outgoing connection and we're
	 * not waiting to receive a connection close event and we want to
	 * automatically free the connection. We check to ensure our request
	 * list is empty one last time just in case our callback added a
	 * new request.
	 */
	if (free_evcon && TAILQ_FIRST(&evcon->requests) == NULL) {
		evhttp_connection_free(evcon);
	}
}

/*
 * Handles reading from a chunked request.
 *   return ALL_DATA_READ:
 *     all data has been read
 *   return MORE_DATA_EXPECTED:
 *     more data is expected
 *   return DATA_CORRUPTED:
 *     data is corrupted
 *   return REQUEST_CANCELED:
 *     request was canceled by the user calling evhttp_cancel_request
 *   return DATA_TOO_LONG:
 *     ran over the maximum limit
 */

static enum message_read_status
evhttp_handle_chunked_read(struct evhttp_request *req, struct evbuffer *buf)
{
	if (req == NULL || buf == NULL) {
	    return DATA_CORRUPTED;
	}

	while (1) {
		size_t buflen;

		if ((buflen = evbuffer_get_length(buf)) == 0) {
			break;
		}

		/* evbuffer_get_length returns size_t, but len variable is ssize_t,
		 * check for overflow conditions */
		if (buflen > EV_SSIZE_MAX) {
			return DATA_CORRUPTED;
		}

		if (req->ntoread < 0) {
			/* Read chunk size */
			ev_int64_t ntoread;
			char *p = evbuffer_readln(buf, NULL, EVBUFFER_EOL_CRLF);
			char *endp;
			int error;
			if (p == NULL)
				break;
			/* the last chunk is on a new line? */
			if (strlen(p) == 0) {
				mm_free(p);
				continue;
			}
			ntoread = evutil_strtoll(p, &endp, 16);
			error = (*p == '\0' ||
			    (*endp != '\0' && *endp != ' ') ||
			    ntoread < 0);
			mm_free(p);
			if (error) {
				/* could not get chunk size */
				return (DATA_CORRUPTED);
			}

			/* ntoread is signed int64, body_size is unsigned size_t, check for under/overflow conditions */
			if ((ev_uint64_t)ntoread > EV_SIZE_MAX - req->body_size) {
			    return DATA_CORRUPTED;
			}

			if (req->body_size + (size_t)ntoread > req->evcon->max_body_size) {
				/* failed body length test */
				event_debug(("Request body is too long"));
				return (DATA_TOO_LONG);
			}

			req->body_size += (size_t)ntoread;
			req->ntoread = ntoread;
			if (req->ntoread == 0) {
				/* Last chunk */
				return (ALL_DATA_READ);
			}
			continue;
		}

		/* req->ntoread is signed int64, len is ssize_t, based on arch,
		 * ssize_t could only be 32b, check for these conditions */
		if (req->ntoread > EV_SSIZE_MAX) {
			return DATA_CORRUPTED;
		}

		/* don't have enough to complete a chunk; wait for more */
		if (req->ntoread > 0 && buflen < (ev_uint64_t)req->ntoread)
			return (MORE_DATA_EXPECTED);

		/* Completed chunk */
		evbuffer_remove_buffer(buf, req->input_buffer, (size_t)req->ntoread);
		req->ntoread = -1;
		if (req->chunk_cb != NULL) {
			req->flags |= EVHTTP_REQ_DEFER_FREE;
			(*req->chunk_cb)(req, req->cb_arg);
			evbuffer_drain(req->input_buffer,
			    evbuffer_get_length(req->input_buffer));
			req->flags &= ~EVHTTP_REQ_DEFER_FREE;
			if ((req->flags & EVHTTP_REQ_NEEDS_FREE) != 0) {
				return (REQUEST_CANCELED);
			}
		}
	}

	return (MORE_DATA_EXPECTED);
}

static void
evhttp_read_trailer(struct evhttp_connection *evcon, struct evhttp_request *req)
{
	struct evbuffer *buf = bufferevent_get_input(evcon->bufev);

	switch (evhttp_parse_headers_(req, buf)) {
	case DATA_CORRUPTED:
	case DATA_TOO_LONG:
		evhttp_connection_fail_(evcon, EVREQ_HTTP_DATA_TOO_LONG);
		break;
	case ALL_DATA_READ:
		bufferevent_disable(evcon->bufev, EV_READ);
		evhttp_connection_done(evcon);
		break;
	case MORE_DATA_EXPECTED:
	case REQUEST_CANCELED: /* ??? */
	default:
		break;
	}
}

static void
evhttp_lingering_close(struct evhttp_connection *evcon,
	struct evhttp_request *req)
{
	struct evbuffer *buf = bufferevent_get_input(evcon->bufev);

	size_t n = evbuffer_get_length(buf);
	if (n > (size_t) req->ntoread)
		n = (size_t) req->ntoread;
	req->ntoread -= n;
	req->body_size += n;

	event_debug(("Request body is too long, left " EV_I64_FMT,
		EV_I64_ARG(req->ntoread)));

	evbuffer_drain(buf, n);
	if (!req->ntoread)
		evhttp_connection_fail_(evcon, EVREQ_HTTP_DATA_TOO_LONG);
}
static void
evhttp_lingering_fail(struct evhttp_connection *evcon,
	struct evhttp_request *req)
{
	if (evcon->flags & EVHTTP_CON_LINGERING_CLOSE)
		evhttp_lingering_close(evcon, req);
	else
		evhttp_connection_fail_(evcon, EVREQ_HTTP_DATA_TOO_LONG);
}

static void
evhttp_read_body(struct evhttp_connection *evcon, struct evhttp_request *req)
{
	struct evbuffer *buf = bufferevent_get_input(evcon->bufev);

	if (req->chunked) {
		switch (evhttp_handle_chunked_read(req, buf)) {
		case ALL_DATA_READ:
			/* finished last chunk */
			evcon->state = EVCON_READING_TRAILER;
			evhttp_read_trailer(evcon, req);
			return;
		case DATA_CORRUPTED:
		case DATA_TOO_LONG:
			/* corrupted data */
			evhttp_connection_fail_(evcon,
			    EVREQ_HTTP_DATA_TOO_LONG);
			return;
		case REQUEST_CANCELED:
			/* request canceled */
			evhttp_request_free_auto(req);
			return;
		case MORE_DATA_EXPECTED:
		default:
			break;
		}
	} else if (req->ntoread < 0) {
		/* Read until connection close. */
		if ((size_t)(req->body_size + evbuffer_get_length(buf)) < req->body_size) {
			evhttp_connection_fail_(evcon, EVREQ_HTTP_INVALID_HEADER);
			return;
		}

		req->body_size += evbuffer_get_length(buf);
		evbuffer_add_buffer(req->input_buffer, buf);
	} else if (req->chunk_cb != NULL || evbuffer_get_length(buf) >= (size_t)req->ntoread) {
		/* XXX: the above get_length comparison has to be fixed for overflow conditions! */
		/* We've postponed moving the data until now, but we're
		 * about to use it. */
		size_t n = evbuffer_get_length(buf);

		if (n > (size_t) req->ntoread)
			n = (size_t) req->ntoread;
		req->ntoread -= n;
		req->body_size += n;
		evbuffer_remove_buffer(buf, req->input_buffer, n);
	}

	if (req->body_size > req->evcon->max_body_size ||
	    (!req->chunked && req->ntoread >= 0 &&
		(size_t)req->ntoread > req->evcon->max_body_size)) {
		/* XXX: The above casted comparison must checked for overflow */
		/* failed body length test */

		evhttp_lingering_fail(evcon, req);
		return;
	}

	if (evbuffer_get_length(req->input_buffer) > 0 && req->chunk_cb != NULL) {
		req->flags |= EVHTTP_REQ_DEFER_FREE;
		(*req->chunk_cb)(req, req->cb_arg);
		req->flags &= ~EVHTTP_REQ_DEFER_FREE;
		evbuffer_drain(req->input_buffer,
		    evbuffer_get_length(req->input_buffer));
		if ((req->flags & EVHTTP_REQ_NEEDS_FREE) != 0) {
			evhttp_request_free_auto(req);
			return;
		}
	}

	if (!req->ntoread) {
		bufferevent_disable(evcon->bufev, EV_READ);
		/* Completed content length */
		evhttp_connection_done(evcon);
		return;
	}
}

#define get_deferred_queue(evcon)		\
	((evcon)->base)

/*
 * Gets called when more data becomes available
 */

static void
evhttp_read_cb(struct bufferevent *bufev, void *arg)
{
	struct evhttp_connection *evcon = arg;
	struct evhttp_request *req = TAILQ_FIRST(&evcon->requests);

	/* Cancel if it's pending. */
	event_deferred_cb_cancel_(get_deferred_queue(evcon),
	    &evcon->read_more_deferred_cb);

	switch (evcon->state) {
	case EVCON_READING_FIRSTLINE:
		evhttp_read_firstline(evcon, req);
		/* note the request may have been freed in
		 * evhttp_read_body */
		break;
	case EVCON_READING_HEADERS:
		evhttp_read_header(evcon, req);
		/* note the request may have been freed in
		 * evhttp_read_body */
		break;
	case EVCON_READING_BODY:
		evhttp_read_body(evcon, req);
		/* note the request may have been freed in
		 * evhttp_read_body */
		break;
	case EVCON_READING_TRAILER:
		evhttp_read_trailer(evcon, req);
		break;
	case EVCON_IDLE:
		{
#ifdef USE_DEBUG
			struct evbuffer *input;
			size_t total_len;

			input = bufferevent_get_input(evcon->bufev);
			total_len = evbuffer_get_length(input);
			event_debug(("%s: read "EV_SIZE_FMT
				" bytes in EVCON_IDLE state,"
				" resetting connection",
				__func__, EV_SIZE_ARG(total_len)));
#endif

			evhttp_connection_reset_(evcon);
		}
		break;
	case EVCON_DISCONNECTED:
	case EVCON_CONNECTING:
	case EVCON_WRITING:
	default:
		event_errx(1, "%s: illegal connection state %d",
			   __func__, evcon->state);
	}
}

static void
evhttp_deferred_read_cb(struct event_callback *cb, void *data)
{
	struct evhttp_connection *evcon = data;
	evhttp_read_cb(evcon->bufev, evcon);
}

static void
evhttp_write_connectioncb(struct evhttp_connection *evcon, void *arg)
{
	/* This is after writing the request to the server */
	struct evhttp_request *req = TAILQ_FIRST(&evcon->requests);
	struct evbuffer *output = bufferevent_get_output(evcon->bufev);
	EVUTIL_ASSERT(req != NULL);

	EVUTIL_ASSERT(evcon->state == EVCON_WRITING);

	/* We need to wait until we've written all of our output data before we can
	 * continue */
	if (evbuffer_get_length(output) > 0)
		return;

	/* We are done writing our header and are now expecting the response */
	req->kind = EVHTTP_RESPONSE;

	evhttp_start_read_(evcon);
}

/*
 * Clean up a connection object
 */

void
evhttp_connection_free(struct evhttp_connection *evcon)
{
	struct evhttp_request *req;

	/* notify interested parties that this connection is going down */
	if (evcon->fd != -1) {
		if (evhttp_connected(evcon) && evcon->closecb != NULL)
			(*evcon->closecb)(evcon, evcon->closecb_arg);
	}

	/* remove all requests that might be queued on this
	 * connection.  for server connections, this should be empty.
	 * because it gets dequeued either in evhttp_connection_done or
	 * evhttp_connection_fail_.
	 */
	while ((req = TAILQ_FIRST(&evcon->requests)) != NULL) {
		evhttp_request_free_(evcon, req);
	}

	if (evcon->http_server != NULL) {
		struct evhttp *http = evcon->http_server;
		TAILQ_REMOVE(&http->connections, evcon, next);
	}

	if (event_initialized(&evcon->retry_ev)) {
		event_del(&evcon->retry_ev);
		event_debug_unassign(&evcon->retry_ev);
	}

	if (evcon->bufev != NULL)
		bufferevent_free(evcon->bufev);

	event_deferred_cb_cancel_(get_deferred_queue(evcon),
	    &evcon->read_more_deferred_cb);

	if (evcon->fd == -1)
		evcon->fd = bufferevent_getfd(evcon->bufev);

	if (evcon->fd != -1) {
		bufferevent_disable(evcon->bufev, EV_READ|EV_WRITE);
		shutdown(evcon->fd, EVUTIL_SHUT_WR);
		if (!(bufferevent_get_options_(evcon->bufev) & BEV_OPT_CLOSE_ON_FREE)) {
			evutil_closesocket(evcon->fd);
		}
	}

	if (evcon->bind_address != NULL)
		mm_free(evcon->bind_address);

	if (evcon->address != NULL)
		mm_free(evcon->address);

	mm_free(evcon);
}

void
evhttp_connection_free_on_completion(struct evhttp_connection *evcon) {
	evcon->flags |= EVHTTP_CON_AUTOFREE;
}

void
evhttp_connection_set_local_address(struct evhttp_connection *evcon,
    const char *address)
{
	EVUTIL_ASSERT(evcon->state == EVCON_DISCONNECTED);
	if (evcon->bind_address)
		mm_free(evcon->bind_address);
	if ((evcon->bind_address = mm_strdup(address)) == NULL)
		event_warn("%s: strdup", __func__);
}

void
evhttp_connection_set_local_port(struct evhttp_connection *evcon,
    ev_uint16_t port)
{
	EVUTIL_ASSERT(evcon->state == EVCON_DISCONNECTED);
	evcon->bind_port = port;
}

static void
evhttp_request_dispatch(struct evhttp_connection* evcon)
{
	struct evhttp_request *req = TAILQ_FIRST(&evcon->requests);

	/* this should not usually happy but it's possible */
	if (req == NULL)
		return;

	/* delete possible close detection events */
	evhttp_connection_stop_detectclose(evcon);

	/* we assume that the connection is connected already */
	EVUTIL_ASSERT(evcon->state == EVCON_IDLE);

	evcon->state = EVCON_WRITING;

	/* Create the header from the store arguments */
	evhttp_make_header(evcon, req);

	evhttp_write_buffer(evcon, evhttp_write_connectioncb, NULL);
}

/* Reset our connection state: disables reading/writing, closes our fd (if
* any), clears out buffers, and puts us in state DISCONNECTED. */
void
evhttp_connection_reset_(struct evhttp_connection *evcon)
{
	struct evbuffer *tmp;
	int err;

	/* XXXX This is not actually an optimal fix.  Instead we ought to have
	   an API for "stop connecting", or use bufferevent_setfd to turn off
	   connecting.  But for Libevent 2.0, this seems like a minimal change
	   least likely to disrupt the rest of the bufferevent and http code.

	   Why is this here?  If the fd is set in the bufferevent, and the
	   bufferevent is connecting, then you can't actually stop the
	   bufferevent from trying to connect with bufferevent_disable().  The
	   connect will never trigger, since we close the fd, but the timeout
	   might.  That caused an assertion failure in evhttp_connection_fail_.
	*/
	bufferevent_disable_hard_(evcon->bufev, EV_READ|EV_WRITE);

	if (evcon->fd == -1)
		evcon->fd = bufferevent_getfd(evcon->bufev);

	if (evcon->fd != -1) {
		/* inform interested parties about connection close */
		if (evhttp_connected(evcon) && evcon->closecb != NULL)
			(*evcon->closecb)(evcon, evcon->closecb_arg);

		shutdown(evcon->fd, EVUTIL_SHUT_WR);
		evutil_closesocket(evcon->fd);
		evcon->fd = -1;
	}
	bufferevent_setfd(evcon->bufev, -1);

	/* we need to clean up any buffered data */
	tmp = bufferevent_get_output(evcon->bufev);
	err = evbuffer_drain(tmp, -1);
	EVUTIL_ASSERT(!err && "drain output");
	tmp = bufferevent_get_input(evcon->bufev);
	err = evbuffer_drain(tmp, -1);
	EVUTIL_ASSERT(!err && "drain input");

	evcon->flags &= ~EVHTTP_CON_READING_ERROR;

	evcon->state = EVCON_DISCONNECTED;
}

static void
evhttp_connection_start_detectclose(struct evhttp_connection *evcon)
{
	evcon->flags |= EVHTTP_CON_CLOSEDETECT;

	bufferevent_enable(evcon->bufev, EV_READ);
}

static void
evhttp_connection_stop_detectclose(struct evhttp_connection *evcon)
{
	evcon->flags &= ~EVHTTP_CON_CLOSEDETECT;

	bufferevent_disable(evcon->bufev, EV_READ);
}

static void
evhttp_connection_retry(evutil_socket_t fd, short what, void *arg)
{
	struct evhttp_connection *evcon = arg;

	evcon->state = EVCON_DISCONNECTED;
	evhttp_connection_connect_(evcon);
}

static void
evhttp_connection_cb_cleanup(struct evhttp_connection *evcon)
{
	struct evcon_requestq requests;

	evhttp_connection_reset_(evcon);
	if (evcon->retry_max < 0 || evcon->retry_cnt < evcon->retry_max) {
		struct timeval tv_retry = evcon->initial_retry_timeout;
		int i;
		evtimer_assign(&evcon->retry_ev, evcon->base, evhttp_connection_retry, evcon);
		/* XXXX handle failure from evhttp_add_event */
		for (i=0; i < evcon->retry_cnt; ++i) {
			tv_retry.tv_usec *= 2;
			if (tv_retry.tv_usec > 1000000) {
				tv_retry.tv_usec -= 1000000;
				tv_retry.tv_sec += 1;
			}
			tv_retry.tv_sec *= 2;
			if (tv_retry.tv_sec > 3600) {
				tv_retry.tv_sec = 3600;
				tv_retry.tv_usec = 0;
			}
		}
		event_add(&evcon->retry_ev, &tv_retry);
		evcon->retry_cnt++;
		return;
	}

	/*
	 * User callback can do evhttp_make_request() on the same
	 * evcon so new request will be added to evcon->requests.  To
	 * avoid freeing it prematurely we iterate over the copy of
	 * the queue.
	 */
	TAILQ_INIT(&requests);
	while (TAILQ_FIRST(&evcon->requests) != NULL) {
		struct evhttp_request *request = TAILQ_FIRST(&evcon->requests);
		TAILQ_REMOVE(&evcon->requests, request, next);
		TAILQ_INSERT_TAIL(&requests, request, next);
	}

	/* for now, we just signal all requests by executing their callbacks */
	while (TAILQ_FIRST(&requests) != NULL) {
		struct evhttp_request *request = TAILQ_FIRST(&requests);
		TAILQ_REMOVE(&requests, request, next);
		request->evcon = NULL;

		/* we might want to set an error here */
		request->cb(request, request->cb_arg);
		evhttp_request_free_auto(request);
	}
}

static void
evhttp_connection_read_on_write_error(struct evhttp_connection *evcon,
    struct evhttp_request *req)
{
	struct evbuffer *buf;

	/** Second time, we can't read anything */
	if (evcon->flags & EVHTTP_CON_READING_ERROR) {
		evcon->flags &= ~EVHTTP_CON_READING_ERROR;
		evhttp_connection_fail_(evcon, EVREQ_HTTP_EOF);
		return;
	}

	req->kind = EVHTTP_RESPONSE;

	buf = bufferevent_get_output(evcon->bufev);
	evbuffer_unfreeze(buf, 1);
	evbuffer_drain(buf, evbuffer_get_length(buf));
	evbuffer_freeze(buf, 1);

	evhttp_start_read_(evcon);
	evcon->flags |= EVHTTP_CON_READING_ERROR;
}

static void
evhttp_error_cb(struct bufferevent *bufev, short what, void *arg)
{
	struct evhttp_connection *evcon = arg;
	struct evhttp_request *req = TAILQ_FIRST(&evcon->requests);

	if (evcon->fd == -1)
		evcon->fd = bufferevent_getfd(bufev);

	switch (evcon->state) {
	case EVCON_CONNECTING:
		if (what & BEV_EVENT_TIMEOUT) {
			event_debug(("%s: connection timeout for \"%s:%d\" on "
				EV_SOCK_FMT,
				__func__, evcon->address, evcon->port,
				EV_SOCK_ARG(evcon->fd)));
			evhttp_connection_cb_cleanup(evcon);
			return;
		}
		break;

	case EVCON_READING_BODY:
		if (!req->chunked && req->ntoread < 0
		    && what == (BEV_EVENT_READING|BEV_EVENT_EOF)) {
			/* EOF on read can be benign */
			evhttp_connection_done(evcon);
			return;
		}
		break;

	case EVCON_DISCONNECTED:
	case EVCON_IDLE:
	case EVCON_READING_FIRSTLINE:
	case EVCON_READING_HEADERS:
	case EVCON_READING_TRAILER:
	case EVCON_WRITING:
	default:
		break;
	}

	/* when we are in close detect mode, a read error means that
	 * the other side closed their connection.
	 */
	if (evcon->flags & EVHTTP_CON_CLOSEDETECT) {
		evcon->flags &= ~EVHTTP_CON_CLOSEDETECT;
		EVUTIL_ASSERT(evcon->http_server == NULL);
		/* For connections from the client, we just
		 * reset the connection so that it becomes
		 * disconnected.
		 */
		EVUTIL_ASSERT(evcon->state == EVCON_IDLE);
		evhttp_connection_reset_(evcon);

		/*
		 * If we have no more requests that need completion
		 * and we want to auto-free the connection when all
		 * requests have been completed.
		 */
		if (TAILQ_FIRST(&evcon->requests) == NULL
		  && (evcon->flags & EVHTTP_CON_OUTGOING)
		  && (evcon->flags & EVHTTP_CON_AUTOFREE)) {
			evhttp_connection_free(evcon);
		}
		return;
	}

	if (what & BEV_EVENT_TIMEOUT) {
		evhttp_connection_fail_(evcon, EVREQ_HTTP_TIMEOUT);
	} else if (what & (BEV_EVENT_EOF|BEV_EVENT_ERROR)) {
		if (what & BEV_EVENT_WRITING &&
			evcon->flags & EVHTTP_CON_READ_ON_WRITE_ERROR) {
			evhttp_connection_read_on_write_error(evcon, req);
			return;
		}

		evhttp_connection_fail_(evcon, EVREQ_HTTP_EOF);
	} else if (what == BEV_EVENT_CONNECTED) {
	} else {
		evhttp_connection_fail_(evcon, EVREQ_HTTP_BUFFER_ERROR);
	}
}

/*
 * Event callback for asynchronous connection attempt.
 */
static void
evhttp_connection_cb(struct bufferevent *bufev, short what, void *arg)
{
	struct evhttp_connection *evcon = arg;
	int error;
	ev_socklen_t errsz = sizeof(error);

	if (evcon->fd == -1)
		evcon->fd = bufferevent_getfd(bufev);

	if (!(what & BEV_EVENT_CONNECTED)) {
		/* some operating systems return ECONNREFUSED immediately
		 * when connecting to a local address.  the cleanup is going
		 * to reschedule this function call.
		 */
#ifndef _WIN32
		if (errno == ECONNREFUSED)
			goto cleanup;
#endif
		evhttp_error_cb(bufev, what, arg);
		return;
	}

	if (evcon->fd == -1) {
		event_debug(("%s: bufferevent_getfd returned -1",
			__func__));
		goto cleanup;
	}

	/* Check if the connection completed */
	if (getsockopt(evcon->fd, SOL_SOCKET, SO_ERROR, (void*)&error,
		       &errsz) == -1) {
		event_debug(("%s: getsockopt for \"%s:%d\" on "EV_SOCK_FMT,
			__func__, evcon->address, evcon->port,
			EV_SOCK_ARG(evcon->fd)));
		goto cleanup;
	}

	if (error) {
		event_debug(("%s: connect failed for \"%s:%d\" on "
			EV_SOCK_FMT": %s",
			__func__, evcon->address, evcon->port,
			EV_SOCK_ARG(evcon->fd),
			evutil_socket_error_to_string(error)));
		goto cleanup;
	}

	/* We are connected to the server now */
	event_debug(("%s: connected to \"%s:%d\" on "EV_SOCK_FMT"\n",
			__func__, evcon->address, evcon->port,
			EV_SOCK_ARG(evcon->fd)));

	/* Reset the retry count as we were successful in connecting */
	evcon->retry_cnt = 0;
	evcon->state = EVCON_IDLE;

	/* reset the bufferevent cbs */
	bufferevent_setcb(evcon->bufev,
	    evhttp_read_cb,
	    evhttp_write_cb,
	    evhttp_error_cb,
	    evcon);

	if (!evutil_timerisset(&evcon->timeout)) {
		const struct timeval read_tv = { HTTP_READ_TIMEOUT, 0 };
		const struct timeval write_tv = { HTTP_WRITE_TIMEOUT, 0 };
		bufferevent_set_timeouts(evcon->bufev, &read_tv, &write_tv);
	} else {
		bufferevent_set_timeouts(evcon->bufev, &evcon->timeout, &evcon->timeout);
	}

	/* try to start requests that have queued up on this connection */
	evhttp_request_dispatch(evcon);
	return;

 cleanup:
	evhttp_connection_cb_cleanup(evcon);
}

/*
 * Check if we got a valid response code.
 */

static int
evhttp_valid_response_code(int code)
{
	if (code == 0)
		return (0);

	return (1);
}

static int
evhttp_parse_http_version(const char *version, struct evhttp_request *req)
{
	int major, minor;
	char ch;
	int n = sscanf(version, "HTTP/%d.%d%c", &major, &minor, &ch);
	if (n != 2 || major > 1) {
		event_debug(("%s: bad version %s on message %p from %s",
			__func__, version, req, req->remote_host));
		return (-1);
	}
	req->major = major;
	req->minor = minor;
	return (0);
}

/* Parses the status line of a web server */

static int
evhttp_parse_response_line(struct evhttp_request *req, char *line)
{
	char *protocol;
	char *number;
	const char *readable = "";

	protocol = strsep(&line, " ");
	if (line == NULL)
		return (-1);
	number = strsep(&line, " ");
	if (line != NULL)
		readable = line;

	if (evhttp_parse_http_version(protocol, req) < 0)
		return (-1);

	req->response_code = atoi(number);
	if (!evhttp_valid_response_code(req->response_code)) {
		event_debug(("%s: bad response code \"%s\"",
			__func__, number));
		return (-1);
	}

	if (req->response_code_line != NULL)
		mm_free(req->response_code_line);
	if ((req->response_code_line = mm_strdup(readable)) == NULL) {
		event_warn("%s: strdup", __func__);
		return (-1);
	}

	return (0);
}

/* Parse the first line of a HTTP request */

static int
evhttp_parse_request_line(struct evhttp_request *req, char *line)
{
	char *method;
	char *uri;
	char *version;
	const char *hostname;
	const char *scheme;
	size_t method_len;
	enum evhttp_cmd_type type;

	/* Parse the request line */
	method = strsep(&line, " ");
	if (line == NULL)
		return (-1);
	uri = strsep(&line, " ");
	if (line == NULL)
		return (-1);
	version = strsep(&line, " ");
	if (line != NULL)
		return (-1);

	method_len = (uri - method) - 1;
	type       = EVHTTP_REQ_UNKNOWN_;

	/* First line */
	switch (method_len) {
	    case 3:
		/* The length of the method string is 3, meaning it can only be one of two methods: GET or PUT */
            
		/* Since both GET and PUT share the same character 'T' at the end,
		 * if the string doesn't have 'T', we can immediately determine this
		 * is an invalid HTTP method */
            
		if (method[2] != 'T') {
		    break;
		}
            
		switch (*method) {
		    case 'G':
			/* This first byte is 'G', so make sure the next byte is
			 * 'E', if it isn't then this isn't a valid method */
                    
			if (method[1] == 'E') {
			    type = EVHTTP_REQ_GET;
			}
                    
			break;
		    case 'P':
			/* First byte is P, check second byte for 'U', if not,
			 * we know it's an invalid method */
			if (method[1] == 'U') {
			    type = EVHTTP_REQ_PUT;
			}
			break;
		    default:
			break;
		}
		break;
	    case 4:
		/* The method length is 4 bytes, leaving only the methods "POST" and "HEAD" */
		switch (*method) {
		    case 'P':
			if (method[3] == 'T' && method[2] == 'S' && method[1] == 'O') {
			    type = EVHTTP_REQ_POST;
			}
			break;
		    case 'H':
			if (method[3] == 'D' && method[2] == 'A' && method[1] == 'E') {
			    type = EVHTTP_REQ_HEAD;
			}
			break;
		    default:
			break;
		}
		break;
	    case 5:
		/* Method length is 5 bytes, which can only encompass PATCH and TRACE */
		switch (*method) {
		    case 'P':
			if (method[4] == 'H' && method[3] == 'C' && method[2] == 'T' && method[1] == 'A') {
			    type = EVHTTP_REQ_PATCH;
			}
			break;
		    case 'T':
			if (method[4] == 'E' && method[3] == 'C' && method[2] == 'A' && method[1] == 'R') {
			    type = EVHTTP_REQ_TRACE;
			}
                    
			break;
		    default:
			break;
		}
		break;
	    case 6:
		/* Method length is 6, only valid method 6 bytes in length is DELEte */
            
		/* If the first byte isn't 'D' then it's invalid */
		if (*method != 'D') {
		    break;
		}

		if (method[5] == 'E' && method[4] == 'T' && method[3] == 'E' && method[2] == 'L' && method[1] == 'E') {
		    type = EVHTTP_REQ_DELETE;
		}

		break;
	    case 7:
		/* Method length is 7, only valid methods are "OPTIONS" and "CONNECT" */
		switch (*method) {
		    case 'O':
			if (method[6] == 'S' && method[5] == 'N' && method[4] == 'O' &&
				method[3] == 'I' && method[2] == 'T' && method[1] == 'P') {
			    type = EVHTTP_REQ_OPTIONS;
			}
                   
		       	break;
		    case 'C':
			if (method[6] == 'T' && method[5] == 'C' && method[4] == 'E' &&
				method[3] == 'N' && method[2] == 'N' && method[1] == 'O') {
			    type = EVHTTP_REQ_CONNECT;
			}
                    
			break;
		    default:
			break;
		}
		break;
	} /* switch */

	if ((int)type == EVHTTP_REQ_UNKNOWN_) {
	        event_debug(("%s: bad method %s on request %p from %s",
			__func__, method, req, req->remote_host));
                /* No error yet; we'll give a better error later when
                 * we see that req->type is unsupported. */
	}
	    
	req->type = type;

	if (evhttp_parse_http_version(version, req) < 0)
		return (-1);

	if ((req->uri = mm_strdup(uri)) == NULL) {
		event_debug(("%s: mm_strdup", __func__));
		return (-1);
	}

	if ((req->uri_elems = evhttp_uri_parse_with_flags(req->uri,
		    EVHTTP_URI_NONCONFORMANT)) == NULL) {
		return -1;
	}

	/* If we have an absolute-URI, check to see if it is an http request
	   for a known vhost or server alias. If we don't know about this
	   host, we consider it a proxy request. */
	scheme = evhttp_uri_get_scheme(req->uri_elems);
	hostname = evhttp_uri_get_host(req->uri_elems);
	if (scheme && (!evutil_ascii_strcasecmp(scheme, "http") ||
		       !evutil_ascii_strcasecmp(scheme, "https")) &&
	    hostname &&
	    !evhttp_find_vhost(req->evcon->http_server, NULL, hostname))
		req->flags |= EVHTTP_PROXY_REQUEST;

	return (0);
}

const char *
evhttp_find_header(const struct evkeyvalq *headers, const char *key)
{
	struct evkeyval *header;

	TAILQ_FOREACH(header, headers, next) {
		if (evutil_ascii_strcasecmp(header->key, key) == 0)
			return (header->value);
	}

	return (NULL);
}

void
evhttp_clear_headers(struct evkeyvalq *headers)
{
	struct evkeyval *header;

	for (header = TAILQ_FIRST(headers);
	    header != NULL;
	    header = TAILQ_FIRST(headers)) {
		TAILQ_REMOVE(headers, header, next);
		mm_free(header->key);
		mm_free(header->value);
		mm_free(header);
	}
}

/*
 * Returns 0,  if the header was successfully removed.
 * Returns -1, if the header could not be found.
 */

int
evhttp_remove_header(struct evkeyvalq *headers, const char *key)
{
	struct evkeyval *header;

	TAILQ_FOREACH(header, headers, next) {
		if (evutil_ascii_strcasecmp(header->key, key) == 0)
			break;
	}

	if (header == NULL)
		return (-1);

	/* Free and remove the header that we found */
	TAILQ_REMOVE(headers, header, next);
	mm_free(header->key);
	mm_free(header->value);
	mm_free(header);

	return (0);
}

static int
evhttp_header_is_valid_value(const char *value)
{
	const char *p = value;

	while ((p = strpbrk(p, "\r\n")) != NULL) {
		/* we really expect only one new line */
		p += strspn(p, "\r\n");
		/* we expect a space or tab for continuation */
		if (*p != ' ' && *p != '\t')
			return (0);
	}
	return (1);
}

int
evhttp_add_header(struct evkeyvalq *headers,
    const char *key, const char *value)
{
	event_debug(("%s: key: %s val: %s\n", __func__, key, value));

	if (strchr(key, '\r') != NULL || strchr(key, '\n') != NULL) {
		/* drop illegal headers */
		event_debug(("%s: dropping illegal header key\n", __func__));
		return (-1);
	}

	if (!evhttp_header_is_valid_value(value)) {
		event_debug(("%s: dropping illegal header value\n", __func__));
		return (-1);
	}

	return (evhttp_add_header_internal(headers, key, value));
}

static int
evhttp_add_header_internal(struct evkeyvalq *headers,
    const char *key, const char *value)
{
	struct evkeyval *header = mm_calloc(1, sizeof(struct evkeyval));
	if (header == NULL) {
		event_warn("%s: calloc", __func__);
		return (-1);
	}
	if ((header->key = mm_strdup(key)) == NULL) {
		mm_free(header);
		event_warn("%s: strdup", __func__);
		return (-1);
	}
	if ((header->value = mm_strdup(value)) == NULL) {
		mm_free(header->key);
		mm_free(header);
		event_warn("%s: strdup", __func__);
		return (-1);
	}

	TAILQ_INSERT_TAIL(headers, header, next);

	return (0);
}

/*
 * Parses header lines from a request or a response into the specified
 * request object given an event buffer.
 *
 * Returns
 *   DATA_CORRUPTED      on error
 *   MORE_DATA_EXPECTED  when we need to read more headers
 *   ALL_DATA_READ       when all headers have been read.
 */

enum message_read_status
evhttp_parse_firstline_(struct evhttp_request *req, struct evbuffer *buffer)
{
	char *line;
	enum message_read_status status = ALL_DATA_READ;

	size_t line_length;
	/* XXX try */
	line = evbuffer_readln(buffer, &line_length, EVBUFFER_EOL_CRLF);
	if (line == NULL) {
		if (req->evcon != NULL &&
		    evbuffer_get_length(buffer) > req->evcon->max_headers_size)
			return (DATA_TOO_LONG);
		else
			return (MORE_DATA_EXPECTED);
	}

	if (req->evcon != NULL &&
	    line_length > req->evcon->max_headers_size) {
		mm_free(line);
		return (DATA_TOO_LONG);
	}

	req->headers_size = line_length;

	switch (req->kind) {
	case EVHTTP_REQUEST:
		if (evhttp_parse_request_line(req, line) == -1)
			status = DATA_CORRUPTED;
		break;
	case EVHTTP_RESPONSE:
		if (evhttp_parse_response_line(req, line) == -1)
			status = DATA_CORRUPTED;
		break;
	default:
		status = DATA_CORRUPTED;
	}

	mm_free(line);
	return (status);
}

static int
evhttp_append_to_last_header(struct evkeyvalq *headers, char *line)
{
	struct evkeyval *header = TAILQ_LAST(headers, evkeyvalq);
	char *newval;
	size_t old_len, line_len;

	if (header == NULL)
		return (-1);

	old_len = strlen(header->value);

	/* Strip space from start and end of line. */
	while (*line == ' ' || *line == '\t')
		++line;
	evutil_rtrim_lws_(line);

	line_len = strlen(line);

	newval = mm_realloc(header->value, old_len + line_len + 2);
	if (newval == NULL)
		return (-1);

	newval[old_len] = ' ';
	memcpy(newval + old_len + 1, line, line_len + 1);
	header->value = newval;

	return (0);
}

enum message_read_status
evhttp_parse_headers_(struct evhttp_request *req, struct evbuffer* buffer)
{
	enum message_read_status errcode = DATA_CORRUPTED;
	char *line;
	enum message_read_status status = MORE_DATA_EXPECTED;

	struct evkeyvalq* headers = req->input_headers;
	size_t line_length;
	while ((line = evbuffer_readln(buffer, &line_length, EVBUFFER_EOL_CRLF))
	       != NULL) {
		char *skey, *svalue;

		req->headers_size += line_length;

		if (req->evcon != NULL &&
		    req->headers_size > req->evcon->max_headers_size) {
			errcode = DATA_TOO_LONG;
			goto error;
		}

		if (*line == '\0') { /* Last header - Done */
			status = ALL_DATA_READ;
			mm_free(line);
			break;
		}

		/* Check if this is a continuation line */
		if (*line == ' ' || *line == '\t') {
			if (evhttp_append_to_last_header(headers, line) == -1)
				goto error;
			mm_free(line);
			continue;
		}

		/* Processing of header lines */
		svalue = line;
		skey = strsep(&svalue, ":");
		if (svalue == NULL)
			goto error;

		svalue += strspn(svalue, " ");
		evutil_rtrim_lws_(svalue);

		if (evhttp_add_header(headers, skey, svalue) == -1)
			goto error;

		mm_free(line);
	}

	if (status == MORE_DATA_EXPECTED) {
		if (req->evcon != NULL &&
		req->headers_size + evbuffer_get_length(buffer) > req->evcon->max_headers_size)
			return (DATA_TOO_LONG);
	}

	return (status);

 error:
	mm_free(line);
	return (errcode);
}

static int
evhttp_get_body_length(struct evhttp_request *req)
{
	struct evkeyvalq *headers = req->input_headers;
	const char *content_length;
	const char *connection;

	content_length = evhttp_find_header(headers, "Content-Length");
	connection = evhttp_find_header(headers, "Connection");

	if (content_length == NULL && connection == NULL)
		req->ntoread = -1;
	else if (content_length == NULL &&
	    evutil_ascii_strcasecmp(connection, "Close") != 0) {
		/* Bad combination, we don't know when it will end */
		event_warnx("%s: we got no content length, but the "
		    "server wants to keep the connection open: %s.",
		    __func__, connection);
		return (-1);
	} else if (content_length == NULL) {
		req->ntoread = -1;
	} else {
		char *endp;
		ev_int64_t ntoread = evutil_strtoll(content_length, &endp, 10);
		if (*content_length == '\0' || *endp != '\0' || ntoread < 0) {
			event_debug(("%s: illegal content length: %s",
				__func__, content_length));
			return (-1);
		}
		req->ntoread = ntoread;
	}

	event_debug(("%s: bytes to read: "EV_I64_FMT" (in buffer "EV_SIZE_FMT")\n",
		__func__, EV_I64_ARG(req->ntoread),
		EV_SIZE_ARG(evbuffer_get_length(bufferevent_get_input(req->evcon->bufev)))));

	return (0);
}

static int
evhttp_method_may_have_body(enum evhttp_cmd_type type)
{
	switch (type) {
	case EVHTTP_REQ_POST:
	case EVHTTP_REQ_PUT:
	case EVHTTP_REQ_PATCH:
		return 1;
	case EVHTTP_REQ_TRACE:
		return 0;
	/* XXX May any of the below methods have a body? */
	case EVHTTP_REQ_GET:
	case EVHTTP_REQ_HEAD:
	case EVHTTP_REQ_DELETE:
	case EVHTTP_REQ_OPTIONS:
	case EVHTTP_REQ_CONNECT:
		return 0;
	default:
		return 0;
	}
}

static void
evhttp_get_body(struct evhttp_connection *evcon, struct evhttp_request *req)
{
	const char *xfer_enc;

	/* If this is a request without a body, then we are done */
	if (req->kind == EVHTTP_REQUEST &&
	    !evhttp_method_may_have_body(req->type)) {
		evhttp_connection_done(evcon);
		return;
	}
	evcon->state = EVCON_READING_BODY;
	xfer_enc = evhttp_find_header(req->input_headers, "Transfer-Encoding");
	if (xfer_enc != NULL && evutil_ascii_strcasecmp(xfer_enc, "chunked") == 0) {
		req->chunked = 1;
		req->ntoread = -1;
	} else {
		if (evhttp_get_body_length(req) == -1) {
			evhttp_connection_fail_(evcon, EVREQ_HTTP_INVALID_HEADER);
			return;
		}
		if (req->kind == EVHTTP_REQUEST && req->ntoread < 1) {
			/* An incoming request with no content-length and no
			 * transfer-encoding has no body. */
			evhttp_connection_done(evcon);
			return;
		}
	}

	/* Should we send a 100 Continue status line? */
	switch (evhttp_have_expect(req, 1)) {
		case CONTINUE:
				/* XXX It would be nice to do some sanity
				   checking here. Does the resource exist?
				   Should the resource accept post requests? If
				   no, we should respond with an error. For
				   now, just optimistically tell the client to
				   send their message body. */
				if (req->ntoread > 0) {
					/* ntoread is ev_int64_t, max_body_size is ev_uint64_t */ 
					if ((req->evcon->max_body_size <= EV_INT64_MAX) &&
						(ev_uint64_t)req->ntoread > req->evcon->max_body_size) {
						evhttp_lingering_fail(evcon, req);
						return;
					}
				}
				if (!evbuffer_get_length(bufferevent_get_input(evcon->bufev)))
					evhttp_send_continue(evcon, req);
			break;
		case OTHER:
			evhttp_send_error(req, HTTP_EXPECTATIONFAILED, NULL);
			return;
		case NO: break;
	}

	evhttp_read_body(evcon, req);
	/* note the request may have been freed in evhttp_read_body */
}

static void
evhttp_read_firstline(struct evhttp_connection *evcon,
		      struct evhttp_request *req)
{
	enum message_read_status res;

	res = evhttp_parse_firstline_(req, bufferevent_get_input(evcon->bufev));
	if (res == DATA_CORRUPTED || res == DATA_TOO_LONG) {
		/* Error while reading, terminate */
		event_debug(("%s: bad header lines on "EV_SOCK_FMT"\n",
			__func__, EV_SOCK_ARG(evcon->fd)));
		evhttp_connection_fail_(evcon, EVREQ_HTTP_INVALID_HEADER);
		return;
	} else if (res == MORE_DATA_EXPECTED) {
		/* Need more header lines */
		return;
	}

	evcon->state = EVCON_READING_HEADERS;
	evhttp_read_header(evcon, req);
}

static void
evhttp_read_header(struct evhttp_connection *evcon,
		   struct evhttp_request *req)
{
	enum message_read_status res;
	evutil_socket_t fd = evcon->fd;

	res = evhttp_parse_headers_(req, bufferevent_get_input(evcon->bufev));
	if (res == DATA_CORRUPTED || res == DATA_TOO_LONG) {
		/* Error while reading, terminate */
		event_debug(("%s: bad header lines on "EV_SOCK_FMT"\n",
			__func__, EV_SOCK_ARG(fd)));
		evhttp_connection_fail_(evcon, EVREQ_HTTP_INVALID_HEADER);
		return;
	} else if (res == MORE_DATA_EXPECTED) {
		/* Need more header lines */
		return;
	}

	/* Callback can shut down connection with negative return value */
	if (req->header_cb != NULL) {
		if ((*req->header_cb)(req, req->cb_arg) < 0) {
			evhttp_connection_fail_(evcon, EVREQ_HTTP_EOF);
			return;
		}
	}

	/* Done reading headers, do the real work */
	switch (req->kind) {
	case EVHTTP_REQUEST:
		event_debug(("%s: checking for post data on "EV_SOCK_FMT"\n",
			__func__, EV_SOCK_ARG(fd)));
		evhttp_get_body(evcon, req);
		/* note the request may have been freed in evhttp_get_body */
		break;

	case EVHTTP_RESPONSE:
		/* Start over if we got a 100 Continue response. */
		if (req->response_code == 100) {
			struct evbuffer *output = bufferevent_get_output(evcon->bufev);
			evbuffer_add_buffer(output, req->output_buffer);
			evhttp_start_write_(evcon);
			return;
		}
		if (!evhttp_response_needs_body(req)) {
			event_debug(("%s: skipping body for code %d\n",
					__func__, req->response_code));
			evhttp_connection_done(evcon);
		} else {
			event_debug(("%s: start of read body for %s on "
				EV_SOCK_FMT"\n",
				__func__, req->remote_host, EV_SOCK_ARG(fd)));
			evhttp_get_body(evcon, req);
			/* note the request may have been freed in
			 * evhttp_get_body */
		}
		break;

	default:
		event_warnx("%s: bad header on "EV_SOCK_FMT, __func__,
		    EV_SOCK_ARG(fd));
		evhttp_connection_fail_(evcon, EVREQ_HTTP_INVALID_HEADER);
		break;
	}
	/* request may have been freed above */
}

/*
 * Creates a TCP connection to the specified port and executes a callback
 * when finished.  Failure or success is indicate by the passed connection
 * object.
 *
 * Although this interface accepts a hostname, it is intended to take
 * only numeric hostnames so that non-blocking DNS resolution can
 * happen elsewhere.
 */

struct evhttp_connection *
evhttp_connection_new(const char *address, ev_uint16_t port)
{
	return (evhttp_connection_base_new(NULL, NULL, address, port));
}

struct evhttp_connection *
evhttp_connection_base_bufferevent_new(struct event_base *base, struct evdns_base *dnsbase, struct bufferevent* bev,
    const char *address, ev_uint16_t port)
{
	struct evhttp_connection *evcon = NULL;

	event_debug(("Attempting connection to %s:%d\n", address, port));

	if ((evcon = mm_calloc(1, sizeof(struct evhttp_connection))) == NULL) {
		event_warn("%s: calloc failed", __func__);
		goto error;
	}

	evcon->fd = -1;
	evcon->port = port;

	evcon->max_headers_size = EV_SIZE_MAX;
	evcon->max_body_size = EV_SIZE_MAX;

	evutil_timerclear(&evcon->timeout);
	evcon->retry_cnt = evcon->retry_max = 0;

	if ((evcon->address = mm_strdup(address)) == NULL) {
		event_warn("%s: strdup failed", __func__);
		goto error;
	}

	if (bev == NULL) {
		if (!(bev = bufferevent_socket_new(base, -1, 0))) {
			event_warn("%s: bufferevent_socket_new failed", __func__);
			goto error;
		}
	}

	bufferevent_setcb(bev, evhttp_read_cb, evhttp_write_cb, evhttp_error_cb, evcon);
	evcon->bufev = bev;

	evcon->state = EVCON_DISCONNECTED;
	TAILQ_INIT(&evcon->requests);

	evcon->initial_retry_timeout.tv_sec = 2;
	evcon->initial_retry_timeout.tv_usec = 0;

	if (base != NULL) {
		evcon->base = base;
		if (bufferevent_get_base(bev) != base)
			bufferevent_base_set(base, evcon->bufev);
	}

	event_deferred_cb_init_(
	    &evcon->read_more_deferred_cb,
	    bufferevent_get_priority(bev),
	    evhttp_deferred_read_cb, evcon);

	evcon->dns_base = dnsbase;
	evcon->ai_family = AF_UNSPEC;

	return (evcon);

 error:
	if (evcon != NULL)
		evhttp_connection_free(evcon);
	return (NULL);
}

struct bufferevent* evhttp_connection_get_bufferevent(struct evhttp_connection *evcon)
{
	return evcon->bufev;
}

struct evhttp *
evhttp_connection_get_server(struct evhttp_connection *evcon)
{
	return evcon->http_server;
}

struct evhttp_connection *
evhttp_connection_base_new(struct event_base *base, struct evdns_base *dnsbase,
    const char *address, ev_uint16_t port)
{
	return evhttp_connection_base_bufferevent_new(base, dnsbase, NULL, address, port);
}

void evhttp_connection_set_family(struct evhttp_connection *evcon,
	int family)
{
	evcon->ai_family = family;
}

int evhttp_connection_set_flags(struct evhttp_connection *evcon,
	int flags)
{
	int avail_flags = 0;
	avail_flags |= EVHTTP_CON_REUSE_CONNECTED_ADDR;
	avail_flags |= EVHTTP_CON_READ_ON_WRITE_ERROR;

	if (flags & ~avail_flags || flags > EVHTTP_CON_PUBLIC_FLAGS_END)
		return 1;
	evcon->flags &= ~avail_flags;

	evcon->flags |= flags;

	return 0;
}

void
evhttp_connection_set_base(struct evhttp_connection *evcon,
    struct event_base *base)
{
	EVUTIL_ASSERT(evcon->base == NULL);
	EVUTIL_ASSERT(evcon->state == EVCON_DISCONNECTED);
	evcon->base = base;
	bufferevent_base_set(base, evcon->bufev);
}

void
evhttp_connection_set_timeout(struct evhttp_connection *evcon,
    int timeout_in_secs)
{
	if (timeout_in_secs == -1)
		evhttp_connection_set_timeout_tv(evcon, NULL);
	else {
		struct timeval tv;
		tv.tv_sec = timeout_in_secs;
		tv.tv_usec = 0;
		evhttp_connection_set_timeout_tv(evcon, &tv);
	}
}

void
evhttp_connection_set_timeout_tv(struct evhttp_connection *evcon,
    const struct timeval* tv)
{
	if (tv) {
		evcon->timeout = *tv;
		bufferevent_set_timeouts(evcon->bufev, &evcon->timeout, &evcon->timeout);
	} else {
		const struct timeval read_tv = { HTTP_READ_TIMEOUT, 0 };
		const struct timeval write_tv = { HTTP_WRITE_TIMEOUT, 0 };
		evutil_timerclear(&evcon->timeout);
		bufferevent_set_timeouts(evcon->bufev, &read_tv, &write_tv);
	}
}

void
evhttp_connection_set_initial_retry_tv(struct evhttp_connection *evcon,
    const struct timeval *tv)
{
	if (tv) {
		evcon->initial_retry_timeout = *tv;
	} else {
		evutil_timerclear(&evcon->initial_retry_timeout);
		evcon->initial_retry_timeout.tv_sec = 2;
	}
}

void
evhttp_connection_set_retries(struct evhttp_connection *evcon,
    int retry_max)
{
	evcon->retry_max = retry_max;
}

void
evhttp_connection_set_closecb(struct evhttp_connection *evcon,
    void (*cb)(struct evhttp_connection *, void *), void *cbarg)
{
	evcon->closecb = cb;
	evcon->closecb_arg = cbarg;
}

void
evhttp_connection_get_peer(struct evhttp_connection *evcon,
    char **address, ev_uint16_t *port)
{
	*address = evcon->address;
	*port = evcon->port;
}

const struct sockaddr*
evhttp_connection_get_addr(struct evhttp_connection *evcon)
{
	return bufferevent_socket_get_conn_address_(evcon->bufev);
}

int
evhttp_connection_connect_(struct evhttp_connection *evcon)
{
	int old_state = evcon->state;
	const char *address = evcon->address;
	const struct sockaddr *sa = evhttp_connection_get_addr(evcon);
	int ret;

	if (evcon->state == EVCON_CONNECTING)
		return (0);

	evhttp_connection_reset_(evcon);

	EVUTIL_ASSERT(!(evcon->flags & EVHTTP_CON_INCOMING));
	evcon->flags |= EVHTTP_CON_OUTGOING;

	if (evcon->bind_address || evcon->bind_port) {
		evcon->fd = bind_socket(
			evcon->bind_address, evcon->bind_port, 0 /*reuse*/);
		if (evcon->fd == -1) {
			event_debug(("%s: failed to bind to \"%s\"",
				__func__, evcon->bind_address));
			return (-1);
		}

		bufferevent_setfd(evcon->bufev, evcon->fd);
	} else {
		bufferevent_setfd(evcon->bufev, -1);
	}

	/* Set up a callback for successful connection setup */
	bufferevent_setcb(evcon->bufev,
	    NULL /* evhttp_read_cb */,
	    NULL /* evhttp_write_cb */,
	    evhttp_connection_cb,
	    evcon);
	if (!evutil_timerisset(&evcon->timeout)) {
		const struct timeval conn_tv = { HTTP_CONNECT_TIMEOUT, 0 };
		bufferevent_set_timeouts(evcon->bufev, &conn_tv, &conn_tv);
	} else {
		bufferevent_set_timeouts(evcon->bufev, &evcon->timeout, &evcon->timeout);
	}
	/* make sure that we get a write callback */
	bufferevent_enable(evcon->bufev, EV_WRITE);

	evcon->state = EVCON_CONNECTING;

	if (evcon->flags & EVHTTP_CON_REUSE_CONNECTED_ADDR &&
		sa &&
		(sa->sa_family == AF_INET || sa->sa_family == AF_INET6)) {
		int socklen = sizeof(struct sockaddr_in);
		if (sa->sa_family == AF_INET6) {
			socklen = sizeof(struct sockaddr_in6);
		}
		ret = bufferevent_socket_connect(evcon->bufev, sa, socklen);
	} else {
		ret = bufferevent_socket_connect_hostname(evcon->bufev,
				evcon->dns_base, evcon->ai_family, address, evcon->port);
	}

	if (ret < 0) {
		evcon->state = old_state;
		event_sock_warn(evcon->fd, "%s: connection to \"%s\" failed",
		    __func__, evcon->address);
		/* some operating systems return ECONNREFUSED immediately
		 * when connecting to a local address.  the cleanup is going
		 * to reschedule this function call.
		 */
		evhttp_connection_cb_cleanup(evcon);
		return (0);
	}

	return (0);
}

/*
 * Starts an HTTP request on the provided evhttp_connection object.
 * If the connection object is not connected to the web server already,
 * this will start the connection.
 */

int
evhttp_make_request(struct evhttp_connection *evcon,
    struct evhttp_request *req,
    enum evhttp_cmd_type type, const char *uri)
{
	/* We are making a request */
	req->kind = EVHTTP_REQUEST;
	req->type = type;
	if (req->uri != NULL)
		mm_free(req->uri);
	if ((req->uri = mm_strdup(uri)) == NULL) {
		event_warn("%s: strdup", __func__);
		evhttp_request_free_auto(req);
		return (-1);
	}

	/* Set the protocol version if it is not supplied */
	if (!req->major && !req->minor) {
		req->major = 1;
		req->minor = 1;
	}

	EVUTIL_ASSERT(req->evcon == NULL);
	req->evcon = evcon;
	EVUTIL_ASSERT(!(req->flags & EVHTTP_REQ_OWN_CONNECTION));

	TAILQ_INSERT_TAIL(&evcon->requests, req, next);

	/* If the connection object is not connected; make it so */
	if (!evhttp_connected(evcon)) {
		int res = evhttp_connection_connect_(evcon);
		/* evhttp_connection_fail_(), which is called through
		 * evhttp_connection_connect_(), assumes that req lies in
		 * evcon->requests.  Thus, enqueue the request in advance and
		 * remove it in the error case. */
		if (res != 0)
			TAILQ_REMOVE(&evcon->requests, req, next);

		return res;
	}

	/*
	 * If it's connected already and we are the first in the queue,
	 * then we can dispatch this request immediately.  Otherwise, it
	 * will be dispatched once the pending requests are completed.
	 */
	if (TAILQ_FIRST(&evcon->requests) == req)
		evhttp_request_dispatch(evcon);

	return (0);
}

void
evhttp_cancel_request(struct evhttp_request *req)
{
	struct evhttp_connection *evcon = req->evcon;
	if (evcon != NULL) {
		/* We need to remove it from the connection */
		if (TAILQ_FIRST(&evcon->requests) == req) {
			/* it's currently being worked on, so reset
			 * the connection.
			 */
			evhttp_connection_fail_(evcon,
			    EVREQ_HTTP_REQUEST_CANCEL);

			/* connection fail freed the request */
			return;
		} else {
			/* otherwise, we can just remove it from the
			 * queue
			 */
			TAILQ_REMOVE(&evcon->requests, req, next);
		}
	}

	evhttp_request_free_auto(req);
}

/*
 * Reads data from file descriptor into request structure
 * Request structure needs to be set up correctly.
 */

void
evhttp_start_read_(struct evhttp_connection *evcon)
{
	bufferevent_disable(evcon->bufev, EV_WRITE);
	bufferevent_enable(evcon->bufev, EV_READ);

	evcon->state = EVCON_READING_FIRSTLINE;
	/* Reset the bufferevent callbacks */
	bufferevent_setcb(evcon->bufev,
	    evhttp_read_cb,
	    evhttp_write_cb,
	    evhttp_error_cb,
	    evcon);

	/* If there's still data pending, process it next time through the
	 * loop.  Don't do it now; that could get recusive. */
	if (evbuffer_get_length(bufferevent_get_input(evcon->bufev))) {
		event_deferred_cb_schedule_(get_deferred_queue(evcon),
		    &evcon->read_more_deferred_cb);
	}
}

void
evhttp_start_write_(struct evhttp_connection *evcon)
{
	bufferevent_disable(evcon->bufev, EV_WRITE);
	bufferevent_enable(evcon->bufev, EV_READ);

	evcon->state = EVCON_WRITING;
	evhttp_write_buffer(evcon, evhttp_write_connectioncb, NULL);
}

static void
evhttp_send_done(struct evhttp_connection *evcon, void *arg)
{
	int need_close;
	struct evhttp_request *req = TAILQ_FIRST(&evcon->requests);
	TAILQ_REMOVE(&evcon->requests, req, next);

	if (req->on_complete_cb != NULL) {
		req->on_complete_cb(req, req->on_complete_cb_arg);
	}

	need_close =
	    (REQ_VERSION_BEFORE(req, 1, 1) &&
	    !evhttp_is_connection_keepalive(req->input_headers)) ||
	    evhttp_is_request_connection_close(req);

	EVUTIL_ASSERT(req->flags & EVHTTP_REQ_OWN_CONNECTION);
	evhttp_request_free(req);

	if (need_close) {
		evhttp_connection_free(evcon);
		return;
	}

	/* we have a persistent connection; try to accept another request. */
	if (evhttp_associate_new_request_with_connection(evcon) == -1) {
		evhttp_connection_free(evcon);
	}
}

/*
 * Returns an error page.
 */

void
evhttp_send_error(struct evhttp_request *req, int error, const char *reason)
{

#define ERR_FORMAT "<HTML><HEAD>\n" \
	    "<TITLE>%d %s</TITLE>\n" \
	    "</HEAD><BODY>\n" \
	    "<H1>%s</H1>\n" \
	    "</BODY></HTML>\n"

	struct evbuffer *buf = evbuffer_new();
	if (buf == NULL) {
		/* if we cannot allocate memory; we just drop the connection */
		evhttp_connection_free(req->evcon);
		return;
	}
	if (reason == NULL) {
		reason = evhttp_response_phrase_internal(error);
	}

	evhttp_response_code_(req, error, reason);

	evbuffer_add_printf(buf, ERR_FORMAT, error, reason, reason);

	evhttp_send_page_(req, buf);

	evbuffer_free(buf);
#undef ERR_FORMAT
}

/* Requires that headers and response code are already set up */

static inline void
evhttp_send(struct evhttp_request *req, struct evbuffer *databuf)
{
	struct evhttp_connection *evcon = req->evcon;

	if (evcon == NULL) {
		evhttp_request_free(req);
		return;
	}

	EVUTIL_ASSERT(TAILQ_FIRST(&evcon->requests) == req);

	/* we expect no more calls form the user on this request */
	req->userdone = 1;

	/* xxx: not sure if we really should expose the data buffer this way */
	if (databuf != NULL)
		evbuffer_add_buffer(req->output_buffer, databuf);

	/* Adds headers to the response */
	evhttp_make_header(evcon, req);

	evhttp_write_buffer(evcon, evhttp_send_done, NULL);
}

void
evhttp_send_reply(struct evhttp_request *req, int code, const char *reason,
    struct evbuffer *databuf)
{
	evhttp_response_code_(req, code, reason);

	evhttp_send(req, databuf);
}

void
evhttp_send_reply_start(struct evhttp_request *req, int code,
    const char *reason)
{
	evhttp_response_code_(req, code, reason);
	if (evhttp_find_header(req->output_headers, "Content-Length") == NULL &&
	    REQ_VERSION_ATLEAST(req, 1, 1) &&
	    evhttp_response_needs_body(req)) {
		/*
		 * prefer HTTP/1.1 chunked encoding to closing the connection;
		 * note RFC 2616 section 4.4 forbids it with Content-Length:
		 * and it's not necessary then anyway.
		 */
		evhttp_add_header(req->output_headers, "Transfer-Encoding",
		    "chunked");
		req->chunked = 1;
	} else {
		req->chunked = 0;
	}
	evhttp_make_header(req->evcon, req);
	evhttp_write_buffer(req->evcon, NULL, NULL);
}

void
evhttp_send_reply_chunk_with_cb(struct evhttp_request *req, struct evbuffer *databuf,
    void (*cb)(struct evhttp_connection *, void *), void *arg)
{
	struct evhttp_connection *evcon = req->evcon;
	struct evbuffer *output;

	if (evcon == NULL)
		return;

	output = bufferevent_get_output(evcon->bufev);

	if (evbuffer_get_length(databuf) == 0)
		return;
	if (!evhttp_response_needs_body(req))
		return;
	if (req->chunked) {
		evbuffer_add_printf(output, "%x\r\n",
				    (unsigned)evbuffer_get_length(databuf));
	}
	evbuffer_add_buffer(output, databuf);
	if (req->chunked) {
		evbuffer_add(output, "\r\n", 2);
	}
	evhttp_write_buffer(evcon, cb, arg);
}

void
evhttp_send_reply_chunk(struct evhttp_request *req, struct evbuffer *databuf)
{
	evhttp_send_reply_chunk_with_cb(req, databuf, NULL, NULL);
}
void
evhttp_send_reply_end(struct evhttp_request *req)
{
	struct evhttp_connection *evcon = req->evcon;
	struct evbuffer *output;

	if (evcon == NULL) {
		evhttp_request_free(req);
		return;
	}

	output = bufferevent_get_output(evcon->bufev);

	/* we expect no more calls form the user on this request */
	req->userdone = 1;

	if (req->chunked) {
		evbuffer_add(output, "0\r\n\r\n", 5);
		evhttp_write_buffer(req->evcon, evhttp_send_done, NULL);
		req->chunked = 0;
	} else if (evbuffer_get_length(output) == 0) {
		/* let the connection know that we are done with the request */
		evhttp_send_done(evcon, NULL);
	} else {
		/* make the callback execute after all data has been written */
		evcon->cb = evhttp_send_done;
		evcon->cb_arg = NULL;
	}
}

static const char *informational_phrases[] = {
	/* 100 */ "Continue",
	/* 101 */ "Switching Protocols"
};

static const char *success_phrases[] = {
	/* 200 */ "OK",
	/* 201 */ "Created",
	/* 202 */ "Accepted",
	/* 203 */ "Non-Authoritative Information",
	/* 204 */ "No Content",
	/* 205 */ "Reset Content",
	/* 206 */ "Partial Content"
};

static const char *redirection_phrases[] = {
	/* 300 */ "Multiple Choices",
	/* 301 */ "Moved Permanently",
	/* 302 */ "Found",
	/* 303 */ "See Other",
	/* 304 */ "Not Modified",
	/* 305 */ "Use Proxy",
	/* 307 */ "Temporary Redirect"
};

static const char *client_error_phrases[] = {
	/* 400 */ "Bad Request",
	/* 401 */ "Unauthorized",
	/* 402 */ "Payment Required",
	/* 403 */ "Forbidden",
	/* 404 */ "Not Found",
	/* 405 */ "Method Not Allowed",
	/* 406 */ "Not Acceptable",
	/* 407 */ "Proxy Authentication Required",
	/* 408 */ "Request Time-out",
	/* 409 */ "Conflict",
	/* 410 */ "Gone",
	/* 411 */ "Length Required",
	/* 412 */ "Precondition Failed",
	/* 413 */ "Request Entity Too Large",
	/* 414 */ "Request-URI Too Large",
	/* 415 */ "Unsupported Media Type",
	/* 416 */ "Requested range not satisfiable",
	/* 417 */ "Expectation Failed"
};

static const char *server_error_phrases[] = {
	/* 500 */ "Internal Server Error",
	/* 501 */ "Not Implemented",
	/* 502 */ "Bad Gateway",
	/* 503 */ "Service Unavailable",
	/* 504 */ "Gateway Time-out",
	/* 505 */ "HTTP Version not supported"
};

struct response_class {
	const char *name;
	size_t num_responses;
	const char **responses;
};

#ifndef MEMBERSOF
#define MEMBERSOF(x) (sizeof(x)/sizeof(x[0]))
#endif

static const struct response_class response_classes[] = {
	/* 1xx */ { "Informational", MEMBERSOF(informational_phrases), informational_phrases },
	/* 2xx */ { "Success", MEMBERSOF(success_phrases), success_phrases },
	/* 3xx */ { "Redirection", MEMBERSOF(redirection_phrases), redirection_phrases },
	/* 4xx */ { "Client Error", MEMBERSOF(client_error_phrases), client_error_phrases },
	/* 5xx */ { "Server Error", MEMBERSOF(server_error_phrases), server_error_phrases }
};

static const char *
evhttp_response_phrase_internal(int code)
{
	int klass = code / 100 - 1;
	int subcode = code % 100;

	/* Unknown class - can't do any better here */
	if (klass < 0 || klass >= (int) MEMBERSOF(response_classes))
		return "Unknown Status Class";

	/* Unknown sub-code, return class name at least */
	if (subcode >= (int) response_classes[klass].num_responses)
		return response_classes[klass].name;

	return response_classes[klass].responses[subcode];
}

void
evhttp_response_code_(struct evhttp_request *req, int code, const char *reason)
{
	req->kind = EVHTTP_RESPONSE;
	req->response_code = code;
	if (req->response_code_line != NULL)
		mm_free(req->response_code_line);
	if (reason == NULL)
		reason = evhttp_response_phrase_internal(code);
	req->response_code_line = mm_strdup(reason);
	if (req->response_code_line == NULL) {
		event_warn("%s: strdup", __func__);
		/* XXX what else can we do? */
	}
}

void
evhttp_send_page_(struct evhttp_request *req, struct evbuffer *databuf)
{
	if (!req->major || !req->minor) {
		req->major = 1;
		req->minor = 1;
	}

	if (req->kind != EVHTTP_RESPONSE)
		evhttp_response_code_(req, 200, "OK");

	evhttp_clear_headers(req->output_headers);
	evhttp_add_header(req->output_headers, "Content-Type", "text/html");
	evhttp_add_header(req->output_headers, "Connection", "close");

	evhttp_send(req, databuf);
}

static const char uri_chars[256] = {
	/* 0 */
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 1, 1, 0,
	1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 0, 0, 0, 0, 0, 0,
	/* 64 */
	0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 0, 1,
	0, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,   1, 1, 1, 0, 0, 0, 1, 0,
	/* 128 */
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	/* 192 */
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,   0, 0, 0, 0, 0, 0, 0, 0,
};

#define CHAR_IS_UNRESERVED(c)			\
	(uri_chars[(unsigned char)(c)])

/*
 * Helper functions to encode/decode a string for inclusion in a URI.
 * The returned string must be freed by the caller.
 */
char *
evhttp_uriencode(const char *uri, ev_ssize_t len, int space_as_plus)
{
	struct evbuffer *buf = evbuffer_new();
	const char *p, *end;
	char *result;

	if (buf == NULL) {
		return (NULL);
	}


	if (len >= 0) {
		if (uri + len < uri) {
			return (NULL);
		}

		end = uri + len;
	} else {
		size_t slen = strlen(uri);

		if (slen >= EV_SSIZE_MAX) {
			/* we don't want to mix signed and unsigned */
			return (NULL);
		}

		if (uri + slen < uri) {
			return (NULL);
		}

		end = uri + slen;
	}

	for (p = uri; p < end; p++) {
		if (CHAR_IS_UNRESERVED(*p)) {
			evbuffer_add(buf, p, 1);
		} else if (*p == ' ' && space_as_plus) {
			evbuffer_add(buf, "+", 1);
		} else {
			evbuffer_add_printf(buf, "%%%02X", (unsigned char)(*p));
		}
	}

	evbuffer_add(buf, "", 1); /* NUL-terminator. */
	result = mm_malloc(evbuffer_get_length(buf));

	if (result)
		evbuffer_remove(buf, result, evbuffer_get_length(buf));

	evbuffer_free(buf);

	return (result);
}

char *
evhttp_encode_uri(const char *str)
{
	return evhttp_uriencode(str, -1, 0);
}

/*
 * @param decode_plus_ctl: if 1, we decode plus into space.  If 0, we don't.
 *     If -1, when true we transform plus to space only after we've seen
 *     a ?.  -1 is deprecated.
 * @return the number of bytes written to 'ret'.
 */
int
evhttp_decode_uri_internal(
	const char *uri, size_t length, char *ret, int decode_plus_ctl)
{
	char c;
	int j;
	int decode_plus = (decode_plus_ctl == 1) ? 1: 0;
	unsigned i;

	for (i = j = 0; i < length; i++) {
		c = uri[i];
		if (c == '?') {
			if (decode_plus_ctl < 0)
				decode_plus = 1;
		} else if (c == '+' && decode_plus) {
			c = ' ';
		} else if ((i + 2) < length && c == '%' &&
			EVUTIL_ISXDIGIT_(uri[i+1]) && EVUTIL_ISXDIGIT_(uri[i+2])) {
			char tmp[3];
			tmp[0] = uri[i+1];
			tmp[1] = uri[i+2];
			tmp[2] = '\0';
			c = (char)strtol(tmp, NULL, 16);
			i += 2;
		}
		ret[j++] = c;
	}
	ret[j] = '\0';

	return (j);
}

/* deprecated */
char *
evhttp_decode_uri(const char *uri)
{
	char *ret;

	if ((ret = mm_malloc(strlen(uri) + 1)) == NULL) {
		event_warn("%s: malloc(%lu)", __func__,
			  (unsigned long)(strlen(uri) + 1));
		return (NULL);
	}

	evhttp_decode_uri_internal(uri, strlen(uri),
	    ret, -1 /*always_decode_plus*/);

	return (ret);
}

char *
evhttp_uridecode(const char *uri, int decode_plus, size_t *size_out)
{
	char *ret;
	int n;

	if ((ret = mm_malloc(strlen(uri) + 1)) == NULL) {
		event_warn("%s: malloc(%lu)", __func__,
			  (unsigned long)(strlen(uri) + 1));
		return (NULL);
	}

	n = evhttp_decode_uri_internal(uri, strlen(uri),
	    ret, !!decode_plus/*always_decode_plus*/);

	if (size_out) {
		EVUTIL_ASSERT(n >= 0);
		*size_out = (size_t)n;
	}

	return (ret);
}

/*
 * Helper function to parse out arguments in a query.
 * The arguments are separated by key and value.
 */

static int
evhttp_parse_query_impl(const char *str, struct evkeyvalq *headers,
    int is_whole_uri)
{
	char *line=NULL;
	char *argument;
	char *p;
	const char *query_part;
	int result = -1;
	struct evhttp_uri *uri=NULL;

	TAILQ_INIT(headers);

	if (is_whole_uri) {
		uri = evhttp_uri_parse(str);
		if (!uri)
			goto error;
		query_part = evhttp_uri_get_query(uri);
	} else {
		query_part = str;
	}

	/* No arguments - we are done */
	if (!query_part || !strlen(query_part)) {
		result = 0;
		goto done;
	}

	if ((line = mm_strdup(query_part)) == NULL) {
		event_warn("%s: strdup", __func__);
		goto error;
	}

	p = argument = line;
	while (p != NULL && *p != '\0') {
		char *key, *value, *decoded_value;
		argument = strsep(&p, "&");

		value = argument;
		key = strsep(&value, "=");
		if (value == NULL || *key == '\0') {
			goto error;
		}

		if ((decoded_value = mm_malloc(strlen(value) + 1)) == NULL) {
			event_warn("%s: mm_malloc", __func__);
			goto error;
		}
		evhttp_decode_uri_internal(value, strlen(value),
		    decoded_value, 1 /*always_decode_plus*/);
		event_debug(("Query Param: %s -> %s\n", key, decoded_value));
		evhttp_add_header_internal(headers, key, decoded_value);
		mm_free(decoded_value);
	}

	result = 0;
	goto done;
error:
	evhttp_clear_headers(headers);
done:
	if (line)
		mm_free(line);
	if (uri)
		evhttp_uri_free(uri);
	return result;
}

int
evhttp_parse_query(const char *uri, struct evkeyvalq *headers)
{
	return evhttp_parse_query_impl(uri, headers, 1);
}
int
evhttp_parse_query_str(const char *uri, struct evkeyvalq *headers)
{
	return evhttp_parse_query_impl(uri, headers, 0);
}

static struct evhttp_cb *
evhttp_dispatch_callback(struct httpcbq *callbacks, struct evhttp_request *req)
{
	struct evhttp_cb *cb;
	size_t offset = 0;
	char *translated;
	const char *path;

	/* Test for different URLs */
	path = evhttp_uri_get_path(req->uri_elems);
	offset = strlen(path);
	if ((translated = mm_malloc(offset + 1)) == NULL)
		return (NULL);
	evhttp_decode_uri_internal(path, offset, translated,
	    0 /* decode_plus */);

	TAILQ_FOREACH(cb, callbacks, next) {
		if (!strcmp(cb->what, translated)) {
			mm_free(translated);
			return (cb);
		}
	}

	mm_free(translated);
	return (NULL);
}


static int
prefix_suffix_match(const char *pattern, const char *name, int ignorecase)
{
	char c;

	while (1) {
		switch (c = *pattern++) {
		case '\0':
			return *name == '\0';

		case '*':
			while (*name != '\0') {
				if (prefix_suffix_match(pattern, name,
					ignorecase))
					return (1);
				++name;
			}
			return (0);
		default:
			if (c != *name) {
				if (!ignorecase ||
				    EVUTIL_TOLOWER_(c) != EVUTIL_TOLOWER_(*name))
					return (0);
			}
			++name;
		}
	}
	/* NOTREACHED */
}

/*
   Search the vhost hierarchy beginning with http for a server alias
   matching hostname.  If a match is found, and outhttp is non-null,
   outhttp is set to the matching http object and 1 is returned.
*/

static int
evhttp_find_alias(struct evhttp *http, struct evhttp **outhttp,
		  const char *hostname)
{
	struct evhttp_server_alias *alias;
	struct evhttp *vhost;

	TAILQ_FOREACH(alias, &http->aliases, next) {
		/* XXX Do we need to handle IP addresses? */
		if (!evutil_ascii_strcasecmp(alias->alias, hostname)) {
			if (outhttp)
				*outhttp = http;
			return 1;
		}
	}

	/* XXX It might be good to avoid recursion here, but I don't
	   see a way to do that w/o a list. */
	TAILQ_FOREACH(vhost, &http->virtualhosts, next_vhost) {
		if (evhttp_find_alias(vhost, outhttp, hostname))
			return 1;
	}

	return 0;
}

/*
   Attempts to find the best http object to handle a request for a hostname.
   All aliases for the root http object and vhosts are searched for an exact
   match. Then, the vhost hierarchy is traversed again for a matching
   pattern.

   If an alias or vhost is matched, 1 is returned, and outhttp, if non-null,
   is set with the best matching http object. If there are no matches, the
   root http object is stored in outhttp and 0 is returned.
*/

static int
evhttp_find_vhost(struct evhttp *http, struct evhttp **outhttp,
		  const char *hostname)
{
	struct evhttp *vhost;
	struct evhttp *oldhttp;
	int match_found = 0;

	if (evhttp_find_alias(http, outhttp, hostname))
		return 1;

	do {
		oldhttp = http;
		TAILQ_FOREACH(vhost, &http->virtualhosts, next_vhost) {
			if (prefix_suffix_match(vhost->vhost_pattern,
				hostname, 1 /* ignorecase */)) {
				http = vhost;
				match_found = 1;
				break;
			}
		}
	} while (oldhttp != http);

	if (outhttp)
		*outhttp = http;

	return match_found;
}

static void
evhttp_handle_request(struct evhttp_request *req, void *arg)
{
	struct evhttp *http = arg;
	struct evhttp_cb *cb = NULL;
	const char *hostname;

	/* we have a new request on which the user needs to take action */
	req->userdone = 0;

	if (req->type == 0 || req->uri == NULL) {
		evhttp_send_error(req, req->response_code, NULL);
		return;
	}

	if ((http->allowed_methods & req->type) == 0) {
		event_debug(("Rejecting disallowed method %x (allowed: %x)\n",
			(unsigned)req->type, (unsigned)http->allowed_methods));
		evhttp_send_error(req, HTTP_NOTIMPLEMENTED, NULL);
		return;
	}

	/* handle potential virtual hosts */
	hostname = evhttp_request_get_host(req);
	if (hostname != NULL) {
		evhttp_find_vhost(http, &http, hostname);
	}

	if ((cb = evhttp_dispatch_callback(&http->callbacks, req)) != NULL) {
		(*cb->cb)(req, cb->cbarg);
		return;
	}

	/* Generic call back */
	if (http->gencb) {
		(*http->gencb)(req, http->gencbarg);
		return;
	} else {
		/* We need to send a 404 here */
#define ERR_FORMAT "<html><head>" \
		    "<title>404 Not Found</title>" \
		    "</head><body>" \
		    "<h1>Not Found</h1>" \
		    "<p>The requested URL %s was not found on this server.</p>"\
		    "</body></html>\n"

		char *escaped_html;
		struct evbuffer *buf;

		if ((escaped_html = evhttp_htmlescape(req->uri)) == NULL) {
			evhttp_connection_free(req->evcon);
			return;
		}

		if ((buf = evbuffer_new()) == NULL) {
			mm_free(escaped_html);
			evhttp_connection_free(req->evcon);
			return;
		}

		evhttp_response_code_(req, HTTP_NOTFOUND, "Not Found");

		evbuffer_add_printf(buf, ERR_FORMAT, escaped_html);

		mm_free(escaped_html);

		evhttp_send_page_(req, buf);

		evbuffer_free(buf);
#undef ERR_FORMAT
	}
}

/* Listener callback when a connection arrives at a server. */
static void
accept_socket_cb(struct evconnlistener *listener, evutil_socket_t nfd, struct sockaddr *peer_sa, int peer_socklen, void *arg)
{
	struct evhttp *http = arg;

	evhttp_get_request(http, nfd, peer_sa, peer_socklen);
}

int
evhttp_bind_socket(struct evhttp *http, const char *address, ev_uint16_t port)
{
	struct evhttp_bound_socket *bound =
		evhttp_bind_socket_with_handle(http, address, port);
	if (bound == NULL)
		return (-1);
	return (0);
}

struct evhttp_bound_socket *
evhttp_bind_socket_with_handle(struct evhttp *http, const char *address, ev_uint16_t port)
{
	evutil_socket_t fd;
	struct evhttp_bound_socket *bound;

	if ((fd = bind_socket(address, port, 1 /*reuse*/)) == -1)
		return (NULL);

	if (listen(fd, 128) == -1) {
		event_sock_warn(fd, "%s: listen", __func__);
		evutil_closesocket(fd);
		return (NULL);
	}

	bound = evhttp_accept_socket_with_handle(http, fd);

	if (bound != NULL) {
		event_debug(("Bound to port %d - Awaiting connections ... ",
			port));
		return (bound);
	}

	return (NULL);
}

int
evhttp_accept_socket(struct evhttp *http, evutil_socket_t fd)
{
	struct evhttp_bound_socket *bound =
		evhttp_accept_socket_with_handle(http, fd);
	if (bound == NULL)
		return (-1);
	return (0);
}

void
evhttp_foreach_bound_socket(struct evhttp *http,
                            evhttp_bound_socket_foreach_fn *function,
                            void *argument)
{
	struct evhttp_bound_socket *bound;

	TAILQ_FOREACH(bound, &http->sockets, next)
		function(bound, argument);
}

struct evhttp_bound_socket *
evhttp_accept_socket_with_handle(struct evhttp *http, evutil_socket_t fd)
{
	struct evhttp_bound_socket *bound;
	struct evconnlistener *listener;
	const int flags =
	    LEV_OPT_REUSEABLE|LEV_OPT_CLOSE_ON_EXEC|LEV_OPT_CLOSE_ON_FREE;

	listener = evconnlistener_new(http->base, NULL, NULL,
	    flags,
	    0, /* Backlog is '0' because we already said 'listen' */
	    fd);
	if (!listener)
		return (NULL);

	bound = evhttp_bind_listener(http, listener);
	if (!bound) {
		evconnlistener_free(listener);
		return (NULL);
	}
	return (bound);
}

struct evhttp_bound_socket *
evhttp_bind_listener(struct evhttp *http, struct evconnlistener *listener)
{
	struct evhttp_bound_socket *bound;

	bound = mm_malloc(sizeof(struct evhttp_bound_socket));
	if (bound == NULL)
		return (NULL);

	bound->listener = listener;
	TAILQ_INSERT_TAIL(&http->sockets, bound, next);

	evconnlistener_set_cb(listener, accept_socket_cb, http);
	return bound;
}

evutil_socket_t
evhttp_bound_socket_get_fd(struct evhttp_bound_socket *bound)
{
	return evconnlistener_get_fd(bound->listener);
}

struct evconnlistener *
evhttp_bound_socket_get_listener(struct evhttp_bound_socket *bound)
{
	return bound->listener;
}

void
evhttp_del_accept_socket(struct evhttp *http, struct evhttp_bound_socket *bound)
{
	TAILQ_REMOVE(&http->sockets, bound, next);
	evconnlistener_free(bound->listener);
	mm_free(bound);
}

static struct evhttp*
evhttp_new_object(void)
{
	struct evhttp *http = NULL;

	if ((http = mm_calloc(1, sizeof(struct evhttp))) == NULL) {
		event_warn("%s: calloc", __func__);
		return (NULL);
	}

	evutil_timerclear(&http->timeout);
	evhttp_set_max_headers_size(http, EV_SIZE_MAX);
	evhttp_set_max_body_size(http, EV_SIZE_MAX);
	evhttp_set_default_content_type(http, "text/html; charset=ISO-8859-1");
	evhttp_set_allowed_methods(http,
	    EVHTTP_REQ_GET |
	    EVHTTP_REQ_POST |
	    EVHTTP_REQ_HEAD |
	    EVHTTP_REQ_PUT |
	    EVHTTP_REQ_DELETE);

	TAILQ_INIT(&http->sockets);
	TAILQ_INIT(&http->callbacks);
	TAILQ_INIT(&http->connections);
	TAILQ_INIT(&http->virtualhosts);
	TAILQ_INIT(&http->aliases);

	return (http);
}

struct evhttp *
evhttp_new(struct event_base *base)
{
	struct evhttp *http = NULL;

	http = evhttp_new_object();
	if (http == NULL)
		return (NULL);
	http->base = base;

	return (http);
}

/*
 * Start a web server on the specified address and port.
 */

struct evhttp *
evhttp_start(const char *address, ev_uint16_t port)
{
	struct evhttp *http = NULL;

	http = evhttp_new_object();
	if (http == NULL)
		return (NULL);
	if (evhttp_bind_socket(http, address, port) == -1) {
		mm_free(http);
		return (NULL);
	}

	return (http);
}

void
evhttp_free(struct evhttp* http)
{
	struct evhttp_cb *http_cb;
	struct evhttp_connection *evcon;
	struct evhttp_bound_socket *bound;
	struct evhttp* vhost;
	struct evhttp_server_alias *alias;

	/* Remove the accepting part */
	while ((bound = TAILQ_FIRST(&http->sockets)) != NULL) {
		TAILQ_REMOVE(&http->sockets, bound, next);

		evconnlistener_free(bound->listener);

		mm_free(bound);
	}

	while ((evcon = TAILQ_FIRST(&http->connections)) != NULL) {
		/* evhttp_connection_free removes the connection */
		evhttp_connection_free(evcon);
	}

	while ((http_cb = TAILQ_FIRST(&http->callbacks)) != NULL) {
		TAILQ_REMOVE(&http->callbacks, http_cb, next);
		mm_free(http_cb->what);
		mm_free(http_cb);
	}

	while ((vhost = TAILQ_FIRST(&http->virtualhosts)) != NULL) {
		TAILQ_REMOVE(&http->virtualhosts, vhost, next_vhost);

		evhttp_free(vhost);
	}

	if (http->vhost_pattern != NULL)
		mm_free(http->vhost_pattern);

	while ((alias = TAILQ_FIRST(&http->aliases)) != NULL) {
		TAILQ_REMOVE(&http->aliases, alias, next);
		mm_free(alias->alias);
		mm_free(alias);
	}

	mm_free(http);
}

int
evhttp_add_virtual_host(struct evhttp* http, const char *pattern,
    struct evhttp* vhost)
{
	/* a vhost can only be a vhost once and should not have bound sockets */
	if (vhost->vhost_pattern != NULL ||
	    TAILQ_FIRST(&vhost->sockets) != NULL)
		return (-1);

	vhost->vhost_pattern = mm_strdup(pattern);
	if (vhost->vhost_pattern == NULL)
		return (-1);

	TAILQ_INSERT_TAIL(&http->virtualhosts, vhost, next_vhost);

	return (0);
}

int
evhttp_remove_virtual_host(struct evhttp* http, struct evhttp* vhost)
{
	if (vhost->vhost_pattern == NULL)
		return (-1);

	TAILQ_REMOVE(&http->virtualhosts, vhost, next_vhost);

	mm_free(vhost->vhost_pattern);
	vhost->vhost_pattern = NULL;

	return (0);
}

int
evhttp_add_server_alias(struct evhttp *http, const char *alias)
{
	struct evhttp_server_alias *evalias;

	evalias = mm_calloc(1, sizeof(*evalias));
	if (!evalias)
		return -1;

	evalias->alias = mm_strdup(alias);
	if (!evalias->alias) {
		mm_free(evalias);
		return -1;
	}

	TAILQ_INSERT_TAIL(&http->aliases, evalias, next);

	return 0;
}

int
evhttp_remove_server_alias(struct evhttp *http, const char *alias)
{
	struct evhttp_server_alias *evalias;

	TAILQ_FOREACH(evalias, &http->aliases, next) {
		if (evutil_ascii_strcasecmp(evalias->alias, alias) == 0) {
			TAILQ_REMOVE(&http->aliases, evalias, next);
			mm_free(evalias->alias);
			mm_free(evalias);
			return 0;
		}
	}

	return -1;
}

void
evhttp_set_timeout(struct evhttp* http, int timeout_in_secs)
{
	if (timeout_in_secs == -1) {
		evhttp_set_timeout_tv(http, NULL);
	} else {
		struct timeval tv;
		tv.tv_sec = timeout_in_secs;
		tv.tv_usec = 0;
		evhttp_set_timeout_tv(http, &tv);
	}
}

void
evhttp_set_timeout_tv(struct evhttp* http, const struct timeval* tv)
{
	if (tv) {
		http->timeout = *tv;
	} else {
		evutil_timerclear(&http->timeout);
	}
}

int evhttp_set_flags(struct evhttp *http, int flags)
{
	int avail_flags = 0;
	avail_flags |= EVHTTP_SERVER_LINGERING_CLOSE;

	if (flags & ~avail_flags)
		return 1;
	http->flags &= ~avail_flags;

	http->flags |= flags;

	return 0;
}

void
evhttp_set_max_headers_size(struct evhttp* http, ev_ssize_t max_headers_size)
{
	if (max_headers_size < 0)
		http->default_max_headers_size = EV_SIZE_MAX;
	else
		http->default_max_headers_size = max_headers_size;
}

void
evhttp_set_max_body_size(struct evhttp* http, ev_ssize_t max_body_size)
{
	if (max_body_size < 0)
		http->default_max_body_size = EV_UINT64_MAX;
	else
		http->default_max_body_size = max_body_size;
}

void
evhttp_set_default_content_type(struct evhttp *http,
	const char *content_type) {
	http->default_content_type = content_type;
}

void
evhttp_set_allowed_methods(struct evhttp* http, ev_uint16_t methods)
{
	http->allowed_methods = methods;
}

int
evhttp_set_cb(struct evhttp *http, const char *uri,
    void (*cb)(struct evhttp_request *, void *), void *cbarg)
{
	struct evhttp_cb *http_cb;

	TAILQ_FOREACH(http_cb, &http->callbacks, next) {
		if (strcmp(http_cb->what, uri) == 0)
			return (-1);
	}

	if ((http_cb = mm_calloc(1, sizeof(struct evhttp_cb))) == NULL) {
		event_warn("%s: calloc", __func__);
		return (-2);
	}

	http_cb->what = mm_strdup(uri);
	if (http_cb->what == NULL) {
		event_warn("%s: strdup", __func__);
		mm_free(http_cb);
		return (-3);
	}
	http_cb->cb = cb;
	http_cb->cbarg = cbarg;

	TAILQ_INSERT_TAIL(&http->callbacks, http_cb, next);

	return (0);
}

int
evhttp_del_cb(struct evhttp *http, const char *uri)
{
	struct evhttp_cb *http_cb;

	TAILQ_FOREACH(http_cb, &http->callbacks, next) {
		if (strcmp(http_cb->what, uri) == 0)
			break;
	}
	if (http_cb == NULL)
		return (-1);

	TAILQ_REMOVE(&http->callbacks, http_cb, next);
	mm_free(http_cb->what);
	mm_free(http_cb);

	return (0);
}

void
evhttp_set_gencb(struct evhttp *http,
    void (*cb)(struct evhttp_request *, void *), void *cbarg)
{
	http->gencb = cb;
	http->gencbarg = cbarg;
}

void
evhttp_set_bevcb(struct evhttp *http,
    struct bufferevent* (*cb)(struct event_base *, void *), void *cbarg)
{
	http->bevcb = cb;
	http->bevcbarg = cbarg;
}

/*
 * Request related functions
 */

struct evhttp_request *
evhttp_request_new(void (*cb)(struct evhttp_request *, void *), void *arg)
{
	struct evhttp_request *req = NULL;

	/* Allocate request structure */
	if ((req = mm_calloc(1, sizeof(struct evhttp_request))) == NULL) {
		event_warn("%s: calloc", __func__);
		goto error;
	}

	req->headers_size = 0;
	req->body_size = 0;

	req->kind = EVHTTP_RESPONSE;
	req->input_headers = mm_calloc(1, sizeof(struct evkeyvalq));
	if (req->input_headers == NULL) {
		event_warn("%s: calloc", __func__);
		goto error;
	}
	TAILQ_INIT(req->input_headers);

	req->output_headers = mm_calloc(1, sizeof(struct evkeyvalq));
	if (req->output_headers == NULL) {
		event_warn("%s: calloc", __func__);
		goto error;
	}
	TAILQ_INIT(req->output_headers);

	if ((req->input_buffer = evbuffer_new()) == NULL) {
		event_warn("%s: evbuffer_new", __func__);
		goto error;
	}

	if ((req->output_buffer = evbuffer_new()) == NULL) {
		event_warn("%s: evbuffer_new", __func__);
		goto error;
	}

	req->cb = cb;
	req->cb_arg = arg;

	return (req);

 error:
	if (req != NULL)
		evhttp_request_free(req);
	return (NULL);
}

void
evhttp_request_free(struct evhttp_request *req)
{
	if ((req->flags & EVHTTP_REQ_DEFER_FREE) != 0) {
		req->flags |= EVHTTP_REQ_NEEDS_FREE;
		return;
	}

	if (req->remote_host != NULL)
		mm_free(req->remote_host);
	if (req->uri != NULL)
		mm_free(req->uri);
	if (req->uri_elems != NULL)
		evhttp_uri_free(req->uri_elems);
	if (req->response_code_line != NULL)
		mm_free(req->response_code_line);
	if (req->host_cache != NULL)
		mm_free(req->host_cache);

	evhttp_clear_headers(req->input_headers);
	mm_free(req->input_headers);

	evhttp_clear_headers(req->output_headers);
	mm_free(req->output_headers);

	if (req->input_buffer != NULL)
		evbuffer_free(req->input_buffer);

	if (req->output_buffer != NULL)
		evbuffer_free(req->output_buffer);

	mm_free(req);
}

void
evhttp_request_own(struct evhttp_request *req)
{
	req->flags |= EVHTTP_USER_OWNED;
}

int
evhttp_request_is_owned(struct evhttp_request *req)
{
	return (req->flags & EVHTTP_USER_OWNED) != 0;
}

struct evhttp_connection *
evhttp_request_get_connection(struct evhttp_request *req)
{
	return req->evcon;
}

struct event_base *
evhttp_connection_get_base(struct evhttp_connection *conn)
{
	return conn->base;
}

void
evhttp_request_set_chunked_cb(struct evhttp_request *req,
    void (*cb)(struct evhttp_request *, void *))
{
	req->chunk_cb = cb;
}

void
evhttp_request_set_header_cb(struct evhttp_request *req,
    int (*cb)(struct evhttp_request *, void *))
{
	req->header_cb = cb;
}

void
evhttp_request_set_error_cb(struct evhttp_request *req,
    void (*cb)(enum evhttp_request_error, void *))
{
	req->error_cb = cb;
}

void
evhttp_request_set_on_complete_cb(struct evhttp_request *req,
    void (*cb)(struct evhttp_request *, void *), void *cb_arg)
{
	req->on_complete_cb = cb;
	req->on_complete_cb_arg = cb_arg;
}

/*
 * Allows for inspection of the request URI
 */

const char *
evhttp_request_get_uri(const struct evhttp_request *req) {
	if (req->uri == NULL)
		event_debug(("%s: request %p has no uri\n", __func__, req));
	return (req->uri);
}

const struct evhttp_uri *
evhttp_request_get_evhttp_uri(const struct evhttp_request *req) {
	if (req->uri_elems == NULL)
		event_debug(("%s: request %p has no uri elems\n",
			    __func__, req));
	return (req->uri_elems);
}

const char *
evhttp_request_get_host(struct evhttp_request *req)
{
	const char *host = NULL;

	if (req->host_cache)
		return req->host_cache;

	if (req->uri_elems)
		host = evhttp_uri_get_host(req->uri_elems);
	if (!host && req->input_headers) {
		const char *p;
		size_t len;

		host = evhttp_find_header(req->input_headers, "Host");
		/* The Host: header may include a port. Remove it here
		   to be consistent with uri_elems case above. */
		if (host) {
			p = host + strlen(host) - 1;
			while (p > host && EVUTIL_ISDIGIT_(*p))
				--p;
			if (p > host && *p == ':') {
				len = p - host;
				req->host_cache = mm_malloc(len + 1);
				if (!req->host_cache) {
					event_warn("%s: malloc", __func__);
					return NULL;
				}
				memcpy(req->host_cache, host, len);
				req->host_cache[len] = '\0';
				host = req->host_cache;
			}
		}
	}

	return host;
}

enum evhttp_cmd_type
evhttp_request_get_command(const struct evhttp_request *req) {
	return (req->type);
}

int
evhttp_request_get_response_code(const struct evhttp_request *req)
{
	return req->response_code;
}

const char *
evhttp_request_get_response_code_line(const struct evhttp_request *req)
{
	return req->response_code_line;
}

/** Returns the input headers */
struct evkeyvalq *evhttp_request_get_input_headers(struct evhttp_request *req)
{
	return (req->input_headers);
}

/** Returns the output headers */
struct evkeyvalq *evhttp_request_get_output_headers(struct evhttp_request *req)
{
	return (req->output_headers);
}

/** Returns the input buffer */
struct evbuffer *evhttp_request_get_input_buffer(struct evhttp_request *req)
{
	return (req->input_buffer);
}

/** Returns the output buffer */
struct evbuffer *evhttp_request_get_output_buffer(struct evhttp_request *req)
{
	return (req->output_buffer);
}


/*
 * Takes a file descriptor to read a request from.
 * The callback is executed once the whole request has been read.
 */

static struct evhttp_connection*
evhttp_get_request_connection(
	struct evhttp* http,
	evutil_socket_t fd, struct sockaddr *sa, ev_socklen_t salen)
{
	struct evhttp_connection *evcon;
	char *hostname = NULL, *portname = NULL;
	struct bufferevent* bev = NULL;

	name_from_addr(sa, salen, &hostname, &portname);
	if (hostname == NULL || portname == NULL) {
		if (hostname) mm_free(hostname);
		if (portname) mm_free(portname);
		return (NULL);
	}

	event_debug(("%s: new request from %s:%s on "EV_SOCK_FMT"\n",
		__func__, hostname, portname, EV_SOCK_ARG(fd)));

	/* we need a connection object to put the http request on */
	if (http->bevcb != NULL) {
		bev = (*http->bevcb)(http->base, http->bevcbarg);
	}
	evcon = evhttp_connection_base_bufferevent_new(
		http->base, NULL, bev, hostname, atoi(portname));
	mm_free(hostname);
	mm_free(portname);
	if (evcon == NULL)
		return (NULL);

	evcon->max_headers_size = http->default_max_headers_size;
	evcon->max_body_size = http->default_max_body_size;
	if (http->flags & EVHTTP_SERVER_LINGERING_CLOSE)
		evcon->flags |= EVHTTP_CON_LINGERING_CLOSE;

	evcon->flags |= EVHTTP_CON_INCOMING;
	evcon->state = EVCON_READING_FIRSTLINE;

	evcon->fd = fd;

	bufferevent_enable(evcon->bufev, EV_READ);
	bufferevent_disable(evcon->bufev, EV_WRITE);
	bufferevent_setfd(evcon->bufev, fd);

	return (evcon);
}

static int
evhttp_associate_new_request_with_connection(struct evhttp_connection *evcon)
{
	struct evhttp *http = evcon->http_server;
	struct evhttp_request *req;
	if ((req = evhttp_request_new(evhttp_handle_request, http)) == NULL)
		return (-1);

	if ((req->remote_host = mm_strdup(evcon->address)) == NULL) {
		event_warn("%s: strdup", __func__);
		evhttp_request_free(req);
		return (-1);
	}
	req->remote_port = evcon->port;

	req->evcon = evcon;	/* the request ends up owning the connection */
	req->flags |= EVHTTP_REQ_OWN_CONNECTION;

	/* We did not present the request to the user user yet, so treat it as
	 * if the user was done with the request.  This allows us to free the
	 * request on a persistent connection if the client drops it without
	 * sending a request.
	 */
	req->userdone = 1;

	TAILQ_INSERT_TAIL(&evcon->requests, req, next);

	req->kind = EVHTTP_REQUEST;


	evhttp_start_read_(evcon);

	return (0);
}

static void
evhttp_get_request(struct evhttp *http, evutil_socket_t fd,
    struct sockaddr *sa, ev_socklen_t salen)
{
	struct evhttp_connection *evcon;

	evcon = evhttp_get_request_connection(http, fd, sa, salen);
	if (evcon == NULL) {
		event_sock_warn(fd, "%s: cannot get connection on "EV_SOCK_FMT,
		    __func__, EV_SOCK_ARG(fd));
		evutil_closesocket(fd);
		return;
	}

	/* the timeout can be used by the server to close idle connections */
	if (evutil_timerisset(&http->timeout))
		evhttp_connection_set_timeout_tv(evcon, &http->timeout);

	/*
	 * if we want to accept more than one request on a connection,
	 * we need to know which http server it belongs to.
	 */
	evcon->http_server = http;
	TAILQ_INSERT_TAIL(&http->connections, evcon, next);

	if (evhttp_associate_new_request_with_connection(evcon) == -1)
		evhttp_connection_free(evcon);
}


/*
 * Network helper functions that we do not want to export to the rest of
 * the world.
 */

static void
name_from_addr(struct sockaddr *sa, ev_socklen_t salen,
    char **phost, char **pport)
{
	char ntop[NI_MAXHOST];
	char strport[NI_MAXSERV];
	int ni_result;

#ifdef EVENT__HAVE_GETNAMEINFO
	ni_result = getnameinfo(sa, salen,
		ntop, sizeof(ntop), strport, sizeof(strport),
		NI_NUMERICHOST|NI_NUMERICSERV);

	if (ni_result != 0) {
#ifdef EAI_SYSTEM
		/* Windows doesn't have an EAI_SYSTEM. */
		if (ni_result == EAI_SYSTEM)
			event_err(1, "getnameinfo failed");
		else
#endif
			event_errx(1, "getnameinfo failed: %s", gai_strerror(ni_result));
		return;
	}
#else
	ni_result = fake_getnameinfo(sa, salen,
		ntop, sizeof(ntop), strport, sizeof(strport),
		NI_NUMERICHOST|NI_NUMERICSERV);
	if (ni_result != 0)
			return;
#endif

	*phost = mm_strdup(ntop);
	*pport = mm_strdup(strport);
}

/* Create a non-blocking socket and bind it */
/* todo: rename this function */
static evutil_socket_t
bind_socket_ai(struct evutil_addrinfo *ai, int reuse)
{
	evutil_socket_t fd;

	int on = 1, r;
	int serrno;

	/* Create listen socket */
	fd = evutil_socket_(ai ? ai->ai_family : AF_INET,
	    SOCK_STREAM|EVUTIL_SOCK_NONBLOCK|EVUTIL_SOCK_CLOEXEC, 0);
	if (fd == -1) {
			event_sock_warn(-1, "socket");
			return (-1);
	}

	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&on, sizeof(on))<0)
		goto out;
	if (reuse) {
		if (evutil_make_listen_socket_reuseable(fd) < 0)
			goto out;
	}

	if (ai != NULL) {
		r = bind(fd, ai->ai_addr, (ev_socklen_t)ai->ai_addrlen);
		if (r == -1)
			goto out;
	}

	return (fd);

 out:
	serrno = EVUTIL_SOCKET_ERROR();
	evutil_closesocket(fd);
	EVUTIL_SET_SOCKET_ERROR(serrno);
	return (-1);
}

static struct evutil_addrinfo *
make_addrinfo(const char *address, ev_uint16_t port)
{
	struct evutil_addrinfo *ai = NULL;

	struct evutil_addrinfo hints;
	char strport[NI_MAXSERV];
	int ai_result;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	/* turn NULL hostname into INADDR_ANY, and skip looking up any address
	 * types we don't have an interface to connect to. */
	hints.ai_flags = EVUTIL_AI_PASSIVE|EVUTIL_AI_ADDRCONFIG;
	evutil_snprintf(strport, sizeof(strport), "%d", port);
	if ((ai_result = evutil_getaddrinfo(address, strport, &hints, &ai))
	    != 0) {
		if (ai_result == EVUTIL_EAI_SYSTEM)
			event_warn("getaddrinfo");
		else
			event_warnx("getaddrinfo: %s",
			    evutil_gai_strerror(ai_result));
		return (NULL);
	}

	return (ai);
}

static evutil_socket_t
bind_socket(const char *address, ev_uint16_t port, int reuse)
{
	evutil_socket_t fd;
	struct evutil_addrinfo *aitop = NULL;

	/* just create an unbound socket */
	if (address == NULL && port == 0)
		return bind_socket_ai(NULL, 0);

	aitop = make_addrinfo(address, port);

	if (aitop == NULL)
		return (-1);

	fd = bind_socket_ai(aitop, reuse);

	evutil_freeaddrinfo(aitop);

	return (fd);
}

struct evhttp_uri {
	unsigned flags;
	char *scheme; /* scheme; e.g http, ftp etc */
	char *userinfo; /* userinfo (typically username:pass), or NULL */
	char *host; /* hostname, IP address, or NULL */
	int port; /* port, or zero */
	char *path; /* path, or "". */
	char *query; /* query, or NULL */
	char *fragment; /* fragment or NULL */
};

struct evhttp_uri *
evhttp_uri_new(void)
{
	struct evhttp_uri *uri = mm_calloc(sizeof(struct evhttp_uri), 1);
	if (uri)
		uri->port = -1;
	return uri;
}

void
evhttp_uri_set_flags(struct evhttp_uri *uri, unsigned flags)
{
	uri->flags = flags;
}

/* Return true if the string starting at s and ending immediately before eos
 * is a valid URI scheme according to RFC3986
 */
static int
scheme_ok(const char *s, const char *eos)
{
	/* scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) */
	EVUTIL_ASSERT(eos >= s);
	if (s == eos)
		return 0;
	if (!EVUTIL_ISALPHA_(*s))
		return 0;
	while (++s < eos) {
		if (! EVUTIL_ISALNUM_(*s) &&
		    *s != '+' && *s != '-' && *s != '.')
			return 0;
	}
	return 1;
}

#define SUBDELIMS "!$&'()*+,;="

/* Return true iff [s..eos) is a valid userinfo */
static int
userinfo_ok(const char *s, const char *eos)
{
	while (s < eos) {
		if (CHAR_IS_UNRESERVED(*s) ||
		    strchr(SUBDELIMS, *s) ||
		    *s == ':')
			++s;
		else if (*s == '%' && s+2 < eos &&
		    EVUTIL_ISXDIGIT_(s[1]) &&
		    EVUTIL_ISXDIGIT_(s[2]))
			s += 3;
		else
			return 0;
	}
	return 1;
}

static int
regname_ok(const char *s, const char *eos)
{
	while (s && s<eos) {
		if (CHAR_IS_UNRESERVED(*s) ||
		    strchr(SUBDELIMS, *s))
			++s;
		else if (*s == '%' &&
		    EVUTIL_ISXDIGIT_(s[1]) &&
		    EVUTIL_ISXDIGIT_(s[2]))
			s += 3;
		else
			return 0;
	}
	return 1;
}

static int
parse_port(const char *s, const char *eos)
{
	int portnum = 0;
	while (s < eos) {
		if (! EVUTIL_ISDIGIT_(*s))
			return -1;
		portnum = (portnum * 10) + (*s - '0');
		if (portnum < 0)
			return -1;
		if (portnum > 65535)
			return -1;
		++s;
	}
	return portnum;
}

/* returns 0 for bad, 1 for ipv6, 2 for IPvFuture */
static int
bracket_addr_ok(const char *s, const char *eos)
{
	if (s + 3 > eos || *s != '[' || *(eos-1) != ']')
		return 0;
	if (s[1] == 'v') {
		/* IPvFuture, or junk.
		   "v" 1*HEXDIG "." 1*( unreserved / sub-delims / ":" )
		 */
		s += 2; /* skip [v */
		--eos;
		if (!EVUTIL_ISXDIGIT_(*s)) /*require at least one*/
			return 0;
		while (s < eos && *s != '.') {
			if (EVUTIL_ISXDIGIT_(*s))
				++s;
			else
				return 0;
		}
		if (*s != '.')
			return 0;
		++s;
		while (s < eos) {
			if (CHAR_IS_UNRESERVED(*s) ||
			    strchr(SUBDELIMS, *s) ||
			    *s == ':')
				++s;
			else
				return 0;
		}
		return 2;
	} else {
		/* IPv6, or junk */
		char buf[64];
		ev_ssize_t n_chars = eos-s-2;
		struct in6_addr in6;
		if (n_chars >= 64) /* way too long */
			return 0;
		memcpy(buf, s+1, n_chars);
		buf[n_chars]='\0';
		return (evutil_inet_pton(AF_INET6,buf,&in6)==1) ? 1 : 0;
	}
}

static int
parse_authority(struct evhttp_uri *uri, char *s, char *eos)
{
	char *cp, *port;
	EVUTIL_ASSERT(eos);
	if (eos == s) {
		uri->host = mm_strdup("");
		if (uri->host == NULL) {
			event_warn("%s: strdup", __func__);
			return -1;
		}
		return 0;
	}

	/* Optionally, we start with "userinfo@" */

	cp = strchr(s, '@');
	if (cp && cp < eos) {
		if (! userinfo_ok(s,cp))
			return -1;
		*cp++ = '\0';
		uri->userinfo = mm_strdup(s);
		if (uri->userinfo == NULL) {
			event_warn("%s: strdup", __func__);
			return -1;
		}
	} else {
		cp = s;
	}
	/* Optionally, we end with ":port" */
	for (port=eos-1; port >= cp && EVUTIL_ISDIGIT_(*port); --port)
		;
	if (port >= cp && *port == ':') {
		if (port+1 == eos) /* Leave port unspecified; the RFC allows a
				    * nil port */
			uri->port = -1;
		else if ((uri->port = parse_port(port+1, eos))<0)
			return -1;
		eos = port;
	}
	/* Now, cp..eos holds the "host" port, which can be an IPv4Address,
	 * an IP-Literal, or a reg-name */
	EVUTIL_ASSERT(eos >= cp);
	if (*cp == '[' && eos >= cp+2 && *(eos-1) == ']') {
		/* IPv6address, IP-Literal, or junk. */
		if (! bracket_addr_ok(cp, eos))
			return -1;
	} else {
		/* Make sure the host part is ok. */
		if (! regname_ok(cp,eos)) /* Match IPv4Address or reg-name */
			return -1;
	}
	uri->host = mm_malloc(eos-cp+1);
	if (uri->host == NULL) {
		event_warn("%s: malloc", __func__);
		return -1;
	}
	memcpy(uri->host, cp, eos-cp);
	uri->host[eos-cp] = '\0';
	return 0;

}

static char *
end_of_authority(char *cp)
{
	while (*cp) {
		if (*cp == '?' || *cp == '#' || *cp == '/')
			return cp;
		++cp;
	}
	return cp;
}

enum uri_part {
	PART_PATH,
	PART_QUERY,
	PART_FRAGMENT
};

/* Return the character after the longest prefix of 'cp' that matches...
 *   *pchar / "/" if allow_qchars is false, or
 *   *(pchar / "/" / "?") if allow_qchars is true.
 */
static char *
end_of_path(char *cp, enum uri_part part, unsigned flags)
{
	if (flags & EVHTTP_URI_NONCONFORMANT) {
		/* If NONCONFORMANT:
		 *   Path is everything up to a # or ? or nul.
		 *   Query is everything up a # or nul
		 *   Fragment is everything up to a nul.
		 */
		switch (part) {
		case PART_PATH:
			while (*cp && *cp != '#' && *cp != '?')
				++cp;
			break;
		case PART_QUERY:
			while (*cp && *cp != '#')
				++cp;
			break;
		case PART_FRAGMENT:
			cp += strlen(cp);
			break;
		};
		return cp;
	}

	while (*cp) {
		if (CHAR_IS_UNRESERVED(*cp) ||
		    strchr(SUBDELIMS, *cp) ||
		    *cp == ':' || *cp == '@' || *cp == '/')
			++cp;
		else if (*cp == '%' && EVUTIL_ISXDIGIT_(cp[1]) &&
		    EVUTIL_ISXDIGIT_(cp[2]))
			cp += 3;
		else if (*cp == '?' && part != PART_PATH)
			++cp;
		else
			return cp;
	}
	return cp;
}

static int
path_matches_noscheme(const char *cp)
{
	while (*cp) {
		if (*cp == ':')
			return 0;
		else if (*cp == '/')
			return 1;
		++cp;
	}
	return 1;
}

struct evhttp_uri *
evhttp_uri_parse(const char *source_uri)
{
	return evhttp_uri_parse_with_flags(source_uri, 0);
}

struct evhttp_uri *
evhttp_uri_parse_with_flags(const char *source_uri, unsigned flags)
{
	char *readbuf = NULL, *readp = NULL, *token = NULL, *query = NULL;
	char *path = NULL, *fragment = NULL;
	int got_authority = 0;

	struct evhttp_uri *uri = mm_calloc(1, sizeof(struct evhttp_uri));
	if (uri == NULL) {
		event_warn("%s: calloc", __func__);
		goto err;
	}
	uri->port = -1;
	uri->flags = flags;

	readbuf = mm_strdup(source_uri);
	if (readbuf == NULL) {
		event_warn("%s: strdup", __func__);
		goto err;
	}

	readp = readbuf;
	token = NULL;

	/* We try to follow RFC3986 here as much as we can, and match
	   the productions

	      URI = scheme ":" hier-part [ "?" query ] [ "#" fragment ]

	      relative-ref  = relative-part [ "?" query ] [ "#" fragment ]
	 */

	/* 1. scheme: */
	token = strchr(readp, ':');
	if (token && scheme_ok(readp,token)) {
		*token = '\0';
		uri->scheme = mm_strdup(readp);
		if (uri->scheme == NULL) {
			event_warn("%s: strdup", __func__);
			goto err;
		}
		readp = token+1; /* eat : */
	}

	/* 2. Optionally, "//" then an 'authority' part. */
	if (readp[0]=='/' && readp[1] == '/') {
		char *authority;
		readp += 2;
		authority = readp;
		path = end_of_authority(readp);
		if (parse_authority(uri, authority, path) < 0)
			goto err;
		readp = path;
		got_authority = 1;
	}

	/* 3. Query: path-abempty, path-absolute, path-rootless, or path-empty
	 */
	path = readp;
	readp = end_of_path(path, PART_PATH, flags);

	/* Query */
	if (*readp == '?') {
		*readp = '\0';
		++readp;
		query = readp;
		readp = end_of_path(readp, PART_QUERY, flags);
	}
	/* fragment */
	if (*readp == '#') {
		*readp = '\0';
		++readp;
		fragment = readp;
		readp = end_of_path(readp, PART_FRAGMENT, flags);
	}
	if (*readp != '\0') {
		goto err;
	}

	/* These next two cases may be unreachable; I'm leaving them
	 * in to be defensive. */
	/* If you didn't get an authority, the path can't begin with "//" */
	if (!got_authority && path[0]=='/' && path[1]=='/')
		goto err;
	/* If you did get an authority, the path must begin with "/" or be
	 * empty. */
	if (got_authority && path[0] != '/' && path[0] != '\0')
		goto err;
	/* (End of maybe-unreachable cases) */

	/* If there was no scheme, the first part of the path (if any) must
	 * have no colon in it. */
	if (! uri->scheme && !path_matches_noscheme(path))
		goto err;

	EVUTIL_ASSERT(path);
	uri->path = mm_strdup(path);
	if (uri->path == NULL) {
		event_warn("%s: strdup", __func__);
		goto err;
	}

	if (query) {
		uri->query = mm_strdup(query);
		if (uri->query == NULL) {
			event_warn("%s: strdup", __func__);
			goto err;
		}
	}
	if (fragment) {
		uri->fragment = mm_strdup(fragment);
		if (uri->fragment == NULL) {
			event_warn("%s: strdup", __func__);
			goto err;
		}
	}

	mm_free(readbuf);

	return uri;
err:
	if (uri)
		evhttp_uri_free(uri);
	if (readbuf)
		mm_free(readbuf);
	return NULL;
}

void
evhttp_uri_free(struct evhttp_uri *uri)
{
#define URI_FREE_STR_(f)		\
	if (uri->f) {			\
		mm_free(uri->f);		\
	}

	URI_FREE_STR_(scheme);
	URI_FREE_STR_(userinfo);
	URI_FREE_STR_(host);
	URI_FREE_STR_(path);
	URI_FREE_STR_(query);
	URI_FREE_STR_(fragment);

	mm_free(uri);
#undef URI_FREE_STR_
}

char *
evhttp_uri_join(struct evhttp_uri *uri, char *buf, size_t limit)
{
	struct evbuffer *tmp = 0;
	size_t joined_size = 0;
	char *output = NULL;

#define URI_ADD_(f)	evbuffer_add(tmp, uri->f, strlen(uri->f))

	if (!uri || !buf || !limit)
		return NULL;

	tmp = evbuffer_new();
	if (!tmp)
		return NULL;

	if (uri->scheme) {
		URI_ADD_(scheme);
		evbuffer_add(tmp, ":", 1);
	}
	if (uri->host) {
		evbuffer_add(tmp, "//", 2);
		if (uri->userinfo)
			evbuffer_add_printf(tmp,"%s@", uri->userinfo);
		URI_ADD_(host);
		if (uri->port >= 0)
			evbuffer_add_printf(tmp,":%d", uri->port);

		if (uri->path && uri->path[0] != '/' && uri->path[0] != '\0')
			goto err;
	}

	if (uri->path)
		URI_ADD_(path);

	if (uri->query) {
		evbuffer_add(tmp, "?", 1);
		URI_ADD_(query);
	}

	if (uri->fragment) {
		evbuffer_add(tmp, "#", 1);
		URI_ADD_(fragment);
	}

	evbuffer_add(tmp, "\0", 1); /* NUL */

	joined_size = evbuffer_get_length(tmp);

	if (joined_size > limit) {
		/* It doesn't fit. */
		evbuffer_free(tmp);
		return NULL;
	}
       	evbuffer_remove(tmp, buf, joined_size);

	output = buf;
err:
	evbuffer_free(tmp);

	return output;
#undef URI_ADD_
}

const char *
evhttp_uri_get_scheme(const struct evhttp_uri *uri)
{
	return uri->scheme;
}
const char *
evhttp_uri_get_userinfo(const struct evhttp_uri *uri)
{
	return uri->userinfo;
}
const char *
evhttp_uri_get_host(const struct evhttp_uri *uri)
{
	return uri->host;
}
int
evhttp_uri_get_port(const struct evhttp_uri *uri)
{
	return uri->port;
}
const char *
evhttp_uri_get_path(const struct evhttp_uri *uri)
{
	return uri->path;
}
const char *
evhttp_uri_get_query(const struct evhttp_uri *uri)
{
	return uri->query;
}
const char *
evhttp_uri_get_fragment(const struct evhttp_uri *uri)
{
	return uri->fragment;
}

#define URI_SET_STR_(f) do {					\
	if (uri->f)						\
		mm_free(uri->f);				\
	if (f) {						\
		if ((uri->f = mm_strdup(f)) == NULL) {		\
			event_warn("%s: strdup()", __func__);	\
			return -1;				\
		}						\
	} else {						\
		uri->f = NULL;					\
	}							\
	} while(0)

int
evhttp_uri_set_scheme(struct evhttp_uri *uri, const char *scheme)
{
	if (scheme && !scheme_ok(scheme, scheme+strlen(scheme)))
		return -1;

	URI_SET_STR_(scheme);
	return 0;
}
int
evhttp_uri_set_userinfo(struct evhttp_uri *uri, const char *userinfo)
{
	if (userinfo && !userinfo_ok(userinfo, userinfo+strlen(userinfo)))
		return -1;
	URI_SET_STR_(userinfo);
	return 0;
}
int
evhttp_uri_set_host(struct evhttp_uri *uri, const char *host)
{
	if (host) {
		if (host[0] == '[') {
			if (! bracket_addr_ok(host, host+strlen(host)))
				return -1;
		} else {
			if (! regname_ok(host, host+strlen(host)))
				return -1;
		}
	}

	URI_SET_STR_(host);
	return 0;
}
int
evhttp_uri_set_port(struct evhttp_uri *uri, int port)
{
	if (port < -1)
		return -1;
	uri->port = port;
	return 0;
}
#define end_of_cpath(cp,p,f) \
	((const char*)(end_of_path(((char*)(cp)), (p), (f))))

int
evhttp_uri_set_path(struct evhttp_uri *uri, const char *path)
{
	if (path && end_of_cpath(path, PART_PATH, uri->flags) != path+strlen(path))
		return -1;

	URI_SET_STR_(path);
	return 0;
}
int
evhttp_uri_set_query(struct evhttp_uri *uri, const char *query)
{
	if (query && end_of_cpath(query, PART_QUERY, uri->flags) != query+strlen(query))
		return -1;
	URI_SET_STR_(query);
	return 0;
}
int
evhttp_uri_set_fragment(struct evhttp_uri *uri, const char *fragment)
{
	if (fragment && end_of_cpath(fragment, PART_FRAGMENT, uri->flags) != fragment+strlen(fragment))
		return -1;
	URI_SET_STR_(fragment);
	return 0;
}
