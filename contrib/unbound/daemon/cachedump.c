/*
 * daemon/cachedump.c - dump the cache to text format.
 *
 * Copyright (c) 2008, NLnet Labs. All rights reserved.
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
 * This file contains functions to read and write the cache(s)
 * to text format.
 */
#include "config.h"
#include <openssl/ssl.h>
#include "daemon/cachedump.h"
#include "daemon/remote.h"
#include "daemon/worker.h"
#include "services/cache/rrset.h"
#include "services/cache/dns.h"
#include "services/cache/infra.h"
#include "util/data/msgreply.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "util/data/dname.h"
#include "iterator/iterator.h"
#include "iterator/iter_delegpt.h"
#include "iterator/iter_utils.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_hints.h"
#include "sldns/sbuffer.h"
#include "sldns/wire2str.h"
#include "sldns/str2wire.h"

/** dump one rrset zonefile line */
static int
dump_rrset_line(RES* ssl, struct ub_packed_rrset_key* k, time_t now, size_t i)
{
	char s[65535];
	if(!packed_rr_to_string(k, i, now, s, sizeof(s))) {
		return ssl_printf(ssl, "BADRR\n");
	}
	return ssl_printf(ssl, "%s", s);
}

/** dump rrset key and data info */
static int
dump_rrset(RES* ssl, struct ub_packed_rrset_key* k, 
	struct packed_rrset_data* d, time_t now)
{
	size_t i;
	/* rd lock held by caller */
	if(!k || !d) return 1;
	if(k->id == 0) return 1; /* deleted */
	if(d->ttl < now) return 1; /* expired */

	/* meta line */
	if(!ssl_printf(ssl, ";rrset%s " ARG_LL "d %u %u %d %d\n",
		(k->rk.flags & PACKED_RRSET_NSEC_AT_APEX)?" nsec_apex":"",
		(long long)(d->ttl - now),
		(unsigned)d->count, (unsigned)d->rrsig_count,
		(int)d->trust, (int)d->security
		)) 
		return 0;
	for(i=0; i<d->count + d->rrsig_count; i++) {
		if(!dump_rrset_line(ssl, k, now, i))
			return 0;
	}
	return 1;
}

/** dump lruhash rrset cache */
static int
dump_rrset_lruhash(RES* ssl, struct lruhash* h, time_t now)
{
	struct lruhash_entry* e;
	/* lruhash already locked by caller */
	/* walk in order of lru; best first */
	for(e=h->lru_start; e; e = e->lru_next) {
		lock_rw_rdlock(&e->lock);
		if(!dump_rrset(ssl, (struct ub_packed_rrset_key*)e->key,
			(struct packed_rrset_data*)e->data, now)) {
			lock_rw_unlock(&e->lock);
			return 0;
		}
		lock_rw_unlock(&e->lock);
	}
	return 1;
}

/** dump rrset cache */
static int
dump_rrset_cache(RES* ssl, struct worker* worker)
{
	struct rrset_cache* r = worker->env.rrset_cache;
	size_t slab;
	if(!ssl_printf(ssl, "START_RRSET_CACHE\n")) return 0;
	for(slab=0; slab<r->table.size; slab++) {
		lock_quick_lock(&r->table.array[slab]->lock);
		if(!dump_rrset_lruhash(ssl, r->table.array[slab],
			*worker->env.now)) {
			lock_quick_unlock(&r->table.array[slab]->lock);
			return 0;
		}
		lock_quick_unlock(&r->table.array[slab]->lock);
	}
	return ssl_printf(ssl, "END_RRSET_CACHE\n");
}

