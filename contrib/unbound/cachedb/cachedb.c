/*
 * cachedb/cachedb.c - cache from a database external to the program module
 *
 * Copyright (c) 2016, NLnet Labs. All rights reserved.
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
 * This file contains a module that uses an external database to cache
 * dns responses.
 */

#include "config.h"
#ifdef USE_CACHEDB
#include "cachedb/cachedb.h"
#include "cachedb/redis.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "util/config_file.h"
#include "util/data/msgreply.h"
#include "util/data/msgencode.h"
#include "services/cache/dns.h"
#include "validator/val_neg.h"
#include "validator/val_secalgo.h"
#include "iterator/iter_utils.h"
#include "sldns/parseutil.h"
#include "sldns/wire2str.h"
#include "sldns/sbuffer.h"

/* header file for htobe64 */
#ifdef HAVE_ENDIAN_H
#  include <endian.h>
#endif
#ifdef HAVE_SYS_ENDIAN_H
#  include <sys/endian.h>
#endif
#ifdef HAVE_LIBKERN_OSBYTEORDER_H
/* In practice this is specific to MacOS X.  We assume it doesn't have
* htobe64/be64toh but has alternatives with a different name. */
#  include <libkern/OSByteOrder.h>
#  define htobe64(x) OSSwapHostToBigInt64(x)
#  define be64toh(x) OSSwapBigToHostInt64(x)
#endif

/** the unit test testframe for cachedb, its module state contains
 * a cache for a couple queries (in memory). */
struct testframe_moddata {
	/** lock for mutex */
	lock_basic_type lock;
	/** key for single stored data element, NULL if none */
	char* stored_key;
	/** data for single stored data element, NULL if none */
	uint8_t* stored_data;
	/** length of stored data */
	size_t stored_datalen;
};

static int
testframe_init(struct module_env* env, struct cachedb_env* cachedb_env)
{
	struct testframe_moddata* d;
	(void)env;
	verbose(VERB_ALGO, "testframe_init");
	d = (struct testframe_moddata*)calloc(1,
		sizeof(struct testframe_moddata));
	cachedb_env->backend_data = (void*)d;
	if(!cachedb_env->backend_data) {
		log_err("out of memory");
		return 0;
	}
	lock_basic_init(&d->lock);
	lock_protect(&d->lock, d, sizeof(*d));
	return 1;
}

static void
testframe_deinit(struct module_env* env, struct cachedb_env* cachedb_env)
{
	struct testframe_moddata* d = (struct testframe_moddata*)
		cachedb_env->backend_data;
	(void)env;
	verbose(VERB_ALGO, "testframe_deinit");
	if(!d)
		return;
	lock_basic_destroy(&d->lock);
	free(d->stored_key);
	free(d->stored_data);
	free(d);
}

static int
testframe_lookup(struct module_env* env, struct cachedb_env* cachedb_env,
	char* key, struct sldns_buffer* result_buffer)
{
	struct testframe_moddata* d = (struct testframe_moddata*)
		cachedb_env->backend_data;
	(void)env;
	verbose(VERB_ALGO, "testframe_lookup of %s", key);
	lock_basic_lock(&d->lock);
	if(d->stored_key && strcmp(d->stored_key, key) == 0) {
		if(d->stored_datalen > sldns_buffer_capacity(result_buffer)) {
			lock_basic_unlock(&d->lock);
			return 0; /* too large */
		}
		verbose(VERB_ALGO, "testframe_lookup found %d bytes",
			(int)d->stored_datalen);
		sldns_buffer_clear(result_buffer);
		sldns_buffer_write(result_buffer, d->stored_data,
			d->stored_datalen);
		sldns_buffer_flip(result_buffer);
		lock_basic_unlock(&d->lock);
		return 1;
	}
	lock_basic_unlock(&d->lock);
	return 0;
}

