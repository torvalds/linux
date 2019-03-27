/*
 * libunbound/context.c - validating context for unbound internal use
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
 * This file contains the validator context structure.
 */
#include "config.h"
#include "libunbound/context.h"
#include "util/module.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "services/modstack.h"
#include "services/localzone.h"
#include "services/cache/rrset.h"
#include "services/cache/infra.h"
#include "services/authzone.h"
#include "util/data/msgreply.h"
#include "util/storage/slabhash.h"
#include "sldns/sbuffer.h"

int 
context_finalize(struct ub_ctx* ctx)
{
	struct config_file* cfg = ctx->env->cfg;
	verbosity = cfg->verbosity;
	if(ctx->logfile_override)
		log_file(ctx->log_out);
	else	log_init(cfg->logfile, cfg->use_syslog, NULL);
	config_apply(cfg);
	if(!modstack_setup(&ctx->mods, cfg->module_conf, ctx->env))
		return UB_INITFAIL;
	log_edns_known_options(VERB_ALGO, ctx->env);
	ctx->local_zones = local_zones_create();
	if(!ctx->local_zones)
		return UB_NOMEM;
	if(!local_zones_apply_cfg(ctx->local_zones, cfg))
		return UB_INITFAIL;
	if(!auth_zones_apply_cfg(ctx->env->auth_zones, cfg, 1))
		return UB_INITFAIL;
	if(!slabhash_is_size(ctx->env->msg_cache, cfg->msg_cache_size,
		cfg->msg_cache_slabs)) {
		slabhash_delete(ctx->env->msg_cache);
		ctx->env->msg_cache = slabhash_create(cfg->msg_cache_slabs,
			HASH_DEFAULT_STARTARRAY, cfg->msg_cache_size,
			msgreply_sizefunc, query_info_compare,
			query_entry_delete, reply_info_delete, NULL);
		if(!ctx->env->msg_cache)
			return UB_NOMEM;
	}
	ctx->env->rrset_cache = rrset_cache_adjust(ctx->env->rrset_cache,
		ctx->env->cfg, ctx->env->alloc);
	if(!ctx->env->rrset_cache)
		return UB_NOMEM;
	ctx->env->infra_cache = infra_adjust(ctx->env->infra_cache, cfg);
	if(!ctx->env->infra_cache)
		return UB_NOMEM;
	ctx->finalized = 1;
	return UB_NOERROR;
}

int context_query_cmp(const void* a, const void* b)
{
	if( *(int*)a < *(int*)b )
		return -1;
	if( *(int*)a > *(int*)b )
		return 1;
	return 0;
}

void
context_query_delete(struct ctx_query* q) 
{
	if(!q) return;
	ub_resolve_free(q->res);
	free(q->msg);
	free(q);
}

/** How many times to try to find an unused query-id-number for async */
#define NUM_ID_TRIES 100000
/** find next useful id number of 0 on error */
static int
find_id(struct ub_ctx* ctx, int* id)
{
	size_t tries = 0;
	ctx->next_querynum++;
	while(rbtree_search(&ctx->queries, &ctx->next_querynum)) {
		ctx->next_querynum++; /* numerical wraparound is fine */
		if(tries++ > NUM_ID_TRIES)
			return 0;
	}
	*id = ctx->next_querynum;
	return 1;
}

struct ctx_query* 
context_new(struct ub_ctx* ctx, const char* name, int rrtype, int rrclass, 
	ub_callback_type cb, ub_event_callback_type cb_event, void* cbarg)
{
	struct ctx_query* q = (struct ctx_query*)calloc(1, sizeof(*q));
	if(!q) return NULL;
	lock_basic_lock(&ctx->cfglock);
	if(!find_id(ctx, &q->querynum)) {
		lock_basic_unlock(&ctx->cfglock);
		free(q);
		return NULL;
	}
	lock_basic_unlock(&ctx->cfglock);
	q->node.key = &q->querynum;
	q->async = (cb != NULL || cb_event != NULL);
	q->cb = cb;
	q->cb_event = cb_event;
	q->cb_arg = cbarg;
	q->res = (struct ub_result*)calloc(1, sizeof(*q->res));
	if(!q->res) {
		free(q);
		return NULL;
	}
	q->res->qname = strdup(name);
	if(!q->res->qname) {
		free(q->res);
		free(q);
		return NULL;
	}
	q->res->qtype = rrtype;
	q->res->qclass = rrclass;

	/* add to query list */
	lock_basic_lock(&ctx->cfglock);
	if(q->async)
		ctx->num_async ++;
	(void)rbtree_insert(&ctx->queries, &q->node);
	lock_basic_unlock(&ctx->cfglock);
	return q;
}