/** dump message to rrset reference */
static int
dump_msg_ref(RES* ssl, struct ub_packed_rrset_key* k)
{
	char* nm, *tp, *cl;
	nm = sldns_wire2str_dname(k->rk.dname, k->rk.dname_len);
	tp = sldns_wire2str_type(ntohs(k->rk.type));
	cl = sldns_wire2str_class(ntohs(k->rk.rrset_class));
	if(!nm || !cl || !tp) {
		free(nm);
		free(tp);
		free(cl);
		return ssl_printf(ssl, "BADREF\n");
	}
	if(!ssl_printf(ssl, "%s %s %s %d\n", nm, cl, tp, (int)k->rk.flags)) {
		free(nm);
		free(tp);
		free(cl);
		return 0;
	}
	free(nm);
	free(tp);
	free(cl);

	return 1;
}

/** dump message entry */
static int
dump_msg(RES* ssl, struct query_info* k, struct reply_info* d, 
	time_t now)
{
	size_t i;
	char* nm, *tp, *cl;
	if(!k || !d) return 1;
	if(d->ttl < now) return 1; /* expired */
	
	nm = sldns_wire2str_dname(k->qname, k->qname_len);
	tp = sldns_wire2str_type(k->qtype);
	cl = sldns_wire2str_class(k->qclass);
	if(!nm || !tp || !cl) {
		free(nm);
		free(tp);
		free(cl);
		return 1; /* skip this entry */
	}
	if(!rrset_array_lock(d->ref, d->rrset_count, now)) {
		/* rrsets have timed out or do not exist */
		free(nm);
		free(tp);
		free(cl);
		return 1; /* skip this entry */
	}
	
	/* meta line */
	if(!ssl_printf(ssl, "msg %s %s %s %d %d " ARG_LL "d %d %u %u %u\n",
			nm, cl, tp,
			(int)d->flags, (int)d->qdcount, 
			(long long)(d->ttl-now), (int)d->security,
			(unsigned)d->an_numrrsets, 
			(unsigned)d->ns_numrrsets,
			(unsigned)d->ar_numrrsets)) {
		free(nm);
		free(tp);
		free(cl);
		rrset_array_unlock(d->ref, d->rrset_count);
		return 0;
	}
	free(nm);
	free(tp);
	free(cl);
	
	for(i=0; i<d->rrset_count; i++) {
		if(!dump_msg_ref(ssl, d->rrsets[i])) {
			rrset_array_unlock(d->ref, d->rrset_count);
			return 0;
		}
	}
	rrset_array_unlock(d->ref, d->rrset_count);

	return 1;
}

/** copy msg to worker pad */
static int
copy_msg(struct regional* region, struct lruhash_entry* e, 
	struct query_info** k, struct reply_info** d)
{
	struct reply_info* rep = (struct reply_info*)e->data;
	if(rep->rrset_count > RR_COUNT_MAX)
		return 0; /* to protect against integer overflow */
	*d = (struct reply_info*)regional_alloc_init(region, e->data,
		sizeof(struct reply_info) + 
		sizeof(struct rrset_ref) * (rep->rrset_count-1) +
		sizeof(struct ub_packed_rrset_key*) * rep->rrset_count);
	if(!*d)
		return 0;
	(*d)->rrsets = (struct ub_packed_rrset_key**)(void *)(
		(uint8_t*)(&((*d)->ref[0])) + 
		sizeof(struct rrset_ref) * rep->rrset_count);
	*k = (struct query_info*)regional_alloc_init(region, 
		e->key, sizeof(struct query_info));
	if(!*k)
		return 0;
	(*k)->qname = regional_alloc_init(region, 
		(*k)->qname, (*k)->qname_len);
	return (*k)->qname != NULL;
}

/** dump lruhash msg cache */
static int
dump_msg_lruhash(RES* ssl, struct worker* worker, struct lruhash* h)
{
	struct lruhash_entry* e;
	struct query_info* k;
	struct reply_info* d;

	/* lruhash already locked by caller */
	/* walk in order of lru; best first */
	for(e=h->lru_start; e; e = e->lru_next) {
		regional_free_all(worker->scratchpad);
		lock_rw_rdlock(&e->lock);
		/* make copy of rrset in worker buffer */
		if(!copy_msg(worker->scratchpad, e, &k, &d)) {
			lock_rw_unlock(&e->lock);
			return 0;
		}
		lock_rw_unlock(&e->lock);
		/* release lock so we can lookup the rrset references 
		 * in the rrset cache */
		if(!dump_msg(ssl, k, d, *worker->env.now)) {
			return 0;
		}
	}
	return 1;
}

