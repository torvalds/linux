/*
 * Copyright (C) 2006-2008, 2010-2012  Internet Systems Consortium, Inc. ("ISC")
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

#include <isc/buffer.h>
#include <isc/httpd.h>
#include <isc/mem.h>
#include <isc/socket.h>
#include <isc/string.h>
#include <isc/task.h>
#include <isc/util.h>

#include <string.h>

/*%
 * TODO:
 *
 *  o  Put in better checks to make certain things are passed in correctly.
 *     This includes a magic number for externally-visible structures,
 *     checking for NULL-ness before dereferencing, etc.
 *  o  Make the URL processing external functions which will fill-in a buffer
 *     structure we provide, or return an error and we will render a generic
 *     page and close the client.
 */

#define MSHUTTINGDOWN(cm) ((cm->flags & ISC_HTTPDMGR_FLAGSHUTTINGDOWN) != 0)
#define MSETSHUTTINGDOWN(cm) (cm->flags |= ISC_HTTPDMGR_FLAGSHUTTINGDOWN)

#ifdef DEBUG_HTTPD
#define ENTER(x) do { fprintf(stderr, "ENTER %s\n", (x)); } while (0)
#define EXIT(x) do { fprintf(stderr, "EXIT %s\n", (x)); } while (0)
#define NOTICE(x) do { fprintf(stderr, "NOTICE %s\n", (x)); } while (0)
#else
#define ENTER(x) do { } while(0)
#define EXIT(x) do { } while(0)
#define NOTICE(x) do { } while(0)
#endif

#define HTTP_RECVLEN			1024
#define HTTP_SENDGROW			1024
#define HTTP_SEND_MAXLEN		10240

/*%
 * HTTP urls.  These are the URLs we manage, and the function to call to
 * provide the data for it.  We pass in the base url (so the same function
 * can handle multiple requests), and a structure to fill in to return a
 * result to the client.  We also pass in a pointer to be filled in for
 * the data cleanup function.
 */
struct isc_httpdurl {
	char			       *url;
	isc_httpdaction_t	       *action;
	void			       *action_arg;
	ISC_LINK(isc_httpdurl_t)	link;
};

#define HTTPD_CLOSE		0x0001 /* Got a Connection: close header */
#define HTTPD_FOUNDHOST		0x0002 /* Got a Host: header */

/*% http client */
struct isc_httpd {
	isc_httpdmgr_t	       *mgr;		/*%< our parent */
	ISC_LINK(isc_httpd_t)	link;
	unsigned int		state;
	isc_socket_t		*sock;

	/*%
	 * Received data state.
	 */
	char			recvbuf[HTTP_RECVLEN]; /*%< receive buffer */
	isc_uint32_t		recvlen;	/*%< length recv'd */
	unsigned int		method;
	char		       *url;
	char		       *querystring;
	char		       *protocol;

	/*
	 * Flags on the httpd client.
	 */
	int			flags;

	/*%
	 * Transmit data state.
	 *
	 * This is the data buffer we will transmit.
	 *
	 * This free function pointer is filled in by the rendering function
	 * we call.  The free function is called after the data is transmitted
	 * to the client.
	 *
	 * The bufflist is the list of buffers we are currently transmitting.
	 * The headerdata is where we render our headers to.  If we run out of
	 * space when rendering a header, we will change the size of our
	 * buffer.  We will not free it until we are finished, and will
	 * allocate an additional HTTP_SENDGROW bytes per header space grow.
	 *
	 * We currently use two buffers total, one for the headers (which
	 * we manage) and another for the client to fill in (which it manages,
	 * it provides the space for it, etc) -- we will pass that buffer
	 * structure back to the caller, who is responsible for managing the
	 * space it may have allocated as backing store for it.  This second
	 * buffer is bodybuffer, and we only allocate the buffer itself, not
	 * the backing store.
	 */
	isc_bufferlist_t	bufflist;
	char		       *headerdata; /*%< send header buf */
	unsigned int		headerlen;  /*%< current header buffer size */
	isc_buffer_t		headerbuffer;

	const char	       *mimetype;
	unsigned int		retcode;
	const char	       *retmsg;
	isc_buffer_t		bodybuffer;
	isc_httpdfree_t	       *freecb;
	void		       *freecb_arg;
};

