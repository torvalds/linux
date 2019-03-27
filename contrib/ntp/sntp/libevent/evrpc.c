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
#include "event2/event-config.h"
#include "evconfig-private.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif

#include <sys/types.h>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#ifdef EVENT__HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#include <sys/queue.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <errno.h>
#include <signal.h>
#include <string.h>

#include <sys/queue.h>

#include "event2/event.h"
#include "event2/event_struct.h"
#include "event2/rpc.h"
#include "event2/rpc_struct.h"
#include "evrpc-internal.h"
#include "event2/http.h"
#include "event2/buffer.h"
#include "event2/tag.h"
#include "event2/http_struct.h"
#include "event2/http_compat.h"
#include "event2/util.h"
#include "util-internal.h"
#include "log-internal.h"
#include "mm-internal.h"

struct evrpc_base *
evrpc_init(struct evhttp *http_server)
{
	struct evrpc_base* base = mm_calloc(1, sizeof(struct evrpc_base));
	if (base == NULL)
		return (NULL);

	/* we rely on the tagging sub system */
	evtag_init();

	TAILQ_INIT(&base->registered_rpcs);
	TAILQ_INIT(&base->input_hooks);
	TAILQ_INIT(&base->output_hooks);

	TAILQ_INIT(&base->paused_requests);

	base->http_server = http_server;

	return (base);
}

void
evrpc_free(struct evrpc_base *base)
{
	struct evrpc *rpc;
	struct evrpc_hook *hook;
	struct evrpc_hook_ctx *pause;
	int r;

	while ((rpc = TAILQ_FIRST(&base->registered_rpcs)) != NULL) {
		r = evrpc_unregister_rpc(base, rpc->uri);
		EVUTIL_ASSERT(r == 0);
	}
	while ((pause = TAILQ_FIRST(&base->paused_requests)) != NULL) {
		TAILQ_REMOVE(&base->paused_requests, pause, next);
		mm_free(pause);
	}
	while ((hook = TAILQ_FIRST(&base->input_hooks)) != NULL) {
		r = evrpc_remove_hook(base, EVRPC_INPUT, hook);
		EVUTIL_ASSERT(r);
	}
	while ((hook = TAILQ_FIRST(&base->output_hooks)) != NULL) {
		r = evrpc_remove_hook(base, EVRPC_OUTPUT, hook);
		EVUTIL_ASSERT(r);
	}
	mm_free(base);
}

void *
evrpc_add_hook(void *vbase,
    enum EVRPC_HOOK_TYPE hook_type,
    int (*cb)(void *, struct evhttp_request *, struct evbuffer *, void *),
    void *cb_arg)
{
	struct evrpc_hooks_ *base = vbase;
	struct evrpc_hook_list *head = NULL;
	struct evrpc_hook *hook = NULL;
	switch (hook_type) {
	case EVRPC_INPUT:
		head = &base->in_hooks;
		break;
	case EVRPC_OUTPUT:
		head = &base->out_hooks;
		break;
	default:
		EVUTIL_ASSERT(hook_type == EVRPC_INPUT || hook_type == EVRPC_OUTPUT);
	}

	hook = mm_calloc(1, sizeof(struct evrpc_hook));
	EVUTIL_ASSERT(hook != NULL);

	hook->process = cb;
	hook->process_arg = cb_arg;
	TAILQ_INSERT_TAIL(head, hook, next);

	return (hook);
}

static int
evrpc_remove_hook_internal(struct evrpc_hook_list *head, void *handle)
{
	struct evrpc_hook *hook = NULL;
	TAILQ_FOREACH(hook, head, next) {
		if (hook == handle) {
			TAILQ_REMOVE(head, hook, next);
			mm_free(hook);
			return (1);
		}
	}

	return (0);
}

/*
 * remove the hook specified by the handle
 */

int
evrpc_remove_hook(void *vbase, enum EVRPC_HOOK_TYPE hook_type, void *handle)
{
	struct evrpc_hooks_ *base = vbase;
	struct evrpc_hook_list *head = NULL;
	switch (hook_type) {
	case EVRPC_INPUT:
		head = &base->in_hooks;
		break;
	case EVRPC_OUTPUT:
		head = &base->out_hooks;
		break;
	default:
		EVUTIL_ASSERT(hook_type == EVRPC_INPUT || hook_type == EVRPC_OUTPUT);
	}

	return (evrpc_remove_hook_internal(head, handle));
}