/** dump msg cache */
static int
dump_msg_cache(RES* ssl, struct worker* worker)
{
	struct slabhash* sh = worker->env.msg_cache;
	size_t slab;
	if(!ssl_printf(ssl, "START_MSG_CACHE\n")) return 0;
	for(slab=0; slab<sh->size; slab++) {
		lock_quick_lock(&sh->array[slab]->lock);
		if(!dump_msg_lruhash(ssl, worker, sh->array[slab])) {
			lock_quick_unlock(&sh->array[slab]->lock);
			return 0;
		}
		lock_quick_unlock(&sh->array[slab]->lock);
	}
	return ssl_printf(ssl, "END_MSG_CACHE\n");
}

int
dump_cache(RES* ssl, struct worker* worker)
{
	if(!dump_rrset_cache(ssl, worker))
		return 0;
	if(!dump_msg_cache(ssl, worker))
		return 0;
	return ssl_printf(ssl, "EOF\n");
}

/** read a line from ssl into buffer */
static int
ssl_read_buf(RES* ssl, sldns_buffer* buf)
{
	return ssl_read_line(ssl, (char*)sldns_buffer_begin(buf), 
		sldns_buffer_capacity(buf));
}

/** check fixed text on line */
static int
read_fixed(RES* ssl, sldns_buffer* buf, const char* str)
{
	if(!ssl_read_buf(ssl, buf)) return 0;
	return (strcmp((char*)sldns_buffer_begin(buf), str) == 0);
}

/** load an RR into rrset */
static int
load_rr(RES* ssl, sldns_buffer* buf, struct regional* region,
	struct ub_packed_rrset_key* rk, struct packed_rrset_data* d,
	unsigned int i, int is_rrsig, int* go_on, time_t now)
{
	uint8_t rr[LDNS_RR_BUF_SIZE];
	size_t rr_len = sizeof(rr), dname_len = 0;
	int status;

	/* read the line */
	if(!ssl_read_buf(ssl, buf))
		return 0;
	if(strncmp((char*)sldns_buffer_begin(buf), "BADRR\n", 6) == 0) {
		*go_on = 0;
		return 1;
	}
	status = sldns_str2wire_rr_buf((char*)sldns_buffer_begin(buf), rr,
		&rr_len, &dname_len, 3600, NULL, 0, NULL, 0);
	if(status != 0) {
		log_warn("error cannot parse rr: %s: %s",
			sldns_get_errorstr_parse(status),
			(char*)sldns_buffer_begin(buf));
		return 0;
	}
	if(is_rrsig && sldns_wirerr_get_type(rr, rr_len, dname_len)
		!= LDNS_RR_TYPE_RRSIG) {
		log_warn("error expected rrsig but got %s",
			(char*)sldns_buffer_begin(buf));
		return 0;
	}

	/* convert ldns rr into packed_rr */
	d->rr_ttl[i] = (time_t)sldns_wirerr_get_ttl(rr, rr_len, dname_len) + now;
	sldns_buffer_clear(buf);
	d->rr_len[i] = sldns_wirerr_get_rdatalen(rr, rr_len, dname_len)+2;
	d->rr_data[i] = (uint8_t*)regional_alloc_init(region, 
		sldns_wirerr_get_rdatawl(rr, rr_len, dname_len), d->rr_len[i]);
	if(!d->rr_data[i]) {
		log_warn("error out of memory");
		return 0;
	}

	/* if first entry, fill the key structure */
	if(i==0) {
		rk->rk.type = htons(sldns_wirerr_get_type(rr, rr_len, dname_len));
		rk->rk.rrset_class = htons(sldns_wirerr_get_class(rr, rr_len, dname_len));
		rk->rk.dname_len = dname_len;
		rk->rk.dname = regional_alloc_init(region, rr, dname_len);
		if(!rk->rk.dname) {
			log_warn("error out of memory");
			return 0;
		}
	}

	return 1;
}