/*% lightweight socket manager for httpd output */
struct isc_httpdmgr {
	isc_mem_t	       *mctx;
	isc_socket_t	       *sock;		/*%< listening socket */
	isc_task_t	       *task;		/*%< owning task */
	isc_timermgr_t	       *timermgr;

	isc_httpdclientok_t    *client_ok;	/*%< client validator */
	isc_httpdondestroy_t   *ondestroy;	/*%< cleanup callback */
	void		       *cb_arg;		/*%< argument for the above */

	unsigned int		flags;
	ISC_LIST(isc_httpd_t)	running;	/*%< running clients */

	isc_mutex_t		lock;

	ISC_LIST(isc_httpdurl_t) urls;		/*%< urls we manage */
	isc_httpdaction_t      *render_404;
	isc_httpdaction_t      *render_500;
};

/*%
 * HTTP methods.
 */
#define ISC_HTTPD_METHODUNKNOWN	0
#define ISC_HTTPD_METHODGET	1
#define ISC_HTTPD_METHODPOST	2

/*%
 * Client states.
 *
 * _IDLE	The client is not doing anything at all.  This state should
 *		only occur just after creation, and just before being
 *		destroyed.
 *
 * _RECV	The client is waiting for data after issuing a socket recv().
 *
 * _RECVDONE	Data has been received, and is being processed.
 *
 * _SEND	All data for a response has completed, and a reply was
 *		sent via a socket send() call.
 *
 * _SENDDONE	Send is completed.
 *
 * Badly formatted state table:
 *
 *	IDLE -> RECV when client has a recv() queued.
 *
 *	RECV -> RECVDONE when recvdone event received.
 *
 *	RECVDONE -> SEND if the data for a reply is at hand.
 *
 *	SEND -> RECV when a senddone event was received.
 *
 *	At any time -> RECV on error.  If RECV fails, the client will
 *	self-destroy, closing the socket and freeing memory.
 */
#define ISC_HTTPD_STATEIDLE	0
#define ISC_HTTPD_STATERECV	1
#define ISC_HTTPD_STATERECVDONE	2
#define ISC_HTTPD_STATESEND	3
#define ISC_HTTPD_STATESENDDONE	4

#define ISC_HTTPD_ISRECV(c)	((c)->state == ISC_HTTPD_STATERECV)
#define ISC_HTTPD_ISRECVDONE(c)	((c)->state == ISC_HTTPD_STATERECVDONE)
#define ISC_HTTPD_ISSEND(c)	((c)->state == ISC_HTTPD_STATESEND)
#define ISC_HTTPD_ISSENDDONE(c)	((c)->state == ISC_HTTPD_STATESENDDONE)

/*%
 * Overall magic test that means we're not idle.
 */
#define ISC_HTTPD_SETRECV(c)	((c)->state = ISC_HTTPD_STATERECV)
#define ISC_HTTPD_SETRECVDONE(c)	((c)->state = ISC_HTTPD_STATERECVDONE)
#define ISC_HTTPD_SETSEND(c)	((c)->state = ISC_HTTPD_STATESEND)
#define ISC_HTTPD_SETSENDDONE(c)	((c)->state = ISC_HTTPD_STATESENDDONE)

static void isc_httpd_accept(isc_task_t *, isc_event_t *);
static void isc_httpd_recvdone(isc_task_t *, isc_event_t *);
static void isc_httpd_senddone(isc_task_t *, isc_event_t *);
static void destroy_client(isc_httpd_t **);
static isc_result_t process_request(isc_httpd_t *, int);
static void httpdmgr_destroy(isc_httpdmgr_t *);
static isc_result_t grow_headerspace(isc_httpd_t *);
static void reset_client(isc_httpd_t *httpd);
static isc_result_t render_404(const char *, const char *,
			       void *,
			       unsigned int *, const char **,
			       const char **, isc_buffer_t *,
			       isc_httpdfree_t **, void **);
static isc_result_t render_500(const char *, const char *,
			       void *,
			       unsigned int *, const char **,
			       const char **, isc_buffer_t *,
			       isc_httpdfree_t **, void **);

static void
destroy_client(isc_httpd_t **httpdp)
{
	isc_httpd_t *httpd = *httpdp;
	isc_httpdmgr_t *httpdmgr = httpd->mgr;

	*httpdp = NULL;

	LOCK(&httpdmgr->lock);

	isc_socket_detach(&httpd->sock);
	ISC_LIST_UNLINK(httpdmgr->running, httpd, link);

	if (httpd->headerlen > 0)
		isc_mem_put(httpdmgr->mctx, httpd->headerdata,
			    httpd->headerlen);

	isc_mem_put(httpdmgr->mctx, httpd, sizeof(isc_httpd_t));

	UNLOCK(&httpdmgr->lock);

	httpdmgr_destroy(httpdmgr);
}