static int
evrpc_process_hooks(struct evrpc_hook_list *head, void *ctx,
    struct evhttp_request *req, struct evbuffer *evbuf)
{
	struct evrpc_hook *hook;
	TAILQ_FOREACH(hook, head, next) {
		int res = hook->process(ctx, req, evbuf, hook->process_arg);
		if (res != EVRPC_CONTINUE)
			return (res);
	}

	return (EVRPC_CONTINUE);
}

static void evrpc_pool_schedule(struct evrpc_pool *pool);
static void evrpc_request_cb(struct evhttp_request *, void *);

/*
 * Registers a new RPC with the HTTP server.   The evrpc object is expected
 * to have been filled in via the EVRPC_REGISTER_OBJECT macro which in turn
 * calls this function.
 */

static char *
evrpc_construct_uri(const char *uri)
{
	char *constructed_uri;
	size_t constructed_uri_len;

	constructed_uri_len = strlen(EVRPC_URI_PREFIX) + strlen(uri) + 1;
	if ((constructed_uri = mm_malloc(constructed_uri_len)) == NULL)
		event_err(1, "%s: failed to register rpc at %s",
		    __func__, uri);
	memcpy(constructed_uri, EVRPC_URI_PREFIX, strlen(EVRPC_URI_PREFIX));
	memcpy(constructed_uri + strlen(EVRPC_URI_PREFIX), uri, strlen(uri));
	constructed_uri[constructed_uri_len - 1] = '\0';

	return (constructed_uri);
}

int
evrpc_register_rpc(struct evrpc_base *base, struct evrpc *rpc,
    void (*cb)(struct evrpc_req_generic *, void *), void *cb_arg)
{
	char *constructed_uri = evrpc_construct_uri(rpc->uri);

	rpc->base = base;
	rpc->cb = cb;
	rpc->cb_arg = cb_arg;

	TAILQ_INSERT_TAIL(&base->registered_rpcs, rpc, next);

	evhttp_set_cb(base->http_server,
	    constructed_uri,
	    evrpc_request_cb,
	    rpc);

	mm_free(constructed_uri);

	return (0);
}

int
evrpc_unregister_rpc(struct evrpc_base *base, const char *name)
{
	char *registered_uri = NULL;
	struct evrpc *rpc;
	int r;

	/* find the right rpc; linear search might be slow */
	TAILQ_FOREACH(rpc, &base->registered_rpcs, next) {
		if (strcmp(rpc->uri, name) == 0)
			break;
	}
	if (rpc == NULL) {
		/* We did not find an RPC with this name */
		return (-1);
	}
	TAILQ_REMOVE(&base->registered_rpcs, rpc, next);

	registered_uri = evrpc_construct_uri(name);

	/* remove the http server callback */
	r = evhttp_del_cb(base->http_server, registered_uri);
	EVUTIL_ASSERT(r == 0);

	mm_free(registered_uri);

	mm_free((char *)rpc->uri);
	mm_free(rpc);
	return (0);
}

static int evrpc_pause_request(void *vbase, void *ctx,
    void (*cb)(void *, enum EVRPC_HOOK_RESULT));
static void evrpc_request_cb_closure(void *, enum EVRPC_HOOK_RESULT);

static void
evrpc_request_cb(struct evhttp_request *req, void *arg)
{
	struct evrpc *rpc = arg;
	struct evrpc_req_generic *rpc_state = NULL;

	/* let's verify the outside parameters */
	if (req->type != EVHTTP_REQ_POST ||
	    evbuffer_get_length(req->input_buffer) <= 0)
		goto error;

	rpc_state = mm_calloc(1, sizeof(struct evrpc_req_generic));
	if (rpc_state == NULL)
		goto error;
	rpc_state->rpc = rpc;
	rpc_state->http_req = req;
	rpc_state->rpc_data = NULL;

	if (TAILQ_FIRST(&rpc->base->input_hooks) != NULL) {
		int hook_res;

		evrpc_hook_associate_meta_(&rpc_state->hook_meta, req->evcon);

		/*
		 * allow hooks to modify the outgoing request
		 */
		hook_res = evrpc_process_hooks(&rpc->base->input_hooks,
		    rpc_state, req, req->input_buffer);
		switch (hook_res) {
		case EVRPC_TERMINATE:
			goto error;
		case EVRPC_PAUSE:
			evrpc_pause_request(rpc->base, rpc_state,
			    evrpc_request_cb_closure);
			return;
		case EVRPC_CONTINUE:
			break;
		default:
			EVUTIL_ASSERT(hook_res == EVRPC_TERMINATE ||
			    hook_res == EVRPC_CONTINUE ||
			    hook_res == EVRPC_PAUSE);
		}
	}

	evrpc_request_cb_closure(rpc_state, EVRPC_CONTINUE);
	return;

error:
	evrpc_reqstate_free_(rpc_state);
	evhttp_send_error(req, HTTP_SERVUNAVAIL, NULL);
	return;
}

