/*
 * daemon/stats.c - collect runtime performance indicators.
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
 * This file describes the data structure used to collect runtime performance
 * numbers. These 'statistics' may be of interest to the operator.
 */
#include "config.h"
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <sys/time.h>
#include <sys/types.h>
#include "daemon/stats.h"
#include "daemon/worker.h"
#include "daemon/daemon.h"
#include "services/mesh.h"
#include "services/outside_network.h"
#include "services/listen_dnsport.h"
#include "util/config_file.h"
#include "util/tube.h"
#include "util/timehist.h"
#include "util/net_help.h"
#include "validator/validator.h"
#include "iterator/iterator.h"
#include "sldns/sbuffer.h"
#include "services/cache/rrset.h"
#include "services/cache/infra.h"
#include "services/authzone.h"
#include "validator/val_kcache.h"
#include "validator/val_neg.h"
#ifdef CLIENT_SUBNET
#include "edns-subnet/subnetmod.h"
#endif

/** add timers and the values do not overflow or become negative */
static void
stats_timeval_add(long long* d_sec, long long* d_usec, long long add_sec, long long add_usec)
{
#ifndef S_SPLINT_S
	(*d_sec) += add_sec;
	(*d_usec) += add_usec;
	if((*d_usec) > 1000000) {
		(*d_usec) -= 1000000;
		(*d_sec)++;
	}
#endif
}

void server_stats_init(struct ub_server_stats* stats, struct config_file* cfg)
{
	memset(stats, 0, sizeof(*stats));
	stats->extended = cfg->stat_extended;
}

void server_stats_querymiss(struct ub_server_stats* stats, struct worker* worker)
{
	stats->num_queries_missed_cache++;
	stats->sum_query_list_size += worker->env.mesh->all.count;
	if((long long)worker->env.mesh->all.count > stats->max_query_list_size)
		stats->max_query_list_size = (long long)worker->env.mesh->all.count;
}

void server_stats_prefetch(struct ub_server_stats* stats, struct worker* worker)
{
	stats->num_queries_prefetch++;
	/* changes the query list size so account that, like a querymiss */
	stats->sum_query_list_size += worker->env.mesh->all.count;
	if((long long)worker->env.mesh->all.count > stats->max_query_list_size)
		stats->max_query_list_size = (long long)worker->env.mesh->all.count;
}

void server_stats_log(struct ub_server_stats* stats, struct worker* worker,
	int threadnum)
{
	log_info("server stats for thread %d: %u queries, "
		"%u answers from cache, %u recursions, %u prefetch, %u rejected by "
		"ip ratelimiting",
		threadnum, (unsigned)stats->num_queries, 
		(unsigned)(stats->num_queries - 
			stats->num_queries_missed_cache),
		(unsigned)stats->num_queries_missed_cache,
		(unsigned)stats->num_queries_prefetch,
		(unsigned)stats->num_queries_ip_ratelimited);
	log_info("server stats for thread %d: requestlist max %u avg %g "
		"exceeded %u jostled %u", threadnum,
		(unsigned)stats->max_query_list_size,
		(stats->num_queries_missed_cache+stats->num_queries_prefetch)?
			(double)stats->sum_query_list_size/
			(double)(stats->num_queries_missed_cache+
			stats->num_queries_prefetch) : 0.0,
		(unsigned)worker->env.mesh->stats_dropped,
		(unsigned)worker->env.mesh->stats_jostled);
}


#ifdef CLIENT_SUBNET
/** Set the EDNS Subnet stats. */
static void
set_subnet_stats(struct worker* worker, struct ub_server_stats* svr,
	int reset)
{
	int m = modstack_find(&worker->env.mesh->mods, "subnet");
	struct subnet_env* sne;
	if(m == -1)
		return;
	sne = (struct subnet_env*)worker->env.modinfo[m];
	if(reset && !worker->env.cfg->stat_cumulative) {
		lock_rw_wrlock(&sne->biglock);
	} else {
		lock_rw_rdlock(&sne->biglock);
	}
	svr->num_query_subnet = (long long)(sne->num_msg_nocache + sne->num_msg_cache);
	svr->num_query_subnet_cache = (long long)sne->num_msg_cache;
	if(reset && !worker->env.cfg->stat_cumulative) {
		sne->num_msg_cache = 0;
		sne->num_msg_nocache = 0;
	}
	lock_rw_unlock(&sne->biglock);
}
#endif /* CLIENT_SUBNET */

