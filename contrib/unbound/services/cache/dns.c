/*
 * services/cache/dns.c - Cache services for DNS using msg and rrset caches.
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
 * This file contains the DNS cache.
 */
#include "config.h"
#include "iterator/iter_delegpt.h"
#include "validator/val_nsec.h"
#include "validator/val_utils.h"
#include "services/cache/dns.h"
#include "services/cache/rrset.h"
#include "util/data/msgreply.h"
#include "util/data/packed_rrset.h"
#include "util/data/dname.h"
#include "util/module.h"
#include "util/net_help.h"
#include "util/regional.h"
#include "util/config_file.h"
#include "sldns/sbuffer.h"

/** store rrsets in the rrset cache. 
 * @param env: module environment with caches.
 * @param rep: contains list of rrsets to store.
 * @param now: current time.
 * @param leeway: during prefetch how much leeway to update TTLs.
 * 	This makes rrsets (other than type NS) timeout sooner so they get
 * 	updated with a new full TTL.
 * 	Type NS does not get this, because it must not be refreshed from the
 * 	child domain, but keep counting down properly.
 * @param pside: if from parentside discovered NS, so that its NS is okay
 * 	in a prefetch situation to be updated (without becoming sticky).
 * @param qrep: update rrsets here if cache is better
 * @param region: for qrep allocs.
 */
static void
store_rrsets(struct module_env* env, struct reply_info* rep, time_t now,
	time_t leeway, int pside, struct reply_info* qrep,
	struct regional* region)
{
        size_t i;
        /* see if rrset already exists in cache, if not insert it. */
        for(i=0; i<rep->rrset_count; i++) {
                rep->ref[i].key = rep->rrsets[i];
                rep->ref[i].id = rep->rrsets[i]->id;
		/* update ref if it was in the cache */ 
		switch(rrset_cache_update(env->rrset_cache, &rep->ref[i],
                        env->alloc, now + ((ntohs(rep->ref[i].key->rk.type)==
			LDNS_RR_TYPE_NS && !pside)?0:leeway))) {
		case 0: /* ref unchanged, item inserted */
			break;
		case 2: /* ref updated, cache is superior */
			if(region) {
				struct ub_packed_rrset_key* ck;
				lock_rw_rdlock(&rep->ref[i].key->entry.lock);
				/* if deleted rrset, do not copy it */
				if(rep->ref[i].key->id == 0)
					ck = NULL;
				else 	ck = packed_rrset_copy_region(
					rep->ref[i].key, region, now);
				lock_rw_unlock(&rep->ref[i].key->entry.lock);
				if(ck) {
					/* use cached copy if memory allows */
					qrep->rrsets[i] = ck;
				}
			}
			/* no break: also copy key item */
			/* the line below is matched by gcc regex and silences
			 * the fallthrough warning */
			/* fallthrough */
		case 1: /* ref updated, item inserted */
                        rep->rrsets[i] = rep->ref[i].key;
		}
        }
}

/** delete message from message cache */
void
msg_cache_remove(struct module_env* env, uint8_t* qname, size_t qnamelen, 
	uint16_t qtype, uint16_t qclass, uint16_t flags)
{
	struct query_info k;
	hashvalue_type h;

	k.qname = qname;
	k.qname_len = qnamelen;
	k.qtype = qtype;
	k.qclass = qclass;
	k.local_alias = NULL;
	h = query_info_hash(&k, flags);
	slabhash_remove(env->msg_cache, h, &k);
}

/** remove servfail msg cache entry */
static void
msg_del_servfail(struct module_env* env, struct query_info* qinfo,
	uint32_t flags)
{
	struct msgreply_entry* e;
	/* see if the entry is servfail, and then remove it, so that
	 * lookups move from the cacheresponse stage to the recursionresponse
	 * stage */
	e = msg_cache_lookup(env, qinfo->qname, qinfo->qname_len,
		qinfo->qtype, qinfo->qclass, flags, 0, 0);
	if(!e) return;
	/* we don't check for the ttl here, also expired servfail entries
	 * are removed.  If the user uses serve-expired, they would still be
	 * used to answer from cache */
	if(FLAGS_GET_RCODE(((struct reply_info*)e->entry.data)->flags)
		!= LDNS_RCODE_SERVFAIL) {
		lock_rw_unlock(&e->entry.lock);
		return;
	}
	lock_rw_unlock(&e->entry.lock);
	msg_cache_remove(env, qinfo->qname, qinfo->qname_len, qinfo->qtype,
		qinfo->qclass, flags);
}

void 
dns_cache_store_msg(struct module_env* env, struct query_info* qinfo,
	hashvalue_type hash, struct reply_info* rep, time_t leeway, int pside,
	struct reply_info* qrep, uint32_t flags, struct regional* region)
{
	struct msgreply_entry* e;
	time_t ttl = rep->ttl;
	size_t i;

