/*
 * validator/val_nsec3.c - validator NSEC3 denial of existence functions.
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
 * This file contains helper functions for the validator module.
 * The functions help with NSEC3 checking, the different NSEC3 proofs
 * for denial of existence, and proofs for presence of types.
 */
#include "config.h"
#include <ctype.h>
#include "validator/val_nsec3.h"
#include "validator/val_secalgo.h"
#include "validator/validator.h"
#include "validator/val_kentry.h"
#include "services/cache/rrset.h"
#include "util/regional.h"
#include "util/rbtree.h"
#include "util/module.h"
#include "util/net_help.h"
#include "util/data/packed_rrset.h"
#include "util/data/dname.h"
#include "util/data/msgreply.h"
/* we include nsec.h for the bitmap_has_type function */
#include "validator/val_nsec.h"
#include "sldns/sbuffer.h"

/** 
 * This function we get from ldns-compat or from base system 
 * it returns the number of data bytes stored at the target, or <0 on error.
 */
int sldns_b32_ntop_extended_hex(uint8_t const *src, size_t srclength,
	char *target, size_t targsize);
/** 
 * This function we get from ldns-compat or from base system 
 * it returns the number of data bytes stored at the target, or <0 on error.
 */
int sldns_b32_pton_extended_hex(char const *src, size_t hashed_owner_str_len, 
	uint8_t *target, size_t targsize);

/**
 * Closest encloser (ce) proof results
 * Contains the ce and the next-closer (nc) proof.
 */
struct ce_response {
	/** the closest encloser name */
	uint8_t* ce;
	/** length of ce */
	size_t ce_len;
	/** NSEC3 record that proved ce. rrset */
	struct ub_packed_rrset_key* ce_rrset;
	/** NSEC3 record that proved ce. rr number */
	int ce_rr;
	/** NSEC3 record that proved nc. rrset */
	struct ub_packed_rrset_key* nc_rrset;
	/** NSEC3 record that proved nc. rr*/
	int nc_rr;
};

/**
 * Filter conditions for NSEC3 proof
 * Used to iterate over the applicable NSEC3 RRs.
 */
struct nsec3_filter {
	/** Zone name, only NSEC3 records for this zone are considered */
	uint8_t* zone;
	/** length of the zonename */
	size_t zone_len;
	/** the list of NSEC3s to filter; array */
	struct ub_packed_rrset_key** list;
	/** number of rrsets in list */
	size_t num;
	/** class of records for the NSEC3, only this class applies */
	uint16_t fclass;
};

/** return number of rrs in an rrset */
static size_t
rrset_get_count(struct ub_packed_rrset_key* rrset)
{
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
        if(!d) return 0;
        return d->count;
}

/** return if nsec3 RR has unknown flags */
static int
nsec3_unknown_flags(struct ub_packed_rrset_key* rrset, int r)
{
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	if(d->rr_len[r] < 2+2)
		return 0; /* malformed */
	return (int)(d->rr_data[r][2+1] & NSEC3_UNKNOWN_FLAGS);
}

int
nsec3_has_optout(struct ub_packed_rrset_key* rrset, int r)
{
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	if(d->rr_len[r] < 2+2)
		return 0; /* malformed */
	return (int)(d->rr_data[r][2+1] & NSEC3_OPTOUT);
}

/** return nsec3 RR algorithm */
static int
nsec3_get_algo(struct ub_packed_rrset_key* rrset, int r)
{
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	if(d->rr_len[r] < 2+1)
		return 0; /* malformed */
	return (int)(d->rr_data[r][2+0]);
}

/** return if nsec3 RR has known algorithm */
static int
nsec3_known_algo(struct ub_packed_rrset_key* rrset, int r)
{
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	if(d->rr_len[r] < 2+1)
		return 0; /* malformed */
	switch(d->rr_data[r][2+0]) {
		case NSEC3_HASH_SHA1:
			return 1;
	}
	return 0;
}

/** return nsec3 RR iteration count */
static size_t
nsec3_get_iter(struct ub_packed_rrset_key* rrset, int r)
{
	uint16_t i;
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	if(d->rr_len[r] < 2+4)
		return 0; /* malformed */
	memmove(&i, d->rr_data[r]+2+2, sizeof(i));
	i = ntohs(i);
	return (size_t)i;
}

/** return nsec3 RR salt */
static int
nsec3_get_salt(struct ub_packed_rrset_key* rrset, int r,
	uint8_t** salt, size_t* saltlen)
{
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	if(d->rr_len[r] < 2+5) {
		*salt = 0;
		*saltlen = 0;
		return 0; /* malformed */
	}
	*saltlen = (size_t)d->rr_data[r][2+4];
	if(d->rr_len[r] < 2+5+(size_t)*saltlen) {
		*salt = 0;
		*saltlen = 0;
		return 0; /* malformed */
	}
	*salt = d->rr_data[r]+2+5;
	return 1;
}

int nsec3_get_params(struct ub_packed_rrset_key* rrset, int r,
	int* algo, size_t* iter, uint8_t** salt, size_t* saltlen)
{
	if(!nsec3_known_algo(rrset, r) || nsec3_unknown_flags(rrset, r))
		return 0;
	if(!nsec3_get_salt(rrset, r, salt, saltlen))
		return 0;
	*algo = nsec3_get_algo(rrset, r);
	*iter = nsec3_get_iter(rrset, r);
	return 1;
}

int
nsec3_get_nextowner(struct ub_packed_rrset_key* rrset, int r,
	uint8_t** next, size_t* nextlen)
{
	size_t saltlen;
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	if(d->rr_len[r] < 2+5) {
		*next = 0;
		*nextlen = 0;
		return 0; /* malformed */
	}
	saltlen = (size_t)d->rr_data[r][2+4];
	if(d->rr_len[r] < 2+5+saltlen+1) {
		*next = 0;
		*nextlen = 0;
		return 0; /* malformed */
	}
	*nextlen = (size_t)d->rr_data[r][2+5+saltlen];
	if(d->rr_len[r] < 2+5+saltlen+1+*nextlen) {
		*next = 0;
		*nextlen = 0;
		return 0; /* malformed */
	}
	*next = d->rr_data[r]+2+5+saltlen+1;
	return 1;
}

size_t nsec3_hash_to_b32(uint8_t* hash, size_t hashlen, uint8_t* zone,
	size_t zonelen, uint8_t* buf, size_t max)
{
	/* write b32 of name, leave one for length */
	int ret;
	if(max < hashlen*2+1) /* quick approx of b32, as if hexb16 */
		return 0;
	ret = sldns_b32_ntop_extended_hex(hash, hashlen, (char*)buf+1, max-1);
	if(ret < 1) 
		return 0;
	buf[0] = (uint8_t)ret; /* length of b32 label */
	ret++;
	if(max - ret < zonelen)
		return 0;
	memmove(buf+ret, zone, zonelen);
	return zonelen+(size_t)ret;
}