/** move entry into cache */
static int
move_into_cache(struct ub_packed_rrset_key* k, 
	struct packed_rrset_data* d, struct worker* worker)
{
	struct ub_packed_rrset_key* ak;
	struct packed_rrset_data* ad;
	size_t s, i, num = d->count + d->rrsig_count;
	struct rrset_ref ref;
	uint8_t* p;

	ak = alloc_special_obtain(&worker->alloc);
	if(!ak) {
		log_warn("error out of memory");
		return 0;
	}
	ak->entry.data = NULL;
	ak->rk = k->rk;
	ak->entry.hash = rrset_key_hash(&k->rk);
	ak->rk.dname = (uint8_t*)memdup(k->rk.dname, k->rk.dname_len);
	if(!ak->rk.dname) {
		log_warn("error out of memory");
		ub_packed_rrset_parsedelete(ak, &worker->alloc);
		return 0;
	}
	s = sizeof(*ad) + (sizeof(size_t) + sizeof(uint8_t*) + 
		sizeof(time_t))* num;
	for(i=0; i<num; i++)
		s += d->rr_len[i];
	ad = (struct packed_rrset_data*)malloc(s);
	if(!ad) {
		log_warn("error out of memory");
		ub_packed_rrset_parsedelete(ak, &worker->alloc);
		return 0;
	}
	p = (uint8_t*)ad;
	memmove(p, d, sizeof(*ad));
	p += sizeof(*ad);
	memmove(p, &d->rr_len[0], sizeof(size_t)*num);
	p += sizeof(size_t)*num;
	memmove(p, &d->rr_data[0], sizeof(uint8_t*)*num);
	p += sizeof(uint8_t*)*num;
	memmove(p, &d->rr_ttl[0], sizeof(time_t)*num);
	p += sizeof(time_t)*num;
	for(i=0; i<num; i++) {
		memmove(p, d->rr_data[i], d->rr_len[i]);
		p += d->rr_len[i];
	}
	packed_rrset_ptr_fixup(ad);

	ak->entry.data = ad;

	ref.key = ak;
	ref.id = ak->id;
	(void)rrset_cache_update(worker->env.rrset_cache, &ref,
		&worker->alloc, *worker->env.now);
	return 1;
}