	/* store RRsets */
        for(i=0; i<rep->rrset_count; i++) {
		rep->ref[i].key = rep->rrsets[i];
		rep->ref[i].id = rep->rrsets[i]->id;
	}

	/* there was a reply_info_sortref(rep) here but it seems to be
	 * unnecessary, because the cache gets locked per rrset. */
	reply_info_set_ttls(rep, *env->now);
	store_rrsets(env, rep, *env->now, leeway, pside, qrep, region);
	if(ttl == 0 && !(flags & DNSCACHE_STORE_ZEROTTL)) {
		/* we do not store the message, but we did store the RRs,
		 * which could be useful for delegation information */
		verbose(VERB_ALGO, "TTL 0: dropped msg from cache");
		free(rep);
		/* if the message is SERVFAIL in cache, remove that SERVFAIL,
		 * so that the TTL 0 response can be returned for future
		 * responses (i.e. don't get answered by the servfail from
		 * cache, but instead go to recursion to get this TTL0
		 * response). */
		msg_del_servfail(env, qinfo, flags);
		return;
	}

	/* store msg in the cache */
	reply_info_sortref(rep);
	if(!(e = query_info_entrysetup(qinfo, rep, hash))) {
		log_err("store_msg: malloc failed");
		return;
	}
	slabhash_insert(env->msg_cache, hash, &e->entry, rep, env->alloc);
}

/** find closest NS or DNAME and returns the rrset (locked) */
static struct ub_packed_rrset_key*
find_closest_of_type(struct module_env* env, uint8_t* qname, size_t qnamelen, 
	uint16_t qclass, time_t now, uint16_t searchtype, int stripfront)
{
	struct ub_packed_rrset_key *rrset;
	uint8_t lablen;

	if(stripfront) {
		/* strip off so that DNAMEs have strict subdomain match */
		lablen = *qname;
		qname += lablen + 1;
		qnamelen -= lablen + 1;
	}

	/* snip off front part of qname until the type is found */
	while(qnamelen > 0) {
		if((rrset = rrset_cache_lookup(env->rrset_cache, qname, 
			qnamelen, searchtype, qclass, 0, now, 0)))
			return rrset;

		/* snip off front label */
		lablen = *qname;
		qname += lablen + 1;
		qnamelen -= lablen + 1;
	}
	return NULL;
}

/** add addr to additional section */
static void
addr_to_additional(struct ub_packed_rrset_key* rrset, struct regional* region,
	struct dns_msg* msg, time_t now)
{
	if((msg->rep->rrsets[msg->rep->rrset_count] = 
		packed_rrset_copy_region(rrset, region, now))) {
		msg->rep->ar_numrrsets++;
		msg->rep->rrset_count++;
	}
}

/** lookup message in message cache */
struct msgreply_entry* 
msg_cache_lookup(struct module_env* env, uint8_t* qname, size_t qnamelen, 
	uint16_t qtype, uint16_t qclass, uint16_t flags, time_t now, int wr)
{
	struct lruhash_entry* e;
	struct query_info k;
	hashvalue_type h;

	k.qname = qname;
	k.qname_len = qnamelen;
	k.qtype = qtype;
	k.qclass = qclass;
	k.local_alias = NULL;
	h = query_info_hash(&k, flags);
	e = slabhash_lookup(env->msg_cache, h, &k, wr);

	if(!e) return NULL;
	if( now > ((struct reply_info*)e->data)->ttl ) {
		lock_rw_unlock(&e->lock);
		return NULL;
	}
	return (struct msgreply_entry*)e->key;
}

/** find and add A and AAAA records for nameservers in delegpt */
static int
find_add_addrs(struct module_env* env, uint16_t qclass, 
	struct regional* region, struct delegpt* dp, time_t now, 
	struct dns_msg** msg)
{
	struct delegpt_ns* ns;
	struct msgreply_entry* neg;
	struct ub_packed_rrset_key* akey;
	for(ns = dp->nslist; ns; ns = ns->next) {
		akey = rrset_cache_lookup(env->rrset_cache, ns->name, 
			ns->namelen, LDNS_RR_TYPE_A, qclass, 0, now, 0);
		if(akey) {
			if(!delegpt_add_rrset_A(dp, region, akey, 0)) {
				lock_rw_unlock(&akey->entry.lock);
				return 0;
			}
			if(msg)
				addr_to_additional(akey, region, *msg, now);
			lock_rw_unlock(&akey->entry.lock);
		} else {
			/* BIT_CD on false because delegpt lookup does
			 * not use dns64 translation */
			neg = msg_cache_lookup(env, ns->name, ns->namelen,
				LDNS_RR_TYPE_A, qclass, 0, now, 0);
			if(neg) {
				delegpt_add_neg_msg(dp, neg);
				lock_rw_unlock(&neg->entry.lock);
			}
		}
		akey = rrset_cache_lookup(env->rrset_cache, ns->name, 
			ns->namelen, LDNS_RR_TYPE_AAAA, qclass, 0, now, 0);
		if(akey) {
			if(!delegpt_add_rrset_AAAA(dp, region, akey, 0)) {
				lock_rw_unlock(&akey->entry.lock);
				return 0;
			}
			if(msg)
				addr_to_additional(akey, region, *msg, now);
			lock_rw_unlock(&akey->entry.lock);
		} else {
			/* BIT_CD on false because delegpt lookup does
			 * not use dns64 translation */
			neg = msg_cache_lookup(env, ns->name, ns->namelen,
				LDNS_RR_TYPE_AAAA, qclass, 0, now, 0);
			if(neg) {
				delegpt_add_neg_msg(dp, neg);
				lock_rw_unlock(&neg->entry.lock);
			}
		}
	}
	return 1;
}