static void
testframe_store(struct module_env* env, struct cachedb_env* cachedb_env,
	char* key, uint8_t* data, size_t data_len)
{
	struct testframe_moddata* d = (struct testframe_moddata*)
		cachedb_env->backend_data;
	(void)env;
	lock_basic_lock(&d->lock);
	verbose(VERB_ALGO, "testframe_store %s (%d bytes)", key, (int)data_len);

	/* free old data element (if any) */
	free(d->stored_key);
	d->stored_key = NULL;
	free(d->stored_data);
	d->stored_data = NULL;
	d->stored_datalen = 0;

	d->stored_data = memdup(data, data_len);
	if(!d->stored_data) {
		lock_basic_unlock(&d->lock);
		log_err("out of memory");
		return;
	}
	d->stored_datalen = data_len;
	d->stored_key = strdup(key);
	if(!d->stored_key) {
		free(d->stored_data);
		d->stored_data = NULL;
		d->stored_datalen = 0;
		lock_basic_unlock(&d->lock);
		return;
	}
	lock_basic_unlock(&d->lock);
	/* (key,data) successfully stored */
}

/** The testframe backend is for unit tests */
static struct cachedb_backend testframe_backend = { "testframe",
	testframe_init, testframe_deinit, testframe_lookup, testframe_store
};

/** find a particular backend from possible backends */
static struct cachedb_backend*
cachedb_find_backend(const char* str)
{
#ifdef USE_REDIS
	if(strcmp(str, redis_backend.name) == 0)
		return &redis_backend;
#endif
	if(strcmp(str, testframe_backend.name) == 0)
		return &testframe_backend;
	/* TODO add more backends here */
	return NULL;
}

/** apply configuration to cachedb module 'global' state */
static int
cachedb_apply_cfg(struct cachedb_env* cachedb_env, struct config_file* cfg)
{
	const char* backend_str = cfg->cachedb_backend;

	/* If unspecified we use the in-memory test DB. */
	if(!backend_str)
		backend_str = "testframe";
	cachedb_env->backend = cachedb_find_backend(backend_str);
	if(!cachedb_env->backend) {
		log_err("cachedb: cannot find backend name '%s'", backend_str);
		return 0;
	}

	/* TODO see if more configuration needs to be applied or not */
	return 1;
}

int 
cachedb_init(struct module_env* env, int id)
{
	struct cachedb_env* cachedb_env = (struct cachedb_env*)calloc(1,
		sizeof(struct cachedb_env));
	if(!cachedb_env) {
		log_err("malloc failure");
		return 0;
	}
	env->modinfo[id] = (void*)cachedb_env;
	if(!cachedb_apply_cfg(cachedb_env, env->cfg)) {
		log_err("cachedb: could not apply configuration settings.");
		return 0;
	}
	/* see if a backend is selected */
	if(!cachedb_env->backend || !cachedb_env->backend->name)
		return 1;
	if(!(*cachedb_env->backend->init)(env, cachedb_env)) {
		log_err("cachedb: could not init %s backend",
			cachedb_env->backend->name);
		return 0;
	}
	cachedb_env->enabled = 1;
	return 1;
}

void 
cachedb_deinit(struct module_env* env, int id)
{
	struct cachedb_env* cachedb_env;
	if(!env || !env->modinfo[id])
		return;
	cachedb_env = (struct cachedb_env*)env->modinfo[id];
	/* free contents */
	/* TODO */
	if(cachedb_env->enabled) {
		(*cachedb_env->backend->deinit)(env, cachedb_env);
	}

	free(cachedb_env);
	env->modinfo[id] = NULL;
}

/** new query for cachedb */
static int
cachedb_new(struct module_qstate* qstate, int id)
{
	struct cachedb_qstate* iq = (struct cachedb_qstate*)regional_alloc(
		qstate->region, sizeof(struct cachedb_qstate));
	qstate->minfo[id] = iq;
	if(!iq) 
		return 0;
	memset(iq, 0, sizeof(*iq));
	/* initialise it */
	/* TODO */

	return 1;
}