size_t nsec3_get_nextowner_b32(struct ub_packed_rrset_key* rrset, int r,
	uint8_t* buf, size_t max)
{
	uint8_t* nm, *zone;
	size_t nmlen, zonelen;
	if(!nsec3_get_nextowner(rrset, r, &nm, &nmlen))
		return 0;
	/* append zone name; the owner name must be <b32>.zone */
	zone = rrset->rk.dname;
	zonelen = rrset->rk.dname_len;
	dname_remove_label(&zone, &zonelen);
	return nsec3_hash_to_b32(nm, nmlen, zone, zonelen, buf, max);
}

int
nsec3_has_type(struct ub_packed_rrset_key* rrset, int r, uint16_t type)
{
	uint8_t* bitmap;
	size_t bitlen, skiplen;
        struct packed_rrset_data* d = (struct packed_rrset_data*)
	        rrset->entry.data;
	log_assert(d && r < (int)d->count);
	skiplen = 2+4;
	/* skip salt */
	if(d->rr_len[r] < skiplen+1)
		return 0; /* malformed, too short */
	skiplen += 1+(size_t)d->rr_data[r][skiplen]; 
	/* skip next hashed owner */
	if(d->rr_len[r] < skiplen+1)
		return 0; /* malformed, too short */
	skiplen += 1+(size_t)d->rr_data[r][skiplen]; 
	if(d->rr_len[r] < skiplen)
		return 0; /* malformed, too short */
	bitlen = d->rr_len[r] - skiplen;
	bitmap = d->rr_data[r]+skiplen;
	return nsecbitmap_has_type_rdata(bitmap, bitlen, type);
}
	
/** 
 * Iterate through NSEC3 list, per RR 
 * This routine gives the next RR in the list (or sets rrset null). 
 * Usage:
 *
 * size_t rrsetnum;
 * int rrnum;
 * struct ub_packed_rrset_key* rrset;
 * for(rrset=filter_first(filter, &rrsetnum, &rrnum); rrset; 
 *	rrset=filter_next(filter, &rrsetnum, &rrnum))
 *		do_stuff;
 * 
 * Also filters out 
 * 	o unknown flag NSEC3s
 * 	o unknown algorithm NSEC3s.
 * @param filter: nsec3 filter structure.
 * @param rrsetnum: in/out rrset number to look at.
 * @param rrnum: in/out rr number in rrset to look at.
 * @returns ptr to the next rrset (or NULL at end).
 */
static struct ub_packed_rrset_key*
filter_next(struct nsec3_filter* filter, size_t* rrsetnum, int* rrnum)
{
	size_t i;
	int r;
	uint8_t* nm;
	size_t nmlen;
	if(!filter->zone) /* empty list */
		return NULL;
	for(i=*rrsetnum; i<filter->num; i++) {
		/* see if RRset qualifies */
		if(ntohs(filter->list[i]->rk.type) != LDNS_RR_TYPE_NSEC3 ||
			ntohs(filter->list[i]->rk.rrset_class) != 
			filter->fclass) 
			continue;
		/* check RRset zone */
		nm = filter->list[i]->rk.dname;
		nmlen = filter->list[i]->rk.dname_len;
		dname_remove_label(&nm, &nmlen);
		if(query_dname_compare(nm, filter->zone) != 0)
			continue;
		if(i == *rrsetnum)
			r = (*rrnum) + 1; /* continue at next RR */
		else	r = 0;		/* new RRset start at first RR */
		for(; r < (int)rrset_get_count(filter->list[i]); r++) {
			/* skip unknown flags, algo */
			if(nsec3_unknown_flags(filter->list[i], r) ||
				!nsec3_known_algo(filter->list[i], r))
				continue;
			/* this one is a good target */
			*rrsetnum = i;
			*rrnum = r;
			return filter->list[i];
		}
	}
	return NULL;
}

/**
 * Start iterating over NSEC3 records.
 * @param filter: the filter structure, must have been filter_init-ed.
 * @param rrsetnum: can be undefined on call, initialised.
 * @param rrnum: can be undefined on call, initialised.
 * @return first rrset of an NSEC3, together with rrnum this points to
 *	the first RR to examine. Is NULL on empty list.
 */
static struct ub_packed_rrset_key*
filter_first(struct nsec3_filter* filter, size_t* rrsetnum, int* rrnum)
{
	*rrsetnum = 0;
	*rrnum = -1;
	return filter_next(filter, rrsetnum, rrnum);
}

/** see if at least one RR is known (flags, algo) */
static int
nsec3_rrset_has_known(struct ub_packed_rrset_key* s)
{
	int r;
	for(r=0; r < (int)rrset_get_count(s); r++) {
		if(!nsec3_unknown_flags(s, r) && nsec3_known_algo(s, r))
			return 1;
	}
	return 0;
}

/** 
 * Initialize the filter structure.
 * Finds the zone by looking at available NSEC3 records and best match.
 * 	(skips the unknown flag and unknown algo NSEC3s).
 *
 * @param filter: nsec3 filter structure.
 * @param list: list of rrsets, an array of them.
 * @param num: number of rrsets in list.
 * @param qinfo: 
 *	query name to match a zone for.
 *	query type (if DS a higher zone must be chosen)
 *	qclass, to filter NSEC3s with.
 */
static void
filter_init(struct nsec3_filter* filter, struct ub_packed_rrset_key** list,
	size_t num, struct query_info* qinfo)
{
	size_t i;
	uint8_t* nm;
	size_t nmlen;
	filter->zone = NULL;
	filter->zone_len = 0;
	filter->list = list;
	filter->num = num;
	filter->fclass = qinfo->qclass;
	for(i=0; i<num; i++) {
		/* ignore other stuff in the list */
		if(ntohs(list[i]->rk.type) != LDNS_RR_TYPE_NSEC3 ||
			ntohs(list[i]->rk.rrset_class) != qinfo->qclass) 
			continue;
		/* skip unknown flags, algo */
		if(!nsec3_rrset_has_known(list[i]))
			continue;

		/* since NSEC3s are base32.zonename, we can find the zone
		 * name by stripping off the first label of the record */
		nm = list[i]->rk.dname;
		nmlen = list[i]->rk.dname_len;
		dname_remove_label(&nm, &nmlen);
		/* if we find a domain that can prove about the qname,
		 * and if this domain is closer to the qname */
		if(dname_subdomain_c(qinfo->qname, nm) && (!filter->zone ||
			dname_subdomain_c(nm, filter->zone))) {
			/* for a type DS do not accept a zone equal to qname*/
			if(qinfo->qtype == LDNS_RR_TYPE_DS && 
				query_dname_compare(qinfo->qname, nm) == 0 &&
				!dname_is_root(qinfo->qname))
				continue;
			filter->zone = nm;
			filter->zone_len = nmlen;
		}
	}
}