isc_result_t
isc_httpdmgr_create(isc_mem_t *mctx, isc_socket_t *sock, isc_task_t *task,
		    isc_httpdclientok_t *client_ok,
		    isc_httpdondestroy_t *ondestroy, void *cb_arg,
		    isc_timermgr_t *tmgr, isc_httpdmgr_t **httpdp)
{
	isc_result_t result;
	isc_httpdmgr_t *httpd;

	REQUIRE(mctx != NULL);
	REQUIRE(sock != NULL);
	REQUIRE(task != NULL);
	REQUIRE(tmgr != NULL);
	REQUIRE(httpdp != NULL && *httpdp == NULL);

	httpd = isc_mem_get(mctx, sizeof(isc_httpdmgr_t));
	if (httpd == NULL)
		return (ISC_R_NOMEMORY);

	result = isc_mutex_init(&httpd->lock);
	if (result != ISC_R_SUCCESS) {
		isc_mem_put(mctx, httpd, sizeof(isc_httpdmgr_t));
		return (result);
	}
	httpd->mctx = NULL;
	isc_mem_attach(mctx, &httpd->mctx);
	httpd->sock = NULL;
	isc_socket_attach(sock, &httpd->sock);
	httpd->task = NULL;
	isc_task_attach(task, &httpd->task);
	httpd->timermgr = tmgr; /* XXXMLG no attach function? */
	httpd->client_ok = client_ok;
	httpd->ondestroy = ondestroy;
	httpd->cb_arg = cb_arg;

	ISC_LIST_INIT(httpd->running);
	ISC_LIST_INIT(httpd->urls);

	/* XXXMLG ignore errors on isc_socket_listen() */
	result = isc_socket_listen(sock, SOMAXCONN);
	if (result != ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_socket_listen() failed: %s",
				 isc_result_totext(result));
		goto cleanup;
	}

	(void)isc_socket_filter(sock, "httpready");

	result = isc_socket_accept(sock, task, isc_httpd_accept, httpd);
	if (result != ISC_R_SUCCESS)
		goto cleanup;

	httpd->render_404 = render_404;
	httpd->render_500 = render_500;

	*httpdp = httpd;
	return (ISC_R_SUCCESS);

  cleanup:
	isc_task_detach(&httpd->task);
	isc_socket_detach(&httpd->sock);
	isc_mem_detach(&httpd->mctx);
	(void)isc_mutex_destroy(&httpd->lock);
	isc_mem_put(mctx, httpd, sizeof(isc_httpdmgr_t));
	return (result);
}

static void
httpdmgr_destroy(isc_httpdmgr_t *httpdmgr)
{
	isc_mem_t *mctx;
	isc_httpdurl_t *url;

	ENTER("httpdmgr_destroy");

	LOCK(&httpdmgr->lock);

	if (!MSHUTTINGDOWN(httpdmgr)) {
		NOTICE("httpdmgr_destroy not shutting down yet");
		UNLOCK(&httpdmgr->lock);
		return;
	}

	/*
	 * If all clients are not shut down, don't do anything yet.
	 */
	if (!ISC_LIST_EMPTY(httpdmgr->running)) {
		NOTICE("httpdmgr_destroy clients still active");
		UNLOCK(&httpdmgr->lock);
		return;
	}

	NOTICE("httpdmgr_destroy detaching socket, task, and timermgr");

	isc_socket_detach(&httpdmgr->sock);
	isc_task_detach(&httpdmgr->task);
	httpdmgr->timermgr = NULL;

	/*
	 * Clear out the list of all actions we know about.  Just free the
	 * memory.
	 */
	url = ISC_LIST_HEAD(httpdmgr->urls);
	while (url != NULL) {
		isc_mem_free(httpdmgr->mctx, url->url);
		ISC_LIST_UNLINK(httpdmgr->urls, url, link);
		isc_mem_put(httpdmgr->mctx, url, sizeof(isc_httpdurl_t));
		url = ISC_LIST_HEAD(httpdmgr->urls);
	}

	UNLOCK(&httpdmgr->lock);
	(void)isc_mutex_destroy(&httpdmgr->lock);

	if (httpdmgr->ondestroy != NULL)
		(httpdmgr->ondestroy)(httpdmgr->cb_arg);

	mctx = httpdmgr->mctx;
	isc_mem_putanddetach(&mctx, httpdmgr, sizeof(isc_httpdmgr_t));

	EXIT("httpdmgr_destroy");
}