/** find and add A and AAAA records for missing nameservers in delegpt */
int
cache_fill_missing(struct module_env* env, uint16_t qclass, 
	struct regional* region, struct delegpt* dp)
{
	struct delegpt_ns* ns;
	struct msgreply_entry* neg;
	struct ub_packed_rrset_key* akey;
	time_t now = *env->now;
	for(ns = dp->nslist; ns; ns = ns->next) {
		akey = rrset_cache_lookup(env->rrset_cache, ns->name, 
			ns->namelen, LDNS_RR_TYPE_A, qclass, 0, now, 0);
		if(akey) {
			if(!delegpt_add_rrset_A(dp, region, akey, ns->lame)) {
				lock_rw_unlock(&akey->entry.lock);
				return 0;
			}
			log_nametypeclass(VERB_ALGO, "found in cache",
				ns->name, LDNS_RR_TYPE_A, qclass);
			lock_rw_unlock(&akey->entry.lock);
		} else {
			/* BIT_CD on false because delegpt lookup does
			 * not use dns64 translation */
			neg = msg_cache_lookup(env, ns->name, ns->namelen,
				LDNS_RR_TYPE_A, qclass, 0, now, 0);
			if(neg) {
				delegpt_add_neg_msg(dp, neg);
				lock_rw_unlock(&neg->entry.lock);
			}
		}
		akey = rrset_cache_lookup(env->rrset_cache, ns->name, 
			ns->namelen, LDNS_RR_TYPE_AAAA, qclass, 0, now, 0);
		if(akey) {
			if(!delegpt_add_rrset_AAAA(dp, region, akey, ns->lame)) {
				lock_rw_unlock(&akey->entry.lock);
				return 0;
			}
			log_nametypeclass(VERB_ALGO, "found in cache",
				ns->name, LDNS_RR_TYPE_AAAA, qclass);
			lock_rw_unlock(&akey->entry.lock);
		} else {
			/* BIT_CD on false because delegpt lookup does
			 * not use dns64 translation */
			neg = msg_cache_lookup(env, ns->name, ns->namelen,
				LDNS_RR_TYPE_AAAA, qclass, 0, now, 0);
			if(neg) {
				delegpt_add_neg_msg(dp, neg);
				lock_rw_unlock(&neg->entry.lock);
			}
		}
	}
	return 1;
}

/** find and add DS or NSEC to delegation msg */
static void
find_add_ds(struct module_env* env, struct regional* region, 
	struct dns_msg* msg, struct delegpt* dp, time_t now)
{
	/* Lookup the DS or NSEC at the delegation point. */
	struct ub_packed_rrset_key* rrset = rrset_cache_lookup(
		env->rrset_cache, dp->name, dp->namelen, LDNS_RR_TYPE_DS, 
		msg->qinfo.qclass, 0, now, 0);
	if(!rrset) {
		/* NOTE: this won't work for alternate NSEC schemes 
		 *	(opt-in, NSEC3) */
		rrset = rrset_cache_lookup(env->rrset_cache, dp->name, 
			dp->namelen, LDNS_RR_TYPE_NSEC, msg->qinfo.qclass, 
			0, now, 0);
		/* Note: the PACKED_RRSET_NSEC_AT_APEX flag is not used.
		 * since this is a referral, we need the NSEC at the parent
		 * side of the zone cut, not the NSEC at apex side. */
		if(rrset && nsec_has_type(rrset, LDNS_RR_TYPE_DS)) {
			lock_rw_unlock(&rrset->entry.lock);
			rrset = NULL; /* discard wrong NSEC */
		}
	}
	if(rrset) {
		/* add it to auth section. This is the second rrset. */
		if((msg->rep->rrsets[msg->rep->rrset_count] = 
			packed_rrset_copy_region(rrset, region, now))) {
			msg->rep->ns_numrrsets++;
			msg->rep->rrset_count++;
		}
		lock_rw_unlock(&rrset->entry.lock);
	}
}

