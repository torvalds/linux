/*
 * libunbound/worker.c - worker thread or process that resolves
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
#include "config.h"
#ifdef HAVE_SSL
#include <openssl/ssl.h>
#endif
#include "libunbound/libworker.h"
#include "libunbound/context.h"
#include "libunbound/unbound.h"
#include "libunbound/worker.h"
#include "libunbound/unbound-event.h"
#include "services/outside_network.h"
#include "services/mesh.h"
#include "services/localzone.h"
#include "services/cache/rrset.h"
#include "services/outbound_list.h"
#include "services/authzone.h"
#include "util/fptr_wlist.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/random.h"
#include "util/config_file.h"
#include "util/netevent.h"
#include "util/storage/lookup3.h"
#include "util/storage/slabhash.h"
#include "util/net_help.h"
#include "util/data/dname.h"
#include "util/data/msgreply.h"
#include "util/data/msgencode.h"
#include "util/tube.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_hints.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"

/** handle new query command for bg worker */
static void handle_newq(struct libworker* w, uint8_t* buf, uint32_t len);

/** delete libworker env */
static void
libworker_delete_env(struct libworker* w)
{
	if(w->env) {
		outside_network_quit_prepare(w->back);
		mesh_delete(w->env->mesh);
		context_release_alloc(w->ctx, w->env->alloc, 
			!w->is_bg || w->is_bg_thread);
		sldns_buffer_free(w->env->scratch_buffer);
		regional_destroy(w->env->scratch);
		forwards_delete(w->env->fwds);
		hints_delete(w->env->hints);
		ub_randfree(w->env->rnd);
		free(w->env);
	}
#ifdef HAVE_SSL
	SSL_CTX_free(w->sslctx);
#endif
	outside_network_delete(w->back);
}

/** delete libworker struct */
static void
libworker_delete(struct libworker* w)
{
	if(!w) return;
	libworker_delete_env(w);
	comm_base_delete(w->base);
	free(w);
}

void
libworker_delete_event(struct libworker* w)
{
	if(!w) return;
	libworker_delete_env(w);
	comm_base_delete_no_base(w->base);
	free(w);
}