/** Set the neg cache stats. */
static void
set_neg_cache_stats(struct worker* worker, struct ub_server_stats* svr,
	int reset)
{
	int m = modstack_find(&worker->env.mesh->mods, "validator");
	struct val_env* ve;
	struct val_neg_cache* neg;
	if(m == -1)
		return;
	ve = (struct val_env*)worker->env.modinfo[m];
	if(!ve->neg_cache)
		return;
	neg = ve->neg_cache;
	lock_basic_lock(&neg->lock);
	svr->num_neg_cache_noerror = (long long)neg->num_neg_cache_noerror;
	svr->num_neg_cache_nxdomain = (long long)neg->num_neg_cache_nxdomain;
	if(reset && !worker->env.cfg->stat_cumulative) {
		neg->num_neg_cache_noerror = 0;
		neg->num_neg_cache_nxdomain = 0;
	}
	lock_basic_unlock(&neg->lock);
}

/** get rrsets bogus number from validator */
static size_t
get_rrset_bogus(struct worker* worker, int reset)
{
	int m = modstack_find(&worker->env.mesh->mods, "validator");
	struct val_env* ve;
	size_t r;
	if(m == -1)
		return 0;
	ve = (struct val_env*)worker->env.modinfo[m];
	lock_basic_lock(&ve->bogus_lock);
	r = ve->num_rrset_bogus;
	if(reset && !worker->env.cfg->stat_cumulative)
		ve->num_rrset_bogus = 0;
	lock_basic_unlock(&ve->bogus_lock);
	return r;
}

/** get number of ratelimited queries from iterator */
static size_t
get_queries_ratelimit(struct worker* worker, int reset)
{
	int m = modstack_find(&worker->env.mesh->mods, "iterator");
	struct iter_env* ie;
	size_t r;
	if(m == -1)
		return 0;
	ie = (struct iter_env*)worker->env.modinfo[m];
	lock_basic_lock(&ie->queries_ratelimit_lock);
	r = ie->num_queries_ratelimited;
	if(reset && !worker->env.cfg->stat_cumulative)
		ie->num_queries_ratelimited = 0;
	lock_basic_unlock(&ie->queries_ratelimit_lock);
	return r;
}

#ifdef USE_DNSCRYPT
/** get the number of shared secret cache miss */
static size_t
get_dnscrypt_cache_miss(struct worker* worker, int reset)
{
	size_t r;
	struct dnsc_env* de = worker->daemon->dnscenv;
	if(!de) return 0;

	lock_basic_lock(&de->shared_secrets_cache_lock);
	r = de->num_query_dnscrypt_secret_missed_cache;
	if(reset && !worker->env.cfg->stat_cumulative)
		de->num_query_dnscrypt_secret_missed_cache = 0;
	lock_basic_unlock(&de->shared_secrets_cache_lock);
	return r;
}

/** get the number of replayed queries */
static size_t
get_dnscrypt_replay(struct worker* worker, int reset)
{
	size_t r;
	struct dnsc_env* de = worker->daemon->dnscenv;

	lock_basic_lock(&de->nonces_cache_lock);
	r = de->num_query_dnscrypt_replay;
	if(reset && !worker->env.cfg->stat_cumulative)
		de->num_query_dnscrypt_replay = 0;
	lock_basic_unlock(&de->nonces_cache_lock);
	return r;
}
#endif /* USE_DNSCRYPT */

