/*
 * daemon/worker.c - worker that handles a pending list of requests.
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
 * This file implements the worker that handles callbacks on events, for
 * pending requests.
 */
#include "config.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/random.h"
#include "daemon/worker.h"
#include "daemon/daemon.h"
#include "daemon/remote.h"
#include "daemon/acl_list.h"
#include "util/netevent.h"
#include "util/config_file.h"
#include "util/module.h"
#include "util/regional.h"
#include "util/storage/slabhash.h"
#include "services/listen_dnsport.h"
#include "services/outside_network.h"
#include "services/outbound_list.h"
#include "services/cache/rrset.h"
#include "services/cache/infra.h"
#include "services/cache/dns.h"
#include "services/authzone.h"
#include "services/mesh.h"
#include "services/localzone.h"
#include "util/data/msgparse.h"
#include "util/data/msgencode.h"
#include "util/data/dname.h"
#include "util/fptr_wlist.h"
#include "util/tube.h"
#include "util/edns.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_hints.h"
#include "validator/autotrust.h"
#include "validator/val_anchor.h"
#include "respip/respip.h"
#include "libunbound/context.h"
#include "libunbound/libworker.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "util/shm_side/shm_main.h"
#include "dnscrypt/dnscrypt.h"

#ifdef HAVE_SYS_TYPES_H
#  include <sys/types.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <signal.h>
#ifdef UB_ON_WINDOWS
#include "winrc/win_svc.h"
#endif

/** Size of an UDP datagram */
#define NORMAL_UDP_SIZE	512 /* bytes */
/** ratelimit for error responses */
#define ERROR_RATELIMIT 100 /* qps */

/** 
 * seconds to add to prefetch leeway.  This is a TTL that expires old rrsets
 * earlier than they should in order to put the new update into the cache.
 * This additional value is to make sure that if not all TTLs are equal in
 * the message to be updated(and replaced), that rrsets with up to this much
 * extra TTL are also replaced.  This means that the resulting new message
 * will have (most likely) this TTL at least, avoiding very small 'split
 * second' TTLs due to operators choosing relative primes for TTLs (or so).
 * Also has to be at least one to break ties (and overwrite cached entry).
 */
#define PREFETCH_EXPIRY_ADD 60

/** Report on memory usage by this thread and global */
static void
worker_mem_report(struct worker* ATTR_UNUSED(worker), 
	struct serviced_query* ATTR_UNUSED(cur_serv))
{
#ifdef UNBOUND_ALLOC_STATS
	/* measure memory leakage */
	extern size_t unbound_mem_alloc, unbound_mem_freed;
	/* debug func in validator module */
	size_t total, front, back, mesh, msg, rrset, infra, ac, superac;
	size_t me, iter, val, anch;
	int i;
#ifdef CLIENT_SUBNET
	size_t subnet = 0;
#endif /* CLIENT_SUBNET */
	if(verbosity < VERB_ALGO) 
		return;
	front = listen_get_mem(worker->front);
	back = outnet_get_mem(worker->back);
	msg = slabhash_get_mem(worker->env.msg_cache);
	rrset = slabhash_get_mem(&worker->env.rrset_cache->table);
	infra = infra_get_mem(worker->env.infra_cache);
	mesh = mesh_get_mem(worker->env.mesh);
	ac = alloc_get_mem(&worker->alloc);
	superac = alloc_get_mem(&worker->daemon->superalloc);
	anch = anchors_get_mem(worker->env.anchors);
	iter = 0;
	val = 0;
	for(i=0; i<worker->env.mesh->mods.num; i++) {
		fptr_ok(fptr_whitelist_mod_get_mem(worker->env.mesh->
			mods.mod[i]->get_mem));
		if(strcmp(worker->env.mesh->mods.mod[i]->name, "validator")==0)
			val += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#ifdef CLIENT_SUBNET
		else if(strcmp(worker->env.mesh->mods.mod[i]->name,
			"subnet")==0)
			subnet += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#endif /* CLIENT_SUBNET */
		else	iter += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
	}
	me = sizeof(*worker) + sizeof(*worker->base) + sizeof(*worker->comsig)
		+ comm_point_get_mem(worker->cmd_com) 
		+ sizeof(worker->rndstate) 
		+ regional_get_mem(worker->scratchpad) 
		+ sizeof(*worker->env.scratch_buffer) 
		+ sldns_buffer_capacity(worker->env.scratch_buffer)
		+ forwards_get_mem(worker->env.fwds)
		+ hints_get_mem(worker->env.hints);
	if(worker->thread_num == 0)
		me += acl_list_get_mem(worker->daemon->acl);
	if(cur_serv) {
		me += serviced_get_mem(cur_serv);
	}
	total = front+back+mesh+msg+rrset+infra+iter+val+ac+superac+me;
#ifdef CLIENT_SUBNET
	total += subnet;
	log_info("Memory conditions: %u front=%u back=%u mesh=%u msg=%u "
		"rrset=%u infra=%u iter=%u val=%u subnet=%u anchors=%u "
		"alloccache=%u globalalloccache=%u me=%u",
		(unsigned)total, (unsigned)front, (unsigned)back, 
		(unsigned)mesh, (unsigned)msg, (unsigned)rrset, (unsigned)infra,
		(unsigned)iter, (unsigned)val,
		(unsigned)subnet, (unsigned)anch, (unsigned)ac,
		(unsigned)superac, (unsigned)me);
#else /* no CLIENT_SUBNET */
	log_info("Memory conditions: %u front=%u back=%u mesh=%u msg=%u "
		"rrset=%u infra=%u iter=%u val=%u anchors=%u "
		"alloccache=%u globalalloccache=%u me=%u",
		(unsigned)total, (unsigned)front, (unsigned)back, 
		(unsigned)mesh, (unsigned)msg, (unsigned)rrset, 
		(unsigned)infra, (unsigned)iter, (unsigned)val, (unsigned)anch,
		(unsigned)ac, (unsigned)superac, (unsigned)me);
#endif /* CLIENT_SUBNET */
	log_info("Total heap memory estimate: %u  total-alloc: %u  "
		"total-free: %u", (unsigned)total, 
		(unsigned)unbound_mem_alloc, (unsigned)unbound_mem_freed);
#else /* no UNBOUND_ALLOC_STATS */
	size_t val = 0;
#ifdef CLIENT_SUBNET
	size_t subnet = 0;
#endif /* CLIENT_SUBNET */
	int i;
	if(verbosity < VERB_QUERY)
		return;
	for(i=0; i<worker->env.mesh->mods.num; i++) {
		fptr_ok(fptr_whitelist_mod_get_mem(worker->env.mesh->
			mods.mod[i]->get_mem));
		if(strcmp(worker->env.mesh->mods.mod[i]->name, "validator")==0)
			val += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#ifdef CLIENT_SUBNET
		else if(strcmp(worker->env.mesh->mods.mod[i]->name,
			"subnet")==0)
			subnet += (*worker->env.mesh->mods.mod[i]->get_mem)
				(&worker->env, i);
#endif /* CLIENT_SUBNET */
	}
#ifdef CLIENT_SUBNET
	verbose(VERB_QUERY, "cache memory msg=%u rrset=%u infra=%u val=%u "
		"subnet=%u",
		(unsigned)slabhash_get_mem(worker->env.msg_cache),
		(unsigned)slabhash_get_mem(&worker->env.rrset_cache->table),
		(unsigned)infra_get_mem(worker->env.infra_cache),
		(unsigned)val, (unsigned)subnet);
#else /* no CLIENT_SUBNET */
	verbose(VERB_QUERY, "cache memory msg=%u rrset=%u infra=%u val=%u",
		(unsigned)slabhash_get_mem(worker->env.msg_cache),
		(unsigned)slabhash_get_mem(&worker->env.rrset_cache->table),
		(unsigned)infra_get_mem(worker->env.infra_cache),
		(unsigned)val);
#endif /* CLIENT_SUBNET */
#endif /* UNBOUND_ALLOC_STATS */
}

void 
worker_send_cmd(struct worker* worker, enum worker_commands cmd)
{
	uint32_t c = (uint32_t)htonl(cmd);
	if(!tube_write_msg(worker->cmd, (uint8_t*)&c, sizeof(c), 0)) {
		log_err("worker send cmd %d failed", (int)cmd);
	}
}

int 
worker_handle_reply(struct comm_point* c, void* arg, int error, 
	struct comm_reply* reply_info)
{
	struct module_qstate* q = (struct module_qstate*)arg;
	struct worker* worker = q->env->worker;
	struct outbound_entry e;
	e.qstate = q;
	e.qsent = NULL;

	if(error != 0) {
		mesh_report_reply(worker->env.mesh, &e, reply_info, error);
		worker_mem_report(worker, NULL);
		return 0;
	}
	/* sanity check. */
	if(!LDNS_QR_WIRE(sldns_buffer_begin(c->buffer))
		|| LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) != 
			LDNS_PACKET_QUERY
		|| LDNS_QDCOUNT(sldns_buffer_begin(c->buffer)) > 1) {
		/* error becomes timeout for the module as if this reply
		 * never arrived. */
		mesh_report_reply(worker->env.mesh, &e, reply_info, 
			NETEVENT_TIMEOUT);
		worker_mem_report(worker, NULL);
		return 0;
	}
	mesh_report_reply(worker->env.mesh, &e, reply_info, NETEVENT_NOERROR);
	worker_mem_report(worker, NULL);
	return 0;
}

int 
worker_handle_service_reply(struct comm_point* c, void* arg, int error, 
	struct comm_reply* reply_info)
{
	struct outbound_entry* e = (struct outbound_entry*)arg;
	struct worker* worker = e->qstate->env->worker;
	struct serviced_query *sq = e->qsent;