static void
evrpc_request_cb_closure(void *arg, enum EVRPC_HOOK_RESULT hook_res)
{
	struct evrpc_req_generic *rpc_state = arg;
	struct evrpc *rpc;
	struct evhttp_request *req;

	EVUTIL_ASSERT(rpc_state);
	rpc = rpc_state->rpc;
	req = rpc_state->http_req;

	if (hook_res == EVRPC_TERMINATE)
		goto error;

	/* let's check that we can parse the request */
	rpc_state->request = rpc->request_new(rpc->request_new_arg);
	if (rpc_state->request == NULL)
		goto error;

	if (rpc->request_unmarshal(
		    rpc_state->request, req->input_buffer) == -1) {
		/* we failed to parse the request; that's a bummer */
		goto error;
	}

	/* at this point, we have a well formed request, prepare the reply */

	rpc_state->reply = rpc->reply_new(rpc->reply_new_arg);
	if (rpc_state->reply == NULL)
		goto error;

	/* give the rpc to the user; they can deal with it */
	rpc->cb(rpc_state, rpc->cb_arg);

	return;

error:
	evrpc_reqstate_free_(rpc_state);
	evhttp_send_error(req, HTTP_SERVUNAVAIL, NULL);
	return;
}


void
evrpc_reqstate_free_(struct evrpc_req_generic* rpc_state)
{
	struct evrpc *rpc;
	EVUTIL_ASSERT(rpc_state != NULL);
	rpc = rpc_state->rpc;

	/* clean up all memory */
	if (rpc_state->hook_meta != NULL)
		evrpc_hook_context_free_(rpc_state->hook_meta);
	if (rpc_state->request != NULL)
		rpc->request_free(rpc_state->request);
	if (rpc_state->reply != NULL)
		rpc->reply_free(rpc_state->reply);
	if (rpc_state->rpc_data != NULL)
		evbuffer_free(rpc_state->rpc_data);
	mm_free(rpc_state);
}

static void
evrpc_request_done_closure(void *, enum EVRPC_HOOK_RESULT);

void
evrpc_request_done(struct evrpc_req_generic *rpc_state)
{
	struct evhttp_request *req;
	struct evrpc *rpc;

	EVUTIL_ASSERT(rpc_state);

	req = rpc_state->http_req;
	rpc = rpc_state->rpc;

	if (rpc->reply_complete(rpc_state->reply) == -1) {
		/* the reply was not completely filled in.  error out */
		goto error;
	}

	if ((rpc_state->rpc_data = evbuffer_new()) == NULL) {
		/* out of memory */
		goto error;
	}

	/* serialize the reply */
	rpc->reply_marshal(rpc_state->rpc_data, rpc_state->reply);

	if (TAILQ_FIRST(&rpc->base->output_hooks) != NULL) {
		int hook_res;

		evrpc_hook_associate_meta_(&rpc_state->hook_meta, req->evcon);

		/* do hook based tweaks to the request */
		hook_res = evrpc_process_hooks(&rpc->base->output_hooks,
		    rpc_state, req, rpc_state->rpc_data);
		switch (hook_res) {
		case EVRPC_TERMINATE:
			goto error;
		case EVRPC_PAUSE:
			if (evrpc_pause_request(rpc->base, rpc_state,
				evrpc_request_done_closure) == -1)
				goto error;
			return;
		case EVRPC_CONTINUE:
			break;
		default:
			EVUTIL_ASSERT(hook_res == EVRPC_TERMINATE ||
			    hook_res == EVRPC_CONTINUE ||
			    hook_res == EVRPC_PAUSE);
		}
	}

	evrpc_request_done_closure(rpc_state, EVRPC_CONTINUE);
	return;

error:
	evrpc_reqstate_free_(rpc_state);
	evhttp_send_error(req, HTTP_SERVUNAVAIL, NULL);
	return;
}

void *
evrpc_get_request(struct evrpc_req_generic *req)
{
	return req->request;
}

void *
evrpc_get_reply(struct evrpc_req_generic *req)
{
	return req->reply;
}