/** load an rrset entry */
static int
load_rrset(RES* ssl, sldns_buffer* buf, struct worker* worker)
{
	char* s = (char*)sldns_buffer_begin(buf);
	struct regional* region = worker->scratchpad;
	struct ub_packed_rrset_key* rk;
	struct packed_rrset_data* d;
	unsigned int rr_count, rrsig_count, trust, security;
	long long ttl;
	unsigned int i;
	int go_on = 1;
	regional_free_all(region);

	rk = (struct ub_packed_rrset_key*)regional_alloc_zero(region, 
		sizeof(*rk));
	d = (struct packed_rrset_data*)regional_alloc_zero(region, sizeof(*d));
	if(!rk || !d) {
		log_warn("error out of memory");
		return 0;
	}

	if(strncmp(s, ";rrset", 6) != 0) {
		log_warn("error expected ';rrset' but got %s", s);
		return 0;
	}
	s += 6;
	if(strncmp(s, " nsec_apex", 10) == 0) {
		s += 10;
		rk->rk.flags |= PACKED_RRSET_NSEC_AT_APEX;
	}
	if(sscanf(s, " " ARG_LL "d %u %u %u %u", &ttl, &rr_count, &rrsig_count,
		&trust, &security) != 5) {
		log_warn("error bad rrset spec %s", s);
		return 0;
	}
	if(rr_count == 0 && rrsig_count == 0) {
		log_warn("bad rrset without contents");
		return 0;
	}
	if(rr_count > RR_COUNT_MAX || rrsig_count > RR_COUNT_MAX) {
		log_warn("bad rrset with too many rrs");
		return 0;
	}
	d->count = (size_t)rr_count;
	d->rrsig_count = (size_t)rrsig_count;
	d->security = (enum sec_status)security;
	d->trust = (enum rrset_trust)trust;
	d->ttl = (time_t)ttl + *worker->env.now;

	d->rr_len = regional_alloc_zero(region, 
		sizeof(size_t)*(d->count+d->rrsig_count));
	d->rr_ttl = regional_alloc_zero(region, 
		sizeof(time_t)*(d->count+d->rrsig_count));
	d->rr_data = regional_alloc_zero(region, 
		sizeof(uint8_t*)*(d->count+d->rrsig_count));
	if(!d->rr_len || !d->rr_ttl || !d->rr_data) {
		log_warn("error out of memory");
		return 0;
	}
	
	/* read the rr's themselves */
	for(i=0; i<rr_count; i++) {
		if(!load_rr(ssl, buf, region, rk, d, i, 0, 
			&go_on, *worker->env.now)) {
			log_warn("could not read rr %u", i);
			return 0;
		}
	}
	for(i=0; i<rrsig_count; i++) {
		if(!load_rr(ssl, buf, region, rk, d, i+rr_count, 1, 
			&go_on, *worker->env.now)) {
			log_warn("could not read rrsig %u", i);
			return 0;
		}
	}
	if(!go_on) {
		/* skip this entry */
		return 1;
	}

	return move_into_cache(rk, d, worker);
}

/** load rrset cache */
static int
load_rrset_cache(RES* ssl, struct worker* worker)
{
	sldns_buffer* buf = worker->env.scratch_buffer;
	if(!read_fixed(ssl, buf, "START_RRSET_CACHE")) return 0;
	while(ssl_read_buf(ssl, buf) && 
		strcmp((char*)sldns_buffer_begin(buf), "END_RRSET_CACHE")!=0) {
		if(!load_rrset(ssl, buf, worker))
			return 0;
	}
	return 1;
}

/** read qinfo from next three words */
static char*
load_qinfo(char* str, struct query_info* qinfo, struct regional* region)
{
	/* s is part of the buf */
	char* s = str;
	uint8_t rr[LDNS_RR_BUF_SIZE];
	size_t rr_len = sizeof(rr), dname_len = 0;
	int status;

	/* skip three words */
	s = strchr(str, ' ');
	if(s) s = strchr(s+1, ' ');
	if(s) s = strchr(s+1, ' ');
	if(!s) {
		log_warn("error line too short, %s", str);
		return NULL;
	}
	s[0] = 0;
	s++;

	/* parse them */
	status = sldns_str2wire_rr_question_buf(str, rr, &rr_len, &dname_len,
		NULL, 0, NULL, 0);
	if(status != 0) {
		log_warn("error cannot parse: %s %s",
			sldns_get_errorstr_parse(status), str);
		return NULL;
	}
	qinfo->qtype = sldns_wirerr_get_type(rr, rr_len, dname_len);
	qinfo->qclass = sldns_wirerr_get_class(rr, rr_len, dname_len);
	qinfo->qname_len = dname_len;
	qinfo->qname = (uint8_t*)regional_alloc_init(region, rr, dname_len);
	qinfo->local_alias = NULL;
	if(!qinfo->qname) {
		log_warn("error out of memory");
		return NULL;
	}

	return s;
}