void
server_stats_compile(struct worker* worker, struct ub_stats_info* s, int reset)
{
	int i;
	struct listen_list* lp;

	s->svr = worker->stats;
	s->mesh_num_states = (long long)worker->env.mesh->all.count;
	s->mesh_num_reply_states = (long long)worker->env.mesh->num_reply_states;
	s->mesh_jostled = (long long)worker->env.mesh->stats_jostled;
	s->mesh_dropped = (long long)worker->env.mesh->stats_dropped;
	s->mesh_replies_sent = (long long)worker->env.mesh->replies_sent;
	s->mesh_replies_sum_wait_sec = (long long)worker->env.mesh->replies_sum_wait.tv_sec;
	s->mesh_replies_sum_wait_usec = (long long)worker->env.mesh->replies_sum_wait.tv_usec;
	s->mesh_time_median = timehist_quartile(worker->env.mesh->histogram,
		0.50);

	/* add in the values from the mesh */
	s->svr.ans_secure += (long long)worker->env.mesh->ans_secure;
	s->svr.ans_bogus += (long long)worker->env.mesh->ans_bogus;
	s->svr.ans_rcode_nodata += (long long)worker->env.mesh->ans_nodata;
	for(i=0; i<16; i++)
		s->svr.ans_rcode[i] += (long long)worker->env.mesh->ans_rcode[i];
	timehist_export(worker->env.mesh->histogram, s->svr.hist, 
		NUM_BUCKETS_HIST);
	/* values from outside network */
	s->svr.unwanted_replies = (long long)worker->back->unwanted_replies;
	s->svr.qtcp_outgoing = (long long)worker->back->num_tcp_outgoing;

	/* get and reset validator rrset bogus number */
	s->svr.rrset_bogus = (long long)get_rrset_bogus(worker, reset);

	/* get and reset iterator query ratelimit number */
	s->svr.queries_ratelimited = (long long)get_queries_ratelimit(worker, reset);

	/* get cache sizes */
	s->svr.msg_cache_count = (long long)count_slabhash_entries(worker->env.msg_cache);
	s->svr.rrset_cache_count = (long long)count_slabhash_entries(&worker->env.rrset_cache->table);
	s->svr.infra_cache_count = (long long)count_slabhash_entries(worker->env.infra_cache->hosts);
	if(worker->env.key_cache)
		s->svr.key_cache_count = (long long)count_slabhash_entries(worker->env.key_cache->slab);
	else	s->svr.key_cache_count = 0;

#ifdef USE_DNSCRYPT
	if(worker->daemon->dnscenv) {
		s->svr.num_query_dnscrypt_secret_missed_cache =
			(long long)get_dnscrypt_cache_miss(worker, reset);
		s->svr.shared_secret_cache_count = (long long)count_slabhash_entries(
			worker->daemon->dnscenv->shared_secrets_cache);
		s->svr.nonce_cache_count = (long long)count_slabhash_entries(
			worker->daemon->dnscenv->nonces_cache);
		s->svr.num_query_dnscrypt_replay =
			(long long)get_dnscrypt_replay(worker, reset);
	} else {
		s->svr.num_query_dnscrypt_secret_missed_cache = 0;
		s->svr.shared_secret_cache_count = 0;
		s->svr.nonce_cache_count = 0;
		s->svr.num_query_dnscrypt_replay = 0;
	}
#else
	s->svr.num_query_dnscrypt_secret_missed_cache = 0;
	s->svr.shared_secret_cache_count = 0;
	s->svr.nonce_cache_count = 0;
	s->svr.num_query_dnscrypt_replay = 0;
#endif /* USE_DNSCRYPT */
	if(worker->env.auth_zones) {
		if(reset && !worker->env.cfg->stat_cumulative) {
			lock_rw_wrlock(&worker->env.auth_zones->lock);
		} else {
			lock_rw_rdlock(&worker->env.auth_zones->lock);
		}
		s->svr.num_query_authzone_up = (long long)worker->env.
			auth_zones->num_query_up;
		s->svr.num_query_authzone_down = (long long)worker->env.
			auth_zones->num_query_down;
		if(reset && !worker->env.cfg->stat_cumulative) {
			worker->env.auth_zones->num_query_up = 0;
			worker->env.auth_zones->num_query_down = 0;
		}
		lock_rw_unlock(&worker->env.auth_zones->lock);
	}

	/* Set neg cache usage numbers */
	set_neg_cache_stats(worker, &s->svr, reset);
#ifdef CLIENT_SUBNET
	/* EDNS Subnet usage numbers */
	set_subnet_stats(worker, &s->svr, reset);
#else
	s->svr.num_query_subnet = 0;
	s->svr.num_query_subnet_cache = 0;
#endif

	/* get tcp accept usage */
	s->svr.tcp_accept_usage = 0;
	for(lp = worker->front->cps; lp; lp = lp->next) {
		if(lp->com->type == comm_tcp_accept)
			s->svr.tcp_accept_usage += (long long)lp->com->cur_tcp_count;
	}

	if(reset && !worker->env.cfg->stat_cumulative) {
		worker_stats_clear(worker);
	}
}