static void
evrpc_request_done_closure(void *arg, enum EVRPC_HOOK_RESULT hook_res)
{
	struct evrpc_req_generic *rpc_state = arg;
	struct evhttp_request *req;
	EVUTIL_ASSERT(rpc_state);
	req = rpc_state->http_req;

	if (hook_res == EVRPC_TERMINATE)
		goto error;

	/* on success, we are going to transmit marshaled binary data */
	if (evhttp_find_header(req->output_headers, "Content-Type") == NULL) {
		evhttp_add_header(req->output_headers,
		    "Content-Type", "application/octet-stream");
	}
	evhttp_send_reply(req, HTTP_OK, "OK", rpc_state->rpc_data);

	evrpc_reqstate_free_(rpc_state);

	return;

error:
	evrpc_reqstate_free_(rpc_state);
	evhttp_send_error(req, HTTP_SERVUNAVAIL, NULL);
	return;
}


/* Client implementation of RPC site */

static int evrpc_schedule_request(struct evhttp_connection *connection,
    struct evrpc_request_wrapper *ctx);

struct evrpc_pool *
evrpc_pool_new(struct event_base *base)
{
	struct evrpc_pool *pool = mm_calloc(1, sizeof(struct evrpc_pool));
	if (pool == NULL)
		return (NULL);

	TAILQ_INIT(&pool->connections);
	TAILQ_INIT(&pool->requests);

	TAILQ_INIT(&pool->paused_requests);

	TAILQ_INIT(&pool->input_hooks);
	TAILQ_INIT(&pool->output_hooks);

	pool->base = base;
	pool->timeout = -1;

	return (pool);
}

static void
evrpc_request_wrapper_free(struct evrpc_request_wrapper *request)
{
	if (request->hook_meta != NULL)
		evrpc_hook_context_free_(request->hook_meta);
	mm_free(request->name);
	mm_free(request);
}

void
evrpc_pool_free(struct evrpc_pool *pool)
{
	struct evhttp_connection *connection;
	struct evrpc_request_wrapper *request;
	struct evrpc_hook_ctx *pause;
	struct evrpc_hook *hook;
	int r;

	while ((request = TAILQ_FIRST(&pool->requests)) != NULL) {
		TAILQ_REMOVE(&pool->requests, request, next);
		evrpc_request_wrapper_free(request);
	}

	while ((pause = TAILQ_FIRST(&pool->paused_requests)) != NULL) {
		TAILQ_REMOVE(&pool->paused_requests, pause, next);
		mm_free(pause);
	}

	while ((connection = TAILQ_FIRST(&pool->connections)) != NULL) {
		TAILQ_REMOVE(&pool->connections, connection, next);
		evhttp_connection_free(connection);
	}

	while ((hook = TAILQ_FIRST(&pool->input_hooks)) != NULL) {
		r = evrpc_remove_hook(pool, EVRPC_INPUT, hook);
		EVUTIL_ASSERT(r);
	}

	while ((hook = TAILQ_FIRST(&pool->output_hooks)) != NULL) {
		r = evrpc_remove_hook(pool, EVRPC_OUTPUT, hook);
		EVUTIL_ASSERT(r);
	}

	mm_free(pool);
}

/*
 * Add a connection to the RPC pool.   A request scheduled on the pool
 * may use any available connection.
 */

void
evrpc_pool_add_connection(struct evrpc_pool *pool,
    struct evhttp_connection *connection)
{
	EVUTIL_ASSERT(connection->http_server == NULL);
	TAILQ_INSERT_TAIL(&pool->connections, connection, next);

	/*
	 * associate an event base with this connection
	 */
	if (pool->base != NULL)
		evhttp_connection_set_base(connection, pool->base);

	/*
	 * unless a timeout was specifically set for a connection,
	 * the connection inherits the timeout from the pool.
	 */
	if (!evutil_timerisset(&connection->timeout))
		evhttp_connection_set_timeout(connection, pool->timeout);

	/*
	 * if we have any requests pending, schedule them with the new
	 * connections.
	 */

	if (TAILQ_FIRST(&pool->requests) != NULL) {
		struct evrpc_request_wrapper *request =
		    TAILQ_FIRST(&pool->requests);
		TAILQ_REMOVE(&pool->requests, request, next);
		evrpc_schedule_request(connection, request);
	}
}

void
evrpc_pool_remove_connection(struct evrpc_pool *pool,
    struct evhttp_connection *connection)
{
	TAILQ_REMOVE(&pool->connections, connection, next);
}