/**
 * Return an error
 * @param qstate: our query state
 * @param id: module id
 * @param rcode: error code (DNS errcode).
 * @return: 0 for use by caller, to make notation easy, like:
 * 	return error_response(..). 
 */
static int
error_response(struct module_qstate* qstate, int id, int rcode)
{
	verbose(VERB_QUERY, "return error response %s", 
		sldns_lookup_by_id(sldns_rcodes, rcode)?
		sldns_lookup_by_id(sldns_rcodes, rcode)->name:"??");
	qstate->return_rcode = rcode;
	qstate->return_msg = NULL;
	qstate->ext_state[id] = module_finished;
	return 0;
}

/**
 * Hash the query name, type, class and dbacess-secret into lookup buffer.
 * @param qstate: query state with query info
 * 	and env->cfg with secret.
 * @param buf: returned buffer with hash to lookup
 * @param len: length of the buffer.
 */
static void
calc_hash(struct module_qstate* qstate, char* buf, size_t len)
{
	uint8_t clear[1024];
	size_t clen = 0;
	uint8_t hash[CACHEDB_HASHSIZE/8];
	const char* hex = "0123456789ABCDEF";
	const char* secret = qstate->env->cfg->cachedb_secret ?
		qstate->env->cfg->cachedb_secret : "default";
	size_t i;

	/* copy the hash info into the clear buffer */
	if(clen + qstate->qinfo.qname_len < sizeof(clear)) {
		memmove(clear+clen, qstate->qinfo.qname,
			qstate->qinfo.qname_len);
		clen += qstate->qinfo.qname_len;
	}
	if(clen + 4 < sizeof(clear)) {
		uint16_t t = htons(qstate->qinfo.qtype);
		uint16_t c = htons(qstate->qinfo.qclass);
		memmove(clear+clen, &t, 2);
		memmove(clear+clen+2, &c, 2);
		clen += 4;
	}
	if(secret && secret[0] && clen + strlen(secret) < sizeof(clear)) {
		memmove(clear+clen, secret, strlen(secret));
		clen += strlen(secret);
	}
	
	/* hash the buffer */
	secalgo_hash_sha256(clear, clen, hash);
	memset(clear, 0, clen);

	/* hex encode output for portability (some online dbs need
	 * no nulls, no control characters, and so on) */
	log_assert(len >= sizeof(hash)*2 + 1);
	(void)len;
	for(i=0; i<sizeof(hash); i++) {
		buf[i*2] = hex[(hash[i]&0xf0)>>4];
		buf[i*2+1] = hex[hash[i]&0x0f];
	}
	buf[sizeof(hash)*2] = 0;
}

/** convert data from return_msg into the data buffer */
static int
prep_data(struct module_qstate* qstate, struct sldns_buffer* buf)
{
	uint64_t timestamp, expiry;
	size_t oldlim;
	struct edns_data edns;
	memset(&edns, 0, sizeof(edns));
	edns.edns_present = 1;
	edns.bits = EDNS_DO;
	edns.ext_rcode = 0;
	edns.edns_version = EDNS_ADVERTISED_VERSION;
	edns.udp_size = EDNS_ADVERTISED_SIZE;

	if(!qstate->return_msg || !qstate->return_msg->rep)
		return 0;
	/* We don't store the reply if its TTL is 0 unless serve-expired is
	 * enabled.  Such a reply won't be reusable and simply be a waste for
	 * the backend.  It's also compatible with the default behavior of
	 * dns_cache_store_msg(). */
	if(qstate->return_msg->rep->ttl == 0 &&
		!qstate->env->cfg->serve_expired)
		return 0;
	if(verbosity >= VERB_ALGO)
		log_dns_msg("cachedb encoding", &qstate->return_msg->qinfo,
	                qstate->return_msg->rep);
	if(!reply_info_answer_encode(&qstate->return_msg->qinfo,
		qstate->return_msg->rep, 0, qstate->query_flags,
		buf, 0, 1, qstate->env->scratch, 65535, &edns, 1, 0))
		return 0;

	/* TTLs in the return_msg are relative to time(0) so we have to
	 * store that, we also store the smallest ttl in the packet+time(0)
	 * as the packet expiry time */
	/* qstate->return_msg->rep->ttl contains that relative shortest ttl */
	timestamp = (uint64_t)*qstate->env->now;
	expiry = timestamp + (uint64_t)qstate->return_msg->rep->ttl;
	timestamp = htobe64(timestamp);
	expiry = htobe64(expiry);
	oldlim = sldns_buffer_limit(buf);
	if(oldlim + sizeof(timestamp)+sizeof(expiry) >=
		sldns_buffer_capacity(buf))
		return 0; /* doesn't fit. */
	sldns_buffer_set_limit(buf, oldlim + sizeof(timestamp)+sizeof(expiry));
	sldns_buffer_write_at(buf, oldlim, &timestamp, sizeof(timestamp));
	sldns_buffer_write_at(buf, oldlim+sizeof(timestamp), &expiry,
		sizeof(expiry));

	return 1;
}