#define LENGTHOK(s) (httpd->recvbuf - (s) < (int)httpd->recvlen)
#define BUFLENOK(s) (httpd->recvbuf - (s) < HTTP_RECVLEN)

static isc_result_t
process_request(isc_httpd_t *httpd, int length)
{
	char *s;
	char *p;
	int delim;

	ENTER("request");

	httpd->recvlen += length;

	httpd->recvbuf[httpd->recvlen] = 0;

	/*
	 * If we don't find a blank line in our buffer, return that we need
	 * more data.
	 */
	s = strstr(httpd->recvbuf, "\r\n\r\n");
	delim = 1;
	if (s == NULL) {
		s = strstr(httpd->recvbuf, "\n\n");
		delim = 2;
	}
	if (s == NULL)
		return (ISC_R_NOTFOUND);

	/*
	 * Determine if this is a POST or GET method.  Any other values will
	 * cause an error to be returned.
	 */
	if (strncmp(httpd->recvbuf, "GET ", 4) == 0) {
		httpd->method = ISC_HTTPD_METHODGET;
		p = httpd->recvbuf + 4;
	} else if (strncmp(httpd->recvbuf, "POST ", 5) == 0) {
		httpd->method = ISC_HTTPD_METHODPOST;
		p = httpd->recvbuf + 5;
	} else {
		return (ISC_R_RANGE);
	}

	/*
	 * From now on, p is the start of our buffer.
	 */

	/*
	 * Extract the URL.
	 */
	s = p;
	while (LENGTHOK(s) && BUFLENOK(s) &&
	       (*s != '\n' && *s != '\r' && *s != '\0' && *s != ' '))
		s++;
	if (!LENGTHOK(s))
		return (ISC_R_NOTFOUND);
	if (!BUFLENOK(s))
		return (ISC_R_NOMEMORY);
	*s = 0;

	/*
	 * Make the URL relative.
	 */
	if ((strncmp(p, "http:/", 6) == 0)
	    || (strncmp(p, "https:/", 7) == 0)) {
		/* Skip first / */
		while (*p != '/' && *p != 0)
			p++;
		if (*p == 0)
			return (ISC_R_RANGE);
		p++;
		/* Skip second / */
		while (*p != '/' && *p != 0)
			p++;
		if (*p == 0)
			return (ISC_R_RANGE);
		p++;
		/* Find third / */
		while (*p != '/' && *p != 0)
			p++;
		if (*p == 0) {
			p--;
			*p = '/';
		}
	}

	httpd->url = p;
	p = s + delim;
	s = p;

	/*
	 * Now, see if there is a ? mark in the URL.  If so, this is
	 * part of the query string, and we will split it from the URL.
	 */
	httpd->querystring = strchr(httpd->url, '?');
	if (httpd->querystring != NULL) {
		*(httpd->querystring) = 0;
		httpd->querystring++;
	}

	/*
	 * Extract the HTTP/1.X protocol.  We will bounce on anything but
	 * HTTP/1.1 for now.
	 */
	while (LENGTHOK(s) && BUFLENOK(s) &&
	       (*s != '\n' && *s != '\r' && *s != '\0'))
		s++;
	if (!LENGTHOK(s))
		return (ISC_R_NOTFOUND);
	if (!BUFLENOK(s))
		return (ISC_R_NOMEMORY);
	*s = 0;
	if ((strncmp(p, "HTTP/1.0", 8) != 0)
	    && (strncmp(p, "HTTP/1.1", 8) != 0))
		return (ISC_R_RANGE);
	httpd->protocol = p;
	p = s + 1;
	s = p;

	if (strstr(s, "Connection: close") != NULL)
		httpd->flags |= HTTPD_CLOSE;

	if (strstr(s, "Host: ") != NULL)
		httpd->flags |= HTTPD_FOUNDHOST;

	/*
	 * Standards compliance hooks here.
	 */
	if (strcmp(httpd->protocol, "HTTP/1.1") == 0
	    && ((httpd->flags & HTTPD_FOUNDHOST) == 0))
		return (ISC_R_RANGE);

	EXIT("request");

	return (ISC_R_SUCCESS);
}