	verbose(VERB_ALGO, "worker svcd callback for qstate %p", e->qstate);
	if(error != 0) {
		mesh_report_reply(worker->env.mesh, e, reply_info, error);
		worker_mem_report(worker, sq);
		return 0;
	}
	/* sanity check. */
	if(!LDNS_QR_WIRE(sldns_buffer_begin(c->buffer))
		|| LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) != 
			LDNS_PACKET_QUERY
		|| LDNS_QDCOUNT(sldns_buffer_begin(c->buffer)) > 1) {
		/* error becomes timeout for the module as if this reply
		 * never arrived. */
		verbose(VERB_ALGO, "worker: bad reply handled as timeout");
		mesh_report_reply(worker->env.mesh, e, reply_info, 
			NETEVENT_TIMEOUT);
		worker_mem_report(worker, sq);
		return 0;
	}
	mesh_report_reply(worker->env.mesh, e, reply_info, NETEVENT_NOERROR);
	worker_mem_report(worker, sq);
	return 0;
}

/** ratelimit error replies
 * @param worker: the worker struct with ratelimit counter
 * @param err: error code that would be wanted.
 * @return value of err if okay, or -1 if it should be discarded instead.
 */
static int
worker_err_ratelimit(struct worker* worker, int err)
{
	if(worker->err_limit_time == *worker->env.now) {
		/* see if limit is exceeded for this second */
		if(worker->err_limit_count++ > ERROR_RATELIMIT)
			return -1;
	} else {
		/* new second, new limits */
		worker->err_limit_time = *worker->env.now;
		worker->err_limit_count = 1;
	}
	return err;
}

/** check request sanity.
 * @param pkt: the wire packet to examine for sanity.
 * @param worker: parameters for checking.
 * @return error code, 0 OK, or -1 discard.
*/
static int 
worker_check_request(sldns_buffer* pkt, struct worker* worker)
{
	if(sldns_buffer_limit(pkt) < LDNS_HEADER_SIZE) {
		verbose(VERB_QUERY, "request too short, discarded");
		return -1;
	}
	if(sldns_buffer_limit(pkt) > NORMAL_UDP_SIZE && 
		worker->daemon->cfg->harden_large_queries) {
		verbose(VERB_QUERY, "request too large, discarded");
		return -1;
	}
	if(LDNS_QR_WIRE(sldns_buffer_begin(pkt))) {
		verbose(VERB_QUERY, "request has QR bit on, discarded");
		return -1;
	}
	if(LDNS_TC_WIRE(sldns_buffer_begin(pkt))) {
		LDNS_TC_CLR(sldns_buffer_begin(pkt));
		verbose(VERB_QUERY, "request bad, has TC bit on");
		return worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
	}
	if(LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_QUERY &&
		LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_NOTIFY) {
		verbose(VERB_QUERY, "request unknown opcode %d", 
			LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)));
		return worker_err_ratelimit(worker, LDNS_RCODE_NOTIMPL);
	}
	if(LDNS_QDCOUNT(sldns_buffer_begin(pkt)) != 1) {
		verbose(VERB_QUERY, "request wrong nr qd=%d", 
			LDNS_QDCOUNT(sldns_buffer_begin(pkt)));
		return worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
	}
	if(LDNS_ANCOUNT(sldns_buffer_begin(pkt)) != 0 && 
		(LDNS_ANCOUNT(sldns_buffer_begin(pkt)) != 1 ||
		LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_NOTIFY)) {
		verbose(VERB_QUERY, "request wrong nr an=%d", 
			LDNS_ANCOUNT(sldns_buffer_begin(pkt)));
		return worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
	}
	if(LDNS_NSCOUNT(sldns_buffer_begin(pkt)) != 0) {
		verbose(VERB_QUERY, "request wrong nr ns=%d", 
			LDNS_NSCOUNT(sldns_buffer_begin(pkt)));
		return worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
	}
	if(LDNS_ARCOUNT(sldns_buffer_begin(pkt)) > 1) {
		verbose(VERB_QUERY, "request wrong nr ar=%d", 
			LDNS_ARCOUNT(sldns_buffer_begin(pkt)));
		return worker_err_ratelimit(worker, LDNS_RCODE_FORMERR);
	}
	return 0;
}

void 
worker_handle_control_cmd(struct tube* ATTR_UNUSED(tube), uint8_t* msg,
	size_t len, int error, void* arg)
{
	struct worker* worker = (struct worker*)arg;
	enum worker_commands cmd;
	if(error != NETEVENT_NOERROR) {
		free(msg);
		if(error == NETEVENT_CLOSED)
			comm_base_exit(worker->base);
		else	log_info("control event: %d", error);
		return;
	}
	if(len != sizeof(uint32_t)) {
		fatal_exit("bad control msg length %d", (int)len);
	}
	cmd = sldns_read_uint32(msg);
	free(msg);
	switch(cmd) {
	case worker_cmd_quit:
		verbose(VERB_ALGO, "got control cmd quit");
		comm_base_exit(worker->base);
		break;
	case worker_cmd_stats:
		verbose(VERB_ALGO, "got control cmd stats");
		server_stats_reply(worker, 1);
		break;
	case worker_cmd_stats_noreset:
		verbose(VERB_ALGO, "got control cmd stats_noreset");
		server_stats_reply(worker, 0);
		break;
	case worker_cmd_remote:
		verbose(VERB_ALGO, "got control cmd remote");
		daemon_remote_exec(worker);
		break;
	default:
		log_err("bad command %d", (int)cmd);
		break;
	}
}

/** check if a delegation is secure */
static enum sec_status
check_delegation_secure(struct reply_info *rep) 
{
	/* return smallest security status */
	size_t i;
	enum sec_status sec = sec_status_secure;
	enum sec_status s;
	size_t num = rep->an_numrrsets + rep->ns_numrrsets;
	/* check if answer and authority are OK */
	for(i=0; i<num; i++) {
		s = ((struct packed_rrset_data*)rep->rrsets[i]->entry.data)
			->security;
		if(s < sec)
			sec = s;
	}
	/* in additional, only unchecked triggers revalidation */
	for(i=num; i<rep->rrset_count; i++) {
		s = ((struct packed_rrset_data*)rep->rrsets[i]->entry.data)
			->security;
		if(s == sec_status_unchecked)
			return s;
	}
	return sec;
}

/** remove nonsecure from a delegation referral additional section */
static void
deleg_remove_nonsecure_additional(struct reply_info* rep)
{
	/* we can simply edit it, since we are working in the scratch region */
	size_t i;
	enum sec_status s;

	for(i = rep->an_numrrsets+rep->ns_numrrsets; i<rep->rrset_count; i++) {
		s = ((struct packed_rrset_data*)rep->rrsets[i]->entry.data)
			->security;
		if(s != sec_status_secure) {
			memmove(rep->rrsets+i, rep->rrsets+i+1, 
				sizeof(struct ub_packed_rrset_key*)* 
				(rep->rrset_count - i - 1));
			rep->ar_numrrsets--; 
			rep->rrset_count--;
			i--;
		}
	}
}

/** answer nonrecursive query from the cache */
static int
answer_norec_from_cache(struct worker* worker, struct query_info* qinfo,
	uint16_t id, uint16_t flags, struct comm_reply* repinfo, 
	struct edns_data* edns)
{
	/* for a nonrecursive query return either:
	 * 	o an error (servfail; we try to avoid this)
	 * 	o a delegation (closest we have; this routine tries that)
	 * 	o the answer (checked by answer_from_cache) 
	 *
	 * So, grab a delegation from the rrset cache. 
	 * Then check if it needs validation, if so, this routine fails,
	 * so that iterator can prime and validator can verify rrsets.
	 */
	struct edns_data edns_bak;
	uint16_t udpsize = edns->udp_size;
	int secure = 0;
	time_t timenow = *worker->env.now;
	int must_validate = (!(flags&BIT_CD) || worker->env.cfg->ignore_cd)
		&& worker->env.need_to_validate;
	struct dns_msg *msg = NULL;
	struct delegpt *dp;

	dp = dns_cache_find_delegation(&worker->env, qinfo->qname, 
		qinfo->qname_len, qinfo->qtype, qinfo->qclass,
		worker->scratchpad, &msg, timenow);
	if(!dp) { /* no delegation, need to reprime */
		return 0;
	}
	/* In case we have a local alias, copy it into the delegation message.
	 * Shallow copy should be fine, as we'll be done with msg in this
	 * function. */
	msg->qinfo.local_alias = qinfo->local_alias;
	if(must_validate) {
		switch(check_delegation_secure(msg->rep)) {
		case sec_status_unchecked:
			/* some rrsets have not been verified yet, go and 
			 * let validator do that */
			return 0;
		case sec_status_bogus:
		case sec_status_secure_sentinel_fail:
			/* some rrsets are bogus, reply servfail */
			edns->edns_version = EDNS_ADVERTISED_VERSION;
			edns->udp_size = EDNS_ADVERTISED_SIZE;
			edns->ext_rcode = 0;
			edns->bits &= EDNS_DO;
			if(!inplace_cb_reply_servfail_call(&worker->env, qinfo, NULL,
				msg->rep, LDNS_RCODE_SERVFAIL, edns, repinfo, worker->scratchpad))
					return 0;
			error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL, 
				&msg->qinfo, id, flags, edns);
			if(worker->stats.extended) {
				worker->stats.ans_bogus++;
				worker->stats.ans_rcode[LDNS_RCODE_SERVFAIL]++;
			}
			return 1;
		case sec_status_secure:
			/* all rrsets are secure */
			/* remove non-secure rrsets from the add. section*/
			if(worker->env.cfg->val_clean_additional)
				deleg_remove_nonsecure_additional(msg->rep);
			secure = 1;
			break;
		case sec_status_indeterminate:
		case sec_status_insecure:
		default:
			/* not secure */
			secure = 0;
			break;
		}
	}
	/* return this delegation from the cache */
	edns_bak = *edns;
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;
	if(!inplace_cb_reply_cache_call(&worker->env, qinfo, NULL, msg->rep,
		(int)(flags&LDNS_RCODE_MASK), edns, repinfo, worker->scratchpad))
			return 0;
	msg->rep->flags |= BIT_QR|BIT_RA;
	if(!apply_edns_options(edns, &edns_bak, worker->env.cfg,
		repinfo->c, worker->scratchpad) ||
		!reply_info_answer_encode(&msg->qinfo, msg->rep, id, flags, 
		repinfo->c->buffer, 0, 1, worker->scratchpad,
		udpsize, edns, (int)(edns->bits & EDNS_DO), secure)) {
		if(!inplace_cb_reply_servfail_call(&worker->env, qinfo, NULL, NULL,
			LDNS_RCODE_SERVFAIL, edns, repinfo, worker->scratchpad))
				edns->opt_list = NULL;
		error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL, 
			&msg->qinfo, id, flags, edns);
	}
	if(worker->stats.extended) {
		if(secure) worker->stats.ans_secure++;
		server_stats_insrcode(&worker->stats, repinfo->c->buffer);
	}
	return 1;
}