/** check expiry, return true if matches OK */
static int
good_expiry_and_qinfo(struct module_qstate* qstate, struct sldns_buffer* buf)
{
	uint64_t expiry;
	/* the expiry time is the last bytes of the buffer */
	if(sldns_buffer_limit(buf) < sizeof(expiry))
		return 0;
	sldns_buffer_read_at(buf, sldns_buffer_limit(buf)-sizeof(expiry),
		&expiry, sizeof(expiry));
	expiry = be64toh(expiry);

	if((time_t)expiry < *qstate->env->now &&
		!qstate->env->cfg->serve_expired)
		return 0;

	return 1;
}

/* Adjust the TTL of the given RRset by 'subtract'.  If 'subtract' is
 * negative, set the TTL to 0. */
static void
packed_rrset_ttl_subtract(struct packed_rrset_data* data, time_t subtract)
{
        size_t i;
        size_t total = data->count + data->rrsig_count;
	if(subtract >= 0 && data->ttl > subtract)
		data->ttl -= subtract;
	else	data->ttl = 0;
        for(i=0; i<total; i++) {
		if(subtract >= 0 && data->rr_ttl[i] > subtract)
                	data->rr_ttl[i] -= subtract;
                else	data->rr_ttl[i] = 0;
	}
}

/* Adjust the TTL of a DNS message and its RRs by 'adjust'.  If 'adjust' is
 * negative, set the TTLs to 0. */
static void
adjust_msg_ttl(struct dns_msg* msg, time_t adjust)
{
	size_t i;
	if(adjust >= 0 && msg->rep->ttl > adjust)
		msg->rep->ttl -= adjust;
	else	msg->rep->ttl = 0;
	msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
	msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;

	for(i=0; i<msg->rep->rrset_count; i++) {
		packed_rrset_ttl_subtract((struct packed_rrset_data*)msg->
			rep->rrsets[i]->entry.data, adjust);
	}
}