struct dns_msg*
dns_msg_create(uint8_t* qname, size_t qnamelen, uint16_t qtype, 
	uint16_t qclass, struct regional* region, size_t capacity)
{
	struct dns_msg* msg = (struct dns_msg*)regional_alloc(region,
		sizeof(struct dns_msg));
	if(!msg)
		return NULL;
	msg->qinfo.qname = regional_alloc_init(region, qname, qnamelen);
	if(!msg->qinfo.qname)
		return NULL;
	msg->qinfo.qname_len = qnamelen;
	msg->qinfo.qtype = qtype;
	msg->qinfo.qclass = qclass;
	msg->qinfo.local_alias = NULL;
	/* non-packed reply_info, because it needs to grow the array */
	msg->rep = (struct reply_info*)regional_alloc_zero(region, 
		sizeof(struct reply_info)-sizeof(struct rrset_ref));
	if(!msg->rep)
		return NULL;
	if(capacity > RR_COUNT_MAX)
		return NULL; /* integer overflow protection */
	msg->rep->flags = BIT_QR; /* with QR, no AA */
	msg->rep->qdcount = 1;
	msg->rep->rrsets = (struct ub_packed_rrset_key**)
		regional_alloc(region, 
		capacity*sizeof(struct ub_packed_rrset_key*));
	if(!msg->rep->rrsets)
		return NULL;
	return msg;
}

int
dns_msg_authadd(struct dns_msg* msg, struct regional* region, 
	struct ub_packed_rrset_key* rrset, time_t now)
{
	if(!(msg->rep->rrsets[msg->rep->rrset_count++] = 
		packed_rrset_copy_region(rrset, region, now)))
		return 0;
	msg->rep->ns_numrrsets++;
	return 1;
}

int
dns_msg_ansadd(struct dns_msg* msg, struct regional* region, 
	struct ub_packed_rrset_key* rrset, time_t now)
{
	if(!(msg->rep->rrsets[msg->rep->rrset_count++] = 
		packed_rrset_copy_region(rrset, region, now)))
		return 0;
	msg->rep->an_numrrsets++;
	return 1;
}

struct delegpt* 
dns_cache_find_delegation(struct module_env* env, uint8_t* qname, 
	size_t qnamelen, uint16_t qtype, uint16_t qclass, 
	struct regional* region, struct dns_msg** msg, time_t now)
{
	/* try to find closest NS rrset */
	struct ub_packed_rrset_key* nskey;
	struct packed_rrset_data* nsdata;
	struct delegpt* dp;

	nskey = find_closest_of_type(env, qname, qnamelen, qclass, now,
		LDNS_RR_TYPE_NS, 0);
	if(!nskey) /* hope the caller has hints to prime or something */
		return NULL;
	nsdata = (struct packed_rrset_data*)nskey->entry.data;
	/* got the NS key, create delegation point */
	dp = delegpt_create(region);
	if(!dp || !delegpt_set_name(dp, region, nskey->rk.dname)) {
		lock_rw_unlock(&nskey->entry.lock);
		log_err("find_delegation: out of memory");
		return NULL;
	}
	/* create referral message */
	if(msg) {
		/* allocate the array to as much as we could need:
		 *	NS rrset + DS/NSEC rrset +
		 *	A rrset for every NS RR
		 *	AAAA rrset for every NS RR
		 */
		*msg = dns_msg_create(qname, qnamelen, qtype, qclass, region, 
			2 + nsdata->count*2);
		if(!*msg || !dns_msg_authadd(*msg, region, nskey, now)) {
			lock_rw_unlock(&nskey->entry.lock);
			log_err("find_delegation: out of memory");
			return NULL;
		}
	}
	if(!delegpt_rrset_add_ns(dp, region, nskey, 0))
		log_err("find_delegation: addns out of memory");
	lock_rw_unlock(&nskey->entry.lock); /* first unlock before next lookup*/
	/* find and add DS/NSEC (if any) */
	if(msg)
		find_add_ds(env, region, *msg, dp, now);
	/* find and add A entries */
	if(!find_add_addrs(env, qclass, region, dp, now, msg))
		log_err("find_delegation: addrs out of memory");
	return dp;
}

/** allocate dns_msg from query_info and reply_info */
static struct dns_msg*
gen_dns_msg(struct regional* region, struct query_info* q, size_t num)
{
	struct dns_msg* msg = (struct dns_msg*)regional_alloc(region, 
		sizeof(struct dns_msg));
	if(!msg)
		return NULL;
	memcpy(&msg->qinfo, q, sizeof(struct query_info));
	msg->qinfo.qname = regional_alloc_init(region, q->qname, q->qname_len);
	if(!msg->qinfo.qname)
		return NULL;
	/* allocate replyinfo struct and rrset key array separately */
	msg->rep = (struct reply_info*)regional_alloc(region,
		sizeof(struct reply_info) - sizeof(struct rrset_ref));
	if(!msg->rep)
		return NULL;
	if(num > RR_COUNT_MAX)
		return NULL; /* integer overflow protection */
	msg->rep->rrsets = (struct ub_packed_rrset_key**)
		regional_alloc(region,
		num * sizeof(struct ub_packed_rrset_key*));
	if(!msg->rep->rrsets)
		return NULL;
	return msg;
}

