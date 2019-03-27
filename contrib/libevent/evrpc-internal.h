/*
 * Copyright (c) 2006-2007 Niels Provos <provos@citi.umich.edu>
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
#ifndef EVRPC_INTERNAL_H_INCLUDED_
#define EVRPC_INTERNAL_H_INCLUDED_

#include "event2/http.h"
#include "http-internal.h"

struct evrpc;
struct evrpc_request_wrapper;

#define EVRPC_URI_PREFIX "/.rpc."

struct evrpc_hook {
	TAILQ_ENTRY(evrpc_hook) next;

	/* returns EVRPC_TERMINATE; if the rpc should be aborted.
	 * a hook is is allowed to rewrite the evbuffer
	 */
	int (*process)(void *, struct evhttp_request *,
	    struct evbuffer *, void *);
	void *process_arg;
};

TAILQ_HEAD(evrpc_hook_list, evrpc_hook);

/*
 * this is shared between the base and the pool, so that we can reuse
 * the hook adding functions; we alias both evrpc_pool and evrpc_base
 * to this common structure.
 */

struct evrpc_hook_ctx;
TAILQ_HEAD(evrpc_pause_list, evrpc_hook_ctx);

struct evrpc_hooks_ {
	/* hooks for processing outbound and inbound rpcs */
	struct evrpc_hook_list in_hooks;
	struct evrpc_hook_list out_hooks;

	struct evrpc_pause_list pause_requests;
};

#define input_hooks common.in_hooks
#define output_hooks common.out_hooks
#define paused_requests common.pause_requests

struct evrpc_base {
	struct evrpc_hooks_ common;

	/* the HTTP server under which we register our RPC calls */
	struct evhttp* http_server;

	/* a list of all RPCs registered with us */
	TAILQ_HEAD(evrpc_list, evrpc) registered_rpcs;
};

struct evrpc_req_generic;
void evrpc_reqstate_free_(struct evrpc_req_generic* rpc_state);

/* A pool for holding evhttp_connection objects */
struct evrpc_pool {
	struct evrpc_hooks_ common;

	struct event_base *base;

	struct evconq connections;

	int timeout;

	TAILQ_HEAD(evrpc_requestq, evrpc_request_wrapper) (requests);
};

struct evrpc_hook_ctx {
	TAILQ_ENTRY(evrpc_hook_ctx) next;

	void *ctx;
	void (*cb)(void *, enum EVRPC_HOOK_RESULT);
};

struct evrpc_meta {
	TAILQ_ENTRY(evrpc_meta) next;
	char *key;

	void *data;
	size_t data_size;
};

TAILQ_HEAD(evrpc_meta_list, evrpc_meta);

struct evrpc_hook_meta {
	struct evrpc_meta_list meta_data;
	struct evhttp_connection *evcon;
};

/* allows association of meta data with a request */
static void evrpc_hook_associate_meta_(struct evrpc_hook_meta **pctx,
    struct evhttp_connection *evcon);

/* creates a new meta data store */
static struct evrpc_hook_meta *evrpc_hook_meta_new_(void);

/* frees the meta data associated with a request */
static void evrpc_hook_context_free_(struct evrpc_hook_meta *ctx);

/* the server side of an rpc */

/* We alias the RPC specific structs to this voided one */
struct evrpc_req_generic {
	/*
	 * allows association of meta data via hooks - needs to be
	 * synchronized with evrpc_request_wrapper
	 */
	struct evrpc_hook_meta *hook_meta;

	/* the unmarshaled request object */
	void *request;

	/* the empty reply object that needs to be filled in */
	void *reply;

	/*
	 * the static structure for this rpc; that can be used to
	 * automatically unmarshal and marshal the http buffers.
	 */
	struct evrpc *rpc;

	/*
	 * the http request structure on which we need to answer.
	 */
	struct evhttp_request* http_req;

	/*
	 * Temporary data store for marshaled data
	 */
	struct evbuffer* rpc_data;
};

/* the client side of an rpc request */
struct evrpc_request_wrapper {
	/*
	 * allows association of meta data via hooks - needs to be
	 * synchronized with evrpc_req_generic.
	 */
	struct evrpc_hook_meta *hook_meta;

	TAILQ_ENTRY(evrpc_request_wrapper) next;

	/* pool on which this rpc request is being made */
	struct evrpc_pool *pool;

	/* connection on which the request is being sent */
	struct evhttp_connection *evcon;

	/* the actual  request */
	struct evhttp_request *req;

	/* event for implementing request timeouts */
	struct event ev_timeout;

	/* the name of the rpc */
	char *name;

	/* callback */
	void (*cb)(struct evrpc_status*, void *request, void *reply, void *arg);
	void *cb_arg;

	void *request;
	void *reply;

	/* unmarshals the buffer into the proper request structure */
	void (*request_marshal)(struct evbuffer *, void *);

	/* removes all stored state in the reply */
	void (*reply_clear)(void *);

	/* marshals the reply into a buffer */
	int (*reply_unmarshal)(void *, struct evbuffer*);
};

#endif /* EVRPC_INTERNAL_H_INCLUDED_ */