/** setup fresh libworker struct */
static struct libworker*
libworker_setup(struct ub_ctx* ctx, int is_bg, struct ub_event_base* eb)
{
	unsigned int seed;
	struct libworker* w = (struct libworker*)calloc(1, sizeof(*w));
	struct config_file* cfg = ctx->env->cfg;
	int* ports;
	int numports;
	if(!w) return NULL;
	w->is_bg = is_bg;
	w->ctx = ctx;
	w->env = (struct module_env*)malloc(sizeof(*w->env));
	if(!w->env) {
		free(w);
		return NULL;
	}
	*w->env = *ctx->env;
	w->env->alloc = context_obtain_alloc(ctx, !w->is_bg || w->is_bg_thread);
	if(!w->env->alloc) {
		libworker_delete(w);
		return NULL;
	}
	w->thread_num = w->env->alloc->thread_num;
	alloc_set_id_cleanup(w->env->alloc, &libworker_alloc_cleanup, w);
	if(!w->is_bg || w->is_bg_thread) {
		lock_basic_lock(&ctx->cfglock);
	}
	w->env->scratch = regional_create_custom(cfg->msg_buffer_size);
	w->env->scratch_buffer = sldns_buffer_new(cfg->msg_buffer_size);
	w->env->fwds = forwards_create();
	if(w->env->fwds && !forwards_apply_cfg(w->env->fwds, cfg)) { 
		forwards_delete(w->env->fwds);
		w->env->fwds = NULL;
	}
	w->env->hints = hints_create();
	if(w->env->hints && !hints_apply_cfg(w->env->hints, cfg)) { 
		hints_delete(w->env->hints);
		w->env->hints = NULL;
	}
	if(cfg->ssl_upstream || (cfg->tls_cert_bundle && cfg->tls_cert_bundle[0]) || cfg->tls_win_cert) {
		w->sslctx = connect_sslctx_create(NULL, NULL,
			cfg->tls_cert_bundle, cfg->tls_win_cert);
		if(!w->sslctx) {
			/* to make the setup fail after unlock */
			hints_delete(w->env->hints);
			w->env->hints = NULL;
		}
	}
	if(!w->is_bg || w->is_bg_thread) {
		lock_basic_unlock(&ctx->cfglock);
	}
	if(!w->env->scratch || !w->env->scratch_buffer || !w->env->fwds ||
		!w->env->hints) {
		libworker_delete(w);
		return NULL;
	}
	w->env->worker = (struct worker*)w;
	w->env->probe_timer = NULL;
	seed = (unsigned int)time(NULL) ^ (unsigned int)getpid() ^
		(((unsigned int)w->thread_num)<<17);
	seed ^= (unsigned int)w->env->alloc->next_id;
	if(!w->is_bg || w->is_bg_thread) {
		lock_basic_lock(&ctx->cfglock);
	}
	if(!(w->env->rnd = ub_initstate(seed, ctx->seed_rnd))) {
		if(!w->is_bg || w->is_bg_thread) {
			lock_basic_unlock(&ctx->cfglock);
		}
		explicit_bzero(&seed, sizeof(seed));
		libworker_delete(w);
		return NULL;
	}
	if(!w->is_bg || w->is_bg_thread) {
		lock_basic_unlock(&ctx->cfglock);
	}
	if(1) {
		/* primitive lockout for threading: if it overwrites another
		 * thread it is like wiping the cache (which is likely empty
		 * at the start) */
		/* note we are holding the ctx lock in normal threaded
		 * cases so that is solved properly, it is only for many ctx
		 * in different threads that this may clash */
		static int done_raninit = 0;
		if(!done_raninit) {
			done_raninit = 1;
			hash_set_raninit((uint32_t)ub_random(w->env->rnd));
		}
	}
	explicit_bzero(&seed, sizeof(seed));

	if(eb)
		w->base = comm_base_create_event(eb);
	else	w->base = comm_base_create(0);
	if(!w->base) {
		libworker_delete(w);
		return NULL;
	}
	w->env->worker_base = w->base;
	if(!w->is_bg || w->is_bg_thread) {
		lock_basic_lock(&ctx->cfglock);
	}
	numports = cfg_condense_ports(cfg, &ports);
	if(numports == 0) {
		int locked = !w->is_bg || w->is_bg_thread;
		libworker_delete(w);
		if(locked) {
			lock_basic_unlock(&ctx->cfglock);
		}
		return NULL;
	}
	w->back = outside_network_create(w->base, cfg->msg_buffer_size,
		(size_t)cfg->outgoing_num_ports, cfg->out_ifs,
		cfg->num_out_ifs, cfg->do_ip4, cfg->do_ip6, 
		cfg->do_tcp?cfg->outgoing_num_tcp:0,
		w->env->infra_cache, w->env->rnd, cfg->use_caps_bits_for_id,
		ports, numports, cfg->unwanted_threshold,
		cfg->outgoing_tcp_mss, &libworker_alloc_cleanup, w,
		cfg->do_udp || cfg->udp_upstream_without_downstream, w->sslctx,
		cfg->delay_close, NULL);
	w->env->outnet = w->back;
	if(!w->is_bg || w->is_bg_thread) {
		lock_basic_unlock(&ctx->cfglock);
	}
	free(ports);
	if(!w->back) {
		libworker_delete(w);
		return NULL;
	}
	w->env->mesh = mesh_create(&ctx->mods, w->env);
	if(!w->env->mesh) {
		libworker_delete(w);
		return NULL;
	}
	w->env->send_query = &libworker_send_query;
	w->env->detach_subs = &mesh_detach_subs;
	w->env->attach_sub = &mesh_attach_sub;
	w->env->add_sub = &mesh_add_sub;
	w->env->kill_sub = &mesh_state_delete;
	w->env->detect_cycle = &mesh_detect_cycle;
	comm_base_timept(w->base, &w->env->now, &w->env->now_tv);
	return w;
}

struct libworker* libworker_create_event(struct ub_ctx* ctx,
	struct ub_event_base* eb)
{
	return libworker_setup(ctx, 0, eb);
}

/** handle cancel command for bg worker */
static void
handle_cancel(struct libworker* w, uint8_t* buf, uint32_t len)
{
	struct ctx_query* q;
	if(w->is_bg_thread) {
		lock_basic_lock(&w->ctx->cfglock);
		q = context_deserialize_cancel(w->ctx, buf, len);
		lock_basic_unlock(&w->ctx->cfglock);
	} else {
		q = context_deserialize_cancel(w->ctx, buf, len);
	}
	if(!q) {
		/* probably simply lookup failed, i.e. the message had been
		 * processed and answered before the cancel arrived */
		return;
	}
	q->cancelled = 1;
	free(buf);
}