struct dns_msg*
tomsg(struct module_env* env, struct query_info* q, struct reply_info* r, 
	struct regional* region, time_t now, struct regional* scratch)
{
	struct dns_msg* msg;
	size_t i;
	if(now > r->ttl)
		return NULL;
	msg = gen_dns_msg(region, q, r->rrset_count);
	if(!msg)
		return NULL;
	msg->rep->flags = r->flags;
	msg->rep->qdcount = r->qdcount;
	msg->rep->ttl = r->ttl - now;
	if(r->prefetch_ttl > now)
		msg->rep->prefetch_ttl = r->prefetch_ttl - now;
	else	msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
	msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
	msg->rep->security = r->security;
	msg->rep->an_numrrsets = r->an_numrrsets;
	msg->rep->ns_numrrsets = r->ns_numrrsets;
	msg->rep->ar_numrrsets = r->ar_numrrsets;
	msg->rep->rrset_count = r->rrset_count;
        msg->rep->authoritative = r->authoritative;
	if(!rrset_array_lock(r->ref, r->rrset_count, now))
		return NULL;
	if(r->an_numrrsets > 0 && (r->rrsets[0]->rk.type == htons(
		LDNS_RR_TYPE_CNAME) || r->rrsets[0]->rk.type == htons(
		LDNS_RR_TYPE_DNAME)) && !reply_check_cname_chain(q, r)) {
		/* cname chain is now invalid, reconstruct msg */
		rrset_array_unlock(r->ref, r->rrset_count);
		return NULL;
	}
	if(r->security == sec_status_secure && !reply_all_rrsets_secure(r)) {
		/* message rrsets have changed status, revalidate */
		rrset_array_unlock(r->ref, r->rrset_count);
		return NULL;
	}
	for(i=0; i<msg->rep->rrset_count; i++) {
		msg->rep->rrsets[i] = packed_rrset_copy_region(r->rrsets[i], 
			region, now);
		if(!msg->rep->rrsets[i]) {
			rrset_array_unlock(r->ref, r->rrset_count);
			return NULL;
		}
	}
	if(env)
		rrset_array_unlock_touch(env->rrset_cache, scratch, r->ref, 
		r->rrset_count);
	else
		rrset_array_unlock(r->ref, r->rrset_count);
	return msg;
}

/** synthesize RRset-only response from cached RRset item */
static struct dns_msg*
rrset_msg(struct ub_packed_rrset_key* rrset, struct regional* region, 
	time_t now, struct query_info* q)
{
	struct dns_msg* msg;
	struct packed_rrset_data* d = (struct packed_rrset_data*)
		rrset->entry.data;
	if(now > d->ttl)
		return NULL;
	msg = gen_dns_msg(region, q, 1); /* only the CNAME (or other) RRset */
	if(!msg)
		return NULL;
	msg->rep->flags = BIT_QR; /* reply, no AA, no error */
        msg->rep->authoritative = 0; /* reply stored in cache can't be authoritative */
	msg->rep->qdcount = 1;
	msg->rep->ttl = d->ttl - now;
	msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
	msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
	msg->rep->security = sec_status_unchecked;
	msg->rep->an_numrrsets = 1;
	msg->rep->ns_numrrsets = 0;
	msg->rep->ar_numrrsets = 0;
	msg->rep->rrset_count = 1;
	msg->rep->rrsets[0] = packed_rrset_copy_region(rrset, region, now);
	if(!msg->rep->rrsets[0]) /* copy CNAME */
		return NULL;
	return msg;
}