static void
isc_httpd_accept(isc_task_t *task, isc_event_t *ev)
{
	isc_result_t result;
	isc_httpdmgr_t *httpdmgr = ev->ev_arg;
	isc_httpd_t *httpd;
	isc_region_t r;
	isc_socket_newconnev_t *nev = (isc_socket_newconnev_t *)ev;
	isc_sockaddr_t peeraddr;

	ENTER("accept");

	LOCK(&httpdmgr->lock);
	if (MSHUTTINGDOWN(httpdmgr)) {
		NOTICE("accept shutting down, goto out");
		goto out;
	}

	if (nev->result == ISC_R_CANCELED) {
		NOTICE("accept canceled, goto out");
		goto out;
	}

	if (nev->result != ISC_R_SUCCESS) {
		/* XXXMLG log failure */
		NOTICE("accept returned failure, goto requeue");
		goto requeue;
	}

	(void)isc_socket_getpeername(nev->newsocket, &peeraddr);
	if (httpdmgr->client_ok != NULL &&
	    !(httpdmgr->client_ok)(&peeraddr, httpdmgr->cb_arg)) {
		isc_socket_detach(&nev->newsocket);
		goto requeue;
	}

	httpd = isc_mem_get(httpdmgr->mctx, sizeof(isc_httpd_t));
	if (httpd == NULL) {
		/* XXXMLG log failure */
		NOTICE("accept failed to allocate memory, goto requeue");
		isc_socket_detach(&nev->newsocket);
		goto requeue;
	}

	httpd->mgr = httpdmgr;
	ISC_LINK_INIT(httpd, link);
	ISC_LIST_APPEND(httpdmgr->running, httpd, link);
	ISC_HTTPD_SETRECV(httpd);
	httpd->sock = nev->newsocket;
	isc_socket_setname(httpd->sock, "httpd", NULL);
	httpd->flags = 0;

	/*
	 * Initialize the buffer for our headers.
	 */
	httpd->headerdata = isc_mem_get(httpdmgr->mctx, HTTP_SENDGROW);
	if (httpd->headerdata == NULL) {
		isc_mem_put(httpdmgr->mctx, httpd, sizeof(isc_httpd_t));
		isc_socket_detach(&nev->newsocket);
		goto requeue;
	}
	httpd->headerlen = HTTP_SENDGROW;
	isc_buffer_init(&httpd->headerbuffer, httpd->headerdata,
			httpd->headerlen);

	ISC_LIST_INIT(httpd->bufflist);

	isc_buffer_initnull(&httpd->bodybuffer);
	reset_client(httpd);

	r.base = (unsigned char *)httpd->recvbuf;
	r.length = HTTP_RECVLEN - 1;
	result = isc_socket_recv(httpd->sock, &r, 1, task, isc_httpd_recvdone,
				 httpd);
	/* FIXME!!! */
	POST(result);
	NOTICE("accept queued recv on socket");

 requeue:
	result = isc_socket_accept(httpdmgr->sock, task, isc_httpd_accept,
				   httpdmgr);
	if (result != ISC_R_SUCCESS) {
		/* XXXMLG what to do?  Log failure... */
		NOTICE("accept could not reaccept due to failure");
	}

 out:
	UNLOCK(&httpdmgr->lock);

	httpdmgr_destroy(httpdmgr);

	isc_event_free(&ev);

	EXIT("accept");
}

static isc_result_t
render_404(const char *url, const char *querystring,
	   void *arg,
	   unsigned int *retcode, const char **retmsg,
	   const char **mimetype, isc_buffer_t *b,
	   isc_httpdfree_t **freecb, void **freecb_args)
{
	static char msg[] = "No such URL.";

	UNUSED(url);
	UNUSED(querystring);
	UNUSED(arg);

	*retcode = 404;
	*retmsg = "No such URL";
	*mimetype = "text/plain";
	isc_buffer_reinit(b, msg, strlen(msg));
	isc_buffer_add(b, strlen(msg));
	*freecb = NULL;
	*freecb_args = NULL;

	return (ISC_R_SUCCESS);
}

