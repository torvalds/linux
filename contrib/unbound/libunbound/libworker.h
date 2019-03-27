/*
 * libunbound/libworker.h - worker thread or process that resolves
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains the worker process or thread that performs
 * the DNS resolving and validation. The worker is called by a procedure
 * and if in the background continues until exit, if in the foreground
 * returns from the procedure when done.
 */
#ifndef LIBUNBOUND_LIBWORKER_H
#define LIBUNBOUND_LIBWORKER_H
#include "util/data/packed_rrset.h"
struct ub_ctx;
struct ub_result;
struct module_env;
struct comm_base;
struct outside_network;
struct ub_randstate;
struct ctx_query;
struct outbound_entry;
struct module_qstate;
struct comm_point;
struct comm_reply;
struct regional;
struct tube;
struct sldns_buffer;
struct ub_event_base;
struct query_info;

/** 
 * The library-worker status structure
 * Internal to the worker.
 */
struct libworker {
	/** every worker has a unique thread_num. (first in struct) */
	int thread_num;
	/** context we are operating under */
	struct ub_ctx* ctx;

	/** is this the bg worker? */
	int is_bg;
	/** is this a bg worker that is threaded (not forked)? */
	int is_bg_thread;
	/** want to quit, stop handling new content */
	int want_quit;

	/** copy of the module environment with worker local entries. */
	struct module_env* env;
	/** the event base this worker works with */
	struct comm_base* base;
	/** the backside outside network interface to the auth servers */
	struct outside_network* back;
	/** random() table for this worker. */
	struct ub_randstate* rndstate;
	/** sslcontext for SSL wrapped DNS over TCP queries */
	void* sslctx;
};

/**
 * Create a background worker
 * @param ctx: is updated with pid/tid of the background worker.
 *	a new allocation cache is obtained from ctx. It contains the
 *	threadnumber and unique id for further (shared) cache insertions.
 * @return 0 if OK, else error.
 *	Further communication is done via the pipes in ctx. 
 */
int libworker_bg(struct ub_ctx* ctx);

/**
 * Create a foreground worker.
 * This worker will join the threadpool of resolver threads.
 * It exits when the query answer has been obtained (or error).
 * This routine blocks until the worker is finished.
 * @param ctx: new allocation cache obtained and returned to it.
 * @param q: query (result is stored in here).
 * @return 0 if finished OK, else error.
 */
int libworker_fg(struct ub_ctx* ctx, struct ctx_query* q);

/**
 * create worker for event-based interface.
 * @param ctx: context with config.
 * @param eb: event base.
 * @return new worker or NULL.
 */
struct libworker* libworker_create_event(struct ub_ctx* ctx,
	struct ub_event_base* eb);

/**
 * Attach context_query to mesh for callback in event-driven setup.
 * @param ctx: context
 * @param q: context query entry
 * @param async_id: store query num if query takes long.
 * @return 0 if finished OK, else error.
 */
int libworker_attach_mesh(struct ub_ctx* ctx, struct ctx_query* q,
	int* async_id);

/** 
 * delete worker for event-based interface.  does not free the event_base.
 * @param w: event-based worker to delete.
 */
void libworker_delete_event(struct libworker* w);

/** cleanup the cache to remove all rrset IDs from it, arg is libworker */
void libworker_alloc_cleanup(void* arg);

/** 
 * fill result from parsed message, on error fills servfail 
 * @param res: is clear at start, filled in at end.
 * @param buf: contains DNS message.
 * @param temp: temporary buffer for parse.
 * @param msg_security: security status of the DNS message.
 *   On error, the res may contain a different status 
 *   (out of memory is not secure, not bogus).
 */
void libworker_enter_result(struct ub_result* res, struct sldns_buffer* buf,
	struct regional* temp, enum sec_status msg_security);

#endif /* LIBUNBOUND_LIBWORKER_H */