/** synthesize DNAME+CNAME response from cached DNAME item */
static struct dns_msg*
synth_dname_msg(struct ub_packed_rrset_key* rrset, struct regional* region, 
	time_t now, struct query_info* q, enum sec_status* sec_status)
{
	struct dns_msg* msg;
	struct ub_packed_rrset_key* ck;
	struct packed_rrset_data* newd, *d = (struct packed_rrset_data*)
		rrset->entry.data;
	uint8_t* newname, *dtarg = NULL;
	size_t newlen, dtarglen;
	if(now > d->ttl)
		return NULL;
	/* only allow validated (with DNSSEC) DNAMEs used from cache 
	 * for insecure DNAMEs, query again. */
	*sec_status = d->security;
	/* return sec status, so the status of the CNAME can be checked
	 * by the calling routine. */
	msg = gen_dns_msg(region, q, 2); /* DNAME + CNAME RRset */
	if(!msg)
		return NULL;
	msg->rep->flags = BIT_QR; /* reply, no AA, no error */
        msg->rep->authoritative = 0; /* reply stored in cache can't be authoritative */
	msg->rep->qdcount = 1;
	msg->rep->ttl = d->ttl - now;
	msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
	msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
	msg->rep->security = sec_status_unchecked;
	msg->rep->an_numrrsets = 1;
	msg->rep->ns_numrrsets = 0;
	msg->rep->ar_numrrsets = 0;
	msg->rep->rrset_count = 1;
	msg->rep->rrsets[0] = packed_rrset_copy_region(rrset, region, now);
	if(!msg->rep->rrsets[0]) /* copy DNAME */
		return NULL;
	/* synth CNAME rrset */
	get_cname_target(rrset, &dtarg, &dtarglen);
	if(!dtarg)
		return NULL;
	newlen = q->qname_len + dtarglen - rrset->rk.dname_len;
	if(newlen > LDNS_MAX_DOMAINLEN) {
		msg->rep->flags |= LDNS_RCODE_YXDOMAIN;
		return msg;
	}
	newname = (uint8_t*)regional_alloc(region, newlen);
	if(!newname)
		return NULL;
	/* new name is concatenation of qname front (without DNAME owner)
	 * and DNAME target name */
	memcpy(newname, q->qname, q->qname_len-rrset->rk.dname_len);
	memmove(newname+(q->qname_len-rrset->rk.dname_len), dtarg, dtarglen);
	/* create rest of CNAME rrset */
	ck = (struct ub_packed_rrset_key*)regional_alloc(region, 
		sizeof(struct ub_packed_rrset_key));
	if(!ck)
		return NULL;
	memset(&ck->entry, 0, sizeof(ck->entry));
	msg->rep->rrsets[1] = ck;
	ck->entry.key = ck;
	ck->rk.type = htons(LDNS_RR_TYPE_CNAME);
	ck->rk.rrset_class = rrset->rk.rrset_class;
	ck->rk.flags = 0;
	ck->rk.dname = regional_alloc_init(region, q->qname, q->qname_len);
	if(!ck->rk.dname)
		return NULL;
	ck->rk.dname_len = q->qname_len;
	ck->entry.hash = rrset_key_hash(&ck->rk);
	newd = (struct packed_rrset_data*)regional_alloc_zero(region,
		sizeof(struct packed_rrset_data) + sizeof(size_t) + 
		sizeof(uint8_t*) + sizeof(time_t) + sizeof(uint16_t) 
		+ newlen);
	if(!newd)
		return NULL;
	ck->entry.data = newd;
	newd->ttl = 0; /* 0 for synthesized CNAME TTL */
	newd->count = 1;
	newd->rrsig_count = 0;
	newd->trust = rrset_trust_ans_noAA;
	newd->rr_len = (size_t*)((uint8_t*)newd + 
		sizeof(struct packed_rrset_data));
	newd->rr_len[0] = newlen + sizeof(uint16_t);
	packed_rrset_ptr_fixup(newd);
	newd->rr_ttl[0] = newd->ttl;
	msg->rep->ttl = newd->ttl;
	msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(newd->ttl);
	msg->rep->serve_expired_ttl = newd->ttl + SERVE_EXPIRED_TTL;
	sldns_write_uint16(newd->rr_data[0], newlen);
	memmove(newd->rr_data[0] + sizeof(uint16_t), newname, newlen);
	msg->rep->an_numrrsets ++;
	msg->rep->rrset_count ++;
	return msg;
}

/** Fill TYPE_ANY response with some data from cache */
static struct dns_msg*
fill_any(struct module_env* env,
	uint8_t* qname, size_t qnamelen, uint16_t qtype, uint16_t qclass,
	struct regional* region)
{
	time_t now = *env->now;
	struct dns_msg* msg = NULL;
	uint16_t lookup[] = {LDNS_RR_TYPE_A, LDNS_RR_TYPE_AAAA,
		LDNS_RR_TYPE_MX, LDNS_RR_TYPE_SOA, LDNS_RR_TYPE_NS,
		LDNS_RR_TYPE_DNAME, 0};
	int i, num=6; /* number of RR types to look up */
	log_assert(lookup[num] == 0);

	for(i=0; i<num; i++) {
		/* look up this RR for inclusion in type ANY response */
		struct ub_packed_rrset_key* rrset = rrset_cache_lookup(
			env->rrset_cache, qname, qnamelen, lookup[i],
			qclass, 0, now, 0);
		struct packed_rrset_data *d;
		if(!rrset)
			continue;

		/* only if rrset from answer section */
		d = (struct packed_rrset_data*)rrset->entry.data;
		if(d->trust == rrset_trust_add_noAA ||
			d->trust == rrset_trust_auth_noAA ||
			d->trust == rrset_trust_add_AA ||
			d->trust == rrset_trust_auth_AA) {
			lock_rw_unlock(&rrset->entry.lock);
			continue;
		}

		/* create msg if none */
		if(!msg) {
			msg = dns_msg_create(qname, qnamelen, qtype, qclass,
				region, (size_t)(num-i));
			if(!msg) {
				lock_rw_unlock(&rrset->entry.lock);
				return NULL;
			}
		}

		/* add RRset to response */
		if(!dns_msg_ansadd(msg, region, rrset, now)) {
			lock_rw_unlock(&rrset->entry.lock);
			return NULL;
		}
		lock_rw_unlock(&rrset->entry.lock);
	}
	return msg;
}