/** convert dns message in buffer to return_msg */
static int
parse_data(struct module_qstate* qstate, struct sldns_buffer* buf)
{
	struct msg_parse* prs;
	struct edns_data edns;
	uint64_t timestamp, expiry;
	time_t adjust;
	size_t lim = sldns_buffer_limit(buf);
	if(lim < LDNS_HEADER_SIZE+sizeof(timestamp)+sizeof(expiry))
		return 0; /* too short */

	/* remove timestamp and expiry from end */
	sldns_buffer_read_at(buf, lim-sizeof(expiry), &expiry, sizeof(expiry));
	sldns_buffer_read_at(buf, lim-sizeof(expiry)-sizeof(timestamp),
		&timestamp, sizeof(timestamp));
	expiry = be64toh(expiry);
	timestamp = be64toh(timestamp);

	/* parse DNS packet */
	regional_free_all(qstate->env->scratch);
	prs = (struct msg_parse*)regional_alloc(qstate->env->scratch,
		sizeof(struct msg_parse));
	if(!prs)
		return 0; /* out of memory */
	memset(prs, 0, sizeof(*prs));
	memset(&edns, 0, sizeof(edns));
	sldns_buffer_set_limit(buf, lim - sizeof(expiry)-sizeof(timestamp));
	if(parse_packet(buf, prs, qstate->env->scratch) != LDNS_RCODE_NOERROR) {
		sldns_buffer_set_limit(buf, lim);
		return 0;
	}
	if(parse_extract_edns(prs, &edns, qstate->env->scratch) !=
		LDNS_RCODE_NOERROR) {
		sldns_buffer_set_limit(buf, lim);
		return 0;
	}

	qstate->return_msg = dns_alloc_msg(buf, prs, qstate->region);
	sldns_buffer_set_limit(buf, lim);
	if(!qstate->return_msg)
		return 0;
	
	qstate->return_rcode = LDNS_RCODE_NOERROR;

	/* see how much of the TTL expired, and remove it */
	if(*qstate->env->now <= (time_t)timestamp) {
		verbose(VERB_ALGO, "cachedb msg adjust by zero");
		return 1; /* message from the future (clock skew?) */
	}
	adjust = *qstate->env->now - (time_t)timestamp;
	if(qstate->return_msg->rep->ttl < adjust) {
		verbose(VERB_ALGO, "cachedb msg expired");
		/* If serve-expired is enabled, we still use an expired message
		 * setting the TTL to 0. */
		if(qstate->env->cfg->serve_expired)
			adjust = -1;
		else
			return 0; /* message expired */
	}
	verbose(VERB_ALGO, "cachedb msg adjusted down by %d", (int)adjust);
	adjust_msg_ttl(qstate->return_msg, adjust);

	/* Similar to the unbound worker, if serve-expired is enabled and
	 * the msg would be considered to be expired, mark the state so a
	 * refetch will be scheduled.  The comparison between 'expiry' and
	 * 'now' should be redundant given how these values were calculated,
	 * but we check it just in case as does good_expiry_and_qinfo(). */
	if(qstate->env->cfg->serve_expired &&
		(adjust == -1 || (time_t)expiry < *qstate->env->now)) {
		qstate->need_refetch = 1;
	}

	return 1;
}

/**
 * Lookup the qstate.qinfo in extcache, store in qstate.return_msg.
 * return true if lookup was successful.
 */
static int
cachedb_extcache_lookup(struct module_qstate* qstate, struct cachedb_env* ie)
{
	char key[(CACHEDB_HASHSIZE/8)*2+1];
	calc_hash(qstate, key, sizeof(key));

	/* call backend to fetch data for key into scratch buffer */
	if( !(*ie->backend->lookup)(qstate->env, ie, key,
		qstate->env->scratch_buffer)) {
		return 0;
	}

	/* check expiry date and check if query-data matches */
	if( !good_expiry_and_qinfo(qstate, qstate->env->scratch_buffer) ) {
		return 0;
	}

	/* parse dns message into return_msg */
	if( !parse_data(qstate, qstate->env->scratch_buffer) ) {
		return 0;
	}
	return 1;
}

/**
 * Store the qstate.return_msg in extcache for key qstate.info
 */
static void
cachedb_extcache_store(struct module_qstate* qstate, struct cachedb_env* ie)
{
	char key[(CACHEDB_HASHSIZE/8)*2+1];
	calc_hash(qstate, key, sizeof(key));

	/* prepare data in scratch buffer */
	if(!prep_data(qstate, qstate->env->scratch_buffer))
		return;
	
	/* call backend */
	(*ie->backend->store)(qstate->env, ie, key,
		sldns_buffer_begin(qstate->env->scratch_buffer),
		sldns_buffer_limit(qstate->env->scratch_buffer));
}