/** Apply, if applicable, a response IP action to a cached answer.
 * If the answer is rewritten as a result of an action, '*encode_repp' will
 * point to the reply info containing the modified answer.  '*encode_repp' will
 * be intact otherwise.
 * It returns 1 on success, 0 otherwise. */
static int
apply_respip_action(struct worker* worker, const struct query_info* qinfo,
	struct respip_client_info* cinfo, struct reply_info* rep,
	struct comm_reply* repinfo, struct ub_packed_rrset_key** alias_rrset,
	struct reply_info** encode_repp)
{
	struct respip_action_info actinfo = {respip_none, NULL};

	if(qinfo->qtype != LDNS_RR_TYPE_A &&
		qinfo->qtype != LDNS_RR_TYPE_AAAA &&
		qinfo->qtype != LDNS_RR_TYPE_ANY)
		return 1;

	if(!respip_rewrite_reply(qinfo, cinfo, rep, encode_repp, &actinfo,
		alias_rrset, 0, worker->scratchpad))
		return 0;

	/* xxx_deny actions mean dropping the reply, unless the original reply
	 * was redirected to response-ip data. */
	if((actinfo.action == respip_deny ||
		actinfo.action == respip_inform_deny) &&
		*encode_repp == rep)
		*encode_repp = NULL;

	/* If address info is returned, it means the action should be an
	 * 'inform' variant and the information should be logged. */
	if(actinfo.addrinfo) {
		respip_inform_print(actinfo.addrinfo, qinfo->qname,
			qinfo->qtype, qinfo->qclass, qinfo->local_alias,
			repinfo);
	}

	return 1;
}

/** answer query from the cache.
 * Normally, the answer message will be built in repinfo->c->buffer; if the
 * answer is supposed to be suppressed or the answer is supposed to be an
 * incomplete CNAME chain, the buffer is explicitly cleared to signal the
 * caller as such.  In the latter case *partial_rep will point to the incomplete
 * reply, and this function is (possibly) supposed to be called again with that
 * *partial_rep value to complete the chain.  In addition, if the query should
 * be completely dropped, '*need_drop' will be set to 1. */
static int
answer_from_cache(struct worker* worker, struct query_info* qinfo,
	struct respip_client_info* cinfo, int* need_drop,
	struct ub_packed_rrset_key** alias_rrset,
	struct reply_info** partial_repp,
	struct reply_info* rep, uint16_t id, uint16_t flags, 
	struct comm_reply* repinfo, struct edns_data* edns)
{
	struct edns_data edns_bak;
	time_t timenow = *worker->env.now;
	uint16_t udpsize = edns->udp_size;
	struct reply_info* encode_rep = rep;
	struct reply_info* partial_rep = *partial_repp;
	int secure;
	int must_validate = (!(flags&BIT_CD) || worker->env.cfg->ignore_cd)
		&& worker->env.need_to_validate;
	*partial_repp = NULL;	/* avoid accidental further pass */
	if(worker->env.cfg->serve_expired) {
		if(worker->env.cfg->serve_expired_ttl &&
			rep->serve_expired_ttl < timenow)
			return 0;
		if(!rrset_array_lock(rep->ref, rep->rrset_count, 0))
			return 0;
		/* below, rrsets with ttl before timenow become TTL 0 in
		 * the response */
		/* This response was served with zero TTL */
		if (timenow >= rep->ttl) {
			worker->stats.zero_ttl_responses++;
		}
	} else {
		/* see if it is possible */
		if(rep->ttl < timenow) {
			/* the rrsets may have been updated in the meantime.
			 * we will refetch the message format from the
			 * authoritative server 
			 */
			return 0;
		}
		if(!rrset_array_lock(rep->ref, rep->rrset_count, timenow))
			return 0;
		/* locked and ids and ttls are OK. */
	}
	/* check CNAME chain (if any) */
	if(rep->an_numrrsets > 0 && (rep->rrsets[0]->rk.type == 
		htons(LDNS_RR_TYPE_CNAME) || rep->rrsets[0]->rk.type == 
		htons(LDNS_RR_TYPE_DNAME))) {
		if(!reply_check_cname_chain(qinfo, rep)) {
			/* cname chain invalid, redo iterator steps */
			verbose(VERB_ALGO, "Cache reply: cname chain broken");
		bail_out:
			rrset_array_unlock_touch(worker->env.rrset_cache, 
				worker->scratchpad, rep->ref, rep->rrset_count);
			return 0;
		}
	}
	/* check security status of the cached answer */
	if(must_validate && (rep->security == sec_status_bogus ||
		rep->security == sec_status_secure_sentinel_fail)) {
		/* BAD cached */
		edns->edns_version = EDNS_ADVERTISED_VERSION;
		edns->udp_size = EDNS_ADVERTISED_SIZE;
		edns->ext_rcode = 0;
		edns->bits &= EDNS_DO;
		if(!inplace_cb_reply_servfail_call(&worker->env, qinfo, NULL, rep,
			LDNS_RCODE_SERVFAIL, edns, repinfo, worker->scratchpad))
			goto bail_out;
		error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL, 
			qinfo, id, flags, edns);
		rrset_array_unlock_touch(worker->env.rrset_cache, 
			worker->scratchpad, rep->ref, rep->rrset_count);
		if(worker->stats.extended) {
			worker->stats.ans_bogus ++;
			worker->stats.ans_rcode[LDNS_RCODE_SERVFAIL] ++;
		}
		return 1;
	} else if( rep->security == sec_status_unchecked && must_validate) {
		verbose(VERB_ALGO, "Cache reply: unchecked entry needs "
			"validation");
		goto bail_out; /* need to validate cache entry first */
	} else if(rep->security == sec_status_secure) {
		if(reply_all_rrsets_secure(rep))
			secure = 1;
		else	{
			if(must_validate) {
				verbose(VERB_ALGO, "Cache reply: secure entry"
					" changed status");
				goto bail_out; /* rrset changed, re-verify */
			}
			secure = 0;
		}
	} else	secure = 0;

	edns_bak = *edns;
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;
	if(!inplace_cb_reply_cache_call(&worker->env, qinfo, NULL, rep,
		(int)(flags&LDNS_RCODE_MASK), edns, repinfo, worker->scratchpad))
		goto bail_out;
	*alias_rrset = NULL; /* avoid confusion if caller set it to non-NULL */
	if(worker->daemon->use_response_ip && !partial_rep &&
	   !apply_respip_action(worker, qinfo, cinfo, rep, repinfo, alias_rrset,
			&encode_rep)) {
		goto bail_out;
	} else if(partial_rep &&
		!respip_merge_cname(partial_rep, qinfo, rep, cinfo,
		must_validate, &encode_rep, worker->scratchpad)) {
		goto bail_out;
	}
	if(encode_rep != rep)
		secure = 0; /* if rewritten, it can't be considered "secure" */
	if(!encode_rep || *alias_rrset) {
		sldns_buffer_clear(repinfo->c->buffer);
		sldns_buffer_flip(repinfo->c->buffer);
		if(!encode_rep)
			*need_drop = 1;
		else {
			/* If a partial CNAME chain is found, we first need to
			 * make a copy of the reply in the scratchpad so we
			 * can release the locks and lookup the cache again. */
			*partial_repp = reply_info_copy(encode_rep, NULL,
				worker->scratchpad);
			if(!*partial_repp)
				goto bail_out;
		}
	} else if(!apply_edns_options(edns, &edns_bak, worker->env.cfg,
		repinfo->c, worker->scratchpad) ||
		!reply_info_answer_encode(qinfo, encode_rep, id, flags,
		repinfo->c->buffer, timenow, 1, worker->scratchpad,
		udpsize, edns, (int)(edns->bits & EDNS_DO), secure)) {
		if(!inplace_cb_reply_servfail_call(&worker->env, qinfo, NULL, NULL,
			LDNS_RCODE_SERVFAIL, edns, repinfo, worker->scratchpad))
				edns->opt_list = NULL;
		error_encode(repinfo->c->buffer, LDNS_RCODE_SERVFAIL, 
			qinfo, id, flags, edns);
	}
	/* cannot send the reply right now, because blocking network syscall
	 * is bad while holding locks. */
	rrset_array_unlock_touch(worker->env.rrset_cache, worker->scratchpad,
		rep->ref, rep->rrset_count);
	if(worker->stats.extended) {
		if(secure) worker->stats.ans_secure++;
		server_stats_insrcode(&worker->stats, repinfo->c->buffer);
	}
	/* go and return this buffer to the client */
	return 1;
}

/** Reply to client and perform prefetch to keep cache up to date.
 * If the buffer for the reply is empty, it indicates that only prefetch is
 * necessary and the reply should be suppressed (because it's dropped or
 * being deferred). */