struct dns_msg* 
dns_cache_lookup(struct module_env* env,
	uint8_t* qname, size_t qnamelen, uint16_t qtype, uint16_t qclass,
	uint16_t flags, struct regional* region, struct regional* scratch,
	int no_partial)
{
	struct lruhash_entry* e;
	struct query_info k;
	hashvalue_type h;
	time_t now = *env->now;
	struct ub_packed_rrset_key* rrset;

	/* lookup first, this has both NXdomains and ANSWER responses */
	k.qname = qname;
	k.qname_len = qnamelen;
	k.qtype = qtype;
	k.qclass = qclass;
	k.local_alias = NULL;
	h = query_info_hash(&k, flags);
	e = slabhash_lookup(env->msg_cache, h, &k, 0);
	if(e) {
		struct msgreply_entry* key = (struct msgreply_entry*)e->key;
		struct reply_info* data = (struct reply_info*)e->data;
		struct dns_msg* msg = tomsg(env, &key->key, data, region, now, 
			scratch);
		if(msg) {
			lock_rw_unlock(&e->lock);
			return msg;
		}
		/* could be msg==NULL; due to TTL or not all rrsets available */
		lock_rw_unlock(&e->lock);
	}

	/* see if a DNAME exists. Checked for first, to enforce that DNAMEs
	 * are more important, the CNAME is resynthesized and thus 
	 * consistent with the DNAME */
	if(!no_partial &&
		(rrset=find_closest_of_type(env, qname, qnamelen, qclass, now,
		LDNS_RR_TYPE_DNAME, 1))) {
		/* synthesize a DNAME+CNAME message based on this */
		enum sec_status sec_status = sec_status_unchecked;
		struct dns_msg* msg = synth_dname_msg(rrset, region, now, &k,
			&sec_status);
		if(msg) {
			struct ub_packed_rrset_key* cname_rrset;
			lock_rw_unlock(&rrset->entry.lock);
			/* now, after unlocking the DNAME rrset lock,
			 * check the sec_status, and see if we need to look
			 * up the CNAME record associated before it can
			 * be used */
			/* normally, only secure DNAMEs allowed from cache*/
			if(sec_status == sec_status_secure)
				return msg;
			/* but if we have a CNAME cached with this name, then we
			 * have previously already allowed this name to pass.
			 * the next cache lookup is going to fetch that CNAME itself,
			 * but it is better to have the (unsigned)DNAME + CNAME in
			 * that case */
			cname_rrset = rrset_cache_lookup(
				env->rrset_cache, qname, qnamelen,
				LDNS_RR_TYPE_CNAME, qclass, 0, now, 0);
			if(cname_rrset) {
				/* CNAME already synthesized by
				 * synth_dname_msg routine, so we can
				 * straight up return the msg */
				lock_rw_unlock(&cname_rrset->entry.lock);
				return msg;
			}
		} else {
			lock_rw_unlock(&rrset->entry.lock);
		}
	}

	/* see if we have CNAME for this domain,
	 * but not for DS records (which are part of the parent) */
	if(!no_partial && qtype != LDNS_RR_TYPE_DS &&
	   (rrset=rrset_cache_lookup(env->rrset_cache, qname, qnamelen, 
		LDNS_RR_TYPE_CNAME, qclass, 0, now, 0))) {
		uint8_t* wc = NULL;
		size_t wl;
		/* if the rrset is not a wildcard expansion, with wcname */
		/* because, if we return that CNAME rrset on its own, it is
		 * missing the NSEC or NSEC3 proof */
		if(!(val_rrset_wildcard(rrset, &wc, &wl) && wc != NULL)) {
			struct dns_msg* msg = rrset_msg(rrset, region, now, &k);
			if(msg) {
				lock_rw_unlock(&rrset->entry.lock);
				return msg;
			}
		}
		lock_rw_unlock(&rrset->entry.lock);
	}

	/* construct DS, DNSKEY, DLV messages from rrset cache. */
	if((qtype == LDNS_RR_TYPE_DS || qtype == LDNS_RR_TYPE_DNSKEY ||
		qtype == LDNS_RR_TYPE_DLV) &&
		(rrset=rrset_cache_lookup(env->rrset_cache, qname, qnamelen, 
		qtype, qclass, 0, now, 0))) {
		/* if the rrset is from the additional section, and the
		 * signatures have fallen off, then do not synthesize a msg
		 * instead, allow a full query for signed results to happen.
		 * Forego all rrset data from additional section, because
		 * some signatures may not be present and cause validation
		 * failure.
		 */
		struct packed_rrset_data *d = (struct packed_rrset_data*)
			rrset->entry.data;
		if(d->trust != rrset_trust_add_noAA && 
			d->trust != rrset_trust_add_AA && 
			(qtype == LDNS_RR_TYPE_DS || 
				(d->trust != rrset_trust_auth_noAA 
				&& d->trust != rrset_trust_auth_AA) )) {
			struct dns_msg* msg = rrset_msg(rrset, region, now, &k);
			if(msg) {
				lock_rw_unlock(&rrset->entry.lock);
				return msg;
			}
		}
		lock_rw_unlock(&rrset->entry.lock);
	}

	/* stop downwards cache search on NXDOMAIN.
	 * Empty nonterminals are NOERROR, so an NXDOMAIN for foo
	 * means bla.foo also does not exist.  The DNSSEC proofs are
	 * the same.  We search upwards for NXDOMAINs. */
	if(env->cfg->harden_below_nxdomain)
	    while(!dname_is_root(k.qname)) {
		dname_remove_label(&k.qname, &k.qname_len);
		h = query_info_hash(&k, flags);
		e = slabhash_lookup(env->msg_cache, h, &k, 0);
		if(!e && k.qtype != LDNS_RR_TYPE_A &&
			env->cfg->qname_minimisation) {
			k.qtype = LDNS_RR_TYPE_A;
			h = query_info_hash(&k, flags);
			e = slabhash_lookup(env->msg_cache, h, &k, 0);
		}
		if(e) {
			struct reply_info* data = (struct reply_info*)e->data;
			struct dns_msg* msg;
			if(FLAGS_GET_RCODE(data->flags) == LDNS_RCODE_NXDOMAIN
			  && data->security == sec_status_secure
			  && (msg=tomsg(env, &k, data, region, now, scratch))){
				lock_rw_unlock(&e->lock);
				msg->qinfo.qname=qname;
				msg->qinfo.qname_len=qnamelen;
				/* check that DNSSEC really works out */
				msg->rep->security = sec_status_unchecked;
				return msg;
			}
			lock_rw_unlock(&e->lock);
		}
		k.qtype = qtype;
	    }

	/* fill common RR types for ANY response to avoid requery */
	if(qtype == LDNS_RR_TYPE_ANY) {
		return fill_any(env, qname, qnamelen, qtype, qclass, region);
	}

	return NULL;
}