static isc_result_t
render_500(const char *url, const char *querystring,
	   void *arg,
	   unsigned int *retcode, const char **retmsg,
	   const char **mimetype, isc_buffer_t *b,
	   isc_httpdfree_t **freecb, void **freecb_args)
{
	static char msg[] = "Internal server failure.";

	UNUSED(url);
	UNUSED(querystring);
	UNUSED(arg);

	*retcode = 500;
	*retmsg = "Internal server failure";
	*mimetype = "text/plain";
	isc_buffer_reinit(b, msg, strlen(msg));
	isc_buffer_add(b, strlen(msg));
	*freecb = NULL;
	*freecb_args = NULL;

	return (ISC_R_SUCCESS);
}

static void
isc_httpd_recvdone(isc_task_t *task, isc_event_t *ev)
{
	isc_region_t r;
	isc_result_t result;
	isc_httpd_t *httpd = ev->ev_arg;
	isc_socketevent_t *sev = (isc_socketevent_t *)ev;
	isc_httpdurl_t *url;
	isc_time_t now;
	char datebuf[32];  /* Only need 30, but safety first */

	ENTER("recv");

	INSIST(ISC_HTTPD_ISRECV(httpd));

	if (sev->result != ISC_R_SUCCESS) {
		NOTICE("recv destroying client");
		destroy_client(&httpd);
		goto out;
	}

	result = process_request(httpd, sev->n);
	if (result == ISC_R_NOTFOUND) {
		if (httpd->recvlen >= HTTP_RECVLEN - 1) {
			destroy_client(&httpd);
			goto out;
		}
		r.base = (unsigned char *)httpd->recvbuf + httpd->recvlen;
		r.length = HTTP_RECVLEN - httpd->recvlen - 1;
		/* check return code? */
		(void)isc_socket_recv(httpd->sock, &r, 1, task,
				      isc_httpd_recvdone, httpd);
		goto out;
	} else if (result != ISC_R_SUCCESS) {
		destroy_client(&httpd);
		goto out;
	}

	ISC_HTTPD_SETSEND(httpd);

	/*
	 * XXXMLG Call function here.  Provide an add-header function
	 * which will append the common headers to a response we generate.
	 */
	isc_buffer_initnull(&httpd->bodybuffer);
	isc_time_now(&now);
	isc_time_formathttptimestamp(&now, datebuf, sizeof(datebuf));
	url = ISC_LIST_HEAD(httpd->mgr->urls);
	while (url != NULL) {
		if (strcmp(httpd->url, url->url) == 0)
			break;
		url = ISC_LIST_NEXT(url, link);
	}
	if (url == NULL)
		result = httpd->mgr->render_404(httpd->url, httpd->querystring,
						NULL,
						&httpd->retcode,
						&httpd->retmsg,
						&httpd->mimetype,
						&httpd->bodybuffer,
						&httpd->freecb,
						&httpd->freecb_arg);
	else
		result = url->action(httpd->url, httpd->querystring,
				     url->action_arg,
				     &httpd->retcode, &httpd->retmsg,
				     &httpd->mimetype, &httpd->bodybuffer,
				     &httpd->freecb, &httpd->freecb_arg);
	if (result != ISC_R_SUCCESS) {
		result = httpd->mgr->render_500(httpd->url, httpd->querystring,
						NULL, &httpd->retcode,
						&httpd->retmsg,
						&httpd->mimetype,
						&httpd->bodybuffer,
						&httpd->freecb,
						&httpd->freecb_arg);
		RUNTIME_CHECK(result == ISC_R_SUCCESS);
	}

	isc_httpd_response(httpd);
	isc_httpd_addheader(httpd, "Content-Type", httpd->mimetype);
	isc_httpd_addheader(httpd, "Date", datebuf);
	isc_httpd_addheader(httpd, "Expires", datebuf);
	isc_httpd_addheader(httpd, "Last-Modified", datebuf);
	isc_httpd_addheader(httpd, "Pragma: no-cache", NULL);
	isc_httpd_addheader(httpd, "Cache-Control: no-cache", NULL);
	isc_httpd_addheader(httpd, "Server: libisc", NULL);
	isc_httpd_addheaderuint(httpd, "Content-Length",
				isc_buffer_usedlength(&httpd->bodybuffer));
	isc_httpd_endheaders(httpd);  /* done */

	ISC_LIST_APPEND(httpd->bufflist, &httpd->headerbuffer, link);
	/*
	 * Link the data buffer into our send queue, should we have any data
	 * rendered into it.  If no data is present, we won't do anything
	 * with the buffer.
	 */
	if (isc_buffer_length(&httpd->bodybuffer) > 0)
		ISC_LIST_APPEND(httpd->bufflist, &httpd->bodybuffer, link);

	/* check return code? */
	(void)isc_socket_sendv(httpd->sock, &httpd->bufflist, task,
			       isc_httpd_senddone, httpd);

 out:
	isc_event_free(&ev);
	EXIT("recv");
}