void
evrpc_pool_set_timeout(struct evrpc_pool *pool, int timeout_in_secs)
{
	struct evhttp_connection *evcon;
	TAILQ_FOREACH(evcon, &pool->connections, next) {
		evhttp_connection_set_timeout(evcon, timeout_in_secs);
	}
	pool->timeout = timeout_in_secs;
}


static void evrpc_reply_done(struct evhttp_request *, void *);
static void evrpc_request_timeout(evutil_socket_t, short, void *);

/*
 * Finds a connection object associated with the pool that is currently
 * idle and can be used to make a request.
 */
static struct evhttp_connection *
evrpc_pool_find_connection(struct evrpc_pool *pool)
{
	struct evhttp_connection *connection;
	TAILQ_FOREACH(connection, &pool->connections, next) {
		if (TAILQ_FIRST(&connection->requests) == NULL)
			return (connection);
	}

	return (NULL);
}

/*
 * Prototypes responsible for evrpc scheduling and hooking
 */

static void evrpc_schedule_request_closure(void *ctx, enum EVRPC_HOOK_RESULT);

/*
 * We assume that the ctx is no longer queued on the pool.
 */
static int
evrpc_schedule_request(struct evhttp_connection *connection,
    struct evrpc_request_wrapper *ctx)
{
	struct evhttp_request *req = NULL;
	struct evrpc_pool *pool = ctx->pool;
	struct evrpc_status status;

	if ((req = evhttp_request_new(evrpc_reply_done, ctx)) == NULL)
		goto error;

	/* serialize the request data into the output buffer */
	ctx->request_marshal(req->output_buffer, ctx->request);

	/* we need to know the connection that we might have to abort */
	ctx->evcon = connection;

	/* if we get paused we also need to know the request */
	ctx->req = req;

	if (TAILQ_FIRST(&pool->output_hooks) != NULL) {
		int hook_res;

		evrpc_hook_associate_meta_(&ctx->hook_meta, connection);

		/* apply hooks to the outgoing request */
		hook_res = evrpc_process_hooks(&pool->output_hooks,
		    ctx, req, req->output_buffer);

		switch (hook_res) {
		case EVRPC_TERMINATE:
			goto error;
		case EVRPC_PAUSE:
			/* we need to be explicitly resumed */
			if (evrpc_pause_request(pool, ctx,
				evrpc_schedule_request_closure) == -1)
				goto error;
			return (0);
		case EVRPC_CONTINUE:
			/* we can just continue */
			break;
		default:
			EVUTIL_ASSERT(hook_res == EVRPC_TERMINATE ||
			    hook_res == EVRPC_CONTINUE ||
			    hook_res == EVRPC_PAUSE);
		}
	}

	evrpc_schedule_request_closure(ctx, EVRPC_CONTINUE);
	return (0);

error:
	memset(&status, 0, sizeof(status));
	status.error = EVRPC_STATUS_ERR_UNSTARTED;
	(*ctx->cb)(&status, ctx->request, ctx->reply, ctx->cb_arg);
	evrpc_request_wrapper_free(ctx);
	return (-1);
}

static void
evrpc_schedule_request_closure(void *arg, enum EVRPC_HOOK_RESULT hook_res)
{
	struct evrpc_request_wrapper *ctx = arg;
	struct evhttp_connection *connection = ctx->evcon;
	struct evhttp_request *req = ctx->req;
	struct evrpc_pool *pool = ctx->pool;
	struct evrpc_status status;
	char *uri = NULL;
	int res = 0;

	if (hook_res == EVRPC_TERMINATE)
		goto error;

	uri = evrpc_construct_uri(ctx->name);
	if (uri == NULL)
		goto error;

	if (pool->timeout > 0) {
		/*
		 * a timeout after which the whole rpc is going to be aborted.
		 */
		struct timeval tv;
		evutil_timerclear(&tv);
		tv.tv_sec = pool->timeout;
		evtimer_add(&ctx->ev_timeout, &tv);
	}

	/* start the request over the connection */
	res = evhttp_make_request(connection, req, EVHTTP_REQ_POST, uri);
	mm_free(uri);

	if (res == -1)
		goto error;

	return;

error:
	memset(&status, 0, sizeof(status));
	status.error = EVRPC_STATUS_ERR_UNSTARTED;
	(*ctx->cb)(&status, ctx->request, ctx->reply, ctx->cb_arg);
	evrpc_request_wrapper_free(ctx);
}