int
dns_cache_store(struct module_env* env, struct query_info* msgqinf,
        struct reply_info* msgrep, int is_referral, time_t leeway, int pside,
	struct regional* region, uint32_t flags)
{
	struct reply_info* rep = NULL;
	/* alloc, malloc properly (not in region, like msg is) */
	rep = reply_info_copy(msgrep, env->alloc, NULL);
	if(!rep)
		return 0;
	/* ttl must be relative ;i.e. 0..86400 not  time(0)+86400.
	 * the env->now is added to message and RRsets in this routine. */
	/* the leeway is used to invalidate other rrsets earlier */

	if(is_referral) {
		/* store rrsets */
		struct rrset_ref ref;
		size_t i;
		for(i=0; i<rep->rrset_count; i++) {
			packed_rrset_ttl_add((struct packed_rrset_data*)
				rep->rrsets[i]->entry.data, *env->now);
			ref.key = rep->rrsets[i];
			ref.id = rep->rrsets[i]->id;
			/*ignore ret: it was in the cache, ref updated */
			/* no leeway for typeNS */
			(void)rrset_cache_update(env->rrset_cache, &ref, 
				env->alloc, *env->now + 
				((ntohs(ref.key->rk.type)==LDNS_RR_TYPE_NS
				 && !pside) ? 0:leeway));
		}
		free(rep);
		return 1;
	} else {
		/* store msg, and rrsets */
		struct query_info qinf;
		hashvalue_type h;

		qinf = *msgqinf;
		qinf.qname = memdup(msgqinf->qname, msgqinf->qname_len);
		if(!qinf.qname) {
			reply_info_parsedelete(rep, env->alloc);
			return 0;
		}
		/* fixup flags to be sensible for a reply based on the cache */
		/* this module means that RA is available. It is an answer QR. 
		 * Not AA from cache. Not CD in cache (depends on client bit). */
		rep->flags |= (BIT_RA | BIT_QR);
		rep->flags &= ~(BIT_AA | BIT_CD);
		h = query_info_hash(&qinf, (uint16_t)flags);
		dns_cache_store_msg(env, &qinf, h, rep, leeway, pside, msgrep,
			flags, region);
		/* qname is used inside query_info_entrysetup, and set to 
		 * NULL. If it has not been used, free it. free(0) is safe. */
		free(qinf.qname);
	}
	return 1;
}

int 
dns_cache_prefetch_adjust(struct module_env* env, struct query_info* qinfo,
        time_t adjust, uint16_t flags)
{
	struct msgreply_entry* msg;
	msg = msg_cache_lookup(env, qinfo->qname, qinfo->qname_len,
		qinfo->qtype, qinfo->qclass, flags, *env->now, 1);
	if(msg) {
		struct reply_info* rep = (struct reply_info*)msg->entry.data;
		if(rep) {
			rep->prefetch_ttl += adjust;
			lock_rw_unlock(&msg->entry.lock);
			return 1;
		}
		lock_rw_unlock(&msg->entry.lock);
	}
	return 0;
}