static void
reply_and_prefetch(struct worker* worker, struct query_info* qinfo, 
	uint16_t flags, struct comm_reply* repinfo, time_t leeway)
{
	/* first send answer to client to keep its latency 
	 * as small as a cachereply */
	if(sldns_buffer_limit(repinfo->c->buffer) != 0)
		comm_point_send_reply(repinfo);
	server_stats_prefetch(&worker->stats, worker);
	
	/* create the prefetch in the mesh as a normal lookup without
	 * client addrs waiting, which has the cache blacklisted (to bypass
	 * the cache and go to the network for the data). */
	/* this (potentially) runs the mesh for the new query */
	mesh_new_prefetch(worker->env.mesh, qinfo, flags, leeway + 
		PREFETCH_EXPIRY_ADD);
}

/**
 * Fill CH class answer into buffer. Keeps query.
 * @param pkt: buffer
 * @param str: string to put into text record (<255).
 * 	array of strings, every string becomes a text record.
 * @param num: number of strings in array.
 * @param edns: edns reply information.
 * @param worker: worker with scratch region.
 * @param repinfo: reply information for a communication point.
 */
static void
chaos_replystr(sldns_buffer* pkt, char** str, int num, struct edns_data* edns,
	struct worker* worker, struct comm_reply* repinfo)
{
	int i;
	unsigned int rd = LDNS_RD_WIRE(sldns_buffer_begin(pkt));
	unsigned int cd = LDNS_CD_WIRE(sldns_buffer_begin(pkt));
	sldns_buffer_clear(pkt);
	sldns_buffer_skip(pkt, (ssize_t)sizeof(uint16_t)); /* skip id */
	sldns_buffer_write_u16(pkt, (uint16_t)(BIT_QR|BIT_RA));
	if(rd) LDNS_RD_SET(sldns_buffer_begin(pkt));
	if(cd) LDNS_CD_SET(sldns_buffer_begin(pkt));
	sldns_buffer_write_u16(pkt, 1); /* qdcount */
	sldns_buffer_write_u16(pkt, (uint16_t)num); /* ancount */
	sldns_buffer_write_u16(pkt, 0); /* nscount */
	sldns_buffer_write_u16(pkt, 0); /* arcount */
	(void)query_dname_len(pkt); /* skip qname */
	sldns_buffer_skip(pkt, (ssize_t)sizeof(uint16_t)); /* skip qtype */
	sldns_buffer_skip(pkt, (ssize_t)sizeof(uint16_t)); /* skip qclass */
	for(i=0; i<num; i++) {
		size_t len = strlen(str[i]);
		if(len>255) len=255; /* cap size of TXT record */
		sldns_buffer_write_u16(pkt, 0xc00c); /* compr ptr to query */
		sldns_buffer_write_u16(pkt, LDNS_RR_TYPE_TXT);
		sldns_buffer_write_u16(pkt, LDNS_RR_CLASS_CH);
		sldns_buffer_write_u32(pkt, 0); /* TTL */
		sldns_buffer_write_u16(pkt, sizeof(uint8_t) + len);
		sldns_buffer_write_u8(pkt, len);
		sldns_buffer_write(pkt, str[i], len);
	}
	sldns_buffer_flip(pkt);
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->bits &= EDNS_DO;
	if(!inplace_cb_reply_local_call(&worker->env, NULL, NULL, NULL,
		LDNS_RCODE_NOERROR, edns, repinfo, worker->scratchpad))
			edns->opt_list = NULL;
	if(sldns_buffer_capacity(pkt) >=
		sldns_buffer_limit(pkt)+calc_edns_field_size(edns))
		attach_edns_record(pkt, edns);
}

/** Reply with one string */
static void
chaos_replyonestr(sldns_buffer* pkt, const char* str, struct edns_data* edns,
	struct worker* worker, struct comm_reply* repinfo)
{
	chaos_replystr(pkt, (char**)&str, 1, edns, worker, repinfo);
}

/**
 * Create CH class trustanchor answer.
 * @param pkt: buffer
 * @param edns: edns reply information.
 * @param w: worker with scratch region.
 * @param repinfo: reply information for a communication point.
 */
static void
chaos_trustanchor(sldns_buffer* pkt, struct edns_data* edns, struct worker* w,
	struct comm_reply* repinfo)
{
#define TA_RESPONSE_MAX_TXT 16 /* max number of TXT records */
#define TA_RESPONSE_MAX_TAGS 32 /* max number of tags printed per zone */
	char* str_array[TA_RESPONSE_MAX_TXT];
	uint16_t tags[TA_RESPONSE_MAX_TAGS];
	int num = 0;
	struct trust_anchor* ta;

	if(!w->env.need_to_validate) {
		/* no validator module, reply no trustanchors */
		chaos_replystr(pkt, NULL, 0, edns, w, repinfo);
		return;
	}

	/* fill the string with contents */
	lock_basic_lock(&w->env.anchors->lock);
	RBTREE_FOR(ta, struct trust_anchor*, w->env.anchors->tree) {
		char* str;
		size_t i, numtag, str_len = 255;
		if(num == TA_RESPONSE_MAX_TXT) continue;
		str = (char*)regional_alloc(w->scratchpad, str_len);
		if(!str) continue;
		lock_basic_lock(&ta->lock);
		numtag = anchor_list_keytags(ta, tags, TA_RESPONSE_MAX_TAGS);
		if(numtag == 0) {
			/* empty, insecure point */
			lock_basic_unlock(&ta->lock);
			continue;
		}
		str_array[num] = str;
		num++;

		/* spool name of anchor */
		(void)sldns_wire2str_dname_buf(ta->name, ta->namelen, str, str_len);
		str_len -= strlen(str); str += strlen(str);
		/* spool tags */
		for(i=0; i<numtag; i++) {
			snprintf(str, str_len, " %u", (unsigned)tags[i]);
			str_len -= strlen(str); str += strlen(str);
		}
		lock_basic_unlock(&ta->lock);
	}
	lock_basic_unlock(&w->env.anchors->lock);

	chaos_replystr(pkt, str_array, num, edns, w, repinfo);
	regional_free_all(w->scratchpad);
}

/**
 * Answer CH class queries.
 * @param w: worker
 * @param qinfo: query info. Pointer into packet buffer.
 * @param edns: edns info from query.
 * @param repinfo: reply information for a communication point.
 * @param pkt: packet buffer.
 * @return: true if a reply is to be sent.
 */
static int
answer_chaos(struct worker* w, struct query_info* qinfo,
	struct edns_data* edns, struct comm_reply* repinfo, sldns_buffer* pkt)
{
	struct config_file* cfg = w->env.cfg;
	if(qinfo->qtype != LDNS_RR_TYPE_ANY && qinfo->qtype != LDNS_RR_TYPE_TXT)
		return 0;
	if(query_dname_compare(qinfo->qname, 
		(uint8_t*)"\002id\006server") == 0 ||
		query_dname_compare(qinfo->qname, 
		(uint8_t*)"\010hostname\004bind") == 0)
	{
		if(cfg->hide_identity) 
			return 0;
		if(cfg->identity==NULL || cfg->identity[0]==0) {
			char buf[MAXHOSTNAMELEN+1];
			if (gethostname(buf, MAXHOSTNAMELEN) == 0) {
				buf[MAXHOSTNAMELEN] = 0;
				chaos_replyonestr(pkt, buf, edns, w, repinfo);
			} else 	{
				log_err("gethostname: %s", strerror(errno));
				chaos_replyonestr(pkt, "no hostname", edns, w, repinfo);
			}
		}
		else 	chaos_replyonestr(pkt, cfg->identity, edns, w, repinfo);
		return 1;
	}
	if(query_dname_compare(qinfo->qname, 
		(uint8_t*)"\007version\006server") == 0 ||
		query_dname_compare(qinfo->qname, 
		(uint8_t*)"\007version\004bind") == 0)
	{
		if(cfg->hide_version) 
			return 0;
		if(cfg->version==NULL || cfg->version[0]==0)
			chaos_replyonestr(pkt, PACKAGE_STRING, edns, w, repinfo);
		else 	chaos_replyonestr(pkt, cfg->version, edns, w, repinfo);
		return 1;
	}
	if(query_dname_compare(qinfo->qname,
		(uint8_t*)"\013trustanchor\007unbound") == 0)
	{
		if(cfg->hide_trustanchor)
			return 0;
		chaos_trustanchor(pkt, edns, w, repinfo);
		return 1;
	}

	return 0;
}

/**
 * Answer notify queries.  These are notifies for authoritative zones,
 * the reply is an ack that the notify has been received.  We need to check
 * access permission here.
 * @param w: worker
 * @param qinfo: query info. Pointer into packet buffer.
 * @param edns: edns info from query.
 * @param repinfo: reply info with source address.
 * @param pkt: packet buffer.
 */
static void
answer_notify(struct worker* w, struct query_info* qinfo, 
	struct edns_data* edns, sldns_buffer* pkt, struct comm_reply* repinfo)
{
	int refused = 0;
	int rcode = LDNS_RCODE_NOERROR;
	uint32_t serial = 0;
	int has_serial;
	if(!w->env.auth_zones) return;
	has_serial = auth_zone_parse_notify_serial(pkt, &serial);
	if(auth_zones_notify(w->env.auth_zones, &w->env, qinfo->qname,
		qinfo->qname_len, qinfo->qclass, &repinfo->addr,
		repinfo->addrlen, has_serial, serial, &refused)) {
		rcode = LDNS_RCODE_NOERROR;
	} else {
		if(refused)
			rcode = LDNS_RCODE_REFUSED;
		else	rcode = LDNS_RCODE_SERVFAIL;
	}