/* we just queue the paused request on the pool under the req object */
static int
evrpc_pause_request(void *vbase, void *ctx,
    void (*cb)(void *, enum EVRPC_HOOK_RESULT))
{
	struct evrpc_hooks_ *base = vbase;
	struct evrpc_hook_ctx *pause = mm_malloc(sizeof(*pause));
	if (pause == NULL)
		return (-1);

	pause->ctx = ctx;
	pause->cb = cb;

	TAILQ_INSERT_TAIL(&base->pause_requests, pause, next);
	return (0);
}

int
evrpc_resume_request(void *vbase, void *ctx, enum EVRPC_HOOK_RESULT res)
{
	struct evrpc_hooks_ *base = vbase;
	struct evrpc_pause_list *head = &base->pause_requests;
	struct evrpc_hook_ctx *pause;

	TAILQ_FOREACH(pause, head, next) {
		if (pause->ctx == ctx)
			break;
	}

	if (pause == NULL)
		return (-1);

	(*pause->cb)(pause->ctx, res);
	TAILQ_REMOVE(head, pause, next);
	mm_free(pause);
	return (0);
}

int
evrpc_make_request(struct evrpc_request_wrapper *ctx)
{
	struct evrpc_pool *pool = ctx->pool;

	/* initialize the event structure for this rpc */
	evtimer_assign(&ctx->ev_timeout, pool->base, evrpc_request_timeout, ctx);

	/* we better have some available connections on the pool */
	EVUTIL_ASSERT(TAILQ_FIRST(&pool->connections) != NULL);

	/*
	 * if no connection is available, we queue the request on the pool,
	 * the next time a connection is empty, the rpc will be send on that.
	 */
	TAILQ_INSERT_TAIL(&pool->requests, ctx, next);

	evrpc_pool_schedule(pool);

	return (0);
}


struct evrpc_request_wrapper *
evrpc_make_request_ctx(
	struct evrpc_pool *pool, void *request, void *reply,
	const char *rpcname,
	void (*req_marshal)(struct evbuffer*, void *),
	void (*rpl_clear)(void *),
	int (*rpl_unmarshal)(void *, struct evbuffer *),
	void (*cb)(struct evrpc_status *, void *, void *, void *),
	void *cbarg)
{
	struct evrpc_request_wrapper *ctx = (struct evrpc_request_wrapper *)
	    mm_malloc(sizeof(struct evrpc_request_wrapper));
	if (ctx == NULL)
		return (NULL);

	ctx->pool = pool;
	ctx->hook_meta = NULL;
	ctx->evcon = NULL;
	ctx->name = mm_strdup(rpcname);
	if (ctx->name == NULL) {
		mm_free(ctx);
		return (NULL);
	}
	ctx->cb = cb;
	ctx->cb_arg = cbarg;
	ctx->request = request;
	ctx->reply = reply;
	ctx->request_marshal = req_marshal;
	ctx->reply_clear = rpl_clear;
	ctx->reply_unmarshal = rpl_unmarshal;

	return (ctx);
}

static void
evrpc_reply_done_closure(void *, enum EVRPC_HOOK_RESULT);

static void
evrpc_reply_done(struct evhttp_request *req, void *arg)
{
	struct evrpc_request_wrapper *ctx = arg;
	struct evrpc_pool *pool = ctx->pool;
	int hook_res = EVRPC_CONTINUE;

	/* cancel any timeout we might have scheduled */
	event_del(&ctx->ev_timeout);

	ctx->req = req;

	/* we need to get the reply now */
	if (req == NULL) {
		evrpc_reply_done_closure(ctx, EVRPC_CONTINUE);
		return;
	}

	if (TAILQ_FIRST(&pool->input_hooks) != NULL) {
		evrpc_hook_associate_meta_(&ctx->hook_meta, ctx->evcon);

		/* apply hooks to the incoming request */
		hook_res = evrpc_process_hooks(&pool->input_hooks,
		    ctx, req, req->input_buffer);

		switch (hook_res) {
		case EVRPC_TERMINATE:
		case EVRPC_CONTINUE:
			break;
		case EVRPC_PAUSE:
			/*
			 * if we get paused we also need to know the
			 * request.  unfortunately, the underlying
			 * layer is going to free it.  we need to
			 * request ownership explicitly
			 */
			if (req != NULL)
				evhttp_request_own(req);

			evrpc_pause_request(pool, ctx,
			    evrpc_reply_done_closure);
			return;
		default:
			EVUTIL_ASSERT(hook_res == EVRPC_TERMINATE ||
			    hook_res == EVRPC_CONTINUE ||
			    hook_res == EVRPC_PAUSE);
		}
	}

	evrpc_reply_done_closure(ctx, hook_res);

	/* http request is being freed by underlying layer */
}