struct alloc_cache* 
context_obtain_alloc(struct ub_ctx* ctx, int locking)
{
	struct alloc_cache* a;
	int tnum = 0;
	if(locking) {
		lock_basic_lock(&ctx->cfglock);
	}
	a = ctx->alloc_list;
	if(a)
		ctx->alloc_list = a->super; /* snip off list */
	else	tnum = ctx->thr_next_num++;
	if(locking) {
		lock_basic_unlock(&ctx->cfglock);
	}
	if(a) {
		a->super = &ctx->superalloc;
		return a;
	}
	a = (struct alloc_cache*)calloc(1, sizeof(*a));
	if(!a)
		return NULL;
	alloc_init(a, &ctx->superalloc, tnum);
	return a;
}

void 
context_release_alloc(struct ub_ctx* ctx, struct alloc_cache* alloc,
	int locking)
{
	if(!ctx || !alloc)
		return;
	if(locking) {
		lock_basic_lock(&ctx->cfglock);
	}
	alloc->super = ctx->alloc_list;
	ctx->alloc_list = alloc;
	if(locking) {
		lock_basic_unlock(&ctx->cfglock);
	}
}

uint8_t* 
context_serialize_new_query(struct ctx_query* q, uint32_t* len)
{
	/* format for new query is
	 * 	o uint32 cmd
	 * 	o uint32 id
	 * 	o uint32 type
	 * 	o uint32 class
	 * 	o rest queryname (string)
	 */
	uint8_t* p;
	size_t slen = strlen(q->res->qname) + 1/*end of string*/;
	*len = sizeof(uint32_t)*4 + slen;
	p = (uint8_t*)malloc(*len);
	if(!p) return NULL;
	sldns_write_uint32(p, UB_LIBCMD_NEWQUERY);
	sldns_write_uint32(p+sizeof(uint32_t), (uint32_t)q->querynum);
	sldns_write_uint32(p+2*sizeof(uint32_t), (uint32_t)q->res->qtype);
	sldns_write_uint32(p+3*sizeof(uint32_t), (uint32_t)q->res->qclass);
	memmove(p+4*sizeof(uint32_t), q->res->qname, slen);
	return p;
}

struct ctx_query* 
context_deserialize_new_query(struct ub_ctx* ctx, uint8_t* p, uint32_t len)
{
	struct ctx_query* q = (struct ctx_query*)calloc(1, sizeof(*q));
	if(!q) return NULL;
	if(len < 4*sizeof(uint32_t)+1) {
		free(q);
		return NULL;
	}
	log_assert( sldns_read_uint32(p) == UB_LIBCMD_NEWQUERY);
	q->querynum = (int)sldns_read_uint32(p+sizeof(uint32_t));
	q->node.key = &q->querynum;
	q->async = 1;
	q->res = (struct ub_result*)calloc(1, sizeof(*q->res));
	if(!q->res) {
		free(q);
		return NULL;
	}
	q->res->qtype = (int)sldns_read_uint32(p+2*sizeof(uint32_t));
	q->res->qclass = (int)sldns_read_uint32(p+3*sizeof(uint32_t));
	q->res->qname = strdup((char*)(p+4*sizeof(uint32_t)));
	if(!q->res->qname) {
		free(q->res);
		free(q);
		return NULL;
	}

	/** add to query list */
	ctx->num_async++;
	(void)rbtree_insert(&ctx->queries, &q->node);
	return q;
}

struct ctx_query* 
context_lookup_new_query(struct ub_ctx* ctx, uint8_t* p, uint32_t len)
{
	struct ctx_query* q;
	int querynum;
	if(len < 4*sizeof(uint32_t)+1) {
		return NULL;
	}
	log_assert( sldns_read_uint32(p) == UB_LIBCMD_NEWQUERY);
	querynum = (int)sldns_read_uint32(p+sizeof(uint32_t));
	q = (struct ctx_query*)rbtree_search(&ctx->queries, &querynum);
	if(!q) {
		return NULL;
	}
	log_assert(q->async);
	return q;
}