	if(verbosity >= VERB_DETAIL) {
		char buf[380];
		char zname[255+1];
		char sr[25];
		dname_str(qinfo->qname, zname);
		sr[0]=0;
		if(has_serial)
			snprintf(sr, sizeof(sr), "serial %u ",
				(unsigned)serial);
		if(rcode == LDNS_RCODE_REFUSED)
			snprintf(buf, sizeof(buf),
				"refused NOTIFY %sfor %s from", sr, zname);
		else if(rcode == LDNS_RCODE_SERVFAIL)
			snprintf(buf, sizeof(buf),
				"servfail for NOTIFY %sfor %s from", sr, zname);
		else	snprintf(buf, sizeof(buf),
				"received NOTIFY %sfor %s from", sr, zname);
		log_addr(VERB_DETAIL, buf, &repinfo->addr, repinfo->addrlen);
	}
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;
	edns->opt_list = NULL;
	error_encode(pkt, rcode, qinfo,
		*(uint16_t*)(void *)sldns_buffer_begin(pkt),
		sldns_buffer_read_u16_at(pkt, 2), edns);
	LDNS_OPCODE_SET(sldns_buffer_begin(pkt), LDNS_PACKET_NOTIFY);
}

static int
deny_refuse(struct comm_point* c, enum acl_access acl,
	enum acl_access deny, enum acl_access refuse,
	struct worker* worker, struct comm_reply* repinfo)
{
	if(acl == deny) {
		comm_point_drop_reply(repinfo);
		if(worker->stats.extended)
			worker->stats.unwanted_queries++;
		return 0;
	} else if(acl == refuse) {
		log_addr(VERB_ALGO, "refused query from",
			&repinfo->addr, repinfo->addrlen);
		log_buf(VERB_ALGO, "refuse", c->buffer);
		if(worker->stats.extended)
			worker->stats.unwanted_queries++;
		if(worker_check_request(c->buffer, worker) == -1) {
			comm_point_drop_reply(repinfo);
			return 0; /* discard this */
		}
		sldns_buffer_set_limit(c->buffer, LDNS_HEADER_SIZE);
		sldns_buffer_write_at(c->buffer, 4, 
			(uint8_t*)"\0\0\0\0\0\0\0\0", 8);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer), 
			LDNS_RCODE_REFUSED);
		sldns_buffer_set_position(c->buffer, LDNS_HEADER_SIZE);
		sldns_buffer_flip(c->buffer);
		return 1;
	}

	return -1;
}

static int
deny_refuse_all(struct comm_point* c, enum acl_access acl,
	struct worker* worker, struct comm_reply* repinfo)
{
	return deny_refuse(c, acl, acl_deny, acl_refuse, worker, repinfo);
}

static int
deny_refuse_non_local(struct comm_point* c, enum acl_access acl,
	struct worker* worker, struct comm_reply* repinfo)
{
	return deny_refuse(c, acl, acl_deny_non_local, acl_refuse_non_local, worker, repinfo);
}

int 
worker_handle_request(struct comm_point* c, void* arg, int error,
	struct comm_reply* repinfo)
{
	struct worker* worker = (struct worker*)arg;
	int ret;
	hashvalue_type h;
	struct lruhash_entry* e;
	struct query_info qinfo;
	struct edns_data edns;
	enum acl_access acl;
	struct acl_addr* acladdr;
	int rc = 0;
	int need_drop = 0;
	/* We might have to chase a CNAME chain internally, in which case
	 * we'll have up to two replies and combine them to build a complete
	 * answer.  These variables control this case. */
	struct ub_packed_rrset_key* alias_rrset = NULL;
	struct reply_info* partial_rep = NULL;
	struct query_info* lookup_qinfo = &qinfo;
	struct query_info qinfo_tmp; /* placeholdoer for lookup_qinfo */
	struct respip_client_info* cinfo = NULL, cinfo_tmp;
	memset(&qinfo, 0, sizeof(qinfo));

	if(error != NETEVENT_NOERROR) {
		/* some bad tcp query DNS formats give these error calls */
		verbose(VERB_ALGO, "handle request called with err=%d", error);
		return 0;
	}
#ifdef USE_DNSCRYPT
	repinfo->max_udp_size = worker->daemon->cfg->max_udp_size;
	if(!dnsc_handle_curved_request(worker->daemon->dnscenv, repinfo)) {
		worker->stats.num_query_dnscrypt_crypted_malformed++;
		return 0;
	}
	if(c->dnscrypt && !repinfo->is_dnscrypted) {
		char buf[LDNS_MAX_DOMAINLEN+1];
		/* Check if this is unencrypted and asking for certs */
		if(worker_check_request(c->buffer, worker) != 0) {
			verbose(VERB_ALGO,
				"dnscrypt: worker check request: bad query.");
			log_addr(VERB_CLIENT,"from",&repinfo->addr,
				repinfo->addrlen);
			comm_point_drop_reply(repinfo);
			return 0;
		}
		if(!query_info_parse(&qinfo, c->buffer)) {
			verbose(VERB_ALGO,
				"dnscrypt: worker parse request: formerror.");
			log_addr(VERB_CLIENT, "from", &repinfo->addr,
				repinfo->addrlen);
			comm_point_drop_reply(repinfo);
			return 0;
		}
		dname_str(qinfo.qname, buf);
		if(!(qinfo.qtype == LDNS_RR_TYPE_TXT &&
			strcasecmp(buf,
			worker->daemon->dnscenv->provider_name) == 0)) {
			verbose(VERB_ALGO,
				"dnscrypt: not TXT \"%s\". Received: %s \"%s\"",
				worker->daemon->dnscenv->provider_name,
				sldns_rr_descript(qinfo.qtype)->_name,
				buf);
			comm_point_drop_reply(repinfo);
			worker->stats.num_query_dnscrypt_cleartext++;
			return 0;
		}
		worker->stats.num_query_dnscrypt_cert++;
		sldns_buffer_rewind(c->buffer);
	} else if(c->dnscrypt && repinfo->is_dnscrypted) {
		worker->stats.num_query_dnscrypt_crypted++;
	}
#endif
#ifdef USE_DNSTAP
	if(worker->dtenv.log_client_query_messages)
		dt_msg_send_client_query(&worker->dtenv, &repinfo->addr, c->type,
			c->buffer);
#endif
	acladdr = acl_addr_lookup(worker->daemon->acl, &repinfo->addr, 
		repinfo->addrlen);
	acl = acl_get_control(acladdr);
	if((ret=deny_refuse_all(c, acl, worker, repinfo)) != -1)
	{
		if(ret == 1)
			goto send_reply;
		return ret;
	}
	if((ret=worker_check_request(c->buffer, worker)) != 0) {
		verbose(VERB_ALGO, "worker check request: bad query.");
		log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
		if(ret != -1) {
			LDNS_QR_SET(sldns_buffer_begin(c->buffer));
			LDNS_RCODE_SET(sldns_buffer_begin(c->buffer), ret);
			return 1;
		}
		comm_point_drop_reply(repinfo);
		return 0;
	}

	worker->stats.num_queries++;

	/* check if this query should be dropped based on source ip rate limiting */
	if(!infra_ip_ratelimit_inc(worker->env.infra_cache, repinfo,
			*worker->env.now)) {
		/* See if we are passed through with slip factor */
		if(worker->env.cfg->ip_ratelimit_factor != 0 &&
			ub_random_max(worker->env.rnd,
						  worker->env.cfg->ip_ratelimit_factor) == 1) {

			char addrbuf[128];
			addr_to_str(&repinfo->addr, repinfo->addrlen,
						addrbuf, sizeof(addrbuf));
		  verbose(VERB_QUERY, "ip_ratelimit allowed through for ip address %s because of slip in ip_ratelimit_factor",
				  addrbuf);
		} else {
			worker->stats.num_queries_ip_ratelimited++;
			comm_point_drop_reply(repinfo);
			return 0;
		}
	}