/**
 * Find max iteration count using config settings and key size
 * @param ve: validator environment with iteration count config settings.
 * @param bits: key size
 * @return max iteration count
 */
static size_t
get_max_iter(struct val_env* ve, size_t bits)
{
	int i;
	log_assert(ve->nsec3_keyiter_count > 0);
	/* round up to nearest config keysize, linear search, keep it small */
	for(i=0; i<ve->nsec3_keyiter_count; i++) {
		if(bits <= ve->nsec3_keysize[i])
			return ve->nsec3_maxiter[i];
	}
	/* else, use value for biggest key */
	return ve->nsec3_maxiter[ve->nsec3_keyiter_count-1];
}

/** 
 * Determine if any of the NSEC3 rrs iteration count is too high, from key.
 * @param ve: validator environment with iteration count config settings.
 * @param filter: what NSEC3s to loop over.
 * @param kkey: key entry used for verification; used for iteration counts.
 * @return 1 if some nsec3s are above the max iteration count.
 */
static int
nsec3_iteration_count_high(struct val_env* ve, struct nsec3_filter* filter, 
	struct key_entry_key* kkey)
{
	size_t rrsetnum;
	int rrnum;
	struct ub_packed_rrset_key* rrset;
	/* first determine the max number of iterations */
	size_t bits = key_entry_keysize(kkey);
	size_t max_iter = get_max_iter(ve, bits);
	verbose(VERB_ALGO, "nsec3: keysize %d bits, max iterations %d",
		(int)bits, (int)max_iter);

	for(rrset=filter_first(filter, &rrsetnum, &rrnum); rrset; 
		rrset=filter_next(filter, &rrsetnum, &rrnum)) {
		if(nsec3_get_iter(rrset, rrnum) > max_iter)
			return 1;
	}
	return 0;
}

/* nsec3_cache_compare for rbtree */
int
nsec3_hash_cmp(const void* c1, const void* c2) 
{
	struct nsec3_cached_hash* h1 = (struct nsec3_cached_hash*)c1;
	struct nsec3_cached_hash* h2 = (struct nsec3_cached_hash*)c2;
	uint8_t* s1, *s2;
	size_t s1len, s2len;
	int c = query_dname_compare(h1->dname, h2->dname);
	if(c != 0)
		return c;
	/* compare parameters */
	/* if both malformed, its equal, robustness */
	if(nsec3_get_algo(h1->nsec3, h1->rr) !=
		nsec3_get_algo(h2->nsec3, h2->rr)) {
		if(nsec3_get_algo(h1->nsec3, h1->rr) <
			nsec3_get_algo(h2->nsec3, h2->rr))
			return -1;
		return 1;
	}
	if(nsec3_get_iter(h1->nsec3, h1->rr) !=
		nsec3_get_iter(h2->nsec3, h2->rr)) {
		if(nsec3_get_iter(h1->nsec3, h1->rr) <
			nsec3_get_iter(h2->nsec3, h2->rr))
			return -1;
		return 1;
	}
	(void)nsec3_get_salt(h1->nsec3, h1->rr, &s1, &s1len);
	(void)nsec3_get_salt(h2->nsec3, h2->rr, &s2, &s2len);
	if(s1len == 0 && s2len == 0)
		return 0;
	if(!s1) return -1;
	if(!s2) return 1;
	if(s1len != s2len) {
		if(s1len < s2len)
			return -1;
		return 1;
	}
	return memcmp(s1, s2, s1len);
}

size_t
nsec3_get_hashed(sldns_buffer* buf, uint8_t* nm, size_t nmlen, int algo, 
	size_t iter, uint8_t* salt, size_t saltlen, uint8_t* res, size_t max)
{
	size_t i, hash_len;
	/* prepare buffer for first iteration */
	sldns_buffer_clear(buf);
	sldns_buffer_write(buf, nm, nmlen);
	query_dname_tolower(sldns_buffer_begin(buf));
	sldns_buffer_write(buf, salt, saltlen);
	sldns_buffer_flip(buf);
	hash_len = nsec3_hash_algo_size_supported(algo);
	if(hash_len == 0) {
		log_err("nsec3 hash of unknown algo %d", algo);
		return 0;
	}
	if(hash_len > max)
		return 0;
	if(!secalgo_nsec3_hash(algo, (unsigned char*)sldns_buffer_begin(buf),
		sldns_buffer_limit(buf), (unsigned char*)res))
		return 0;
	for(i=0; i<iter; i++) {
		sldns_buffer_clear(buf);
		sldns_buffer_write(buf, res, hash_len);
		sldns_buffer_write(buf, salt, saltlen);
		sldns_buffer_flip(buf);
		if(!secalgo_nsec3_hash(algo,
			(unsigned char*)sldns_buffer_begin(buf),
			sldns_buffer_limit(buf), (unsigned char*)res))
			return 0;
	}
	return hash_len;
}

/** perform hash of name */
static int
nsec3_calc_hash(struct regional* region, sldns_buffer* buf, 
	struct nsec3_cached_hash* c)
{
	int algo = nsec3_get_algo(c->nsec3, c->rr);
	size_t iter = nsec3_get_iter(c->nsec3, c->rr);
	uint8_t* salt;
	size_t saltlen, i;
	if(!nsec3_get_salt(c->nsec3, c->rr, &salt, &saltlen))
		return -1;
	/* prepare buffer for first iteration */
	sldns_buffer_clear(buf);
	sldns_buffer_write(buf, c->dname, c->dname_len);
	query_dname_tolower(sldns_buffer_begin(buf));
	sldns_buffer_write(buf, salt, saltlen);
	sldns_buffer_flip(buf);
	c->hash_len = nsec3_hash_algo_size_supported(algo);
	if(c->hash_len == 0) {
		log_err("nsec3 hash of unknown algo %d", algo);
		return -1;
	}
	c->hash = (uint8_t*)regional_alloc(region, c->hash_len);
	if(!c->hash)
		return 0;
	(void)secalgo_nsec3_hash(algo, (unsigned char*)sldns_buffer_begin(buf),
		sldns_buffer_limit(buf), (unsigned char*)c->hash);
	for(i=0; i<iter; i++) {
		sldns_buffer_clear(buf);
		sldns_buffer_write(buf, c->hash, c->hash_len);
		sldns_buffer_write(buf, salt, saltlen);
		sldns_buffer_flip(buf);
		(void)secalgo_nsec3_hash(algo,
			(unsigned char*)sldns_buffer_begin(buf),
			sldns_buffer_limit(buf), (unsigned char*)c->hash);
	}
	return 1;
}