static void
evrpc_reply_done_closure(void *arg, enum EVRPC_HOOK_RESULT hook_res)
{
	struct evrpc_request_wrapper *ctx = arg;
	struct evhttp_request *req = ctx->req;
	struct evrpc_pool *pool = ctx->pool;
	struct evrpc_status status;
	int res = -1;

	memset(&status, 0, sizeof(status));
	status.http_req = req;

	/* we need to get the reply now */
	if (req == NULL) {
		status.error = EVRPC_STATUS_ERR_TIMEOUT;
	} else if (hook_res == EVRPC_TERMINATE) {
		status.error = EVRPC_STATUS_ERR_HOOKABORTED;
	} else {
		res = ctx->reply_unmarshal(ctx->reply, req->input_buffer);
		if (res == -1)
			status.error = EVRPC_STATUS_ERR_BADPAYLOAD;
	}

	if (res == -1) {
		/* clear everything that we might have written previously */
		ctx->reply_clear(ctx->reply);
	}

	(*ctx->cb)(&status, ctx->request, ctx->reply, ctx->cb_arg);

	evrpc_request_wrapper_free(ctx);

	/* the http layer owned the original request structure, but if we
	 * got paused, we asked for ownership and need to free it here. */
	if (req != NULL && evhttp_request_is_owned(req))
		evhttp_request_free(req);

	/* see if we can schedule another request */
	evrpc_pool_schedule(pool);
}

static void
evrpc_pool_schedule(struct evrpc_pool *pool)
{
	struct evrpc_request_wrapper *ctx = TAILQ_FIRST(&pool->requests);
	struct evhttp_connection *evcon;

	/* if no requests are pending, we have no work */
	if (ctx == NULL)
		return;

	if ((evcon = evrpc_pool_find_connection(pool)) != NULL) {
		TAILQ_REMOVE(&pool->requests, ctx, next);
		evrpc_schedule_request(evcon, ctx);
	}
}

static void
evrpc_request_timeout(evutil_socket_t fd, short what, void *arg)
{
	struct evrpc_request_wrapper *ctx = arg;
	struct evhttp_connection *evcon = ctx->evcon;
	EVUTIL_ASSERT(evcon != NULL);

	evhttp_connection_fail_(evcon, EVREQ_HTTP_TIMEOUT);
}

/*
 * frees potential meta data associated with a request.
 */

static void
evrpc_meta_data_free(struct evrpc_meta_list *meta_data)
{
	struct evrpc_meta *entry;
	EVUTIL_ASSERT(meta_data != NULL);

	while ((entry = TAILQ_FIRST(meta_data)) != NULL) {
		TAILQ_REMOVE(meta_data, entry, next);
		mm_free(entry->key);
		mm_free(entry->data);
		mm_free(entry);
	}
}

static struct evrpc_hook_meta *
evrpc_hook_meta_new_(void)
{
	struct evrpc_hook_meta *ctx;
	ctx = mm_malloc(sizeof(struct evrpc_hook_meta));
	EVUTIL_ASSERT(ctx != NULL);

	TAILQ_INIT(&ctx->meta_data);
	ctx->evcon = NULL;

	return (ctx);
}

static void
evrpc_hook_associate_meta_(struct evrpc_hook_meta **pctx,
    struct evhttp_connection *evcon)
{
	struct evrpc_hook_meta *ctx = *pctx;
	if (ctx == NULL)
		*pctx = ctx = evrpc_hook_meta_new_();
	ctx->evcon = evcon;
}

static void
evrpc_hook_context_free_(struct evrpc_hook_meta *ctx)
{
	evrpc_meta_data_free(&ctx->meta_data);
	mm_free(ctx);
}

/* Adds meta data */
void
evrpc_hook_add_meta(void *ctx, const char *key,
    const void *data, size_t data_size)
{
	struct evrpc_request_wrapper *req = ctx;
	struct evrpc_hook_meta *store = NULL;
	struct evrpc_meta *meta = NULL;

	if ((store = req->hook_meta) == NULL)
		store = req->hook_meta = evrpc_hook_meta_new_();

	meta = mm_malloc(sizeof(struct evrpc_meta));
	EVUTIL_ASSERT(meta != NULL);
	meta->key = mm_strdup(key);
	EVUTIL_ASSERT(meta->key != NULL);
	meta->data_size = data_size;
	meta->data = mm_malloc(data_size);
	EVUTIL_ASSERT(meta->data != NULL);
	memcpy(meta->data, data, data_size);

	TAILQ_INSERT_TAIL(&store->meta_data, meta, next);
}