/** do control command coming into bg server */
static void
libworker_do_cmd(struct libworker* w, uint8_t* msg, uint32_t len)
{
	switch(context_serial_getcmd(msg, len)) {
		default:
		case UB_LIBCMD_ANSWER:
			log_err("unknown command for bg worker %d", 
				(int)context_serial_getcmd(msg, len));
			/* and fall through to quit */
			/* fallthrough */
		case UB_LIBCMD_QUIT:
			free(msg);
			comm_base_exit(w->base);
			break;
		case UB_LIBCMD_NEWQUERY:
			handle_newq(w, msg, len);
			break;
		case UB_LIBCMD_CANCEL:
			handle_cancel(w, msg, len);
			break;
	}
}

/** handle control command coming into server */
void 
libworker_handle_control_cmd(struct tube* ATTR_UNUSED(tube), 
	uint8_t* msg, size_t len, int err, void* arg)
{
	struct libworker* w = (struct libworker*)arg;

	if(err != 0) {
		free(msg);
		/* it is of no use to go on, exit */
		comm_base_exit(w->base);
		return;
	}
	libworker_do_cmd(w, msg, len); /* also frees the buf */
}

/** the background thread func */
static void*
libworker_dobg(void* arg)
{
	/* setup */
	uint32_t m;
	struct libworker* w = (struct libworker*)arg;
	struct ub_ctx* ctx;
	if(!w) {
		log_err("libunbound bg worker init failed, nomem");
		return NULL;
	}
	ctx = w->ctx;
	log_thread_set(&w->thread_num);
#ifdef THREADS_DISABLED
	/* we are forked */
	w->is_bg_thread = 0;
	/* close non-used parts of the pipes */
	tube_close_write(ctx->qq_pipe);
	tube_close_read(ctx->rr_pipe);
#endif
	if(!tube_setup_bg_listen(ctx->qq_pipe, w->base, 
		libworker_handle_control_cmd, w)) {
		log_err("libunbound bg worker init failed, no bglisten");
		return NULL;
	}
	if(!tube_setup_bg_write(ctx->rr_pipe, w->base)) {
		log_err("libunbound bg worker init failed, no bgwrite");
		return NULL;
	}

	/* do the work */
	comm_base_dispatch(w->base);

	/* cleanup */
	m = UB_LIBCMD_QUIT;
	w->want_quit = 1;
	tube_remove_bg_listen(w->ctx->qq_pipe);
	tube_remove_bg_write(w->ctx->rr_pipe);
	libworker_delete(w);
	(void)tube_write_msg(ctx->rr_pipe, (uint8_t*)&m, 
		(uint32_t)sizeof(m), 0);
#ifdef THREADS_DISABLED
	/* close pipes from forked process before exit */
	tube_close_read(ctx->qq_pipe);
	tube_close_write(ctx->rr_pipe);
#endif
	return NULL;
}

int libworker_bg(struct ub_ctx* ctx)
{
	struct libworker* w;
	/* fork or threadcreate */
	lock_basic_lock(&ctx->cfglock);
	if(ctx->dothread) {
		lock_basic_unlock(&ctx->cfglock);
		w = libworker_setup(ctx, 1, NULL);
		if(!w) return UB_NOMEM;
		w->is_bg_thread = 1;
#ifdef ENABLE_LOCK_CHECKS
		w->thread_num = 1; /* for nicer DEBUG checklocks */
#endif
		ub_thread_create(&ctx->bg_tid, libworker_dobg, w);
	} else {
		lock_basic_unlock(&ctx->cfglock);
#ifndef HAVE_FORK
		/* no fork on windows */
		return UB_FORKFAIL;
#else /* HAVE_FORK */
		switch((ctx->bg_pid=fork())) {
			case 0:
				w = libworker_setup(ctx, 1, NULL);
				if(!w) fatal_exit("out of memory");
				/* close non-used parts of the pipes */
				tube_close_write(ctx->qq_pipe);
				tube_close_read(ctx->rr_pipe);
				(void)libworker_dobg(w);
				exit(0);
				break;
			case -1:
				return UB_FORKFAIL;
			default:
				/* close non-used parts, so that the worker
				 * bgprocess gets 'pipe closed' when the
				 * main process exits */
				tube_close_read(ctx->qq_pipe);
				tube_close_write(ctx->rr_pipe);
				break;
		}
#endif /* HAVE_FORK */ 
	}
	return UB_NOERROR;
}