/** perform b32 encoding of hash */
static int
nsec3_calc_b32(struct regional* region, sldns_buffer* buf, 
	struct nsec3_cached_hash* c)
{
	int r;
	sldns_buffer_clear(buf);
	r = sldns_b32_ntop_extended_hex(c->hash, c->hash_len,
		(char*)sldns_buffer_begin(buf), sldns_buffer_limit(buf));
	if(r < 1) {
		log_err("b32_ntop_extended_hex: error in encoding: %d", r);
		return 0;
	}
	c->b32_len = (size_t)r;
	c->b32 = regional_alloc_init(region, sldns_buffer_begin(buf), 
		c->b32_len);
	if(!c->b32)
		return 0;
	return 1;
}

int
nsec3_hash_name(rbtree_type* table, struct regional* region, sldns_buffer* buf,
	struct ub_packed_rrset_key* nsec3, int rr, uint8_t* dname, 
	size_t dname_len, struct nsec3_cached_hash** hash)
{
	struct nsec3_cached_hash* c;
	struct nsec3_cached_hash looki;
#ifdef UNBOUND_DEBUG
	rbnode_type* n;
#endif
	int r;
	looki.node.key = &looki;
	looki.nsec3 = nsec3;
	looki.rr = rr;
	looki.dname = dname;
	looki.dname_len = dname_len;
	/* lookup first in cache */
	c = (struct nsec3_cached_hash*)rbtree_search(table, &looki);
	if(c) {
		*hash = c;
		return 1;
	}
	/* create a new entry */
	c = (struct nsec3_cached_hash*)regional_alloc(region, sizeof(*c));
	if(!c) return 0;
	c->node.key = c;
	c->nsec3 = nsec3;
	c->rr = rr;
	c->dname = dname;
	c->dname_len = dname_len;
	r = nsec3_calc_hash(region, buf, c);
	if(r != 1)
		return r;
	r = nsec3_calc_b32(region, buf, c);
	if(r != 1)
		return r;
#ifdef UNBOUND_DEBUG
	n =
#else
	(void)
#endif
	rbtree_insert(table, &c->node);
	log_assert(n); /* cannot be duplicate, just did lookup */
	*hash = c;
	return 1;
}

/**
 * compare a label lowercased
 */
static int
label_compare_lower(uint8_t* lab1, uint8_t* lab2, size_t lablen)
{
	size_t i;
	for(i=0; i<lablen; i++) {
		if(tolower((unsigned char)*lab1) != tolower((unsigned char)*lab2)) {
			if(tolower((unsigned char)*lab1) < tolower((unsigned char)*lab2))
				return -1;
			return 1;
		}
		lab1++;
		lab2++;
	}
	return 0;
}

/**
 * Compare a hashed name with the owner name of an NSEC3 RRset.
 * @param flt: filter with zone name.
 * @param hash: the hashed name.
 * @param s: rrset with owner name.
 * @return true if matches exactly, false if not.
 */
static int
nsec3_hash_matches_owner(struct nsec3_filter* flt, 
	struct nsec3_cached_hash* hash, struct ub_packed_rrset_key* s)
{
	uint8_t* nm = s->rk.dname;
	/* compare, does hash of name based on params in this NSEC3
	 * match the owner name of this NSEC3? 
	 * name must be: <hashlength>base32 . zone name 
	 * so; first label must not be root label (not zero length),
	 * and match the b32 encoded hash length, 
	 * and the label content match the b32 encoded hash
	 * and the rest must be the zone name.
	 */
	if(hash->b32_len != 0 && (size_t)nm[0] == hash->b32_len &&
		label_compare_lower(nm+1, hash->b32, hash->b32_len) == 0 &&
		query_dname_compare(nm+(size_t)nm[0]+1, flt->zone) == 0) {
		return 1;
	}
	return 0;
}

/**
 * Find matching NSEC3
 * Find the NSEC3Record that matches a hash of a name.
 * @param env: module environment with temporary region and buffer.
 * @param flt: the NSEC3 RR filter, contains zone name and RRs.
 * @param ct: cached hashes table.
 * @param nm: name to look for.
 * @param nmlen: length of name.
 * @param rrset: nsec3 that matches is returned here.
 * @param rr: rr number in nsec3 rrset that matches.
 * @return true if a matching NSEC3 is found, false if not.
 */
static int
find_matching_nsec3(struct module_env* env, struct nsec3_filter* flt,
	rbtree_type* ct, uint8_t* nm, size_t nmlen, 
	struct ub_packed_rrset_key** rrset, int* rr)
{
	size_t i_rs;
	int i_rr;
	struct ub_packed_rrset_key* s;
	struct nsec3_cached_hash* hash = NULL;
	int r;

	/* this loop skips other-zone and unknown NSEC3s, also non-NSEC3 RRs */
	for(s=filter_first(flt, &i_rs, &i_rr); s; 
		s=filter_next(flt, &i_rs, &i_rr)) {
		/* get name hashed for this NSEC3 RR */
		r = nsec3_hash_name(ct, env->scratch, env->scratch_buffer,
			s, i_rr, nm, nmlen, &hash);
		if(r == 0) {
			log_err("nsec3: malloc failure");
			break; /* alloc failure */
		} else if(r != 1)
			continue; /* malformed NSEC3 */
		else if(nsec3_hash_matches_owner(flt, hash, s)) {
			*rrset = s; /* rrset with this name */
			*rr = i_rr; /* matches hash with these parameters */
			return 1;
		}
	}
	*rrset = NULL;
	*rr = 0;
	return 0;
}

int
nsec3_covers(uint8_t* zone, struct nsec3_cached_hash* hash,
	struct ub_packed_rrset_key* rrset, int rr, sldns_buffer* buf)
{
	uint8_t* next, *owner;
	size_t nextlen;
	int len;
	if(!nsec3_get_nextowner(rrset, rr, &next, &nextlen))
		return 0; /* malformed RR proves nothing */

	/* check the owner name is a hashed value . apex
	 * base32 encoded values must have equal length. 
	 * hash_value and next hash value must have equal length. */
	if(nextlen != hash->hash_len || hash->hash_len==0||hash->b32_len==0|| 
		(size_t)*rrset->rk.dname != hash->b32_len ||
		query_dname_compare(rrset->rk.dname+1+
			(size_t)*rrset->rk.dname, zone) != 0)
		return 0; /* bad lengths or owner name */

	/* This is the "normal case: owner < next and owner < hash < next */
	if(label_compare_lower(rrset->rk.dname+1, hash->b32, 
		hash->b32_len) < 0 && 
		memcmp(hash->hash, next, nextlen) < 0)
		return 1;

	/* convert owner name from text to binary */
	sldns_buffer_clear(buf);
	owner = sldns_buffer_begin(buf);
	len = sldns_b32_pton_extended_hex((char*)rrset->rk.dname+1, 
		hash->b32_len, owner, sldns_buffer_limit(buf));
	if(len<1)
		return 0; /* bad owner name in some way */
	if((size_t)len != hash->hash_len || (size_t)len != nextlen)
		return 0; /* wrong length */