void server_stats_obtain(struct worker* worker, struct worker* who,
	struct ub_stats_info* s, int reset)
{
	uint8_t *reply = NULL;
	uint32_t len = 0;
	if(worker == who) {
		/* just fill it in */
		server_stats_compile(worker, s, reset);
		return;
	}
	/* communicate over tube */
	verbose(VERB_ALGO, "write stats cmd");
	if(reset)
		worker_send_cmd(who, worker_cmd_stats);
	else 	worker_send_cmd(who, worker_cmd_stats_noreset);
	verbose(VERB_ALGO, "wait for stats reply");
	if(!tube_read_msg(worker->cmd, &reply, &len, 0))
		fatal_exit("failed to read stats over cmd channel");
	if(len != (uint32_t)sizeof(*s))
		fatal_exit("stats on cmd channel wrong length %d %d",
			(int)len, (int)sizeof(*s));
	memcpy(s, reply, (size_t)len);
	free(reply);
}

void server_stats_reply(struct worker* worker, int reset)
{
	struct ub_stats_info s;
	server_stats_compile(worker, &s, reset);
	verbose(VERB_ALGO, "write stats replymsg");
	if(!tube_write_msg(worker->daemon->workers[0]->cmd, 
		(uint8_t*)&s, sizeof(s), 0))
		fatal_exit("could not write stat values over cmd channel");
}

void server_stats_add(struct ub_stats_info* total, struct ub_stats_info* a)
{
	total->svr.num_queries += a->svr.num_queries;
	total->svr.num_queries_ip_ratelimited += a->svr.num_queries_ip_ratelimited;
	total->svr.num_queries_missed_cache += a->svr.num_queries_missed_cache;
	total->svr.num_queries_prefetch += a->svr.num_queries_prefetch;
	total->svr.sum_query_list_size += a->svr.sum_query_list_size;
#ifdef USE_DNSCRYPT
	total->svr.num_query_dnscrypt_crypted += a->svr.num_query_dnscrypt_crypted;
	total->svr.num_query_dnscrypt_cert += a->svr.num_query_dnscrypt_cert;
	total->svr.num_query_dnscrypt_cleartext += \
		a->svr.num_query_dnscrypt_cleartext;
	total->svr.num_query_dnscrypt_crypted_malformed += \
		a->svr.num_query_dnscrypt_crypted_malformed;
#endif /* USE_DNSCRYPT */
	/* the max size reached is upped to higher of both */
	if(a->svr.max_query_list_size > total->svr.max_query_list_size)
		total->svr.max_query_list_size = a->svr.max_query_list_size;

	if(a->svr.extended) {
		int i;
		total->svr.qtype_big += a->svr.qtype_big;
		total->svr.qclass_big += a->svr.qclass_big;
		total->svr.qtcp += a->svr.qtcp;
		total->svr.qtcp_outgoing += a->svr.qtcp_outgoing;
		total->svr.qtls += a->svr.qtls;
		total->svr.qipv6 += a->svr.qipv6;
		total->svr.qbit_QR += a->svr.qbit_QR;
		total->svr.qbit_AA += a->svr.qbit_AA;
		total->svr.qbit_TC += a->svr.qbit_TC;
		total->svr.qbit_RD += a->svr.qbit_RD;
		total->svr.qbit_RA += a->svr.qbit_RA;
		total->svr.qbit_Z += a->svr.qbit_Z;
		total->svr.qbit_AD += a->svr.qbit_AD;
		total->svr.qbit_CD += a->svr.qbit_CD;
		total->svr.qEDNS += a->svr.qEDNS;
		total->svr.qEDNS_DO += a->svr.qEDNS_DO;
		total->svr.ans_rcode_nodata += a->svr.ans_rcode_nodata;
		total->svr.zero_ttl_responses += a->svr.zero_ttl_responses;
		total->svr.ans_secure += a->svr.ans_secure;
		total->svr.ans_bogus += a->svr.ans_bogus;
		total->svr.unwanted_replies += a->svr.unwanted_replies;
		total->svr.unwanted_queries += a->svr.unwanted_queries;
		total->svr.tcp_accept_usage += a->svr.tcp_accept_usage;
		for(i=0; i<UB_STATS_QTYPE_NUM; i++)
			total->svr.qtype[i] += a->svr.qtype[i];
		for(i=0; i<UB_STATS_QCLASS_NUM; i++)
			total->svr.qclass[i] += a->svr.qclass[i];
		for(i=0; i<UB_STATS_OPCODE_NUM; i++)
			total->svr.qopcode[i] += a->svr.qopcode[i];
		for(i=0; i<UB_STATS_RCODE_NUM; i++)
			total->svr.ans_rcode[i] += a->svr.ans_rcode[i];
		for(i=0; i<NUM_BUCKETS_HIST; i++)
			total->svr.hist[i] += a->svr.hist[i];
	}

	total->mesh_num_states += a->mesh_num_states;
	total->mesh_num_reply_states += a->mesh_num_reply_states;
	total->mesh_jostled += a->mesh_jostled;
	total->mesh_dropped += a->mesh_dropped;
	total->mesh_replies_sent += a->mesh_replies_sent;
	stats_timeval_add(&total->mesh_replies_sum_wait_sec, &total->mesh_replies_sum_wait_usec, a->mesh_replies_sum_wait_sec, a->mesh_replies_sum_wait_usec);
	/* the medians are averaged together, this is not as accurate as
	 * taking the median over all of the data, but is good and fast
	 * added up here, division later*/
	total->mesh_time_median += a->mesh_time_median;
}