void
isc_httpdmgr_shutdown(isc_httpdmgr_t **httpdmgrp)
{
	isc_httpdmgr_t *httpdmgr;
	isc_httpd_t *httpd;
	httpdmgr = *httpdmgrp;
	*httpdmgrp = NULL;

	ENTER("isc_httpdmgr_shutdown");

	LOCK(&httpdmgr->lock);

	MSETSHUTTINGDOWN(httpdmgr);

	isc_socket_cancel(httpdmgr->sock, httpdmgr->task, ISC_SOCKCANCEL_ALL);

	httpd = ISC_LIST_HEAD(httpdmgr->running);
	while (httpd != NULL) {
		isc_socket_cancel(httpd->sock, httpdmgr->task,
				  ISC_SOCKCANCEL_ALL);
		httpd = ISC_LIST_NEXT(httpd, link);
	}

	UNLOCK(&httpdmgr->lock);

	EXIT("isc_httpdmgr_shutdown");
}

static isc_result_t
grow_headerspace(isc_httpd_t *httpd)
{
	char *newspace;
	unsigned int newlen;
	isc_region_t r;

	newlen = httpd->headerlen + HTTP_SENDGROW;
	if (newlen > HTTP_SEND_MAXLEN)
		return (ISC_R_NOSPACE);

	newspace = isc_mem_get(httpd->mgr->mctx, newlen);
	if (newspace == NULL)
		return (ISC_R_NOMEMORY);
	isc_buffer_region(&httpd->headerbuffer, &r);
	isc_buffer_reinit(&httpd->headerbuffer, newspace, newlen);

	isc_mem_put(httpd->mgr->mctx, r.base, r.length);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_httpd_response(isc_httpd_t *httpd)
{
	isc_result_t result;
	unsigned int needlen;

	needlen = strlen(httpd->protocol) + 1; /* protocol + space */
	needlen += 3 + 1;  /* room for response code, always 3 bytes */
	needlen += strlen(httpd->retmsg) + 2;  /* return msg + CRLF */

	while (isc_buffer_availablelength(&httpd->headerbuffer) < needlen) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS)
			return (result);
	}

	sprintf(isc_buffer_used(&httpd->headerbuffer), "%s %03d %s\r\n",
		httpd->protocol, httpd->retcode, httpd->retmsg);
	isc_buffer_add(&httpd->headerbuffer, needlen);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_httpd_addheader(isc_httpd_t *httpd, const char *name,
		    const char *val)
{
	isc_result_t result;
	unsigned int needlen;

	needlen = strlen(name); /* name itself */
	if (val != NULL)
		needlen += 2 + strlen(val); /* :<space> and val */
	needlen += 2; /* CRLF */

	while (isc_buffer_availablelength(&httpd->headerbuffer) < needlen) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS)
			return (result);
	}

	if (val != NULL)
		sprintf(isc_buffer_used(&httpd->headerbuffer),
			"%s: %s\r\n", name, val);
	else
		sprintf(isc_buffer_used(&httpd->headerbuffer),
			"%s\r\n", name);

	isc_buffer_add(&httpd->headerbuffer, needlen);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_httpd_endheaders(isc_httpd_t *httpd)
{
	isc_result_t result;

	while (isc_buffer_availablelength(&httpd->headerbuffer) < 2) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS)
			return (result);
	}

	sprintf(isc_buffer_used(&httpd->headerbuffer), "\r\n");
	isc_buffer_add(&httpd->headerbuffer, 2);

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_httpd_addheaderuint(isc_httpd_t *httpd, const char *name, int val) {
	isc_result_t result;
	unsigned int needlen;
	char buf[sizeof "18446744073709551616"];

	sprintf(buf, "%d", val);

	needlen = strlen(name); /* name itself */
	needlen += 2 + strlen(buf); /* :<space> and val */
	needlen += 2; /* CRLF */

	while (isc_buffer_availablelength(&httpd->headerbuffer) < needlen) {
		result = grow_headerspace(httpd);
		if (result != ISC_R_SUCCESS)
			return (result);
	}

	sprintf(isc_buffer_used(&httpd->headerbuffer),
		"%s: %s\r\n", name, buf);

	isc_buffer_add(&httpd->headerbuffer, needlen);

	return (ISC_R_SUCCESS);
}