	/* this is the end of zone case: next <= owner && 
	 * 	(hash > owner || hash < next) 
	 * this also covers the only-apex case of next==owner.
	 */
	if(memcmp(next, owner, nextlen) <= 0 &&
		( memcmp(hash->hash, owner, nextlen) > 0 ||
		  memcmp(hash->hash, next, nextlen) < 0)) {
		return 1;
	}
	return 0;
}

/**
 * findCoveringNSEC3
 * Given a name, find a covering NSEC3 from among a list of NSEC3s.
 *
 * @param env: module environment with temporary region and buffer.
 * @param flt: the NSEC3 RR filter, contains zone name and RRs.
 * @param ct: cached hashes table.
 * @param nm: name to check if covered.
 * @param nmlen: length of name.
 * @param rrset: covering NSEC3 rrset is returned here.
 * @param rr: rr of cover is returned here.
 * @return true if a covering NSEC3 is found, false if not.
 */
static int
find_covering_nsec3(struct module_env* env, struct nsec3_filter* flt,
        rbtree_type* ct, uint8_t* nm, size_t nmlen, 
	struct ub_packed_rrset_key** rrset, int* rr)
{
	size_t i_rs;
	int i_rr;
	struct ub_packed_rrset_key* s;
	struct nsec3_cached_hash* hash = NULL;
	int r;

	/* this loop skips other-zone and unknown NSEC3s, also non-NSEC3 RRs */
	for(s=filter_first(flt, &i_rs, &i_rr); s; 
		s=filter_next(flt, &i_rs, &i_rr)) {
		/* get name hashed for this NSEC3 RR */
		r = nsec3_hash_name(ct, env->scratch, env->scratch_buffer,
			s, i_rr, nm, nmlen, &hash);
		if(r == 0) {
			log_err("nsec3: malloc failure");
			break; /* alloc failure */
		} else if(r != 1)
			continue; /* malformed NSEC3 */
		else if(nsec3_covers(flt->zone, hash, s, i_rr, 
			env->scratch_buffer)) {
			*rrset = s; /* rrset with this name */
			*rr = i_rr; /* covers hash with these parameters */
			return 1;
		}
	}
	*rrset = NULL;
	*rr = 0;
	return 0;
}

/**
 * findClosestEncloser
 * Given a name and a list of NSEC3s, find the candidate closest encloser.
 * This will be the first ancestor of 'name' (including itself) to have a
 * matching NSEC3 RR.
 * @param env: module environment with temporary region and buffer.
 * @param flt: the NSEC3 RR filter, contains zone name and RRs.
 * @param ct: cached hashes table.
 * @param qinfo: query that is verified for.
 * @param ce: closest encloser information is returned in here.
 * @return true if a closest encloser candidate is found, false if not.
 */
static int
nsec3_find_closest_encloser(struct module_env* env, struct nsec3_filter* flt, 
	rbtree_type* ct, struct query_info* qinfo, struct ce_response* ce)
{
	uint8_t* nm = qinfo->qname;
	size_t nmlen = qinfo->qname_len;

	/* This scans from longest name to shortest, so the first match 
	 * we find is the only viable candidate. */

	/* (David:) FIXME: modify so that the NSEC3 matching the zone apex need 
	 * not be present. (Mark Andrews idea).
	 * (Wouter:) But make sure you check for DNAME bit in zone apex,
	 * if the NSEC3 you find is the only NSEC3 in the zone, then this
	 * may be the case. */

	while(dname_subdomain_c(nm, flt->zone)) {
		if(find_matching_nsec3(env, flt, ct, nm, nmlen, 
			&ce->ce_rrset, &ce->ce_rr)) {
			ce->ce = nm;
			ce->ce_len = nmlen;
			return 1;
		}
		dname_remove_label(&nm, &nmlen);
	}
	return 0;
}

/**
 * Given a qname and its proven closest encloser, calculate the "next
 * closest" name. Basically, this is the name that is one label longer than
 * the closest encloser that is still a subdomain of qname.
 *
 * @param qname: query name.
 * @param qnamelen: length of qname.
 * @param ce: closest encloser
 * @param nm: result name.
 * @param nmlen: length of nm.
 */
static void
next_closer(uint8_t* qname, size_t qnamelen, uint8_t* ce, 
	uint8_t** nm, size_t* nmlen)
{
	int strip = dname_count_labels(qname) - dname_count_labels(ce) -1;
	*nm = qname;
	*nmlen = qnamelen;
	if(strip>0)
		dname_remove_labels(nm, nmlen, strip);
}

/**
 * proveClosestEncloser
 * Given a List of nsec3 RRs, find and prove the closest encloser to qname.
 * @param env: module environment with temporary region and buffer.
 * @param flt: the NSEC3 RR filter, contains zone name and RRs.
 * @param ct: cached hashes table.
 * @param qinfo: query that is verified for.
 * @param prove_does_not_exist: If true, then if the closest encloser 
 * 	turns out to be qname, then null is returned.
 * 	If set true, and the return value is true, then you can be 
 * 	certain that the ce.nc_rrset and ce.nc_rr are set properly.
 * @param ce: closest encloser information is returned in here.
 * @return bogus if no closest encloser could be proven.
 * 	secure if a closest encloser could be proven, ce is set.
 * 	insecure if the closest-encloser candidate turns out to prove
 * 		that an insecure delegation exists above the qname.
 */
static enum sec_status
nsec3_prove_closest_encloser(struct module_env* env, struct nsec3_filter* flt, 
	rbtree_type* ct, struct query_info* qinfo, int prove_does_not_exist,
	struct ce_response* ce)
{
	uint8_t* nc;
	size_t nc_len;
	/* robust: clean out ce, in case it gets abused later */
	memset(ce, 0, sizeof(*ce));

	if(!nsec3_find_closest_encloser(env, flt, ct, qinfo, ce)) {
		verbose(VERB_ALGO, "nsec3 proveClosestEncloser: could "
			"not find a candidate for the closest encloser.");
		return sec_status_bogus;
	}
	log_nametypeclass(VERB_ALGO, "ce candidate", ce->ce, 0, 0);

	if(query_dname_compare(ce->ce, qinfo->qname) == 0) {
		if(prove_does_not_exist) {
			verbose(VERB_ALGO, "nsec3 proveClosestEncloser: "
				"proved that qname existed, bad");
			return sec_status_bogus;
		}
		/* otherwise, we need to nothing else to prove that qname 
		 * is its own closest encloser. */
		return sec_status_secure;
	}