/** load a msg rrset reference */
static int
load_ref(RES* ssl, sldns_buffer* buf, struct worker* worker, 
	struct regional *region, struct ub_packed_rrset_key** rrset, 
	int* go_on)
{
	char* s = (char*)sldns_buffer_begin(buf);
	struct query_info qinfo;
	unsigned int flags;
	struct ub_packed_rrset_key* k;

	/* read line */
	if(!ssl_read_buf(ssl, buf))
		return 0;
	if(strncmp(s, "BADREF", 6) == 0) {
		*go_on = 0; /* its bad, skip it and skip message */
		return 1;
	}

	s = load_qinfo(s, &qinfo, region);
	if(!s) {
		return 0;
	}
	if(sscanf(s, " %u", &flags) != 1) {
		log_warn("error cannot parse flags: %s", s);
		return 0;
	}

	/* lookup in cache */
	k = rrset_cache_lookup(worker->env.rrset_cache, qinfo.qname,
		qinfo.qname_len, qinfo.qtype, qinfo.qclass,
		(uint32_t)flags, *worker->env.now, 0);
	if(!k) {
		/* not found or expired */
		*go_on = 0;
		return 1;
	}

	/* store in result */
	*rrset = packed_rrset_copy_region(k, region, *worker->env.now);
	lock_rw_unlock(&k->entry.lock);

	return (*rrset != NULL);
}

/** load a msg entry */
static int
load_msg(RES* ssl, sldns_buffer* buf, struct worker* worker)
{
	struct regional* region = worker->scratchpad;
	struct query_info qinf;
	struct reply_info rep;
	char* s = (char*)sldns_buffer_begin(buf);
	unsigned int flags, qdcount, security, an, ns, ar;
	long long ttl;
	size_t i;
	int go_on = 1;

	regional_free_all(region);

	if(strncmp(s, "msg ", 4) != 0) {
		log_warn("error expected msg but got %s", s);
		return 0;
	}
	s += 4;
	s = load_qinfo(s, &qinf, region);
	if(!s) {
		return 0;
	}

	/* read remainder of line */
	if(sscanf(s, " %u %u " ARG_LL "d %u %u %u %u", &flags, &qdcount, &ttl, 
		&security, &an, &ns, &ar) != 7) {
		log_warn("error cannot parse numbers: %s", s);
		return 0;
	}
	rep.flags = (uint16_t)flags;
	rep.qdcount = (uint16_t)qdcount;
	rep.ttl = (time_t)ttl;
	rep.prefetch_ttl = PREFETCH_TTL_CALC(rep.ttl);
	rep.serve_expired_ttl = rep.ttl + SERVE_EXPIRED_TTL;
	rep.security = (enum sec_status)security;
	if(an > RR_COUNT_MAX || ns > RR_COUNT_MAX || ar > RR_COUNT_MAX) {
		log_warn("error too many rrsets");
		return 0; /* protect against integer overflow in alloc */
	}
	rep.an_numrrsets = (size_t)an;
	rep.ns_numrrsets = (size_t)ns;
	rep.ar_numrrsets = (size_t)ar;
	rep.rrset_count = (size_t)an+(size_t)ns+(size_t)ar;
	rep.rrsets = (struct ub_packed_rrset_key**)regional_alloc_zero(
		region, sizeof(struct ub_packed_rrset_key*)*rep.rrset_count);

	/* fill repinfo with references */
	for(i=0; i<rep.rrset_count; i++) {
		if(!load_ref(ssl, buf, worker, region, &rep.rrsets[i], 
			&go_on)) {
			return 0;
		}
	}

	if(!go_on) 
		return 1; /* skip this one, not all references satisfied */

	if(!dns_cache_store(&worker->env, &qinf, &rep, 0, 0, 0, NULL, flags)) {
		log_warn("error out of memory");
		return 0;
	}
	return 1;
}

/** load msg cache */
static int
load_msg_cache(RES* ssl, struct worker* worker)
{
	sldns_buffer* buf = worker->env.scratch_buffer;
	if(!read_fixed(ssl, buf, "START_MSG_CACHE")) return 0;
	while(ssl_read_buf(ssl, buf) && 
		strcmp((char*)sldns_buffer_begin(buf), "END_MSG_CACHE")!=0) {
		if(!load_msg(ssl, buf, worker))
			return 0;
	}
	return 1;
}