/** insert canonname */
static int
fill_canon(struct ub_result* res, uint8_t* s)
{
	char buf[255+2];
	dname_str(s, buf);
	res->canonname = strdup(buf);
	return res->canonname != 0;
}

/** fill data into result */
static int
fill_res(struct ub_result* res, struct ub_packed_rrset_key* answer,
	uint8_t* finalcname, struct query_info* rq, struct reply_info* rep)
{
	size_t i;
	struct packed_rrset_data* data;
	res->ttl = 0;
	if(!answer) {
		if(finalcname) {
			if(!fill_canon(res, finalcname))
				return 0; /* out of memory */
		}
		if(rep->rrset_count != 0)
			res->ttl = (int)rep->ttl;
		res->data = (char**)calloc(1, sizeof(char*));
		res->len = (int*)calloc(1, sizeof(int));
		return (res->data && res->len);
	}
	data = (struct packed_rrset_data*)answer->entry.data;
	if(query_dname_compare(rq->qname, answer->rk.dname) != 0) {
		if(!fill_canon(res, answer->rk.dname))
			return 0; /* out of memory */
	} else	res->canonname = NULL;
	res->data = (char**)calloc(data->count+1, sizeof(char*));
	res->len = (int*)calloc(data->count+1, sizeof(int));
	if(!res->data || !res->len)
		return 0; /* out of memory */
	for(i=0; i<data->count; i++) {
		/* remove rdlength from rdata */
		res->len[i] = (int)(data->rr_len[i] - 2);
		res->data[i] = memdup(data->rr_data[i]+2, (size_t)res->len[i]);
		if(!res->data[i])
			return 0; /* out of memory */
	}
	/* ttl for positive answers, from CNAME and answer RRs */
	if(data->count != 0) {
		size_t j;
		res->ttl = (int)data->ttl;
		for(j=0; j<rep->an_numrrsets; j++) {
			struct packed_rrset_data* d =
				(struct packed_rrset_data*)rep->rrsets[j]->
				entry.data;
			if((int)d->ttl < res->ttl)
				res->ttl = (int)d->ttl;
		}
	}
	/* ttl for negative answers */
	if(data->count == 0 && rep->rrset_count != 0)
		res->ttl = (int)rep->ttl;
	res->data[data->count] = NULL;
	res->len[data->count] = 0;
	return 1;
}

/** fill result from parsed message, on error fills servfail */
void
libworker_enter_result(struct ub_result* res, sldns_buffer* buf,
	struct regional* temp, enum sec_status msg_security)
{
	struct query_info rq;
	struct reply_info* rep;
	res->rcode = LDNS_RCODE_SERVFAIL;
	rep = parse_reply_in_temp_region(buf, temp, &rq);
	if(!rep) {
		log_err("cannot parse buf");
		return; /* error parsing buf, or out of memory */
	}
	if(!fill_res(res, reply_find_answer_rrset(&rq, rep), 
		reply_find_final_cname_target(&rq, rep), &rq, rep))
		return; /* out of memory */
	/* rcode, havedata, nxdomain, secure, bogus */
	res->rcode = (int)FLAGS_GET_RCODE(rep->flags);
	if(res->data && res->data[0])
		res->havedata = 1;
	if(res->rcode == LDNS_RCODE_NXDOMAIN)
		res->nxdomain = 1;
	if(msg_security == sec_status_secure)
		res->secure = 1;
	if(msg_security == sec_status_bogus ||
		msg_security == sec_status_secure_sentinel_fail)
		res->bogus = 1;
}

/** fillup fg results */
static void
libworker_fillup_fg(struct ctx_query* q, int rcode, sldns_buffer* buf, 
	enum sec_status s, char* why_bogus, int was_ratelimited)
{
	q->res->was_ratelimited = was_ratelimited;
	if(why_bogus)
		q->res->why_bogus = strdup(why_bogus);
	if(rcode != 0) {
		q->res->rcode = rcode;
		q->msg_security = s;
		return;
	}

	q->res->rcode = LDNS_RCODE_SERVFAIL;
	q->msg_security = 0;
	q->msg = memdup(sldns_buffer_begin(buf), sldns_buffer_limit(buf));
	q->msg_len = sldns_buffer_limit(buf);
	if(!q->msg) {
		return; /* the error is in the rcode */
	}