	/* see if query is in the cache */
	if(!query_info_parse(&qinfo, c->buffer)) {
		verbose(VERB_ALGO, "worker parse request: formerror.");
		log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
		memset(&qinfo, 0, sizeof(qinfo)); /* zero qinfo.qname */
		if(worker_err_ratelimit(worker, LDNS_RCODE_FORMERR) == -1) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		sldns_buffer_rewind(c->buffer);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer), 
			LDNS_RCODE_FORMERR);
		server_stats_insrcode(&worker->stats, c->buffer);
		goto send_reply;
	}
	if(worker->env.cfg->log_queries) {
		char ip[128];
		addr_to_str(&repinfo->addr, repinfo->addrlen, ip, sizeof(ip));
		log_nametypeclass(0, ip, qinfo.qname, qinfo.qtype, qinfo.qclass);
	}
	if(qinfo.qtype == LDNS_RR_TYPE_AXFR || 
		qinfo.qtype == LDNS_RR_TYPE_IXFR) {
		verbose(VERB_ALGO, "worker request: refused zone transfer.");
		log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
		sldns_buffer_rewind(c->buffer);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer), 
			LDNS_RCODE_REFUSED);
		if(worker->stats.extended) {
			worker->stats.qtype[qinfo.qtype]++;
			server_stats_insrcode(&worker->stats, c->buffer);
		}
		goto send_reply;
	}
	if(qinfo.qtype == LDNS_RR_TYPE_OPT || 
		qinfo.qtype == LDNS_RR_TYPE_TSIG ||
		qinfo.qtype == LDNS_RR_TYPE_TKEY ||
		qinfo.qtype == LDNS_RR_TYPE_MAILA ||
		qinfo.qtype == LDNS_RR_TYPE_MAILB ||
		(qinfo.qtype >= 128 && qinfo.qtype <= 248)) {
		verbose(VERB_ALGO, "worker request: formerror for meta-type.");
		log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
		if(worker_err_ratelimit(worker, LDNS_RCODE_FORMERR) == -1) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		sldns_buffer_rewind(c->buffer);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer), 
			LDNS_RCODE_FORMERR);
		if(worker->stats.extended) {
			worker->stats.qtype[qinfo.qtype]++;
			server_stats_insrcode(&worker->stats, c->buffer);
		}
		goto send_reply;
	}
	if((ret=parse_edns_from_pkt(c->buffer, &edns, worker->scratchpad)) != 0) {
		struct edns_data reply_edns;
		verbose(VERB_ALGO, "worker parse edns: formerror.");
		log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
		memset(&reply_edns, 0, sizeof(reply_edns));
		reply_edns.edns_present = 1;
		reply_edns.udp_size = EDNS_ADVERTISED_SIZE;
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer), ret);
		error_encode(c->buffer, ret, &qinfo,
			*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
			sldns_buffer_read_u16_at(c->buffer, 2), &reply_edns);
		regional_free_all(worker->scratchpad);
		server_stats_insrcode(&worker->stats, c->buffer);
		goto send_reply;
	}
	if(edns.edns_present) {
		struct edns_option* edns_opt;
		if(edns.edns_version != 0) {
			edns.ext_rcode = (uint8_t)(EDNS_RCODE_BADVERS>>4);
			edns.edns_version = EDNS_ADVERTISED_VERSION;
			edns.udp_size = EDNS_ADVERTISED_SIZE;
			edns.bits &= EDNS_DO;
			edns.opt_list = NULL;
			verbose(VERB_ALGO, "query with bad edns version.");
			log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
			error_encode(c->buffer, EDNS_RCODE_BADVERS&0xf, &qinfo,
				*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
				sldns_buffer_read_u16_at(c->buffer, 2), NULL);
			if(sldns_buffer_capacity(c->buffer) >=
			   sldns_buffer_limit(c->buffer)+calc_edns_field_size(&edns))
				attach_edns_record(c->buffer, &edns);
			regional_free_all(worker->scratchpad);
			goto send_reply;
		}
		if(edns.udp_size < NORMAL_UDP_SIZE &&
		   worker->daemon->cfg->harden_short_bufsize) {
			verbose(VERB_QUERY, "worker request: EDNS bufsize %d ignored",
				(int)edns.udp_size);
			log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
			edns.udp_size = NORMAL_UDP_SIZE;
		}
		if(c->type != comm_udp) {
			edns_opt = edns_opt_list_find(edns.opt_list, LDNS_EDNS_KEEPALIVE);
			if(edns_opt && edns_opt->opt_len > 0) {
				edns.ext_rcode = 0;
				edns.edns_version = EDNS_ADVERTISED_VERSION;
				edns.udp_size = EDNS_ADVERTISED_SIZE;
				edns.bits &= EDNS_DO;
				edns.opt_list = NULL;
				verbose(VERB_ALGO, "query with bad edns keepalive.");
				log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
				error_encode(c->buffer, LDNS_RCODE_FORMERR, &qinfo,
					*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
					sldns_buffer_read_u16_at(c->buffer, 2), NULL);
				if(sldns_buffer_capacity(c->buffer) >=
				   sldns_buffer_limit(c->buffer)+calc_edns_field_size(&edns))
					attach_edns_record(c->buffer, &edns);
				regional_free_all(worker->scratchpad);
				goto send_reply;
			}
		}
	}
	if(edns.udp_size > worker->daemon->cfg->max_udp_size &&
		c->type == comm_udp) {
		verbose(VERB_QUERY,
			"worker request: max UDP reply size modified"
			" (%d to max-udp-size)", (int)edns.udp_size);
		log_addr(VERB_CLIENT,"from",&repinfo->addr, repinfo->addrlen);
		edns.udp_size = worker->daemon->cfg->max_udp_size;
	}
	if(edns.udp_size < LDNS_HEADER_SIZE) {
		verbose(VERB_ALGO, "worker request: edns is too small.");
		log_addr(VERB_CLIENT, "from", &repinfo->addr, repinfo->addrlen);
		LDNS_QR_SET(sldns_buffer_begin(c->buffer));
		LDNS_TC_SET(sldns_buffer_begin(c->buffer));
		LDNS_RCODE_SET(sldns_buffer_begin(c->buffer), 
			LDNS_RCODE_SERVFAIL);
		sldns_buffer_set_position(c->buffer, LDNS_HEADER_SIZE);
		sldns_buffer_write_at(c->buffer, 4, 
			(uint8_t*)"\0\0\0\0\0\0\0\0", 8);
		sldns_buffer_flip(c->buffer);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}
	if(worker->stats.extended)
		server_stats_insquery(&worker->stats, c, qinfo.qtype,
			qinfo.qclass, &edns, repinfo);
	if(c->type != comm_udp)
		edns.udp_size = 65535; /* max size for TCP replies */
	if(qinfo.qclass == LDNS_RR_CLASS_CH && answer_chaos(worker, &qinfo,
		&edns, repinfo, c->buffer)) {
		server_stats_insrcode(&worker->stats, c->buffer);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}
	if(LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) ==
		LDNS_PACKET_NOTIFY) {
		answer_notify(worker, &qinfo, &edns, c->buffer, repinfo);
		regional_free_all(worker->scratchpad);
		goto send_reply;
	}
	if(local_zones_answer(worker->daemon->local_zones, &worker->env, &qinfo,
		&edns, c->buffer, worker->scratchpad, repinfo, acladdr->taglist,
		acladdr->taglen, acladdr->tag_actions,
		acladdr->tag_actions_size, acladdr->tag_datas,
		acladdr->tag_datas_size, worker->daemon->cfg->tagname,
		worker->daemon->cfg->num_tags, acladdr->view)) {
		regional_free_all(worker->scratchpad);
		if(sldns_buffer_limit(c->buffer) == 0) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		server_stats_insrcode(&worker->stats, c->buffer);
		goto send_reply;
	}
	if(worker->env.auth_zones &&
		auth_zones_answer(worker->env.auth_zones, &worker->env,
		&qinfo, &edns, repinfo, c->buffer, worker->scratchpad)) {
		regional_free_all(worker->scratchpad);
		if(sldns_buffer_limit(c->buffer) == 0) {
			comm_point_drop_reply(repinfo);
			return 0;
		}
		/* set RA for everyone that can have recursion (based on
		 * access control list) */
		if(LDNS_RD_WIRE(sldns_buffer_begin(c->buffer)) &&
		   acl != acl_deny_non_local && acl != acl_refuse_non_local)
			LDNS_RA_SET(sldns_buffer_begin(c->buffer));
		server_stats_insrcode(&worker->stats, c->buffer);
		goto send_reply;
	}

	/* We've looked in our local zones. If the answer isn't there, we
	 * might need to bail out based on ACLs now. */
	if((ret=deny_refuse_non_local(c, acl, worker, repinfo)) != -1)
	{
		regional_free_all(worker->scratchpad);
		if(ret == 1)
			goto send_reply;
		return ret;
	}

	/* If this request does not have the recursion bit set, verify
	 * ACLs allow the recursion bit to be treated as set. */
	if(!(LDNS_RD_WIRE(sldns_buffer_begin(c->buffer))) &&
		acl == acl_allow_setrd ) {
		LDNS_RD_SET(sldns_buffer_begin(c->buffer));
	}

	/* If this request does not have the recursion bit set, verify
	 * ACLs allow the snooping. */
	if(!(LDNS_RD_WIRE(sldns_buffer_begin(c->buffer))) &&
		acl != acl_allow_snoop ) {
		error_encode(c->buffer, LDNS_RCODE_REFUSED, &qinfo,
			*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
			sldns_buffer_read_u16_at(c->buffer, 2), NULL);
		regional_free_all(worker->scratchpad);
		server_stats_insrcode(&worker->stats, c->buffer);
		log_addr(VERB_ALGO, "refused nonrec (cache snoop) query from",
			&repinfo->addr, repinfo->addrlen);
		goto send_reply;
	}

	/* If we've found a local alias, replace the qname with the alias
	 * target before resolving it. */
	if(qinfo.local_alias) {
		struct ub_packed_rrset_key* rrset = qinfo.local_alias->rrset;
		struct packed_rrset_data* d = rrset->entry.data;

		/* Sanity check: our current implementation only supports
		 * a single CNAME RRset as a local alias. */
		if(qinfo.local_alias->next ||
			rrset->rk.type != htons(LDNS_RR_TYPE_CNAME) ||
			d->count != 1) {
			log_err("assumption failure: unexpected local alias");
			regional_free_all(worker->scratchpad);
			return 0; /* drop it */
		}
		qinfo.qname = d->rr_data[0] + 2;
		qinfo.qname_len = d->rr_len[0] - 2;
	}

	/* If we may apply IP-based actions to the answer, build the client
	 * information.  As this can be expensive, skip it if there is
	 * absolutely no possibility of it. */
	if(worker->daemon->use_response_ip &&
		(qinfo.qtype == LDNS_RR_TYPE_A ||
		qinfo.qtype == LDNS_RR_TYPE_AAAA ||
		qinfo.qtype == LDNS_RR_TYPE_ANY)) {
		cinfo_tmp.taglist = acladdr->taglist;
		cinfo_tmp.taglen = acladdr->taglen;
		cinfo_tmp.tag_actions = acladdr->tag_actions;
		cinfo_tmp.tag_actions_size = acladdr->tag_actions_size;
		cinfo_tmp.tag_datas = acladdr->tag_datas;
		cinfo_tmp.tag_datas_size = acladdr->tag_datas_size;
		cinfo_tmp.view = acladdr->view;
		cinfo_tmp.respip_set = worker->daemon->respip_set;
		cinfo = &cinfo_tmp;
	}