static void
isc_httpd_senddone(isc_task_t *task, isc_event_t *ev)
{
	isc_httpd_t *httpd = ev->ev_arg;
	isc_region_t r;
	isc_socketevent_t *sev = (isc_socketevent_t *)ev;

	ENTER("senddone");
	INSIST(ISC_HTTPD_ISSEND(httpd));

	/*
	 * First, unlink our header buffer from the socket's bufflist.  This
	 * is sort of an evil hack, since we know our buffer will be there,
	 * and we know it's address, so we can just remove it directly.
	 */
	NOTICE("senddone unlinked header");
	ISC_LIST_UNLINK(sev->bufferlist, &httpd->headerbuffer, link);

	/*
	 * We will always want to clean up our receive buffer, even if we
	 * got an error on send or we are shutting down.
	 *
	 * We will pass in the buffer only if there is data in it.  If
	 * there is no data, we will pass in a NULL.
	 */
	if (httpd->freecb != NULL) {
		isc_buffer_t *b = NULL;
		if (isc_buffer_length(&httpd->bodybuffer) > 0)
			b = &httpd->bodybuffer;
		httpd->freecb(b, httpd->freecb_arg);
		NOTICE("senddone free callback performed");
	}
	if (ISC_LINK_LINKED(&httpd->bodybuffer, link)) {
		ISC_LIST_UNLINK(sev->bufferlist, &httpd->bodybuffer, link);
		NOTICE("senddone body buffer unlinked");
	}

	if (sev->result != ISC_R_SUCCESS) {
		destroy_client(&httpd);
		goto out;
	}

	if ((httpd->flags & HTTPD_CLOSE) != 0) {
		destroy_client(&httpd);
		goto out;
	}

	ISC_HTTPD_SETRECV(httpd);

	NOTICE("senddone restarting recv on socket");

	reset_client(httpd);

	r.base = (unsigned char *)httpd->recvbuf;
	r.length = HTTP_RECVLEN - 1;
	/* check return code? */
	(void)isc_socket_recv(httpd->sock, &r, 1, task,
			      isc_httpd_recvdone, httpd);

out:
	isc_event_free(&ev);
	EXIT("senddone");
}

static void
reset_client(isc_httpd_t *httpd)
{
	/*
	 * Catch errors here.  We MUST be in RECV mode, and we MUST NOT have
	 * any outstanding buffers.  If we have buffers, we have a leak.
	 */
	INSIST(ISC_HTTPD_ISRECV(httpd));
	INSIST(!ISC_LINK_LINKED(&httpd->headerbuffer, link));
	INSIST(!ISC_LINK_LINKED(&httpd->bodybuffer, link));

	httpd->recvbuf[0] = 0;
	httpd->recvlen = 0;
	httpd->method = ISC_HTTPD_METHODUNKNOWN;
	httpd->url = NULL;
	httpd->querystring = NULL;
	httpd->protocol = NULL;
	httpd->flags = 0;

	isc_buffer_clear(&httpd->headerbuffer);
	isc_buffer_invalidate(&httpd->bodybuffer);
}

isc_result_t
isc_httpdmgr_addurl(isc_httpdmgr_t *httpdmgr, const char *url,
		    isc_httpdaction_t *func, void *arg)
{
	isc_httpdurl_t *item;

	if (url == NULL) {
		httpdmgr->render_404 = func;
		return (ISC_R_SUCCESS);
	}

	item = isc_mem_get(httpdmgr->mctx, sizeof(isc_httpdurl_t));
	if (item == NULL)
		return (ISC_R_NOMEMORY);

	item->url = isc_mem_strdup(httpdmgr->mctx, url);
	if (item->url == NULL) {
		isc_mem_put(httpdmgr->mctx, item, sizeof(isc_httpdurl_t));
		return (ISC_R_NOMEMORY);
	}

	item->action = func;
	item->action_arg = arg;
	ISC_LINK_INIT(item, link);
	ISC_LIST_APPEND(httpdmgr->urls, item, link);

	return (ISC_R_SUCCESS);
}