	/* canonname and results */
	q->msg_security = s;
	libworker_enter_result(q->res, buf, q->w->env->scratch, s);
}

void
libworker_fg_done_cb(void* arg, int rcode, sldns_buffer* buf, enum sec_status s,
	char* why_bogus, int was_ratelimited)
{
	struct ctx_query* q = (struct ctx_query*)arg;
	/* fg query is done; exit comm base */
	comm_base_exit(q->w->base);

	libworker_fillup_fg(q, rcode, buf, s, why_bogus, was_ratelimited);
}

/** setup qinfo and edns */
static int
setup_qinfo_edns(struct libworker* w, struct ctx_query* q, 
	struct query_info* qinfo, struct edns_data* edns)
{
	qinfo->qtype = (uint16_t)q->res->qtype;
	qinfo->qclass = (uint16_t)q->res->qclass;
	qinfo->local_alias = NULL;
	qinfo->qname = sldns_str2wire_dname(q->res->qname, &qinfo->qname_len);
	if(!qinfo->qname) {
		return 0;
	}
	qinfo->local_alias = NULL;
	edns->edns_present = 1;
	edns->ext_rcode = 0;
	edns->edns_version = 0;
	edns->bits = EDNS_DO;
	edns->opt_list = NULL;
	if(sldns_buffer_capacity(w->back->udp_buff) < 65535)
		edns->udp_size = (uint16_t)sldns_buffer_capacity(
			w->back->udp_buff);
	else	edns->udp_size = 65535;
	return 1;
}

int libworker_fg(struct ub_ctx* ctx, struct ctx_query* q)
{
	struct libworker* w = libworker_setup(ctx, 0, NULL);
	uint16_t qflags, qid;
	struct query_info qinfo;
	struct edns_data edns;
	if(!w)
		return UB_INITFAIL;
	if(!setup_qinfo_edns(w, q, &qinfo, &edns)) {
		libworker_delete(w);
		return UB_SYNTAX;
	}
	qid = 0;
	qflags = BIT_RD;
	q->w = w;
	/* see if there is a fixed answer */
	sldns_buffer_write_u16_at(w->back->udp_buff, 0, qid);
	sldns_buffer_write_u16_at(w->back->udp_buff, 2, qflags);
	if(local_zones_answer(ctx->local_zones, w->env, &qinfo, &edns, 
		w->back->udp_buff, w->env->scratch, NULL, NULL, 0, NULL, 0,
		NULL, 0, NULL, 0, NULL)) {
		regional_free_all(w->env->scratch);
		libworker_fillup_fg(q, LDNS_RCODE_NOERROR, 
			w->back->udp_buff, sec_status_insecure, NULL, 0);
		libworker_delete(w);
		free(qinfo.qname);
		return UB_NOERROR;
	}
	if(ctx->env->auth_zones && auth_zones_answer(ctx->env->auth_zones,
		w->env, &qinfo, &edns, NULL, w->back->udp_buff, w->env->scratch)) {
		regional_free_all(w->env->scratch);
		libworker_fillup_fg(q, LDNS_RCODE_NOERROR, 
			w->back->udp_buff, sec_status_insecure, NULL, 0);
		libworker_delete(w);
		free(qinfo.qname);
		return UB_NOERROR;
	}
	/* process new query */
	if(!mesh_new_callback(w->env->mesh, &qinfo, qflags, &edns, 
		w->back->udp_buff, qid, libworker_fg_done_cb, q)) {
		free(qinfo.qname);
		return UB_NOMEM;
	}
	free(qinfo.qname);

	/* wait for reply */
	comm_base_dispatch(w->base);

	libworker_delete(w);
	return UB_NOERROR;
}

void
libworker_event_done_cb(void* arg, int rcode, sldns_buffer* buf,
	enum sec_status s, char* why_bogus, int was_ratelimited)
{
	struct ctx_query* q = (struct ctx_query*)arg;
	ub_event_callback_type cb = q->cb_event;
	void* cb_arg = q->cb_arg;
	int cancelled = q->cancelled;

	/* delete it now */
	struct ub_ctx* ctx = q->w->ctx;
	lock_basic_lock(&ctx->cfglock);
	(void)rbtree_delete(&ctx->queries, q->node.key);
	ctx->num_async--;
	context_query_delete(q);
	lock_basic_unlock(&ctx->cfglock);