uint8_t* 
context_serialize_answer(struct ctx_query* q, int err, sldns_buffer* pkt,
	uint32_t* len)
{
	/* answer format
	 * 	o uint32 cmd
	 * 	o uint32 id
	 * 	o uint32 error_code
	 * 	o uint32 msg_security
	 * 	o uint32 was_ratelimited
	 * 	o uint32 length of why_bogus string (+1 for eos); 0 absent.
	 * 	o why_bogus_string
	 * 	o the remainder is the answer msg from resolver lookup.
	 * 	  remainder can be length 0.
	 */
	size_t size_of_uint32s = 6 * sizeof(uint32_t);
	size_t pkt_len = pkt?sldns_buffer_remaining(pkt):0;
	size_t wlen = (pkt&&q->res->why_bogus)?strlen(q->res->why_bogus)+1:0;
	uint8_t* p;
	*len = size_of_uint32s + pkt_len + wlen;
	p = (uint8_t*)malloc(*len);
	if(!p) return NULL;
	sldns_write_uint32(p, UB_LIBCMD_ANSWER);
	sldns_write_uint32(p+sizeof(uint32_t), (uint32_t)q->querynum);
	sldns_write_uint32(p+2*sizeof(uint32_t), (uint32_t)err);
	sldns_write_uint32(p+3*sizeof(uint32_t), (uint32_t)q->msg_security);
	sldns_write_uint32(p+4*sizeof(uint32_t), (uint32_t)q->res->was_ratelimited);
	sldns_write_uint32(p+5*sizeof(uint32_t), (uint32_t)wlen);
	if(wlen > 0)
		memmove(p+size_of_uint32s, q->res->why_bogus, wlen);
	if(pkt_len > 0)
		memmove(p+size_of_uint32s+wlen,
			sldns_buffer_begin(pkt), pkt_len);
	return p;
}

struct ctx_query* 
context_deserialize_answer(struct ub_ctx* ctx,
        uint8_t* p, uint32_t len, int* err)
{
	size_t size_of_uint32s = 6 * sizeof(uint32_t);
	struct ctx_query* q = NULL ;
	int id;
	size_t wlen;
	if(len < size_of_uint32s) return NULL;
	log_assert( sldns_read_uint32(p) == UB_LIBCMD_ANSWER);
	id = (int)sldns_read_uint32(p+sizeof(uint32_t));
	q = (struct ctx_query*)rbtree_search(&ctx->queries, &id);
	if(!q) return NULL; 
	*err = (int)sldns_read_uint32(p+2*sizeof(uint32_t));
	q->msg_security = sldns_read_uint32(p+3*sizeof(uint32_t));
	q->res->was_ratelimited = (int)sldns_read_uint32(p+4*sizeof(uint32_t));
	wlen = (size_t)sldns_read_uint32(p+5*sizeof(uint32_t));
	if(len > size_of_uint32s && wlen > 0) {
		if(len >= size_of_uint32s+wlen)
			q->res->why_bogus = (char*)memdup(
				p+size_of_uint32s, wlen);
		if(!q->res->why_bogus) {
			/* pass malloc failure to the user callback */
			q->msg_len = 0;
			*err = UB_NOMEM;
			return q;
		}
		q->res->why_bogus[wlen-1] = 0; /* zero terminated for sure */
	}
	if(len > size_of_uint32s+wlen) {
		q->msg_len = len - size_of_uint32s - wlen;
		q->msg = (uint8_t*)memdup(p+size_of_uint32s+wlen,
			q->msg_len);
		if(!q->msg) {
			/* pass malloc failure to the user callback */
			q->msg_len = 0;
			*err = UB_NOMEM;
			return q;
		}
	} 
	return q;
}

uint8_t* 
context_serialize_cancel(struct ctx_query* q, uint32_t* len)
{
	/* format of cancel:
	 * 	o uint32 cmd
	 * 	o uint32 async-id */
	uint8_t* p = (uint8_t*)reallocarray(NULL, sizeof(uint32_t), 2);
	if(!p) return NULL;
	*len = 2*sizeof(uint32_t);
	sldns_write_uint32(p, UB_LIBCMD_CANCEL);
	sldns_write_uint32(p+sizeof(uint32_t), (uint32_t)q->querynum);
	return p;
}

struct ctx_query* context_deserialize_cancel(struct ub_ctx* ctx,
        uint8_t* p, uint32_t len)
{
	struct ctx_query* q;
	int id;
	if(len != 2*sizeof(uint32_t)) return NULL;
	log_assert( sldns_read_uint32(p) == UB_LIBCMD_CANCEL);
	id = (int)sldns_read_uint32(p+sizeof(uint32_t));
	q = (struct ctx_query*)rbtree_search(&ctx->queries, &id);
	return q;
}

uint8_t* 
context_serialize_quit(uint32_t* len)
{
	uint32_t* p = (uint32_t*)malloc(sizeof(uint32_t));
	if(!p)
		return NULL;
	*len = sizeof(uint32_t);
	sldns_write_uint32(p, UB_LIBCMD_QUIT);
	return (uint8_t*)p;
}

enum ub_ctx_cmd context_serial_getcmd(uint8_t* p, uint32_t len)
{
	uint32_t v;
	if((size_t)len < sizeof(v))
		return UB_LIBCMD_QUIT;
	v = sldns_read_uint32(p);
	return v;
}