lookup_cache:
	/* Lookup the cache.  In case we chase an intermediate CNAME chain
	 * this is a two-pass operation, and lookup_qinfo is different for
	 * each pass.  We should still pass the original qinfo to
	 * answer_from_cache(), however, since it's used to build the reply. */
	if(!edns_bypass_cache_stage(edns.opt_list, &worker->env)) {
		h = query_info_hash(lookup_qinfo, sldns_buffer_read_u16_at(c->buffer, 2));
		if((e=slabhash_lookup(worker->env.msg_cache, h, lookup_qinfo, 0))) {
			/* answer from cache - we have acquired a readlock on it */
			if(answer_from_cache(worker, &qinfo,
				cinfo, &need_drop, &alias_rrset, &partial_rep,
				(struct reply_info*)e->data,
				*(uint16_t*)(void *)sldns_buffer_begin(c->buffer),
				sldns_buffer_read_u16_at(c->buffer, 2), repinfo,
				&edns)) {
				/* prefetch it if the prefetch TTL expired.
				 * Note that if there is more than one pass
				 * its qname must be that used for cache
				 * lookup. */
				if((worker->env.cfg->prefetch || worker->env.cfg->serve_expired)
					&& *worker->env.now >=
					((struct reply_info*)e->data)->prefetch_ttl) {
					time_t leeway = ((struct reply_info*)e->
						data)->ttl - *worker->env.now;
					if(((struct reply_info*)e->data)->ttl
						< *worker->env.now)
						leeway = 0;
					lock_rw_unlock(&e->lock);
					reply_and_prefetch(worker, lookup_qinfo,
						sldns_buffer_read_u16_at(c->buffer, 2),
						repinfo, leeway);
					if(!partial_rep) {
						rc = 0;
						regional_free_all(worker->scratchpad);
						goto send_reply_rc;
					}
				} else if(!partial_rep) {
					lock_rw_unlock(&e->lock);
					regional_free_all(worker->scratchpad);
					goto send_reply;
				} else {
					/* Note that we've already released the
					 * lock if we're here after prefetch. */
					lock_rw_unlock(&e->lock);
				}
				/* We've found a partial reply ending with an
				 * alias.  Replace the lookup qinfo for the
				 * alias target and lookup the cache again to
				 * (possibly) complete the reply.  As we're
				 * passing the "base" reply, there will be no
				 * more alias chasing. */
				memset(&qinfo_tmp, 0, sizeof(qinfo_tmp));
				get_cname_target(alias_rrset, &qinfo_tmp.qname,
					&qinfo_tmp.qname_len);
				if(!qinfo_tmp.qname) {
					log_err("unexpected: invalid answer alias");
					regional_free_all(worker->scratchpad);
					return 0; /* drop query */
				}
				qinfo_tmp.qtype = qinfo.qtype;
				qinfo_tmp.qclass = qinfo.qclass;
				lookup_qinfo = &qinfo_tmp;
				goto lookup_cache;
			}
			verbose(VERB_ALGO, "answer from the cache failed");
			lock_rw_unlock(&e->lock);
		}
		if(!LDNS_RD_WIRE(sldns_buffer_begin(c->buffer))) {
			if(answer_norec_from_cache(worker, &qinfo,
				*(uint16_t*)(void *)sldns_buffer_begin(c->buffer), 
				sldns_buffer_read_u16_at(c->buffer, 2), repinfo, 
				&edns)) {
				regional_free_all(worker->scratchpad);
				goto send_reply;
			}
			verbose(VERB_ALGO, "answer norec from cache -- "
				"need to validate or not primed");
		}
	}
	sldns_buffer_rewind(c->buffer);
	server_stats_querymiss(&worker->stats, worker);

	if(verbosity >= VERB_CLIENT) {
		if(c->type == comm_udp)
			log_addr(VERB_CLIENT, "udp request from",
				&repinfo->addr, repinfo->addrlen);
		else	log_addr(VERB_CLIENT, "tcp request from",
				&repinfo->addr, repinfo->addrlen);
	}

	/* grab a work request structure for this new request */
	mesh_new_client(worker->env.mesh, &qinfo, cinfo,
		sldns_buffer_read_u16_at(c->buffer, 2),
		&edns, repinfo, *(uint16_t*)(void *)sldns_buffer_begin(c->buffer));
	regional_free_all(worker->scratchpad);
	worker_mem_report(worker, NULL);
	return 0;

send_reply:
	rc = 1;
send_reply_rc:
	if(need_drop) {
		comm_point_drop_reply(repinfo);
		return 0;
	}
#ifdef USE_DNSTAP
	if(worker->dtenv.log_client_response_messages)
		dt_msg_send_client_response(&worker->dtenv, &repinfo->addr,
			c->type, c->buffer);
#endif
	if(worker->env.cfg->log_replies)
	{
		struct timeval tv = {0, 0};
		log_reply_info(0, &qinfo, &repinfo->addr, repinfo->addrlen,
			tv, 1, c->buffer);
	}
#ifdef USE_DNSCRYPT
	if(!dnsc_handle_uncurved_request(repinfo)) {
		return 0;
	}
#endif
	return rc;
}

void 
worker_sighandler(int sig, void* arg)
{
	/* note that log, print, syscalls here give race conditions. 
	 * And cause hangups if the log-lock is held by the application. */
	struct worker* worker = (struct worker*)arg;
	switch(sig) {
#ifdef SIGHUP
		case SIGHUP:
			comm_base_exit(worker->base);
			break;
#endif
		case SIGINT:
			worker->need_to_exit = 1;
			comm_base_exit(worker->base);
			break;
#ifdef SIGQUIT
		case SIGQUIT:
			worker->need_to_exit = 1;
			comm_base_exit(worker->base);
			break;
#endif
		case SIGTERM:
			worker->need_to_exit = 1;
			comm_base_exit(worker->base);
			break;
		default:
			/* unknown signal, ignored */
			break;
	}
}

/** restart statistics timer for worker, if enabled */
static void
worker_restart_timer(struct worker* worker)
{
	if(worker->env.cfg->stat_interval > 0) {
		struct timeval tv;
#ifndef S_SPLINT_S
		tv.tv_sec = worker->env.cfg->stat_interval;
		tv.tv_usec = 0;
#endif
		comm_timer_set(worker->stat_timer, &tv);
	}
}

void worker_stat_timer_cb(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	server_stats_log(&worker->stats, worker, worker->thread_num);
	mesh_stats(worker->env.mesh, "mesh has");
	worker_mem_report(worker, NULL);
	/* SHM is enabled, process data to SHM */
	if (worker->daemon->cfg->shm_enable) {
		shm_main_run(worker);
	}
	if(!worker->daemon->cfg->stat_cumulative) {
		worker_stats_clear(worker);
	}
	/* start next timer */
	worker_restart_timer(worker);
}

void worker_probe_timer_cb(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	struct timeval tv;
#ifndef S_SPLINT_S
	tv.tv_sec = (time_t)autr_probe_timer(&worker->env);
	tv.tv_usec = 0;
#endif
	if(tv.tv_sec != 0)
		comm_timer_set(worker->env.probe_timer, &tv);
}

struct worker* 
worker_create(struct daemon* daemon, int id, int* ports, int n)
{
	unsigned int seed;
	struct worker* worker = (struct worker*)calloc(1, 
		sizeof(struct worker));
	if(!worker) 
		return NULL;
	worker->numports = n;
	worker->ports = (int*)memdup(ports, sizeof(int)*n);
	if(!worker->ports) {
		free(worker);
		return NULL;
	}
	worker->daemon = daemon;
	worker->thread_num = id;
	if(!(worker->cmd = tube_create())) {
		free(worker->ports);
		free(worker);
		return NULL;
	}
	/* create random state here to avoid locking trouble in RAND_bytes */
	seed = (unsigned int)time(NULL) ^ (unsigned int)getpid() ^
		(((unsigned int)worker->thread_num)<<17);
		/* shift thread_num so it does not match out pid bits */
	if(!(worker->rndstate = ub_initstate(seed, daemon->rand))) {
		explicit_bzero(&seed, sizeof(seed));
		log_err("could not init random numbers.");
		tube_delete(worker->cmd);
		free(worker->ports);
		free(worker);
		return NULL;
	}
	explicit_bzero(&seed, sizeof(seed));
#ifdef USE_DNSTAP
	if(daemon->cfg->dnstap) {
		log_assert(daemon->dtenv != NULL);
		memcpy(&worker->dtenv, daemon->dtenv, sizeof(struct dt_env));
		if(!dt_init(&worker->dtenv))
			fatal_exit("dt_init failed");
	}
#endif
	return worker;
}

int
worker_init(struct worker* worker, struct config_file *cfg, 
	struct listen_port* ports, int do_sigs)
{
#ifdef USE_DNSTAP
	struct dt_env* dtenv = &worker->dtenv;
#else
	void* dtenv = NULL;
#endif
	worker->need_to_exit = 0;
	worker->base = comm_base_create(do_sigs);
	if(!worker->base) {
		log_err("could not create event handling base");
		worker_delete(worker);
		return 0;
	}
	comm_base_set_slow_accept_handlers(worker->base, &worker_stop_accept,
		&worker_start_accept, worker);
	if(do_sigs) {
#ifdef SIGHUP
		ub_thread_sig_unblock(SIGHUP);
#endif
		ub_thread_sig_unblock(SIGINT);
#ifdef SIGQUIT
		ub_thread_sig_unblock(SIGQUIT);
#endif
		ub_thread_sig_unblock(SIGTERM);
#ifndef LIBEVENT_SIGNAL_PROBLEM
		worker->comsig = comm_signal_create(worker->base, 
			worker_sighandler, worker);
		if(!worker->comsig 
#ifdef SIGHUP
			|| !comm_signal_bind(worker->comsig, SIGHUP)
#endif
#ifdef SIGQUIT
			|| !comm_signal_bind(worker->comsig, SIGQUIT)
#endif
			|| !comm_signal_bind(worker->comsig, SIGTERM)
			|| !comm_signal_bind(worker->comsig, SIGINT)) {
			log_err("could not create signal handlers");
			worker_delete(worker);
			return 0;
		}
#endif /* LIBEVENT_SIGNAL_PROBLEM */
		if(!daemon_remote_open_accept(worker->daemon->rc, 
			worker->daemon->rc_ports, worker)) {
			worker_delete(worker);
			return 0;
		}
#ifdef UB_ON_WINDOWS
		wsvc_setup_worker(worker);
#endif /* UB_ON_WINDOWS */
	} else { /* !do_sigs */
		worker->comsig = NULL;
	}
	worker->front = listen_create(worker->base, ports,
		cfg->msg_buffer_size, (int)cfg->incoming_num_tcp,
		cfg->do_tcp_keepalive
			? cfg->tcp_keepalive_timeout
			: cfg->tcp_idle_timeout,
			worker->daemon->tcl,
		worker->daemon->listen_sslctx,
		dtenv, worker_handle_request, worker);
	if(!worker->front) {
		log_err("could not create listening sockets");
		worker_delete(worker);
		return 0;
	}
	worker->back = outside_network_create(worker->base,
		cfg->msg_buffer_size, (size_t)cfg->outgoing_num_ports, 
		cfg->out_ifs, cfg->num_out_ifs, cfg->do_ip4, cfg->do_ip6, 
		cfg->do_tcp?cfg->outgoing_num_tcp:0, 
		worker->daemon->env->infra_cache, worker->rndstate,
		cfg->use_caps_bits_for_id, worker->ports, worker->numports,
		cfg->unwanted_threshold, cfg->outgoing_tcp_mss,
		&worker_alloc_cleanup, worker,
		cfg->do_udp || cfg->udp_upstream_without_downstream,
		worker->daemon->connect_sslctx, cfg->delay_close,
		dtenv);
	if(!worker->back) {
		log_err("could not create outgoing sockets");
		worker_delete(worker);
		return 0;
	}
	/* start listening to commands */
	if(!tube_setup_bg_listen(worker->cmd, worker->base,
		&worker_handle_control_cmd, worker)) {
		log_err("could not create control compt.");
		worker_delete(worker);
		return 0;
	}
	worker->stat_timer = comm_timer_create(worker->base, 
		worker_stat_timer_cb, worker);
	if(!worker->stat_timer) {
		log_err("could not create statistics timer");
	}