/**
 * See if unbound's internal cache can answer the query
 */
static int
cachedb_intcache_lookup(struct module_qstate* qstate)
{
	struct dns_msg* msg;
	msg = dns_cache_lookup(qstate->env, qstate->qinfo.qname,
		qstate->qinfo.qname_len, qstate->qinfo.qtype,
		qstate->qinfo.qclass, qstate->query_flags,
		qstate->region, qstate->env->scratch,
		1 /* no partial messages with only a CNAME */
		);
	if(!msg && qstate->env->neg_cache &&
		iter_qname_indicates_dnssec(qstate->env, &qstate->qinfo)) {
		/* lookup in negative cache; may result in 
		 * NOERROR/NODATA or NXDOMAIN answers that need validation */
		msg = val_neg_getmsg(qstate->env->neg_cache, &qstate->qinfo,
			qstate->region, qstate->env->rrset_cache,
			qstate->env->scratch_buffer,
			*qstate->env->now, 1/*add SOA*/, NULL,
			qstate->env->cfg);
	}
	if(!msg)
		return 0;
	/* this is the returned msg */
	qstate->return_rcode = LDNS_RCODE_NOERROR;
	qstate->return_msg = msg;
	return 1;
}

/**
 * Store query into the internal cache of unbound.
 */
static void
cachedb_intcache_store(struct module_qstate* qstate)
{
	uint32_t store_flags = qstate->query_flags;

	if(qstate->env->cfg->serve_expired)
		store_flags |= DNSCACHE_STORE_ZEROTTL;
	if(!qstate->return_msg)
		return;
	(void)dns_cache_store(qstate->env, &qstate->qinfo,
		qstate->return_msg->rep, 0, qstate->prefetch_leeway, 0,
		qstate->region, store_flags);
}

/**
 * Handle a cachedb module event with a query
 * @param qstate: query state (from the mesh), passed between modules.
 * 	contains qstate->env module environment with global caches and so on.
 * @param iq: query state specific for this module.  per-query.
 * @param ie: environment specific for this module.  global.
 * @param id: module id.
 */
static void
cachedb_handle_query(struct module_qstate* qstate,
	struct cachedb_qstate* ATTR_UNUSED(iq),
	struct cachedb_env* ie, int id)
{
	/* check if we are enabled, and skip if so */
	if(!ie->enabled) {
		/* pass request to next module */
		qstate->ext_state[id] = module_wait_module;
		return;
	}

	if(qstate->blacklist || qstate->no_cache_lookup) {
		/* cache is blacklisted or we are instructed from edns to not look */
		/* pass request to next module */
		qstate->ext_state[id] = module_wait_module;
		return;
	}

	/* lookup inside unbound's internal cache */
	if(cachedb_intcache_lookup(qstate)) {
		if(verbosity >= VERB_ALGO) {
			if(qstate->return_msg->rep)
				log_dns_msg("cachedb internal cache lookup",
					&qstate->return_msg->qinfo,
					qstate->return_msg->rep);
			else log_info("cachedb internal cache lookup: rcode %s",
				sldns_lookup_by_id(sldns_rcodes, qstate->return_rcode)?
				sldns_lookup_by_id(sldns_rcodes, qstate->return_rcode)->name:"??");
		}
		/* we are done with the query */
		qstate->ext_state[id] = module_finished;
		return;
	}

	/* ask backend cache to see if we have data */
	if(cachedb_extcache_lookup(qstate, ie)) {
		if(verbosity >= VERB_ALGO)
			log_dns_msg(ie->backend->name,
				&qstate->return_msg->qinfo,
				qstate->return_msg->rep);
		/* store this result in internal cache */
		cachedb_intcache_store(qstate);
		/* we are done with the query */
		qstate->ext_state[id] = module_finished;
		return;
	}

	/* no cache fetches */
	/* pass request to next module */
	qstate->ext_state[id] = module_wait_module;
}