	/* If the closest encloser is actually a delegation, then the 
	 * response should have been a referral. If it is a DNAME, then 
	 * it should have been a DNAME response. */
	if(nsec3_has_type(ce->ce_rrset, ce->ce_rr, LDNS_RR_TYPE_NS) &&
		!nsec3_has_type(ce->ce_rrset, ce->ce_rr, LDNS_RR_TYPE_SOA)) {
		if(!nsec3_has_type(ce->ce_rrset, ce->ce_rr, LDNS_RR_TYPE_DS)) {
			verbose(VERB_ALGO, "nsec3 proveClosestEncloser: "
				"closest encloser is insecure delegation");
			return sec_status_insecure;
		}
		verbose(VERB_ALGO, "nsec3 proveClosestEncloser: closest "
			"encloser was a delegation, bad");
		return sec_status_bogus;
	}
	if(nsec3_has_type(ce->ce_rrset, ce->ce_rr, LDNS_RR_TYPE_DNAME)) {
		verbose(VERB_ALGO, "nsec3 proveClosestEncloser: closest "
			"encloser was a DNAME, bad");
		return sec_status_bogus;
	}
	
	/* Otherwise, we need to show that the next closer name is covered. */
	next_closer(qinfo->qname, qinfo->qname_len, ce->ce, &nc, &nc_len);
	if(!find_covering_nsec3(env, flt, ct, nc, nc_len, 
		&ce->nc_rrset, &ce->nc_rr)) {
		verbose(VERB_ALGO, "nsec3: Could not find proof that the "
		          "candidate encloser was the closest encloser");
		return sec_status_bogus;
	}
	return sec_status_secure;
}

/** allocate a wildcard for the closest encloser */
static uint8_t*
nsec3_ce_wildcard(struct regional* region, uint8_t* ce, size_t celen,
	size_t* len)
{
	uint8_t* nm;
	if(celen > LDNS_MAX_DOMAINLEN - 2)
		return 0; /* too long */
	nm = (uint8_t*)regional_alloc(region, celen+2);
	if(!nm) {
		log_err("nsec3 wildcard: out of memory");
		return 0; /* alloc failure */
	}
	nm[0] = 1;
	nm[1] = (uint8_t)'*'; /* wildcard label */
	memmove(nm+2, ce, celen);
	*len = celen+2;
	return nm;
}

/** Do the name error proof */
static enum sec_status
nsec3_do_prove_nameerror(struct module_env* env, struct nsec3_filter* flt, 
	rbtree_type* ct, struct query_info* qinfo)
{
	struct ce_response ce;
	uint8_t* wc;
	size_t wclen;
	struct ub_packed_rrset_key* wc_rrset;
	int wc_rr;
	enum sec_status sec;

	/* First locate and prove the closest encloser to qname. We will 
	 * use the variant that fails if the closest encloser turns out 
	 * to be qname. */
	sec = nsec3_prove_closest_encloser(env, flt, ct, qinfo, 1, &ce);
	if(sec != sec_status_secure) {
		if(sec == sec_status_bogus)
			verbose(VERB_ALGO, "nsec3 nameerror proof: failed "
				"to prove a closest encloser");
		else 	verbose(VERB_ALGO, "nsec3 nameerror proof: closest "
				"nsec3 is an insecure delegation");
		return sec;
	}
	log_nametypeclass(VERB_ALGO, "nsec3 nameerror: proven ce=", ce.ce,0,0);

	/* At this point, we know that qname does not exist. Now we need 
	 * to prove that the wildcard does not exist. */
	log_assert(ce.ce);
	wc = nsec3_ce_wildcard(env->scratch, ce.ce, ce.ce_len, &wclen);
	if(!wc || !find_covering_nsec3(env, flt, ct, wc, wclen, 
		&wc_rrset, &wc_rr)) {
		verbose(VERB_ALGO, "nsec3 nameerror proof: could not prove "
			"that the applicable wildcard did not exist.");
		return sec_status_bogus;
	}

	if(ce.nc_rrset && nsec3_has_optout(ce.nc_rrset, ce.nc_rr)) {
		verbose(VERB_ALGO, "nsec3 nameerror proof: nc has optout");
		return sec_status_insecure;
	}
	return sec_status_secure;
}

enum sec_status
nsec3_prove_nameerror(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num,
	struct query_info* qinfo, struct key_entry_key* kkey)
{
	rbtree_type ct;
	struct nsec3_filter flt;

	if(!list || num == 0 || !kkey || !key_entry_isgood(kkey))
		return sec_status_bogus; /* no valid NSEC3s, bogus */
	rbtree_init(&ct, &nsec3_hash_cmp); /* init names-to-hash cache */
	filter_init(&flt, list, num, qinfo); /* init RR iterator */
	if(!flt.zone)
		return sec_status_bogus; /* no RRs */
	if(nsec3_iteration_count_high(ve, &flt, kkey))
		return sec_status_insecure; /* iteration count too high */
	log_nametypeclass(VERB_ALGO, "start nsec3 nameerror proof, zone", 
		flt.zone, 0, 0);
	return nsec3_do_prove_nameerror(env, &flt, &ct, qinfo);
}

/* 
 * No code to handle qtype=NSEC3 specially. 
 * This existed in early drafts, but was later (-05) removed.
 */