	/* we use the msg_buffer_size as a good estimate for what the 
	 * user wants for memory usage sizes */
	worker->scratchpad = regional_create_custom(cfg->msg_buffer_size);
	if(!worker->scratchpad) {
		log_err("malloc failure");
		worker_delete(worker);
		return 0;
	}

	server_stats_init(&worker->stats, cfg);
	alloc_init(&worker->alloc, &worker->daemon->superalloc, 
		worker->thread_num);
	alloc_set_id_cleanup(&worker->alloc, &worker_alloc_cleanup, worker);
	worker->env = *worker->daemon->env;
	comm_base_timept(worker->base, &worker->env.now, &worker->env.now_tv);
	if(worker->thread_num == 0)
		log_set_time(worker->env.now);
	worker->env.worker = worker;
	worker->env.worker_base = worker->base;
	worker->env.send_query = &worker_send_query;
	worker->env.alloc = &worker->alloc;
	worker->env.outnet = worker->back;
	worker->env.rnd = worker->rndstate;
	/* If case prefetch is triggered, the corresponding mesh will clear
	 * the scratchpad for the module env in the middle of request handling.
	 * It would be prone to a use-after-free kind of bug, so we avoid
	 * sharing it with worker's own scratchpad at the cost of having
	 * one more pad per worker. */
	worker->env.scratch = regional_create_custom(cfg->msg_buffer_size);
	if(!worker->env.scratch) {
		log_err("malloc failure");
		worker_delete(worker);
		return 0;
	}
	worker->env.mesh = mesh_create(&worker->daemon->mods, &worker->env);
	worker->env.detach_subs = &mesh_detach_subs;
	worker->env.attach_sub = &mesh_attach_sub;
	worker->env.add_sub = &mesh_add_sub;
	worker->env.kill_sub = &mesh_state_delete;
	worker->env.detect_cycle = &mesh_detect_cycle;
	worker->env.scratch_buffer = sldns_buffer_new(cfg->msg_buffer_size);
	if(!(worker->env.fwds = forwards_create()) ||
		!forwards_apply_cfg(worker->env.fwds, cfg)) {
		log_err("Could not set forward zones");
		worker_delete(worker);
		return 0;
	}
	if(!(worker->env.hints = hints_create()) ||
		!hints_apply_cfg(worker->env.hints, cfg)) {
		log_err("Could not set root or stub hints");
		worker_delete(worker);
		return 0;
	}
	/* one probe timer per process -- if we have 5011 anchors */
	if(autr_get_num_anchors(worker->env.anchors) > 0
#ifndef THREADS_DISABLED
		&& worker->thread_num == 0
#endif
		) {
		struct timeval tv;
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		worker->env.probe_timer = comm_timer_create(worker->base,
			worker_probe_timer_cb, worker);
		if(!worker->env.probe_timer) {
			log_err("could not create 5011-probe timer");
		} else {
			/* let timer fire, then it can reset itself */
			comm_timer_set(worker->env.probe_timer, &tv);
		}
	}
	/* zone transfer tasks, setup once per process, if any */
	if(worker->env.auth_zones
#ifndef THREADS_DISABLED
		&& worker->thread_num == 0
#endif
		) {
		auth_xfer_pickup_initial(worker->env.auth_zones, &worker->env);
	}
	if(!worker->env.mesh || !worker->env.scratch_buffer) {
		worker_delete(worker);
		return 0;
	}
	worker_mem_report(worker, NULL);
	/* if statistics enabled start timer */
	if(worker->env.cfg->stat_interval > 0) {
		verbose(VERB_ALGO, "set statistics interval %d secs", 
			worker->env.cfg->stat_interval);
		worker_restart_timer(worker);
	}
	return 1;
}

void 
worker_work(struct worker* worker)
{
	comm_base_dispatch(worker->base);
}

void 
worker_delete(struct worker* worker)
{
	if(!worker) 
		return;
	if(worker->env.mesh && verbosity >= VERB_OPS) {
		server_stats_log(&worker->stats, worker, worker->thread_num);
		mesh_stats(worker->env.mesh, "mesh has");
		worker_mem_report(worker, NULL);
	}
	outside_network_quit_prepare(worker->back);
	mesh_delete(worker->env.mesh);
	sldns_buffer_free(worker->env.scratch_buffer);
	forwards_delete(worker->env.fwds);
	hints_delete(worker->env.hints);
	listen_delete(worker->front);
	outside_network_delete(worker->back);
	comm_signal_delete(worker->comsig);
	tube_delete(worker->cmd);
	comm_timer_delete(worker->stat_timer);
	comm_timer_delete(worker->env.probe_timer);
	free(worker->ports);
	if(worker->thread_num == 0) {
		log_set_time(NULL);
#ifdef UB_ON_WINDOWS
		wsvc_desetup_worker(worker);
#endif /* UB_ON_WINDOWS */
	}
	comm_base_delete(worker->base);
	ub_randfree(worker->rndstate);
	alloc_clear(&worker->alloc);
	regional_destroy(worker->env.scratch);
	regional_destroy(worker->scratchpad);
	free(worker);
}

struct outbound_entry*
worker_send_query(struct query_info* qinfo, uint16_t flags, int dnssec,
	int want_dnssec, int nocaps, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* zone, size_t zonelen, int ssl_upstream,
	char* tls_auth_name, struct module_qstate* q)
{
	struct worker* worker = q->env->worker;
	struct outbound_entry* e = (struct outbound_entry*)regional_alloc(
		q->region, sizeof(*e));
	if(!e) 
		return NULL;
	e->qstate = q;
	e->qsent = outnet_serviced_query(worker->back, qinfo, flags, dnssec,
		want_dnssec, nocaps, q->env->cfg->tcp_upstream,
		ssl_upstream, tls_auth_name, addr, addrlen, zone, zonelen, q,
		worker_handle_service_reply, e, worker->back->udp_buff, q->env);
	if(!e->qsent) {
		return NULL;
	}
	return e;
}

void 
worker_alloc_cleanup(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	slabhash_clear(&worker->env.rrset_cache->table);
	slabhash_clear(worker->env.msg_cache);
}

void worker_stats_clear(struct worker* worker)
{
	server_stats_init(&worker->stats, worker->env.cfg);
	mesh_stats_clear(worker->env.mesh);
	worker->back->unwanted_replies = 0;
	worker->back->num_tcp_outgoing = 0;
}

void worker_start_accept(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	listen_start_accept(worker->front);
	if(worker->thread_num == 0)
		daemon_remote_start_accept(worker->daemon->rc);
}

void worker_stop_accept(void* arg)
{
	struct worker* worker = (struct worker*)arg;
	listen_stop_accept(worker->front);
	if(worker->thread_num == 0)
		daemon_remote_stop_accept(worker->daemon->rc);
}

/* --- fake callbacks for fptr_wlist to work --- */
struct outbound_entry* libworker_send_query(
	struct query_info* ATTR_UNUSED(qinfo),
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

int libworker_handle_reply(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

int libworker_handle_service_reply(struct comm_point* ATTR_UNUSED(c), 
	void* ATTR_UNUSED(arg), int ATTR_UNUSED(error),
        struct comm_reply* ATTR_UNUSED(reply_info))
{
	log_assert(0);
	return 0;
}

void libworker_handle_control_cmd(struct tube* ATTR_UNUSED(tube),
        uint8_t* ATTR_UNUSED(buffer), size_t ATTR_UNUSED(len),
        int ATTR_UNUSED(error), void* ATTR_UNUSED(arg))
{
	log_assert(0);
}

void libworker_fg_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode),
	sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

void libworker_bg_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode),
	sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

void libworker_event_done_cb(void* ATTR_UNUSED(arg), int ATTR_UNUSED(rcode),
	sldns_buffer* ATTR_UNUSED(buf), enum sec_status ATTR_UNUSED(s),
	char* ATTR_UNUSED(why_bogus), int ATTR_UNUSED(was_ratelimited))
{
	log_assert(0);
}

int context_query_cmp(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
	log_assert(0);
	return 0;
}

int order_lock_cmp(const void* ATTR_UNUSED(e1), const void* ATTR_UNUSED(e2))
{
	log_assert(0);
	return 0;
}

int codeline_cmp(const void* ATTR_UNUSED(a), const void* ATTR_UNUSED(b))
{
	log_assert(0);
	return 0;
}