/**
 * Handle a cachedb module event with a response from the iterator.
 * @param qstate: query state (from the mesh), passed between modules.
 * 	contains qstate->env module environment with global caches and so on.
 * @param iq: query state specific for this module.  per-query.
 * @param ie: environment specific for this module.  global.
 * @param id: module id.
 */
static void
cachedb_handle_response(struct module_qstate* qstate,
	struct cachedb_qstate* ATTR_UNUSED(iq), struct cachedb_env* ie, int id)
{
	/* check if we are not enabled or instructed to not cache, and skip */
	if(!ie->enabled || qstate->no_cache_store) {
		/* we are done with the query */
		qstate->ext_state[id] = module_finished;
		return;
	}

	/* store the item into the backend cache */
	cachedb_extcache_store(qstate, ie);

	/* we are done with the query */
	qstate->ext_state[id] = module_finished;
}

void 
cachedb_operate(struct module_qstate* qstate, enum module_ev event, int id,
	struct outbound_entry* outbound)
{
	struct cachedb_env* ie = (struct cachedb_env*)qstate->env->modinfo[id];
	struct cachedb_qstate* iq = (struct cachedb_qstate*)qstate->minfo[id];
	verbose(VERB_QUERY, "cachedb[module %d] operate: extstate:%s event:%s", 
		id, strextstate(qstate->ext_state[id]), strmodulevent(event));
	if(iq) log_query_info(VERB_QUERY, "cachedb operate: query", 
		&qstate->qinfo);

	/* perform cachedb state machine */
	if((event == module_event_new || event == module_event_pass) && 
		iq == NULL) {
		if(!cachedb_new(qstate, id)) {
			(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
			return;
		}
		iq = (struct cachedb_qstate*)qstate->minfo[id];
	}
	if(iq && (event == module_event_pass || event == module_event_new)) {
		cachedb_handle_query(qstate, iq, ie, id);
		return;
	}
	if(iq && (event == module_event_moddone)) {
		cachedb_handle_response(qstate, iq, ie, id);
		return;
	}
	if(iq && outbound) {
		/* cachedb does not need to process responses at this time
		 * ignore it.
		cachedb_process_response(qstate, iq, ie, id, outbound, event);
		*/
		return;
	}
	if(event == module_event_error) {
		verbose(VERB_ALGO, "got called with event error, giving up");
		(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
		return;
	}
	if(!iq && (event == module_event_moddone)) {
		/* during priming, module done but we never started */
		qstate->ext_state[id] = module_finished;
		return;
	}

	log_err("bad event for cachedb");
	(void)error_response(qstate, id, LDNS_RCODE_SERVFAIL);
}

void
cachedb_inform_super(struct module_qstate* ATTR_UNUSED(qstate),
	int ATTR_UNUSED(id), struct module_qstate* ATTR_UNUSED(super))
{
	/* cachedb does not use subordinate requests at this time */
	verbose(VERB_ALGO, "cachedb inform_super was called");
}

void 
cachedb_clear(struct module_qstate* qstate, int id)
{
	struct cachedb_qstate* iq;
	if(!qstate)
		return;
	iq = (struct cachedb_qstate*)qstate->minfo[id];
	if(iq) {
		/* free contents of iq */
		/* TODO */
	}
	qstate->minfo[id] = NULL;
}

size_t 
cachedb_get_mem(struct module_env* env, int id)
{
	struct cachedb_env* ie = (struct cachedb_env*)env->modinfo[id];
	if(!ie)
		return 0;
	return sizeof(*ie); /* TODO - more mem */
}

/**
 * The cachedb function block 
 */
static struct module_func_block cachedb_block = {
	"cachedb",
	&cachedb_init, &cachedb_deinit, &cachedb_operate,
	&cachedb_inform_super, &cachedb_clear, &cachedb_get_mem
};

struct module_func_block* 
cachedb_get_funcblock(void)
{
	return &cachedb_block;
}
#endif /* USE_CACHEDB */