int
load_cache(RES* ssl, struct worker* worker)
{
	if(!load_rrset_cache(ssl, worker))
		return 0;
	if(!load_msg_cache(ssl, worker))
		return 0;
	return read_fixed(ssl, worker->env.scratch_buffer, "EOF");
}

/** print details on a delegation point */
static void
print_dp_details(RES* ssl, struct worker* worker, struct delegpt* dp)
{
	char buf[257];
	struct delegpt_addr* a;
	int lame, dlame, rlame, rto, edns_vs, to, delay,
		tA = 0, tAAAA = 0, tother = 0;
	long long entry_ttl;
	struct rtt_info ri;
	uint8_t edns_lame_known;
	for(a = dp->target_list; a; a = a->next_target) {
		addr_to_str(&a->addr, a->addrlen, buf, sizeof(buf));
		if(!ssl_printf(ssl, "%-16s\t", buf))
			return;
		if(a->bogus) {
			if(!ssl_printf(ssl, "Address is BOGUS. ")) 
				return;
		}
		/* lookup in infra cache */
		delay=0;
		entry_ttl = infra_get_host_rto(worker->env.infra_cache,
			&a->addr, a->addrlen, dp->name, dp->namelen,
			&ri, &delay, *worker->env.now, &tA, &tAAAA, &tother);
		if(entry_ttl == -2 && ri.rto >= USEFUL_SERVER_TOP_TIMEOUT) {
			if(!ssl_printf(ssl, "expired, rto %d msec, tA %d "
				"tAAAA %d tother %d.\n", ri.rto, tA, tAAAA,
				tother))
				return;
			continue;
		}
		if(entry_ttl == -1 || entry_ttl == -2) {
			if(!ssl_printf(ssl, "not in infra cache.\n"))
				return;
			continue; /* skip stuff not in infra cache */
		}

		/* uses type_A because most often looked up, but other
		 * lameness won't be reported then */
		if(!infra_get_lame_rtt(worker->env.infra_cache, 
			&a->addr, a->addrlen, dp->name, dp->namelen,
			LDNS_RR_TYPE_A, &lame, &dlame, &rlame, &rto,
			*worker->env.now)) {
			if(!ssl_printf(ssl, "not in infra cache.\n"))
				return;
			continue; /* skip stuff not in infra cache */
		}
		if(!ssl_printf(ssl, "%s%s%s%srto %d msec, ttl " ARG_LL "d, "
			"ping %d var %d rtt %d, tA %d, tAAAA %d, tother %d",
			lame?"LAME ":"", dlame?"NoDNSSEC ":"",
			a->lame?"AddrWasParentSide ":"",
			rlame?"NoAuthButRecursive ":"", rto, entry_ttl,
			ri.srtt, ri.rttvar, rtt_notimeout(&ri),
			tA, tAAAA, tother))
			return;
		if(delay)
			if(!ssl_printf(ssl, ", probedelay %d", delay))
				return;
		if(infra_host(worker->env.infra_cache, &a->addr, a->addrlen,
			dp->name, dp->namelen, *worker->env.now, &edns_vs,
			&edns_lame_known, &to)) {
			if(edns_vs == -1) {
				if(!ssl_printf(ssl, ", noEDNS%s.",
					edns_lame_known?" probed":" assumed"))
					return;
			} else {
				if(!ssl_printf(ssl, ", EDNS %d%s.", edns_vs,
					edns_lame_known?" probed":" assumed"))
					return;
			}
		}
		if(!ssl_printf(ssl, "\n"))
			return;
	}
}