	if(!cancelled) {
		/* call callback */
		int sec = 0;
		if(s == sec_status_bogus)
			sec = 1;
		else if(s == sec_status_secure)
			sec = 2;
		(*cb)(cb_arg, rcode, (void*)sldns_buffer_begin(buf),
			(int)sldns_buffer_limit(buf), sec, why_bogus, was_ratelimited);
	}
}

int libworker_attach_mesh(struct ub_ctx* ctx, struct ctx_query* q,
	int* async_id)
{
	struct libworker* w = ctx->event_worker;
	uint16_t qflags, qid;
	struct query_info qinfo;
	struct edns_data edns;
	if(!w)
		return UB_INITFAIL;
	if(!setup_qinfo_edns(w, q, &qinfo, &edns))
		return UB_SYNTAX;
	qid = 0;
	qflags = BIT_RD;
	q->w = w;
	/* see if there is a fixed answer */
	sldns_buffer_write_u16_at(w->back->udp_buff, 0, qid);
	sldns_buffer_write_u16_at(w->back->udp_buff, 2, qflags);
	if(local_zones_answer(ctx->local_zones, w->env, &qinfo, &edns, 
		w->back->udp_buff, w->env->scratch, NULL, NULL, 0, NULL, 0,
		NULL, 0, NULL, 0, NULL)) {
		regional_free_all(w->env->scratch);
		free(qinfo.qname);
		libworker_event_done_cb(q, LDNS_RCODE_NOERROR,
			w->back->udp_buff, sec_status_insecure, NULL, 0);
		return UB_NOERROR;
	}
	if(ctx->env->auth_zones && auth_zones_answer(ctx->env->auth_zones,
		w->env, &qinfo, &edns, NULL, w->back->udp_buff, w->env->scratch)) {
		regional_free_all(w->env->scratch);
		free(qinfo.qname);
		libworker_event_done_cb(q, LDNS_RCODE_NOERROR,
			w->back->udp_buff, sec_status_insecure, NULL, 0);
		return UB_NOERROR;
	}
	/* process new query */
	if(async_id)
		*async_id = q->querynum;
	if(!mesh_new_callback(w->env->mesh, &qinfo, qflags, &edns, 
		w->back->udp_buff, qid, libworker_event_done_cb, q)) {
		free(qinfo.qname);
		return UB_NOMEM;
	}
	free(qinfo.qname);
	return UB_NOERROR;
}

/** add result to the bg worker result queue */
static void
add_bg_result(struct libworker* w, struct ctx_query* q, sldns_buffer* pkt, 
	int err, char* reason, int was_ratelimited)
{
	uint8_t* msg = NULL;
	uint32_t len = 0;

	if(w->want_quit) {
		context_query_delete(q);
		return;
	}
	/* serialize and delete unneeded q */
	if(w->is_bg_thread) {
		lock_basic_lock(&w->ctx->cfglock);
		if(reason)
			q->res->why_bogus = strdup(reason);
		q->res->was_ratelimited = was_ratelimited;
		if(pkt) {
			q->msg_len = sldns_buffer_remaining(pkt);
			q->msg = memdup(sldns_buffer_begin(pkt), q->msg_len);
			if(!q->msg) {
				msg = context_serialize_answer(q, UB_NOMEM, NULL, &len);
			} else {
				msg = context_serialize_answer(q, err, NULL, &len);
			}
		} else {
			msg = context_serialize_answer(q, err, NULL, &len);
		}
		lock_basic_unlock(&w->ctx->cfglock);
	} else {
		if(reason)
			q->res->why_bogus = strdup(reason);
		q->res->was_ratelimited = was_ratelimited;
		msg = context_serialize_answer(q, err, pkt, &len);
		(void)rbtree_delete(&w->ctx->queries, q->node.key);
		w->ctx->num_async--;
		context_query_delete(q);
	}

	if(!msg) {
		log_err("out of memory for async answer");
		return;
	}
	if(!tube_queue_item(w->ctx->rr_pipe, msg, len)) {
		log_err("out of memory for async answer");
		return;
	}
}