/** Do the nodata proof */
static enum sec_status
nsec3_do_prove_nodata(struct module_env* env, struct nsec3_filter* flt, 
	rbtree_type* ct, struct query_info* qinfo)
{
	struct ce_response ce;
	uint8_t* wc;
	size_t wclen;
	struct ub_packed_rrset_key* rrset;
	int rr;
	enum sec_status sec;

	if(find_matching_nsec3(env, flt, ct, qinfo->qname, qinfo->qname_len, 
		&rrset, &rr)) {
		/* cases 1 and 2 */
		if(nsec3_has_type(rrset, rr, qinfo->qtype)) {
			verbose(VERB_ALGO, "proveNodata: Matching NSEC3 "
				"proved that type existed, bogus");
			return sec_status_bogus;
		} else if(nsec3_has_type(rrset, rr, LDNS_RR_TYPE_CNAME)) {
			verbose(VERB_ALGO, "proveNodata: Matching NSEC3 "
				"proved that a CNAME existed, bogus");
			return sec_status_bogus;
		}

		/* 
		 * If type DS: filter_init zone find already found a parent
		 *   zone, so this nsec3 is from a parent zone. 
		 *   o can be not a delegation (unusual query for normal name,
		 *   	no DS anyway, but we can verify that).
		 *   o can be a delegation (which is the usual DS check).
		 *   o may not have the SOA bit set (only the top of the
		 *   	zone, which must have been above the name, has that).
		 *   	Except for the root; which is checked by itself.
		 *
		 * If not type DS: matching nsec3 must not be a delegation.
		 */
		if(qinfo->qtype == LDNS_RR_TYPE_DS && qinfo->qname_len != 1 
			&& nsec3_has_type(rrset, rr, LDNS_RR_TYPE_SOA) &&
			!dname_is_root(qinfo->qname)) {
			verbose(VERB_ALGO, "proveNodata: apex NSEC3 "
				"abused for no DS proof, bogus");
			return sec_status_bogus;
		} else if(qinfo->qtype != LDNS_RR_TYPE_DS && 
			nsec3_has_type(rrset, rr, LDNS_RR_TYPE_NS) &&
			!nsec3_has_type(rrset, rr, LDNS_RR_TYPE_SOA)) {
			if(!nsec3_has_type(rrset, rr, LDNS_RR_TYPE_DS)) {
				verbose(VERB_ALGO, "proveNodata: matching "
					"NSEC3 is insecure delegation");
				return sec_status_insecure;
			}
			verbose(VERB_ALGO, "proveNodata: matching "
				"NSEC3 is a delegation, bogus");
			return sec_status_bogus;
		}
		return sec_status_secure;
	}

	/* For cases 3 - 5, we need the proven closest encloser, and it 
	 * can't match qname. Although, at this point, we know that it 
	 * won't since we just checked that. */
	sec = nsec3_prove_closest_encloser(env, flt, ct, qinfo, 1, &ce);
	if(sec == sec_status_bogus) {
		verbose(VERB_ALGO, "proveNodata: did not match qname, "
		          "nor found a proven closest encloser.");
		return sec_status_bogus;
	} else if(sec==sec_status_insecure && qinfo->qtype!=LDNS_RR_TYPE_DS){
		verbose(VERB_ALGO, "proveNodata: closest nsec3 is insecure "
		          "delegation.");
		return sec_status_insecure;
	}

	/* Case 3: removed */

	/* Case 4: */
	log_assert(ce.ce);
	wc = nsec3_ce_wildcard(env->scratch, ce.ce, ce.ce_len, &wclen);
	if(wc && find_matching_nsec3(env, flt, ct, wc, wclen, &rrset, &rr)) {
		/* found wildcard */
		if(nsec3_has_type(rrset, rr, qinfo->qtype)) {
			verbose(VERB_ALGO, "nsec3 nodata proof: matching "
				"wildcard had qtype, bogus");
			return sec_status_bogus;
		} else if(nsec3_has_type(rrset, rr, LDNS_RR_TYPE_CNAME)) {
			verbose(VERB_ALGO, "nsec3 nodata proof: matching "
				"wildcard had a CNAME, bogus");
			return sec_status_bogus;
		}
		if(qinfo->qtype == LDNS_RR_TYPE_DS && qinfo->qname_len != 1 
			&& nsec3_has_type(rrset, rr, LDNS_RR_TYPE_SOA)) {
			verbose(VERB_ALGO, "nsec3 nodata proof: matching "
				"wildcard for no DS proof has a SOA, bogus");
			return sec_status_bogus;
		} else if(qinfo->qtype != LDNS_RR_TYPE_DS && 
			nsec3_has_type(rrset, rr, LDNS_RR_TYPE_NS) &&
			!nsec3_has_type(rrset, rr, LDNS_RR_TYPE_SOA)) {
			verbose(VERB_ALGO, "nsec3 nodata proof: matching "
				"wildcard is a delegation, bogus");
			return sec_status_bogus;
		}
		/* everything is peachy keen, except for optout spans */
		if(ce.nc_rrset && nsec3_has_optout(ce.nc_rrset, ce.nc_rr)) {
			verbose(VERB_ALGO, "nsec3 nodata proof: matching "
				"wildcard is in optout range, insecure");
			return sec_status_insecure;
		}
		return sec_status_secure;
	}

	/* Case 5: */
	/* Due to forwarders, cnames, and other collating effects, we
	 * can see the ordinary unsigned data from a zone beneath an
	 * insecure delegation under an optout here */
	if(!ce.nc_rrset) {
		verbose(VERB_ALGO, "nsec3 nodata proof: no next closer nsec3");
		return sec_status_bogus;
	}

	/* We need to make sure that the covering NSEC3 is opt-out. */
	log_assert(ce.nc_rrset);
	if(!nsec3_has_optout(ce.nc_rrset, ce.nc_rr)) {
		if(qinfo->qtype == LDNS_RR_TYPE_DS)
		  verbose(VERB_ALGO, "proveNodata: covering NSEC3 was not "
			"opt-out in an opt-out DS NOERROR/NODATA case.");
		else verbose(VERB_ALGO, "proveNodata: could not find matching "
			"NSEC3, nor matching wildcard, nor optout NSEC3 "
			"-- no more options, bogus.");
		return sec_status_bogus;
	}
	/* RFC5155 section 9.2: if nc has optout then no AD flag set */
	return sec_status_insecure;
}

enum sec_status
nsec3_prove_nodata(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num,
	struct query_info* qinfo, struct key_entry_key* kkey)
{
	rbtree_type ct;
	struct nsec3_filter flt;

	if(!list || num == 0 || !kkey || !key_entry_isgood(kkey))
		return sec_status_bogus; /* no valid NSEC3s, bogus */
	rbtree_init(&ct, &nsec3_hash_cmp); /* init names-to-hash cache */
	filter_init(&flt, list, num, qinfo); /* init RR iterator */
	if(!flt.zone)
		return sec_status_bogus; /* no RRs */
	if(nsec3_iteration_count_high(ve, &flt, kkey))
		return sec_status_insecure; /* iteration count too high */
	return nsec3_do_prove_nodata(env, &flt, &ct, qinfo);
}

enum sec_status
nsec3_prove_wildcard(struct module_env* env, struct val_env* ve,
        struct ub_packed_rrset_key** list, size_t num,
	struct query_info* qinfo, struct key_entry_key* kkey, uint8_t* wc)
{
	rbtree_type ct;
	struct nsec3_filter flt;
	struct ce_response ce;
	uint8_t* nc;
	size_t nc_len;
	size_t wclen;
	(void)dname_count_size_labels(wc, &wclen);

	if(!list || num == 0 || !kkey || !key_entry_isgood(kkey))
		return sec_status_bogus; /* no valid NSEC3s, bogus */
	rbtree_init(&ct, &nsec3_hash_cmp); /* init names-to-hash cache */
	filter_init(&flt, list, num, qinfo); /* init RR iterator */
	if(!flt.zone)
		return sec_status_bogus; /* no RRs */
	if(nsec3_iteration_count_high(ve, &flt, kkey))
		return sec_status_insecure; /* iteration count too high */

	/* We know what the (purported) closest encloser is by just 
	 * looking at the supposed generating wildcard. 
	 * The *. has already been removed from the wc name.
	 */
	memset(&ce, 0, sizeof(ce));
	ce.ce = wc;
	ce.ce_len = wclen;

	/* Now we still need to prove that the original data did not exist.
	 * Otherwise, we need to show that the next closer name is covered. */
	next_closer(qinfo->qname, qinfo->qname_len, ce.ce, &nc, &nc_len);
	if(!find_covering_nsec3(env, &flt, &ct, nc, nc_len, 
		&ce.nc_rrset, &ce.nc_rr)) {
		verbose(VERB_ALGO, "proveWildcard: did not find a covering "
			"NSEC3 that covered the next closer name.");
		return sec_status_bogus;
	}
	if(ce.nc_rrset && nsec3_has_optout(ce.nc_rrset, ce.nc_rr)) {
		verbose(VERB_ALGO, "proveWildcard: NSEC3 optout");
		return sec_status_insecure;
	}
	return sec_status_secure;
}