int
evrpc_hook_find_meta(void *ctx, const char *key, void **data, size_t *data_size)
{
	struct evrpc_request_wrapper *req = ctx;
	struct evrpc_meta *meta = NULL;

	if (req->hook_meta == NULL)
		return (-1);

	TAILQ_FOREACH(meta, &req->hook_meta->meta_data, next) {
		if (strcmp(meta->key, key) == 0) {
			*data = meta->data;
			*data_size = meta->data_size;
			return (0);
		}
	}

	return (-1);
}

struct evhttp_connection *
evrpc_hook_get_connection(void *ctx)
{
	struct evrpc_request_wrapper *req = ctx;
	return (req->hook_meta != NULL ? req->hook_meta->evcon : NULL);
}

int
evrpc_send_request_generic(struct evrpc_pool *pool,
    void *request, void *reply,
    void (*cb)(struct evrpc_status *, void *, void *, void *),
    void *cb_arg,
    const char *rpcname,
    void (*req_marshal)(struct evbuffer *, void *),
    void (*rpl_clear)(void *),
    int (*rpl_unmarshal)(void *, struct evbuffer *))
{
	struct evrpc_status status;
	struct evrpc_request_wrapper *ctx;
	ctx = evrpc_make_request_ctx(pool, request, reply,
	    rpcname, req_marshal, rpl_clear, rpl_unmarshal, cb, cb_arg);
	if (ctx == NULL)
		goto error;
	return (evrpc_make_request(ctx));
error:
	memset(&status, 0, sizeof(status));
	status.error = EVRPC_STATUS_ERR_UNSTARTED;
	(*(cb))(&status, request, reply, cb_arg);
	return (-1);
}

/** Takes a request object and fills it in with the right magic */
static struct evrpc *
evrpc_register_object(const char *name,
    void *(*req_new)(void*), void *req_new_arg, void (*req_free)(void *),
    int (*req_unmarshal)(void *, struct evbuffer *),
    void *(*rpl_new)(void*), void *rpl_new_arg, void (*rpl_free)(void *),
    int (*rpl_complete)(void *),
    void (*rpl_marshal)(struct evbuffer *, void *))
{
	struct evrpc* rpc = (struct evrpc *)mm_calloc(1, sizeof(struct evrpc));
	if (rpc == NULL)
		return (NULL);
	rpc->uri = mm_strdup(name);
	if (rpc->uri == NULL) {
		mm_free(rpc);
		return (NULL);
	}
	rpc->request_new = req_new;
	rpc->request_new_arg = req_new_arg;
	rpc->request_free = req_free;
	rpc->request_unmarshal = req_unmarshal;
	rpc->reply_new = rpl_new;
	rpc->reply_new_arg = rpl_new_arg;
	rpc->reply_free = rpl_free;
	rpc->reply_complete = rpl_complete;
	rpc->reply_marshal = rpl_marshal;
	return (rpc);
}

int
evrpc_register_generic(struct evrpc_base *base, const char *name,
    void (*callback)(struct evrpc_req_generic *, void *), void *cbarg,
    void *(*req_new)(void *), void *req_new_arg, void (*req_free)(void *),
    int (*req_unmarshal)(void *, struct evbuffer *),
    void *(*rpl_new)(void *), void *rpl_new_arg, void (*rpl_free)(void *),
    int (*rpl_complete)(void *),
    void (*rpl_marshal)(struct evbuffer *, void *))
{
	struct evrpc* rpc =
	    evrpc_register_object(name, req_new, req_new_arg, req_free, req_unmarshal,
		rpl_new, rpl_new_arg, rpl_free, rpl_complete, rpl_marshal);
	if (rpc == NULL)
		return (-1);
	evrpc_register_rpc(base, rpc,
	    (void (*)(struct evrpc_req_generic*, void *))callback, cbarg);
	return (0);
}

/** accessors for obscure and undocumented functionality */
struct evrpc_pool *
evrpc_request_get_pool(struct evrpc_request_wrapper *ctx)
{
	return (ctx->pool);
}

void
evrpc_request_set_pool(struct evrpc_request_wrapper *ctx,
    struct evrpc_pool *pool)
{
	ctx->pool = pool;
}

void
evrpc_request_set_cb(struct evrpc_request_wrapper *ctx,
    void (*cb)(struct evrpc_status*, void *request, void *reply, void *arg),
    void *cb_arg)
{
	ctx->cb = cb;
	ctx->cb_arg = cb_arg;
}