void server_stats_insquery(struct ub_server_stats* stats, struct comm_point* c,
	uint16_t qtype, uint16_t qclass, struct edns_data* edns,
	struct comm_reply* repinfo)
{
	uint16_t flags = sldns_buffer_read_u16_at(c->buffer, 2);
	if(qtype < UB_STATS_QTYPE_NUM)
		stats->qtype[qtype]++;
	else	stats->qtype_big++;
	if(qclass < UB_STATS_QCLASS_NUM)
		stats->qclass[qclass]++;
	else	stats->qclass_big++;
	stats->qopcode[ LDNS_OPCODE_WIRE(sldns_buffer_begin(c->buffer)) ]++;
	if(c->type != comm_udp) {
		stats->qtcp++;
		if(c->ssl != NULL)
			stats->qtls++;
	}
	if(repinfo && addr_is_ip6(&repinfo->addr, repinfo->addrlen))
		stats->qipv6++;
	if( (flags&BIT_QR) )
		stats->qbit_QR++;
	if( (flags&BIT_AA) )
		stats->qbit_AA++;
	if( (flags&BIT_TC) )
		stats->qbit_TC++;
	if( (flags&BIT_RD) )
		stats->qbit_RD++;
	if( (flags&BIT_RA) )
		stats->qbit_RA++;
	if( (flags&BIT_Z) )
		stats->qbit_Z++;
	if( (flags&BIT_AD) )
		stats->qbit_AD++;
	if( (flags&BIT_CD) )
		stats->qbit_CD++;
	if(edns->edns_present) {
		stats->qEDNS++;
		if( (edns->bits & EDNS_DO) )
			stats->qEDNS_DO++;
	}
}

void server_stats_insrcode(struct ub_server_stats* stats, sldns_buffer* buf)
{
	if(stats->extended && sldns_buffer_limit(buf) != 0) {
		int r = (int)LDNS_RCODE_WIRE( sldns_buffer_begin(buf) );
		stats->ans_rcode[r] ++;
		if(r == 0 && LDNS_ANCOUNT( sldns_buffer_begin(buf) ) == 0)
			stats->ans_rcode_nodata ++;
	}
}