/** print main dp info */
static void
print_dp_main(RES* ssl, struct delegpt* dp, struct dns_msg* msg)
{
	size_t i, n_ns, n_miss, n_addr, n_res, n_avail;

	/* print the dp */
	if(msg)
	    for(i=0; i<msg->rep->rrset_count; i++) {
		struct ub_packed_rrset_key* k = msg->rep->rrsets[i];
		struct packed_rrset_data* d = 
			(struct packed_rrset_data*)k->entry.data;
		if(d->security == sec_status_bogus) {
			if(!ssl_printf(ssl, "Address is BOGUS:\n"))
				return;
		}
		if(!dump_rrset(ssl, k, d, 0))
			return;
	    }
	delegpt_count_ns(dp, &n_ns, &n_miss);
	delegpt_count_addr(dp, &n_addr, &n_res, &n_avail);
	/* since dp has not been used by iterator, all are available*/
	if(!ssl_printf(ssl, "Delegation with %d names, of which %d "
		"can be examined to query further addresses.\n"
		"%sIt provides %d IP addresses.\n", 
		(int)n_ns, (int)n_miss, (dp->bogus?"It is BOGUS. ":""),
		(int)n_addr))
		return;
}

int print_deleg_lookup(RES* ssl, struct worker* worker, uint8_t* nm,
	size_t nmlen, int ATTR_UNUSED(nmlabs))
{
	/* deep links into the iterator module */
	struct delegpt* dp;
	struct dns_msg* msg;
	struct regional* region = worker->scratchpad;
	char b[260];
	struct query_info qinfo;
	struct iter_hints_stub* stub;
	regional_free_all(region);
	qinfo.qname = nm;
	qinfo.qname_len = nmlen;
	qinfo.qtype = LDNS_RR_TYPE_A;
	qinfo.qclass = LDNS_RR_CLASS_IN;
	qinfo.local_alias = NULL;

	dname_str(nm, b);
	if(!ssl_printf(ssl, "The following name servers are used for lookup "
		"of %s\n", b)) 
		return 0;
	
	dp = forwards_lookup(worker->env.fwds, nm, qinfo.qclass);
	if(dp) {
		if(!ssl_printf(ssl, "forwarding request:\n"))
			return 0;
		print_dp_main(ssl, dp, NULL);
		print_dp_details(ssl, worker, dp);
		return 1;
	}
	
	while(1) {
		dp = dns_cache_find_delegation(&worker->env, nm, nmlen, 
			qinfo.qtype, qinfo.qclass, region, &msg, 
			*worker->env.now);
		if(!dp) {
			return ssl_printf(ssl, "no delegation from "
				"cache; goes to configured roots\n");
		}
		/* go up? */
		if(iter_dp_is_useless(&qinfo, BIT_RD, dp)) {
			print_dp_main(ssl, dp, msg);
			print_dp_details(ssl, worker, dp);
			if(!ssl_printf(ssl, "cache delegation was "
				"useless (no IP addresses)\n"))
				return 0;
			if(dname_is_root(nm)) {
				/* goes to root config */
				return ssl_printf(ssl, "no delegation from "
					"cache; goes to configured roots\n");
			} else {
				/* useless, goes up */
				nm = dp->name;
				nmlen = dp->namelen;
				dname_remove_label(&nm, &nmlen);
				dname_str(nm, b);
				if(!ssl_printf(ssl, "going up, lookup %s\n", b))
					return 0;
				continue;
			}
		} 
		stub = hints_lookup_stub(worker->env.hints, nm, qinfo.qclass,
			dp);
		if(stub) {
			if(stub->noprime) {
				if(!ssl_printf(ssl, "The noprime stub servers "
					"are used:\n"))
					return 0;
			} else {
				if(!ssl_printf(ssl, "The stub is primed "
						"with servers:\n"))
					return 0;
			}
			print_dp_main(ssl, stub->dp, NULL);
			print_dp_details(ssl, worker, stub->dp);
		} else {
			print_dp_main(ssl, dp, msg);
			print_dp_details(ssl, worker, dp);
		}
		break;
	}

	return 1;
}