void
libworker_bg_done_cb(void* arg, int rcode, sldns_buffer* buf, enum sec_status s,
	char* why_bogus, int was_ratelimited)
{
	struct ctx_query* q = (struct ctx_query*)arg;

	if(q->cancelled || q->w->back->want_to_quit) {
		if(q->w->is_bg_thread) {
			/* delete it now */
			struct ub_ctx* ctx = q->w->ctx;
			lock_basic_lock(&ctx->cfglock);
			(void)rbtree_delete(&ctx->queries, q->node.key);
			ctx->num_async--;
			context_query_delete(q);
			lock_basic_unlock(&ctx->cfglock);
		}
		/* cancelled, do not give answer */
		return;
	}
	q->msg_security = s;
	if(!buf) {
		buf = q->w->env->scratch_buffer;
	}
	if(rcode != 0) {
		error_encode(buf, rcode, NULL, 0, BIT_RD, NULL);
	}
	add_bg_result(q->w, q, buf, UB_NOERROR, why_bogus, was_ratelimited);
}


/** handle new query command for bg worker */
static void
handle_newq(struct libworker* w, uint8_t* buf, uint32_t len)
{
	uint16_t qflags, qid;
	struct query_info qinfo;
	struct edns_data edns;
	struct ctx_query* q;
	if(w->is_bg_thread) {
		lock_basic_lock(&w->ctx->cfglock);
		q = context_lookup_new_query(w->ctx, buf, len);
		lock_basic_unlock(&w->ctx->cfglock);
	} else {
		q = context_deserialize_new_query(w->ctx, buf, len);
	}
	free(buf);
	if(!q) {
		log_err("failed to deserialize newq");
		return;
	}
	if(!setup_qinfo_edns(w, q, &qinfo, &edns)) {
		add_bg_result(w, q, NULL, UB_SYNTAX, NULL, 0);
		return;
	}
	qid = 0;
	qflags = BIT_RD;
	/* see if there is a fixed answer */
	sldns_buffer_write_u16_at(w->back->udp_buff, 0, qid);
	sldns_buffer_write_u16_at(w->back->udp_buff, 2, qflags);
	if(local_zones_answer(w->ctx->local_zones, w->env, &qinfo, &edns, 
		w->back->udp_buff, w->env->scratch, NULL, NULL, 0, NULL, 0,
		NULL, 0, NULL, 0, NULL)) {
		regional_free_all(w->env->scratch);
		q->msg_security = sec_status_insecure;
		add_bg_result(w, q, w->back->udp_buff, UB_NOERROR, NULL, 0);
		free(qinfo.qname);
		return;
	}
	if(w->ctx->env->auth_zones && auth_zones_answer(w->ctx->env->auth_zones,
		w->env, &qinfo, &edns, NULL, w->back->udp_buff, w->env->scratch)) {
		regional_free_all(w->env->scratch);
		q->msg_security = sec_status_insecure;
		add_bg_result(w, q, w->back->udp_buff, UB_NOERROR, NULL, 0);
		free(qinfo.qname);
		return;
	}
	q->w = w;
	/* process new query */
	if(!mesh_new_callback(w->env->mesh, &qinfo, qflags, &edns, 
		w->back->udp_buff, qid, libworker_bg_done_cb, q)) {
		add_bg_result(w, q, NULL, UB_NOMEM, NULL, 0);
	}
	free(qinfo.qname);
}

void libworker_alloc_cleanup(void* arg)
{
	struct libworker* w = (struct libworker*)arg;
	slabhash_clear(&w->env->rrset_cache->table);
        slabhash_clear(w->env->msg_cache);
}

struct outbound_entry* libworker_send_query(struct query_info* qinfo,
	uint16_t flags, int dnssec, int want_dnssec, int nocaps,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* zone,
	size_t zonelen, int ssl_upstream, char* tls_auth_name,
	struct module_qstate* q)
{
	struct libworker* w = (struct libworker*)q->env->worker;
	struct outbound_entry* e = (struct outbound_entry*)regional_alloc(
		q->region, sizeof(*e));
	if(!e)
		return NULL;
	e->qstate = q;
	e->qsent = outnet_serviced_query(w->back, qinfo, flags, dnssec,
		want_dnssec, nocaps, q->env->cfg->tcp_upstream, ssl_upstream,
		tls_auth_name, addr, addrlen, zone, zonelen, q,
		libworker_handle_service_reply, e, w->back->udp_buff, q->env);
	if(!e->qsent) {
		return NULL;
	}
	return e;
}