/** test if list is all secure */
static int
list_is_secure(struct module_env* env, struct val_env* ve, 
	struct ub_packed_rrset_key** list, size_t num,
	struct key_entry_key* kkey, char** reason, struct module_qstate* qstate)
{
	struct packed_rrset_data* d;
	size_t i;
	for(i=0; i<num; i++) {
		d = (struct packed_rrset_data*)list[i]->entry.data;
		if(list[i]->rk.type != htons(LDNS_RR_TYPE_NSEC3))
			continue;
		if(d->security == sec_status_secure)
			continue;
		rrset_check_sec_status(env->rrset_cache, list[i], *env->now);
		if(d->security == sec_status_secure)
			continue;
		d->security = val_verify_rrset_entry(env, ve, list[i], kkey,
			reason, LDNS_SECTION_AUTHORITY, qstate);
		if(d->security != sec_status_secure) {
			verbose(VERB_ALGO, "NSEC3 did not verify");
			return 0;
		}
		rrset_update_sec_status(env->rrset_cache, list[i], *env->now);
	}
	return 1;
}

enum sec_status
nsec3_prove_nods(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num,
	struct query_info* qinfo, struct key_entry_key* kkey, char** reason,
	struct module_qstate* qstate)
{
	rbtree_type ct;
	struct nsec3_filter flt;
	struct ce_response ce;
	struct ub_packed_rrset_key* rrset;
	int rr;
	log_assert(qinfo->qtype == LDNS_RR_TYPE_DS);

	if(!list || num == 0 || !kkey || !key_entry_isgood(kkey)) {
		*reason = "no valid NSEC3s";
		return sec_status_bogus; /* no valid NSEC3s, bogus */
	}
	if(!list_is_secure(env, ve, list, num, kkey, reason, qstate))
		return sec_status_bogus; /* not all NSEC3 records secure */
	rbtree_init(&ct, &nsec3_hash_cmp); /* init names-to-hash cache */
	filter_init(&flt, list, num, qinfo); /* init RR iterator */
	if(!flt.zone) {
		*reason = "no NSEC3 records";
		return sec_status_bogus; /* no RRs */
	}
	if(nsec3_iteration_count_high(ve, &flt, kkey))
		return sec_status_insecure; /* iteration count too high */

	/* Look for a matching NSEC3 to qname -- this is the normal 
	 * NODATA case. */
	if(find_matching_nsec3(env, &flt, &ct, qinfo->qname, qinfo->qname_len, 
		&rrset, &rr)) {
		/* If the matching NSEC3 has the SOA bit set, it is from 
		 * the wrong zone (the child instead of the parent). If 
		 * it has the DS bit set, then we were lied to. */
		if(nsec3_has_type(rrset, rr, LDNS_RR_TYPE_SOA) && 
			qinfo->qname_len != 1) {
			verbose(VERB_ALGO, "nsec3 provenods: NSEC3 is from"
				" child zone, bogus");
			*reason = "NSEC3 from child zone";
			return sec_status_bogus;
		} else if(nsec3_has_type(rrset, rr, LDNS_RR_TYPE_DS)) {
			verbose(VERB_ALGO, "nsec3 provenods: NSEC3 has qtype"
				" DS, bogus");
			*reason = "NSEC3 has DS in bitmap";
			return sec_status_bogus;
		}
		/* If the NSEC3 RR doesn't have the NS bit set, then 
		 * this wasn't a delegation point. */
		if(!nsec3_has_type(rrset, rr, LDNS_RR_TYPE_NS))
			return sec_status_indeterminate;
		/* Otherwise, this proves no DS. */
		return sec_status_secure;
	}

	/* Otherwise, we are probably in the opt-out case. */
	if(nsec3_prove_closest_encloser(env, &flt, &ct, qinfo, 1, &ce)
		!= sec_status_secure) {
		/* an insecure delegation *above* the qname does not prove
		 * anything about this qname exactly, and bogus is bogus */
		verbose(VERB_ALGO, "nsec3 provenods: did not match qname, "
		          "nor found a proven closest encloser.");
		*reason = "no NSEC3 closest encloser";
		return sec_status_bogus;
	}

	/* robust extra check */
	if(!ce.nc_rrset) {
		verbose(VERB_ALGO, "nsec3 nods proof: no next closer nsec3");
		*reason = "no NSEC3 next closer";
		return sec_status_bogus;
	}

	/* we had the closest encloser proof, then we need to check that the
	 * covering NSEC3 was opt-out -- the proveClosestEncloser step already
	 * checked to see if the closest encloser was a delegation or DNAME.
	 */
	log_assert(ce.nc_rrset);
	if(!nsec3_has_optout(ce.nc_rrset, ce.nc_rr)) {
		verbose(VERB_ALGO, "nsec3 provenods: covering NSEC3 was not "
			"opt-out in an opt-out DS NOERROR/NODATA case.");
		*reason = "covering NSEC3 was not opt-out in an opt-out "
			"DS NOERROR/NODATA case";
		return sec_status_bogus;
	}
	/* RFC5155 section 9.2: if nc has optout then no AD flag set */
	return sec_status_insecure;
}

enum sec_status
nsec3_prove_nxornodata(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num, 
	struct query_info* qinfo, struct key_entry_key* kkey, int* nodata)
{
	enum sec_status sec, secnx;
	rbtree_type ct;
	struct nsec3_filter flt;
	*nodata = 0;

	if(!list || num == 0 || !kkey || !key_entry_isgood(kkey))
		return sec_status_bogus; /* no valid NSEC3s, bogus */
	rbtree_init(&ct, &nsec3_hash_cmp); /* init names-to-hash cache */
	filter_init(&flt, list, num, qinfo); /* init RR iterator */
	if(!flt.zone)
		return sec_status_bogus; /* no RRs */
	if(nsec3_iteration_count_high(ve, &flt, kkey))
		return sec_status_insecure; /* iteration count too high */

	/* try nxdomain and nodata after another, while keeping the
	 * hash cache intact */

	secnx = nsec3_do_prove_nameerror(env, &flt, &ct, qinfo);
	if(secnx==sec_status_secure)
		return sec_status_secure;
	sec = nsec3_do_prove_nodata(env, &flt, &ct, qinfo);
	if(sec==sec_status_secure) {
		*nodata = 1;
	} else if(sec == sec_status_insecure) {
		*nodata = 1;
	} else if(secnx == sec_status_insecure) {
		sec = sec_status_insecure;
	}
	return sec;
}