int 
libworker_handle_reply(struct comm_point* c, void* arg, int error,
        struct comm_reply* reply_info)
{
	struct module_qstate* q = (struct module_qstate*)arg;
	struct libworker* lw = (struct libworker*)q->env->worker;
	struct outbound_entry e;
	e.qstate = q;
	e.qsent = NULL;

	if(error != 0) {
		mesh_report_reply(lw->env->mesh, &e, reply_info, error);
		return 0;
	}
	/* sanity check. */
	if(!LDNS_QR_WIRE(sldns_buffer_begin(c->buffer))
		|| LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) !=
			LDNS_PACKET_QUERY
		|| LDNS_QDCOUNT(sldns_buffer_begin(c->buffer)) > 1) {
		/* error becomes timeout for the module as if this reply
		 * never arrived. */
		mesh_report_reply(lw->env->mesh, &e, reply_info, 
			NETEVENT_TIMEOUT);
		return 0;
	}
	mesh_report_reply(lw->env->mesh, &e, reply_info, NETEVENT_NOERROR);
	return 0;
}

int 
libworker_handle_service_reply(struct comm_point* c, void* arg, int error,
        struct comm_reply* reply_info)
{
	struct outbound_entry* e = (struct outbound_entry*)arg;
	struct libworker* lw = (struct libworker*)e->qstate->env->worker;

	if(error != 0) {
		mesh_report_reply(lw->env->mesh, e, reply_info, error);
		return 0;
	}
	/* sanity check. */
	if(!LDNS_QR_WIRE(sldns_buffer_begin(c->buffer))
		|| LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) !=
			LDNS_PACKET_QUERY
		|| LDNS_QDCOUNT(sldns_buffer_begin(c->buffer)) > 1) {
		/* error becomes timeout for the module as if this reply
		 * never arrived. */
		mesh_report_reply(lw->env->mesh, e, reply_info, 
			NETEVENT_TIMEOUT);
		return 0;
	}
	mesh_report_reply(lw->env->mesh,  e, reply_info, NETEVENT_NOERROR);
	return 0;
}

/* --- fake callbacks for fptr_wlist to work --- */
void worker_handle_control_cmd(struct tube* ATTR_UNUSED(tube), 
	uint8_t* ATTR_UNUSED(buffer), size_t ATTR_UNUSED(len),
	int ATTR_UNUSED(error), void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

int worker_handle_request(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

int worker_handle_reply(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

int worker_handle_service_reply(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

int remote_accept_callback(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

int remote_control_callback(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(repinfo))
{
	log_assert(0);
	return 0;
}

void worker_sighandler(int ATTR_UNUSED(sig), void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

struct outbound_entry* worker_send_query(struct query_info* ATTR_UNUSED(qinfo),
	uint16_t ATTR_UNUSED(flags), int ATTR_UNUSED(dnssec),
	int ATTR_UNUSED(want_dnssec), int ATTR_UNUSED(nocaps),
	struct sockaddr_storage* ATTR_UNUSED(addr), socklen_t ATTR_UNUSED(addrlen),
	uint8_t* ATTR_UNUSED(zone), size_t ATTR_UNUSED(zonelen),
	int ATTR_UNUSED(ssl_upstream), char* ATTR_UNUSED(tls_auth_name),
	struct module_qstate* ATTR_UNUSED(q))
{
	log_assert(0);
	return 0;
}

void 
worker_alloc_cleanup(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void worker_stat_timer_cb(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void worker_probe_timer_cb(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void worker_start_accept(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void worker_stop_accept(void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

int order_lock_cmp(const void* ATTR_UNUSED(e1), const void* ATTR_UNUSED(e2))
{
	log_assert(0);
	return 0;
}

int
codeline_cmp(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
	log_assert(0);
	return 0;
}

int replay_var_compare(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
        log_assert(0);
        return 0;
}

void remote_get_opt_ssl(char* ATTR_UNUSED(str), void* ATTR_UNUSED(arg))
{
        log_assert(0);
}

#ifdef UB_ON_WINDOWS
void
worker_win_stop_cb(int ATTR_UNUSED(fd), short ATTR_UNUSED(ev), void* 
        ATTR_UNUSED(arg)) {
        log_assert(0);
}

void
wsvc_cron_cb(void* ATTR_UNUSED(arg))
{
        log_assert(0);
}
#endif /* UB_ON_WINDOWS */
