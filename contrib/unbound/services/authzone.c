/*
 * services/authzone.c - authoritative zone that is locally hosted.
 *
 * Copyright (c) 2017, NLnet Labs. All rights reserved.
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
 * This file contains the functions for an authority zone.  This zone
 * is queried by the iterator, just like a stub or forward zone, but then
 * the data is locally held.
 */

#include "config.h"
#include "services/authzone.h"
#include "util/data/dname.h"
#include "util/data/msgparse.h"
#include "util/data/msgreply.h"
#include "util/data/msgencode.h"
#include "util/data/packed_rrset.h"
#include "util/regional.h"
#include "util/net_help.h"
#include "util/netevent.h"
#include "util/config_file.h"
#include "util/log.h"
#include "util/module.h"
#include "util/random.h"
#include "services/cache/dns.h"
#include "services/outside_network.h"
#include "services/listen_dnsport.h"
#include "services/mesh.h"
#include "sldns/rrdef.h"
#include "sldns/pkthdr.h"
#include "sldns/sbuffer.h"
#include "sldns/str2wire.h"
#include "sldns/wire2str.h"
#include "sldns/parseutil.h"
#include "sldns/keyraw.h"
#include "validator/val_nsec3.h"
#include "validator/val_secalgo.h"
#include <ctype.h>

/** bytes to use for NSEC3 hash buffer. 20 for sha1 */
#define N3HASHBUFLEN 32
/** max number of CNAMEs we are willing to follow (in one answer) */
#define MAX_CNAME_CHAIN 8
/** timeout for probe packets for SOA */
#define AUTH_PROBE_TIMEOUT 100 /* msec */
/** when to stop with SOA probes (when exponential timeouts exceed this) */
#define AUTH_PROBE_TIMEOUT_STOP 1000 /* msec */
/* auth transfer timeout for TCP connections, in msec */
#define AUTH_TRANSFER_TIMEOUT 10000 /* msec */
/* auth transfer max backoff for failed tranfers and probes */
#define AUTH_TRANSFER_MAX_BACKOFF 86400 /* sec */
/* auth http port number */
#define AUTH_HTTP_PORT 80
/* auth https port number */
#define AUTH_HTTPS_PORT 443
/* max depth for nested $INCLUDEs */
#define MAX_INCLUDE_DEPTH 10

/** pick up nextprobe task to start waiting to perform transfer actions */
static void xfr_set_timeout(struct auth_xfer* xfr, struct module_env* env,
	int failure, int lookup_only);
/** move to sending the probe packets, next if fails. task_probe */
static void xfr_probe_send_or_end(struct auth_xfer* xfr,
	struct module_env* env);
/** pick up probe task with specified(or NULL) destination first,
 * or transfer task if nothing to probe, or false if already in progress */
static int xfr_start_probe(struct auth_xfer* xfr, struct module_env* env,
	struct auth_master* spec);
/** delete xfer structure (not its tree entry) */
static void auth_xfer_delete(struct auth_xfer* xfr);

/** create new dns_msg */
static struct dns_msg*
msg_create(struct regional* region, struct query_info* qinfo)
{
	struct dns_msg* msg = (struct dns_msg*)regional_alloc(region,
		sizeof(struct dns_msg));
	if(!msg)
		return NULL;
	msg->qinfo.qname = regional_alloc_init(region, qinfo->qname,
		qinfo->qname_len);
	if(!msg->qinfo.qname)
		return NULL;
	msg->qinfo.qname_len = qinfo->qname_len;
	msg->qinfo.qtype = qinfo->qtype;
	msg->qinfo.qclass = qinfo->qclass;
	msg->qinfo.local_alias = NULL;
	/* non-packed reply_info, because it needs to grow the array */
	msg->rep = (struct reply_info*)regional_alloc_zero(region,
		sizeof(struct reply_info)-sizeof(struct rrset_ref));
	if(!msg->rep)
		return NULL;
	msg->rep->flags = (uint16_t)(BIT_QR | BIT_AA);
	msg->rep->authoritative = 1;
	msg->rep->qdcount = 1;
	/* rrsets is NULL, no rrsets yet */
	return msg;
}

/** grow rrset array by one in msg */
static int
msg_grow_array(struct regional* region, struct dns_msg* msg)
{
	if(msg->rep->rrsets == NULL) {
		msg->rep->rrsets = regional_alloc_zero(region,
			sizeof(struct ub_packed_rrset_key*)*(msg->rep->rrset_count+1));
		if(!msg->rep->rrsets)
			return 0;
	} else {
		struct ub_packed_rrset_key** rrsets_old = msg->rep->rrsets;
		msg->rep->rrsets = regional_alloc_zero(region,
			sizeof(struct ub_packed_rrset_key*)*(msg->rep->rrset_count+1));
		if(!msg->rep->rrsets)
			return 0;
		memmove(msg->rep->rrsets, rrsets_old,
			sizeof(struct ub_packed_rrset_key*)*msg->rep->rrset_count);
	}
	return 1;
}

/** get ttl of rrset */
static time_t
get_rrset_ttl(struct ub_packed_rrset_key* k)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)
		k->entry.data;
	return d->ttl;
}

/** Copy rrset into region from domain-datanode and packet rrset */
static struct ub_packed_rrset_key*
auth_packed_rrset_copy_region(struct auth_zone* z, struct auth_data* node,
	struct auth_rrset* rrset, struct regional* region, time_t adjust)
{
	struct ub_packed_rrset_key key;
	memset(&key, 0, sizeof(key));
	key.entry.key = &key;
	key.entry.data = rrset->data;
	key.rk.dname = node->name;
	key.rk.dname_len = node->namelen;
	key.rk.type = htons(rrset->type);
	key.rk.rrset_class = htons(z->dclass);
	key.entry.hash = rrset_key_hash(&key.rk);
	return packed_rrset_copy_region(&key, region, adjust);
}

/** fix up msg->rep TTL and prefetch ttl */
static void
msg_ttl(struct dns_msg* msg)
{
	if(msg->rep->rrset_count == 0) return;
	if(msg->rep->rrset_count == 1) {
		msg->rep->ttl = get_rrset_ttl(msg->rep->rrsets[0]);
		msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
		msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
	} else if(get_rrset_ttl(msg->rep->rrsets[msg->rep->rrset_count-1]) <
		msg->rep->ttl) {
		msg->rep->ttl = get_rrset_ttl(msg->rep->rrsets[
			msg->rep->rrset_count-1]);
		msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
		msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
	}
}

/** see if rrset is a duplicate in the answer message */
static int
msg_rrset_duplicate(struct dns_msg* msg, uint8_t* nm, size_t nmlen,
	uint16_t type, uint16_t dclass)
{
	size_t i;
	for(i=0; i<msg->rep->rrset_count; i++) {
		struct ub_packed_rrset_key* k = msg->rep->rrsets[i];
		if(ntohs(k->rk.type) == type && k->rk.dname_len == nmlen &&
			ntohs(k->rk.rrset_class) == dclass &&
			query_dname_compare(k->rk.dname, nm) == 0)
			return 1;
	}
	return 0;
}

/** add rrset to answer section (no auth, add rrsets yet) */
static int
msg_add_rrset_an(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* node, struct auth_rrset* rrset)
{
	log_assert(msg->rep->ns_numrrsets == 0);
	log_assert(msg->rep->ar_numrrsets == 0);
	if(!rrset || !node)
		return 1;
	if(msg_rrset_duplicate(msg, node->name, node->namelen, rrset->type,
		z->dclass))
		return 1;
	/* grow array */
	if(!msg_grow_array(region, msg))
		return 0;
	/* copy it */
	if(!(msg->rep->rrsets[msg->rep->rrset_count] =
		auth_packed_rrset_copy_region(z, node, rrset, region, 0)))
		return 0;
	msg->rep->rrset_count++;
	msg->rep->an_numrrsets++;
	msg_ttl(msg);
	return 1;
}

/** add rrset to authority section (no additonal section rrsets yet) */
static int
msg_add_rrset_ns(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* node, struct auth_rrset* rrset)
{
	log_assert(msg->rep->ar_numrrsets == 0);
	if(!rrset || !node)
		return 1;
	if(msg_rrset_duplicate(msg, node->name, node->namelen, rrset->type,
		z->dclass))
		return 1;
	/* grow array */
	if(!msg_grow_array(region, msg))
		return 0;
	/* copy it */
	if(!(msg->rep->rrsets[msg->rep->rrset_count] =
		auth_packed_rrset_copy_region(z, node, rrset, region, 0)))
		return 0;
	msg->rep->rrset_count++;
	msg->rep->ns_numrrsets++;
	msg_ttl(msg);
	return 1;
}

/** add rrset to additional section */
static int
msg_add_rrset_ar(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* node, struct auth_rrset* rrset)
{
	if(!rrset || !node)
		return 1;
	if(msg_rrset_duplicate(msg, node->name, node->namelen, rrset->type,
		z->dclass))
		return 1;
	/* grow array */
	if(!msg_grow_array(region, msg))
		return 0;
	/* copy it */
	if(!(msg->rep->rrsets[msg->rep->rrset_count] =
		auth_packed_rrset_copy_region(z, node, rrset, region, 0)))
		return 0;
	msg->rep->rrset_count++;
	msg->rep->ar_numrrsets++;
	msg_ttl(msg);
	return 1;
}

struct auth_zones* auth_zones_create(void)
{
	struct auth_zones* az = (struct auth_zones*)calloc(1, sizeof(*az));
	if(!az) {
		log_err("out of memory");
		return NULL;
	}
	rbtree_init(&az->ztree, &auth_zone_cmp);
	rbtree_init(&az->xtree, &auth_xfer_cmp);
	lock_rw_init(&az->lock);
	lock_protect(&az->lock, &az->ztree, sizeof(az->ztree));
	lock_protect(&az->lock, &az->xtree, sizeof(az->xtree));
	/* also lock protects the rbnode's in struct auth_zone, auth_xfer */
	return az;
}

int auth_zone_cmp(const void* z1, const void* z2)
{
	/* first sort on class, so that hierarchy can be maintained within
	 * a class */
	struct auth_zone* a = (struct auth_zone*)z1;
	struct auth_zone* b = (struct auth_zone*)z2;
	int m;
	if(a->dclass != b->dclass) {
		if(a->dclass < b->dclass)
			return -1;
		return 1;
	}
	/* sorted such that higher zones sort before lower zones (their
	 * contents) */
	return dname_lab_cmp(a->name, a->namelabs, b->name, b->namelabs, &m);
}

int auth_data_cmp(const void* z1, const void* z2)
{
	struct auth_data* a = (struct auth_data*)z1;
	struct auth_data* b = (struct auth_data*)z2;
	int m;
	/* canonical sort, because DNSSEC needs that */
	return dname_canon_lab_cmp(a->name, a->namelabs, b->name,
		b->namelabs, &m);
}

int auth_xfer_cmp(const void* z1, const void* z2)
{
	/* first sort on class, so that hierarchy can be maintained within
	 * a class */
	struct auth_xfer* a = (struct auth_xfer*)z1;
	struct auth_xfer* b = (struct auth_xfer*)z2;
	int m;
	if(a->dclass != b->dclass) {
		if(a->dclass < b->dclass)
			return -1;
		return 1;
	}
	/* sorted such that higher zones sort before lower zones (their
	 * contents) */
	return dname_lab_cmp(a->name, a->namelabs, b->name, b->namelabs, &m);
}

/** delete auth rrset node */
static void
auth_rrset_delete(struct auth_rrset* rrset)
{
	if(!rrset) return;
	free(rrset->data);
	free(rrset);
}

/** delete auth data domain node */
static void
auth_data_delete(struct auth_data* n)
{
	struct auth_rrset* p, *np;
	if(!n) return;
	p = n->rrsets;
	while(p) {
		np = p->next;
		auth_rrset_delete(p);
		p = np;
	}
	free(n->name);
	free(n);
}

/** helper traverse to delete zones */
static void
auth_data_del(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct auth_data* z = (struct auth_data*)n->key;
	auth_data_delete(z);
}

/** delete an auth zone structure (tree remove must be done elsewhere) */
static void
auth_zone_delete(struct auth_zone* z)
{
	if(!z) return;
	lock_rw_destroy(&z->lock);
	traverse_postorder(&z->data, auth_data_del, NULL);
	free(z->name);
	free(z->zonefile);
	free(z);
}

struct auth_zone*
auth_zone_create(struct auth_zones* az, uint8_t* nm, size_t nmlen,
	uint16_t dclass)
{
	struct auth_zone* z = (struct auth_zone*)calloc(1, sizeof(*z));
	if(!z) {
		return NULL;
	}
	z->node.key = z;
	z->dclass = dclass;
	z->namelen = nmlen;
	z->namelabs = dname_count_labels(nm);
	z->name = memdup(nm, nmlen);
	if(!z->name) {
		free(z);
		return NULL;
	}
	rbtree_init(&z->data, &auth_data_cmp);
	lock_rw_init(&z->lock);
	lock_protect(&z->lock, &z->name, sizeof(*z)-sizeof(rbnode_type));
	lock_rw_wrlock(&z->lock);
	/* z lock protects all, except rbtree itself, which is az->lock */
	if(!rbtree_insert(&az->ztree, &z->node)) {
		lock_rw_unlock(&z->lock);
		auth_zone_delete(z);
		log_warn("duplicate auth zone");
		return NULL;
	}
	return z;
}

struct auth_zone*
auth_zone_find(struct auth_zones* az, uint8_t* nm, size_t nmlen,
	uint16_t dclass)
{
	struct auth_zone key;
	key.node.key = &key;
	key.dclass = dclass;
	key.name = nm;
	key.namelen = nmlen;
	key.namelabs = dname_count_labels(nm);
	return (struct auth_zone*)rbtree_search(&az->ztree, &key);
}

struct auth_xfer*
auth_xfer_find(struct auth_zones* az, uint8_t* nm, size_t nmlen,
	uint16_t dclass)
{
	struct auth_xfer key;
	key.node.key = &key;
	key.dclass = dclass;
	key.name = nm;
	key.namelen = nmlen;
	key.namelabs = dname_count_labels(nm);
	return (struct auth_xfer*)rbtree_search(&az->xtree, &key);
}

/** find an auth zone or sorted less-or-equal, return true if exact */
static int
auth_zone_find_less_equal(struct auth_zones* az, uint8_t* nm, size_t nmlen,
	uint16_t dclass, struct auth_zone** z)
{
	struct auth_zone key;
	key.node.key = &key;
	key.dclass = dclass;
	key.name = nm;
	key.namelen = nmlen;
	key.namelabs = dname_count_labels(nm);
	return rbtree_find_less_equal(&az->ztree, &key, (rbnode_type**)z);
}


/** find the auth zone that is above the given name */
struct auth_zone*
auth_zones_find_zone(struct auth_zones* az, uint8_t* name, size_t name_len,
	uint16_t dclass)
{
	uint8_t* nm = name;
	size_t nmlen = name_len;
	struct auth_zone* z;
	if(auth_zone_find_less_equal(az, nm, nmlen, dclass, &z)) {
		/* exact match */
		return z;
	} else {
		/* less-or-nothing */
		if(!z) return NULL; /* nothing smaller, nothing above it */
		/* we found smaller name; smaller may be above the name,
		 * but not below it. */
		nm = dname_get_shared_topdomain(z->name, name);
		dname_count_size_labels(nm, &nmlen);
		z = NULL;
	}

	/* search up */
	while(!z) {
		z = auth_zone_find(az, nm, nmlen, dclass);
		if(z) return z;
		if(dname_is_root(nm)) break;
		dname_remove_label(&nm, &nmlen);
	}
	return NULL;
}

/** find or create zone with name str. caller must have lock on az. 
 * returns a wrlocked zone */
static struct auth_zone*
auth_zones_find_or_add_zone(struct auth_zones* az, char* name)
{
	uint8_t nm[LDNS_MAX_DOMAINLEN+1];
	size_t nmlen = sizeof(nm);
	struct auth_zone* z;

	if(sldns_str2wire_dname_buf(name, nm, &nmlen) != 0) {
		log_err("cannot parse auth zone name: %s", name);
		return 0;
	}
	z = auth_zone_find(az, nm, nmlen, LDNS_RR_CLASS_IN);
	if(!z) {
		/* not found, create the zone */
		z = auth_zone_create(az, nm, nmlen, LDNS_RR_CLASS_IN);
	} else {
		lock_rw_wrlock(&z->lock);
	}
	return z;
}

/** find or create xfer zone with name str. caller must have lock on az. 
 * returns a locked xfer */
static struct auth_xfer*
auth_zones_find_or_add_xfer(struct auth_zones* az, struct auth_zone* z)
{
	struct auth_xfer* x;
	x = auth_xfer_find(az, z->name, z->namelen, z->dclass);
	if(!x) {
		/* not found, create the zone */
		x = auth_xfer_create(az, z);
	} else {
		lock_basic_lock(&x->lock);
	}
	return x;
}

int
auth_zone_set_zonefile(struct auth_zone* z, char* zonefile)
{
	if(z->zonefile) free(z->zonefile);
	if(zonefile == NULL) {
		z->zonefile = NULL;
	} else {
		z->zonefile = strdup(zonefile);
		if(!z->zonefile) {
			log_err("malloc failure");
			return 0;
		}
	}
	return 1;
}

/** set auth zone fallback. caller must have lock on zone */
int
auth_zone_set_fallback(struct auth_zone* z, char* fallbackstr)
{
	if(strcmp(fallbackstr, "yes") != 0 && strcmp(fallbackstr, "no") != 0){
		log_err("auth zone fallback, expected yes or no, got %s",
			fallbackstr);
		return 0;
	}
	z->fallback_enabled = (strcmp(fallbackstr, "yes")==0);
	return 1;
}

/** create domain with the given name */
static struct auth_data*
az_domain_create(struct auth_zone* z, uint8_t* nm, size_t nmlen)
{
	struct auth_data* n = (struct auth_data*)malloc(sizeof(*n));
	if(!n) return NULL;
	memset(n, 0, sizeof(*n));
	n->node.key = n;
	n->name = memdup(nm, nmlen);
	if(!n->name) {
		free(n);
		return NULL;
	}
	n->namelen = nmlen;
	n->namelabs = dname_count_labels(nm);
	if(!rbtree_insert(&z->data, &n->node)) {
		log_warn("duplicate auth domain name");
		free(n->name);
		free(n);
		return NULL;
	}
	return n;
}

/** find domain with exactly the given name */
static struct auth_data*
az_find_name(struct auth_zone* z, uint8_t* nm, size_t nmlen)
{
	struct auth_zone key;
	key.node.key = &key;
	key.name = nm;
	key.namelen = nmlen;
	key.namelabs = dname_count_labels(nm);
	return (struct auth_data*)rbtree_search(&z->data, &key);
}

/** Find domain name (or closest match) */
static void
az_find_domain(struct auth_zone* z, struct query_info* qinfo, int* node_exact,
	struct auth_data** node)
{
	struct auth_zone key;
	key.node.key = &key;
	key.name = qinfo->qname;
	key.namelen = qinfo->qname_len;
	key.namelabs = dname_count_labels(key.name);
	*node_exact = rbtree_find_less_equal(&z->data, &key,
		(rbnode_type**)node);
}

/** find or create domain with name in zone */
static struct auth_data*
az_domain_find_or_create(struct auth_zone* z, uint8_t* dname,
	size_t dname_len)
{
	struct auth_data* n = az_find_name(z, dname, dname_len);
	if(!n) {
		n = az_domain_create(z, dname, dname_len);
	}
	return n;
}

/** find rrset of given type in the domain */
static struct auth_rrset*
az_domain_rrset(struct auth_data* n, uint16_t t)
{
	struct auth_rrset* rrset;
	if(!n) return NULL;
	rrset = n->rrsets;
	while(rrset) {
		if(rrset->type == t)
			return rrset;
		rrset = rrset->next;
	}
	return NULL;
}

/** remove rrset of this type from domain */
static void
domain_remove_rrset(struct auth_data* node, uint16_t rr_type)
{
	struct auth_rrset* rrset, *prev;
	if(!node) return;
	prev = NULL;
	rrset = node->rrsets;
	while(rrset) {
		if(rrset->type == rr_type) {
			/* found it, now delete it */
			if(prev) prev->next = rrset->next;
			else	node->rrsets = rrset->next;
			auth_rrset_delete(rrset);
			return;
		}
		prev = rrset;
		rrset = rrset->next;
	}
}

/** find an rr index in the rrset.  returns true if found */
static int
az_rrset_find_rr(struct packed_rrset_data* d, uint8_t* rdata, size_t len,
	size_t* index)
{
	size_t i;
	for(i=0; i<d->count; i++) {
		if(d->rr_len[i] != len)
			continue;
		if(memcmp(d->rr_data[i], rdata, len) == 0) {
			*index = i;
			return 1;
		}
	}
	return 0;
}

/** find an rrsig index in the rrset.  returns true if found */
static int
az_rrset_find_rrsig(struct packed_rrset_data* d, uint8_t* rdata, size_t len,
	size_t* index)
{
	size_t i;
	for(i=d->count; i<d->count + d->rrsig_count; i++) {
		if(d->rr_len[i] != len)
			continue;
		if(memcmp(d->rr_data[i], rdata, len) == 0) {
			*index = i;
			return 1;
		}
	}
	return 0;
}

/** see if rdata is duplicate */
static int
rdata_duplicate(struct packed_rrset_data* d, uint8_t* rdata, size_t len)
{
	size_t i;
	for(i=0; i<d->count + d->rrsig_count; i++) {
		if(d->rr_len[i] != len)
			continue;
		if(memcmp(d->rr_data[i], rdata, len) == 0)
			return 1;
	}
	return 0;
}

/** get rrsig type covered from rdata.
 * @param rdata: rdata in wireformat, starting with 16bit rdlength.
 * @param rdatalen: length of rdata buffer.
 * @return type covered (or 0).
 */
static uint16_t
rrsig_rdata_get_type_covered(uint8_t* rdata, size_t rdatalen)
{
	if(rdatalen < 4)
		return 0;
	return sldns_read_uint16(rdata+2);
}

/** remove RR from existing RRset. Also sig, if it is a signature.
 * reallocates the packed rrset for a new one, false on alloc failure */
static int
rrset_remove_rr(struct auth_rrset* rrset, size_t index)
{
	struct packed_rrset_data* d, *old = rrset->data;
	size_t i;
	if(index >= old->count + old->rrsig_count)
		return 0; /* index out of bounds */
	d = (struct packed_rrset_data*)calloc(1, packed_rrset_sizeof(old) - (
		sizeof(size_t) + sizeof(uint8_t*) + sizeof(time_t) +
		old->rr_len[index]));
	if(!d) {
		log_err("malloc failure");
		return 0;
	}
	d->ttl = old->ttl;
	d->count = old->count;
	d->rrsig_count = old->rrsig_count;
	if(index < d->count) d->count--;
	else d->rrsig_count--;
	d->trust = old->trust;
	d->security = old->security;

	/* set rr_len, needed for ptr_fixup */
	d->rr_len = (size_t*)((uint8_t*)d +
		sizeof(struct packed_rrset_data));
	if(index > 0)
		memmove(d->rr_len, old->rr_len, (index)*sizeof(size_t));
	if(index+1 < old->count+old->rrsig_count)
		memmove(&d->rr_len[index], &old->rr_len[index+1],
		(old->count+old->rrsig_count - (index+1))*sizeof(size_t));
	packed_rrset_ptr_fixup(d);

	/* move over ttls */
	if(index > 0)
		memmove(d->rr_ttl, old->rr_ttl, (index)*sizeof(time_t));
	if(index+1 < old->count+old->rrsig_count)
		memmove(&d->rr_ttl[index], &old->rr_ttl[index+1],
		(old->count+old->rrsig_count - (index+1))*sizeof(time_t));
	
	/* move over rr_data */
	for(i=0; i<d->count+d->rrsig_count; i++) {
		size_t oldi;
		if(i < index) oldi = i;
		else oldi = i+1;
		memmove(d->rr_data[i], old->rr_data[oldi], d->rr_len[i]);
	}

	/* recalc ttl (lowest of remaining RR ttls) */
	if(d->count + d->rrsig_count > 0)
		d->ttl = d->rr_ttl[0];
	for(i=0; i<d->count+d->rrsig_count; i++) {
		if(d->rr_ttl[i] < d->ttl)
			d->ttl = d->rr_ttl[i];
	}

	free(rrset->data);
	rrset->data = d;
	return 1;
}

/** add RR to existing RRset. If insert_sig is true, add to rrsigs. 
 * This reallocates the packed rrset for a new one */
static int
rrset_add_rr(struct auth_rrset* rrset, uint32_t rr_ttl, uint8_t* rdata,
	size_t rdatalen, int insert_sig)
{
	struct packed_rrset_data* d, *old = rrset->data;
	size_t total, old_total;

	d = (struct packed_rrset_data*)calloc(1, packed_rrset_sizeof(old)
		+ sizeof(size_t) + sizeof(uint8_t*) + sizeof(time_t)
		+ rdatalen);
	if(!d) {
		log_err("out of memory");
		return 0;
	}
	/* copy base values */
	memcpy(d, old, sizeof(struct packed_rrset_data));
	if(!insert_sig) {
		d->count++;
	} else {
		d->rrsig_count++;
	}
	old_total = old->count + old->rrsig_count;
	total = d->count + d->rrsig_count;
	/* set rr_len, needed for ptr_fixup */
	d->rr_len = (size_t*)((uint8_t*)d +
		sizeof(struct packed_rrset_data));
	if(old->count != 0)
		memmove(d->rr_len, old->rr_len, old->count*sizeof(size_t));
	if(old->rrsig_count != 0)
		memmove(d->rr_len+d->count, old->rr_len+old->count,
			old->rrsig_count*sizeof(size_t));
	if(!insert_sig)
		d->rr_len[d->count-1] = rdatalen;
	else	d->rr_len[total-1] = rdatalen;
	packed_rrset_ptr_fixup(d);
	if((time_t)rr_ttl < d->ttl)
		d->ttl = rr_ttl;

	/* copy old values into new array */
	if(old->count != 0) {
		memmove(d->rr_ttl, old->rr_ttl, old->count*sizeof(time_t));
		/* all the old rr pieces are allocated sequential, so we
		 * can copy them in one go */
		memmove(d->rr_data[0], old->rr_data[0],
			(old->rr_data[old->count-1] - old->rr_data[0]) +
			old->rr_len[old->count-1]);
	}
	if(old->rrsig_count != 0) {
		memmove(d->rr_ttl+d->count, old->rr_ttl+old->count,
			old->rrsig_count*sizeof(time_t));
		memmove(d->rr_data[d->count], old->rr_data[old->count],
			(old->rr_data[old_total-1] - old->rr_data[old->count]) +
			old->rr_len[old_total-1]);
	}

	/* insert new value */
	if(!insert_sig) {
		d->rr_ttl[d->count-1] = rr_ttl;
		memmove(d->rr_data[d->count-1], rdata, rdatalen);
	} else {
		d->rr_ttl[total-1] = rr_ttl;
		memmove(d->rr_data[total-1], rdata, rdatalen);
	}

	rrset->data = d;
	free(old);
	return 1;
}

/** Create new rrset for node with packed rrset with one RR element */
static struct auth_rrset*
rrset_create(struct auth_data* node, uint16_t rr_type, uint32_t rr_ttl,
	uint8_t* rdata, size_t rdatalen)
{
	struct auth_rrset* rrset = (struct auth_rrset*)calloc(1,
		sizeof(*rrset));
	struct auth_rrset* p, *prev;
	struct packed_rrset_data* d;
	if(!rrset) {
		log_err("out of memory");
		return NULL;
	}
	rrset->type = rr_type;

	/* the rrset data structure, with one RR */
	d = (struct packed_rrset_data*)calloc(1,
		sizeof(struct packed_rrset_data) + sizeof(size_t) +
		sizeof(uint8_t*) + sizeof(time_t) + rdatalen);
	if(!d) {
		free(rrset);
		log_err("out of memory");
		return NULL;
	}
	rrset->data = d;
	d->ttl = rr_ttl;
	d->trust = rrset_trust_prim_noglue;
	d->rr_len = (size_t*)((uint8_t*)d + sizeof(struct packed_rrset_data));
	d->rr_data = (uint8_t**)&(d->rr_len[1]);
	d->rr_ttl = (time_t*)&(d->rr_data[1]);
	d->rr_data[0] = (uint8_t*)&(d->rr_ttl[1]);

	/* insert the RR */
	d->rr_len[0] = rdatalen;
	d->rr_ttl[0] = rr_ttl;
	memmove(d->rr_data[0], rdata, rdatalen);
	d->count++;

	/* insert rrset into linked list for domain */
	/* find sorted place to link the rrset into the list */
	prev = NULL;
	p = node->rrsets;
	while(p && p->type<=rr_type) {
		prev = p;
		p = p->next;
	}
	/* so, prev is smaller, and p is larger than rr_type */
	rrset->next = p;
	if(prev) prev->next = rrset;
	else node->rrsets = rrset;
	return rrset;
}

/** count number (and size) of rrsigs that cover a type */
static size_t
rrsig_num_that_cover(struct auth_rrset* rrsig, uint16_t rr_type, size_t* sigsz)
{
	struct packed_rrset_data* d = rrsig->data;
	size_t i, num = 0;
	*sigsz = 0;
	log_assert(d && rrsig->type == LDNS_RR_TYPE_RRSIG);
	for(i=0; i<d->count+d->rrsig_count; i++) {
		if(rrsig_rdata_get_type_covered(d->rr_data[i],
			d->rr_len[i]) == rr_type) {
			num++;
			(*sigsz) += d->rr_len[i];
		}
	}
	return num;
}

/** See if rrsig set has covered sigs for rrset and move them over */
static int
rrset_moveover_rrsigs(struct auth_data* node, uint16_t rr_type,
	struct auth_rrset* rrset, struct auth_rrset* rrsig)
{
	size_t sigs, sigsz, i, j, total;
	struct packed_rrset_data* sigold = rrsig->data;
	struct packed_rrset_data* old = rrset->data;
	struct packed_rrset_data* d, *sigd;

	log_assert(rrset->type == rr_type);
	log_assert(rrsig->type == LDNS_RR_TYPE_RRSIG);
	sigs = rrsig_num_that_cover(rrsig, rr_type, &sigsz);
	if(sigs == 0) {
		/* 0 rrsigs to move over, done */
		return 1;
	}

	/* allocate rrset sigsz larger for extra sigs elements, and
	 * allocate rrsig sigsz smaller for less sigs elements. */
	d = (struct packed_rrset_data*)calloc(1, packed_rrset_sizeof(old)
		+ sigs*(sizeof(size_t) + sizeof(uint8_t*) + sizeof(time_t))
		+ sigsz);
	if(!d) {
		log_err("out of memory");
		return 0;
	}
	/* copy base values */
	total = old->count + old->rrsig_count;
	memcpy(d, old, sizeof(struct packed_rrset_data));
	d->rrsig_count += sigs;
	/* setup rr_len */
	d->rr_len = (size_t*)((uint8_t*)d +
		sizeof(struct packed_rrset_data));
	if(total != 0)
		memmove(d->rr_len, old->rr_len, total*sizeof(size_t));
	j = d->count+d->rrsig_count-sigs;
	for(i=0; i<sigold->count+sigold->rrsig_count; i++) {
		if(rrsig_rdata_get_type_covered(sigold->rr_data[i],
			sigold->rr_len[i]) == rr_type) {
			d->rr_len[j] = sigold->rr_len[i];
			j++;
		}
	}
	packed_rrset_ptr_fixup(d);

	/* copy old values into new array */
	if(total != 0) {
		memmove(d->rr_ttl, old->rr_ttl, total*sizeof(time_t));
		/* all the old rr pieces are allocated sequential, so we
		 * can copy them in one go */
		memmove(d->rr_data[0], old->rr_data[0],
			(old->rr_data[total-1] - old->rr_data[0]) +
			old->rr_len[total-1]);
	}

	/* move over the rrsigs to the larger rrset*/
	j = d->count+d->rrsig_count-sigs;
	for(i=0; i<sigold->count+sigold->rrsig_count; i++) {
		if(rrsig_rdata_get_type_covered(sigold->rr_data[i],
			sigold->rr_len[i]) == rr_type) {
			/* move this one over to location j */
			d->rr_ttl[j] = sigold->rr_ttl[i];
			memmove(d->rr_data[j], sigold->rr_data[i],
				sigold->rr_len[i]);
			if(d->rr_ttl[j] < d->ttl)
				d->ttl = d->rr_ttl[j];
			j++;
		}
	}

	/* put it in and deallocate the old rrset */
	rrset->data = d;
	free(old);

	/* now make rrsig set smaller */
	if(sigold->count+sigold->rrsig_count == sigs) {
		/* remove all sigs from rrsig, remove it entirely */
		domain_remove_rrset(node, LDNS_RR_TYPE_RRSIG);
		return 1;
	}
	log_assert(packed_rrset_sizeof(sigold) > sigs*(sizeof(size_t) +
		sizeof(uint8_t*) + sizeof(time_t)) + sigsz);
	sigd = (struct packed_rrset_data*)calloc(1, packed_rrset_sizeof(sigold)
		- sigs*(sizeof(size_t) + sizeof(uint8_t*) + sizeof(time_t))
		- sigsz);
	if(!sigd) {
		/* no need to free up d, it has already been placed in the
		 * node->rrset structure */
		log_err("out of memory");
		return 0;
	}
	/* copy base values */
	memcpy(sigd, sigold, sizeof(struct packed_rrset_data));
	/* in sigd the RRSIGs are stored in the base of the RR, in count */
	sigd->count -= sigs;
	/* setup rr_len */
	sigd->rr_len = (size_t*)((uint8_t*)sigd +
		sizeof(struct packed_rrset_data));
	j = 0;
	for(i=0; i<sigold->count+sigold->rrsig_count; i++) {
		if(rrsig_rdata_get_type_covered(sigold->rr_data[i],
			sigold->rr_len[i]) != rr_type) {
			sigd->rr_len[j] = sigold->rr_len[i];
			j++;
		}
	}
	packed_rrset_ptr_fixup(sigd);

	/* copy old values into new rrsig array */
	j = 0;
	for(i=0; i<sigold->count+sigold->rrsig_count; i++) {
		if(rrsig_rdata_get_type_covered(sigold->rr_data[i],
			sigold->rr_len[i]) != rr_type) {
			/* move this one over to location j */
			sigd->rr_ttl[j] = sigold->rr_ttl[i];
			memmove(sigd->rr_data[j], sigold->rr_data[i],
				sigold->rr_len[i]);
			if(j==0) sigd->ttl = sigd->rr_ttl[j];
			else {
				if(sigd->rr_ttl[j] < sigd->ttl)
					sigd->ttl = sigd->rr_ttl[j];
			}
			j++;
		}
	}

	/* put it in and deallocate the old rrset */
	rrsig->data = sigd;
	free(sigold);

	return 1;
}

/** copy the rrsigs from the rrset to the rrsig rrset, because the rrset
 * is going to be deleted.  reallocates the RRSIG rrset data. */
static int
rrsigs_copy_from_rrset_to_rrsigset(struct auth_rrset* rrset,
	struct auth_rrset* rrsigset)
{
	size_t i;
	if(rrset->data->rrsig_count == 0)
		return 1;

	/* move them over one by one, because there might be duplicates,
	 * duplicates are ignored */
	for(i=rrset->data->count;
		i<rrset->data->count+rrset->data->rrsig_count; i++) {
		uint8_t* rdata = rrset->data->rr_data[i];
		size_t rdatalen = rrset->data->rr_len[i];
		time_t rr_ttl  = rrset->data->rr_ttl[i];

		if(rdata_duplicate(rrsigset->data, rdata, rdatalen)) {
			continue;
		}
		if(!rrset_add_rr(rrsigset, rr_ttl, rdata, rdatalen, 0))
			return 0;
	}
	return 1;
}

/** Add rr to node, ignores duplicate RRs,
 * rdata points to buffer with rdatalen octets, starts with 2bytelength. */
static int
az_domain_add_rr(struct auth_data* node, uint16_t rr_type, uint32_t rr_ttl,
	uint8_t* rdata, size_t rdatalen, int* duplicate)
{
	struct auth_rrset* rrset;
	/* packed rrsets have their rrsigs along with them, sort them out */
	if(rr_type == LDNS_RR_TYPE_RRSIG) {
		uint16_t ctype = rrsig_rdata_get_type_covered(rdata, rdatalen);
		if((rrset=az_domain_rrset(node, ctype))!= NULL) {
			/* a node of the correct type exists, add the RRSIG
			 * to the rrset of the covered data type */
			if(rdata_duplicate(rrset->data, rdata, rdatalen)) {
				if(duplicate) *duplicate = 1;
				return 1;
			}
			if(!rrset_add_rr(rrset, rr_ttl, rdata, rdatalen, 1))
				return 0;
		} else if((rrset=az_domain_rrset(node, rr_type))!= NULL) {
			/* add RRSIG to rrset of type RRSIG */
			if(rdata_duplicate(rrset->data, rdata, rdatalen)) {
				if(duplicate) *duplicate = 1;
				return 1;
			}
			if(!rrset_add_rr(rrset, rr_ttl, rdata, rdatalen, 0))
				return 0;
		} else {
			/* create rrset of type RRSIG */
			if(!rrset_create(node, rr_type, rr_ttl, rdata,
				rdatalen))
				return 0;
		}
	} else {
		/* normal RR type */
		if((rrset=az_domain_rrset(node, rr_type))!= NULL) {
			/* add data to existing node with data type */
			if(rdata_duplicate(rrset->data, rdata, rdatalen)) {
				if(duplicate) *duplicate = 1;
				return 1;
			}
			if(!rrset_add_rr(rrset, rr_ttl, rdata, rdatalen, 0))
				return 0;
		} else {
			struct auth_rrset* rrsig;
			/* create new node with data type */
			if(!(rrset=rrset_create(node, rr_type, rr_ttl, rdata,
				rdatalen)))
				return 0;

			/* see if node of type RRSIG has signatures that
			 * cover the data type, and move them over */
			/* and then make the RRSIG type smaller */
			if((rrsig=az_domain_rrset(node, LDNS_RR_TYPE_RRSIG))
				!= NULL) {
				if(!rrset_moveover_rrsigs(node, rr_type,
					rrset, rrsig))
					return 0;
			}
		}
	}
	return 1;
}

/** insert RR into zone, ignore duplicates */
static int
az_insert_rr(struct auth_zone* z, uint8_t* rr, size_t rr_len,
	size_t dname_len, int* duplicate)
{
	struct auth_data* node;
	uint8_t* dname = rr;
	uint16_t rr_type = sldns_wirerr_get_type(rr, rr_len, dname_len);
	uint16_t rr_class = sldns_wirerr_get_class(rr, rr_len, dname_len);
	uint32_t rr_ttl = sldns_wirerr_get_ttl(rr, rr_len, dname_len);
	size_t rdatalen = ((size_t)sldns_wirerr_get_rdatalen(rr, rr_len,
		dname_len))+2;
	/* rdata points to rdata prefixed with uint16 rdatalength */
	uint8_t* rdata = sldns_wirerr_get_rdatawl(rr, rr_len, dname_len);

	if(rr_class != z->dclass) {
		log_err("wrong class for RR");
		return 0;
	}
	if(!(node=az_domain_find_or_create(z, dname, dname_len))) {
		log_err("cannot create domain");
		return 0;
	}
	if(!az_domain_add_rr(node, rr_type, rr_ttl, rdata, rdatalen,
		duplicate)) {
		log_err("cannot add RR to domain");
		return 0;
	}
	return 1;
}

/** Remove rr from node, ignores nonexisting RRs,
 * rdata points to buffer with rdatalen octets, starts with 2bytelength. */
static int
az_domain_remove_rr(struct auth_data* node, uint16_t rr_type,
	uint8_t* rdata, size_t rdatalen, int* nonexist)
{
	struct auth_rrset* rrset;
	size_t index = 0;

	/* find the plain RR of the given type */
	if((rrset=az_domain_rrset(node, rr_type))!= NULL) {
		if(az_rrset_find_rr(rrset->data, rdata, rdatalen, &index)) {
			if(rrset->data->count == 1 &&
				rrset->data->rrsig_count == 0) {
				/* last RR, delete the rrset */
				domain_remove_rrset(node, rr_type);
			} else if(rrset->data->count == 1 &&
				rrset->data->rrsig_count != 0) {
				/* move RRSIGs to the RRSIG rrset, or
				 * this one becomes that RRset */
				struct auth_rrset* rrsigset = az_domain_rrset(
					node, LDNS_RR_TYPE_RRSIG);
				if(rrsigset) {
					/* move left over rrsigs to the
					 * existing rrset of type RRSIG */
					rrsigs_copy_from_rrset_to_rrsigset(
						rrset, rrsigset);
					/* and then delete the rrset */
					domain_remove_rrset(node, rr_type);
				} else {
					/* no rrset of type RRSIG, this
					 * set is now of that type,
					 * just remove the rr */
					if(!rrset_remove_rr(rrset, index))
						return 0;
					rrset->type = LDNS_RR_TYPE_RRSIG;
					rrset->data->count = rrset->data->rrsig_count;
					rrset->data->rrsig_count = 0;
				}
			} else {
				/* remove the RR from the rrset */
				if(!rrset_remove_rr(rrset, index))
					return 0;
			}
			return 1;
		}
		/* rr not found in rrset */
	}

	/* is it a type RRSIG, look under the covered type */
	if(rr_type == LDNS_RR_TYPE_RRSIG) {
		uint16_t ctype = rrsig_rdata_get_type_covered(rdata, rdatalen);
		if((rrset=az_domain_rrset(node, ctype))!= NULL) {
			if(az_rrset_find_rrsig(rrset->data, rdata, rdatalen,
				&index)) {
				/* rrsig should have d->count > 0, be
				 * over some rr of that type */
				/* remove the rrsig from the rrsigs list of the
				 * rrset */
				if(!rrset_remove_rr(rrset, index))
					return 0;
				return 1;
			}
		}
		/* also RRSIG not found */
	}

	/* nothing found to delete */
	if(nonexist) *nonexist = 1;
	return 1;
}

/** remove RR from zone, ignore if it does not exist, false on alloc failure*/
static int
az_remove_rr(struct auth_zone* z, uint8_t* rr, size_t rr_len,
	size_t dname_len, int* nonexist)
{
	struct auth_data* node;
	uint8_t* dname = rr;
	uint16_t rr_type = sldns_wirerr_get_type(rr, rr_len, dname_len);
	uint16_t rr_class = sldns_wirerr_get_class(rr, rr_len, dname_len);
	size_t rdatalen = ((size_t)sldns_wirerr_get_rdatalen(rr, rr_len,
		dname_len))+2;
	/* rdata points to rdata prefixed with uint16 rdatalength */
	uint8_t* rdata = sldns_wirerr_get_rdatawl(rr, rr_len, dname_len);

	if(rr_class != z->dclass) {
		log_err("wrong class for RR");
		/* really also a nonexisting entry, because no records
		 * of that class in the zone, but return an error because
		 * getting records of the wrong class is a failure of the
		 * zone transfer */
		return 0;
	}
	node = az_find_name(z, dname, dname_len);
	if(!node) {
		/* node with that name does not exist */
		/* nonexisting entry, because no such name */
		*nonexist = 1;
		return 1;
	}
	if(!az_domain_remove_rr(node, rr_type, rdata, rdatalen, nonexist)) {
		/* alloc failure or so */
		return 0;
	}
	/* remove the node, if necessary */
	/* an rrsets==NULL entry is not kept around for empty nonterminals,
	 * and also parent nodes are not kept around, so we just delete it */
	if(node->rrsets == NULL) {
		(void)rbtree_delete(&z->data, node);
		auth_data_delete(node);
	}
	return 1;
}

/** decompress an RR into the buffer where it'll be an uncompressed RR
 * with uncompressed dname and uncompressed rdata (dnames) */
static int
decompress_rr_into_buffer(struct sldns_buffer* buf, uint8_t* pkt,
	size_t pktlen, uint8_t* dname, uint16_t rr_type, uint16_t rr_class,
	uint32_t rr_ttl, uint8_t* rr_data, uint16_t rr_rdlen)
{
	sldns_buffer pktbuf;
	size_t dname_len = 0;
	size_t rdlenpos;
	size_t rdlen;
	uint8_t* rd;
	const sldns_rr_descriptor* desc;
	sldns_buffer_init_frm_data(&pktbuf, pkt, pktlen);
	sldns_buffer_clear(buf);

	/* decompress dname */
	sldns_buffer_set_position(&pktbuf,
		(size_t)(dname - sldns_buffer_current(&pktbuf)));
	dname_len = pkt_dname_len(&pktbuf);
	if(dname_len == 0) return 0; /* parse fail on dname */
	if(!sldns_buffer_available(buf, dname_len)) return 0;
	dname_pkt_copy(&pktbuf, sldns_buffer_current(buf), dname);
	sldns_buffer_skip(buf, (ssize_t)dname_len);

	/* type, class, ttl and rdatalength fields */
	if(!sldns_buffer_available(buf, 10)) return 0;
	sldns_buffer_write_u16(buf, rr_type);
	sldns_buffer_write_u16(buf, rr_class);
	sldns_buffer_write_u32(buf, rr_ttl);
	rdlenpos = sldns_buffer_position(buf);
	sldns_buffer_write_u16(buf, 0); /* rd length position */

	/* decompress rdata */
	desc = sldns_rr_descript(rr_type);
	rd = rr_data;
	rdlen = rr_rdlen;
	if(rdlen > 0 && desc && desc->_dname_count > 0) {
		int count = (int)desc->_dname_count;
		int rdf = 0;
		size_t len; /* how much rdata to plain copy */
		size_t uncompressed_len, compressed_len;
		size_t oldpos;
		/* decompress dnames. */
		while(rdlen > 0 && count) {
			switch(desc->_wireformat[rdf]) {
			case LDNS_RDF_TYPE_DNAME:
				sldns_buffer_set_position(&pktbuf,
					(size_t)(rd -
					sldns_buffer_begin(&pktbuf)));
				oldpos = sldns_buffer_position(&pktbuf);
				/* moves pktbuf to right after the
				 * compressed dname, and returns uncompressed
				 * dname length */
				uncompressed_len = pkt_dname_len(&pktbuf);
				if(!uncompressed_len)
					return 0; /* parse error in dname */
				if(!sldns_buffer_available(buf,
					uncompressed_len))
					/* dname too long for buffer */
					return 0;
				dname_pkt_copy(&pktbuf, 
					sldns_buffer_current(buf), rd);
				sldns_buffer_skip(buf, (ssize_t)uncompressed_len);
				compressed_len = sldns_buffer_position(
					&pktbuf) - oldpos;
				rd += compressed_len;
				rdlen -= compressed_len;
				count--;
				len = 0;
				break;
			case LDNS_RDF_TYPE_STR:
				len = rd[0] + 1;
				break;
			default:
				len = get_rdf_size(desc->_wireformat[rdf]);
				break;
			}
			if(len) {
				if(!sldns_buffer_available(buf, len))
					return 0; /* too long for buffer */
				sldns_buffer_write(buf, rd, len);
				rd += len;
				rdlen -= len;
			}
			rdf++;
		}
	}
	/* copy remaining data */
	if(rdlen > 0) {
		if(!sldns_buffer_available(buf, rdlen)) return 0;
		sldns_buffer_write(buf, rd, rdlen);
	}
	/* fixup rdlength */
	sldns_buffer_write_u16_at(buf, rdlenpos,
		sldns_buffer_position(buf)-rdlenpos-2);
	sldns_buffer_flip(buf);
	return 1;
}

/** insert RR into zone, from packet, decompress RR,
 * if duplicate is nonNULL set the flag but otherwise ignore duplicates */
static int
az_insert_rr_decompress(struct auth_zone* z, uint8_t* pkt, size_t pktlen,
	struct sldns_buffer* scratch_buffer, uint8_t* dname, uint16_t rr_type,
	uint16_t rr_class, uint32_t rr_ttl, uint8_t* rr_data,
	uint16_t rr_rdlen, int* duplicate)
{
	uint8_t* rr;
	size_t rr_len;
	size_t dname_len;
	if(!decompress_rr_into_buffer(scratch_buffer, pkt, pktlen, dname,
		rr_type, rr_class, rr_ttl, rr_data, rr_rdlen)) {
		log_err("could not decompress RR");
		return 0;
	}
	rr = sldns_buffer_begin(scratch_buffer);
	rr_len = sldns_buffer_limit(scratch_buffer);
	dname_len = dname_valid(rr, rr_len);
	return az_insert_rr(z, rr, rr_len, dname_len, duplicate);
}

/** remove RR from zone, from packet, decompress RR,
 * if nonexist is nonNULL set the flag but otherwise ignore nonexisting entries*/
static int
az_remove_rr_decompress(struct auth_zone* z, uint8_t* pkt, size_t pktlen,
	struct sldns_buffer* scratch_buffer, uint8_t* dname, uint16_t rr_type,
	uint16_t rr_class, uint32_t rr_ttl, uint8_t* rr_data,
	uint16_t rr_rdlen, int* nonexist)
{
	uint8_t* rr;
	size_t rr_len;
	size_t dname_len;
	if(!decompress_rr_into_buffer(scratch_buffer, pkt, pktlen, dname,
		rr_type, rr_class, rr_ttl, rr_data, rr_rdlen)) {
		log_err("could not decompress RR");
		return 0;
	}
	rr = sldns_buffer_begin(scratch_buffer);
	rr_len = sldns_buffer_limit(scratch_buffer);
	dname_len = dname_valid(rr, rr_len);
	return az_remove_rr(z, rr, rr_len, dname_len, nonexist);
}

/** 
 * Parse zonefile
 * @param z: zone to read in.
 * @param in: file to read from (just opened).
 * @param rr: buffer to use for RRs, 64k.
 *	passed so that recursive includes can use the same buffer and do
 *	not grow the stack too much.
 * @param rrbuflen: sizeof rr buffer.
 * @param state: parse state with $ORIGIN, $TTL and 'prev-dname' and so on,
 *	that is kept between includes.
 *	The lineno is set at 1 and then increased by the function.
 * @param fname: file name.
 * @param depth: recursion depth for includes
 * returns false on failure, has printed an error message
 */
static int
az_parse_file(struct auth_zone* z, FILE* in, uint8_t* rr, size_t rrbuflen,
	struct sldns_file_parse_state* state, char* fname, int depth)
{
	size_t rr_len, dname_len;
	int status;
	state->lineno = 1;

	while(!feof(in)) {
		rr_len = rrbuflen;
		dname_len = 0;
		status = sldns_fp2wire_rr_buf(in, rr, &rr_len, &dname_len,
			state);
		if(status == LDNS_WIREPARSE_ERR_INCLUDE && rr_len == 0) {
			/* we have $INCLUDE or $something */
			if(strncmp((char*)rr, "$INCLUDE ", 9) == 0 ||
			   strncmp((char*)rr, "$INCLUDE\t", 9) == 0) {
				FILE* inc;
				int lineno_orig = state->lineno;
				char* incfile = (char*)rr + 8;
				if(depth > MAX_INCLUDE_DEPTH) {
					log_err("%s:%d max include depth"
					  "exceeded", fname, state->lineno);
					return 0;
				}
				/* skip spaces */
				while(*incfile == ' ' || *incfile == '\t')
					incfile++;
				incfile = strdup(incfile);
				if(!incfile) {
					log_err("malloc failure");
					return 0;
				}
				verbose(VERB_ALGO, "opening $INCLUDE %s",
					incfile);
				inc = fopen(incfile, "r");
				if(!inc) {
					log_err("%s:%d cannot open include "
						"file %s: %s", z->zonefile,
						lineno_orig, incfile,
						strerror(errno));
					free(incfile);
					return 0;
				}
				/* recurse read that file now */
				if(!az_parse_file(z, inc, rr, rrbuflen,
					state, incfile, depth+1)) {
					log_err("%s:%d cannot parse include "
						"file %s", fname,
						lineno_orig, incfile);
					fclose(inc);
					free(incfile);
					return 0;
				}
				fclose(inc);
				verbose(VERB_ALGO, "done with $INCLUDE %s",
					incfile);
				free(incfile);
				state->lineno = lineno_orig;
			}
			continue;
		}
		if(status != 0) {
			log_err("parse error %s %d:%d: %s", fname,
				state->lineno, LDNS_WIREPARSE_OFFSET(status),
				sldns_get_errorstr_parse(status));
			return 0;
		}
		if(rr_len == 0) {
			/* EMPTY line, TTL or ORIGIN */
			continue;
		}
		/* insert wirerr in rrbuf */
		if(!az_insert_rr(z, rr, rr_len, dname_len, NULL)) {
			char buf[17];
			sldns_wire2str_type_buf(sldns_wirerr_get_type(rr,
				rr_len, dname_len), buf, sizeof(buf));
			log_err("%s:%d cannot insert RR of type %s",
				fname, state->lineno, buf);
			return 0;
		}
	}
	return 1;
}

int
auth_zone_read_zonefile(struct auth_zone* z)
{
	uint8_t rr[LDNS_RR_BUF_SIZE];
	struct sldns_file_parse_state state;
	FILE* in;
	if(!z || !z->zonefile || z->zonefile[0]==0)
		return 1; /* no file, or "", nothing to read */
	if(verbosity >= VERB_ALGO) {
		char nm[255+1];
		dname_str(z->name, nm);
		verbose(VERB_ALGO, "read zonefile %s for %s", z->zonefile, nm);
	}
	in = fopen(z->zonefile, "r");
	if(!in) {
		char* n = sldns_wire2str_dname(z->name, z->namelen);
		if(z->zone_is_slave && errno == ENOENT) {
			/* we fetch the zone contents later, no file yet */
			verbose(VERB_ALGO, "no zonefile %s for %s",
				z->zonefile, n?n:"error");
			free(n);
			return 1;
		}
		log_err("cannot open zonefile %s for %s: %s",
			z->zonefile, n?n:"error", strerror(errno));
		free(n);
		return 0;
	}

	/* clear the data tree */
	traverse_postorder(&z->data, auth_data_del, NULL);
	rbtree_init(&z->data, &auth_data_cmp);

	memset(&state, 0, sizeof(state));
	/* default TTL to 3600 */
	state.default_ttl = 3600;
	/* set $ORIGIN to the zone name */
	if(z->namelen <= sizeof(state.origin)) {
		memcpy(state.origin, z->name, z->namelen);
		state.origin_len = z->namelen;
	}
	/* parse the (toplevel) file */
	if(!az_parse_file(z, in, rr, sizeof(rr), &state, z->zonefile, 0)) {
		char* n = sldns_wire2str_dname(z->name, z->namelen);
		log_err("error parsing zonefile %s for %s",
			z->zonefile, n?n:"error");
		free(n);
		fclose(in);
		return 0;
	}
	fclose(in);
	return 1;
}

/** write buffer to file and check return codes */
static int
write_out(FILE* out, const char* str, size_t len)
{
	size_t r;
	if(len == 0)
		return 1;
	r = fwrite(str, 1, len, out);
	if(r == 0) {
		log_err("write failed: %s", strerror(errno));
		return 0;
	} else if(r < len) {
		log_err("write failed: too short (disk full?)");
		return 0;
	}
	return 1;
}

/** convert auth rr to string */
static int
auth_rr_to_string(uint8_t* nm, size_t nmlen, uint16_t tp, uint16_t cl,
	struct packed_rrset_data* data, size_t i, char* s, size_t buflen)
{
	int w = 0;
	size_t slen = buflen, datlen;
	uint8_t* dat;
	if(i >= data->count) tp = LDNS_RR_TYPE_RRSIG;
	dat = nm;
	datlen = nmlen;
	w += sldns_wire2str_dname_scan(&dat, &datlen, &s, &slen, NULL, 0);
	w += sldns_str_print(&s, &slen, "\t");
	w += sldns_str_print(&s, &slen, "%lu\t", (unsigned long)data->rr_ttl[i]);
	w += sldns_wire2str_class_print(&s, &slen, cl);
	w += sldns_str_print(&s, &slen, "\t");
	w += sldns_wire2str_type_print(&s, &slen, tp);
	w += sldns_str_print(&s, &slen, "\t");
	datlen = data->rr_len[i]-2;
	dat = data->rr_data[i]+2;
	w += sldns_wire2str_rdata_scan(&dat, &datlen, &s, &slen, tp, NULL, 0);

	if(tp == LDNS_RR_TYPE_DNSKEY) {
		w += sldns_str_print(&s, &slen, " ;{id = %u}",
			sldns_calc_keytag_raw(data->rr_data[i]+2,
				data->rr_len[i]-2));
	}
	w += sldns_str_print(&s, &slen, "\n");

	if(w > (int)buflen) {
		log_nametypeclass(0, "RR too long to print", nm, tp, cl);
		return 0;
	}
	return 1;
}

/** write rrset to file */
static int
auth_zone_write_rrset(struct auth_zone* z, struct auth_data* node,
	struct auth_rrset* r, FILE* out)
{
	size_t i, count = r->data->count + r->data->rrsig_count;
	char buf[LDNS_RR_BUF_SIZE];
	for(i=0; i<count; i++) {
		if(!auth_rr_to_string(node->name, node->namelen, r->type,
			z->dclass, r->data, i, buf, sizeof(buf))) {
			verbose(VERB_ALGO, "failed to rr2str rr %d", (int)i);
			continue;
		}
		if(!write_out(out, buf, strlen(buf)))
			return 0;
	}
	return 1;
}

/** write domain to file */
static int
auth_zone_write_domain(struct auth_zone* z, struct auth_data* n, FILE* out)
{
	struct auth_rrset* r;
	/* if this is zone apex, write SOA first */
	if(z->namelen == n->namelen) {
		struct auth_rrset* soa = az_domain_rrset(n, LDNS_RR_TYPE_SOA);
		if(soa) {
			if(!auth_zone_write_rrset(z, n, soa, out))
				return 0;
		}
	}
	/* write all the RRsets for this domain */
	for(r = n->rrsets; r; r = r->next) {
		if(z->namelen == n->namelen &&
			r->type == LDNS_RR_TYPE_SOA)
			continue; /* skip SOA here */
		if(!auth_zone_write_rrset(z, n, r, out))
			return 0;
	}
	return 1;
}

int auth_zone_write_file(struct auth_zone* z, const char* fname)
{
	FILE* out;
	struct auth_data* n;
	out = fopen(fname, "w");
	if(!out) {
		log_err("could not open %s: %s", fname, strerror(errno));
		return 0;
	}
	RBTREE_FOR(n, struct auth_data*, &z->data) {
		if(!auth_zone_write_domain(z, n, out)) {
			log_err("could not write domain to %s", fname);
			fclose(out);
			return 0;
		}
	}
	fclose(out);
	return 1;
}

/** read all auth zones from file (if they have) */
static int
auth_zones_read_zones(struct auth_zones* az)
{
	struct auth_zone* z;
	lock_rw_wrlock(&az->lock);
	RBTREE_FOR(z, struct auth_zone*, &az->ztree) {
		lock_rw_wrlock(&z->lock);
		if(!auth_zone_read_zonefile(z)) {
			lock_rw_unlock(&z->lock);
			lock_rw_unlock(&az->lock);
			return 0;
		}
		lock_rw_unlock(&z->lock);
	}
	lock_rw_unlock(&az->lock);
	return 1;
}

/** find serial number of zone or false if none */
int
auth_zone_get_serial(struct auth_zone* z, uint32_t* serial)
{
	struct auth_data* apex;
	struct auth_rrset* soa;
	struct packed_rrset_data* d;
	apex = az_find_name(z, z->name, z->namelen);
	if(!apex) return 0;
	soa = az_domain_rrset(apex, LDNS_RR_TYPE_SOA);
	if(!soa || soa->data->count==0)
		return 0; /* no RRset or no RRs in rrset */
	if(soa->data->rr_len[0] < 2+4*5) return 0; /* SOA too short */
	d = soa->data;
	*serial = sldns_read_uint32(d->rr_data[0]+(d->rr_len[0]-20));
	return 1;
}

/** Find auth_zone SOA and populate the values in xfr(soa values). */
static int
xfr_find_soa(struct auth_zone* z, struct auth_xfer* xfr)
{
	struct auth_data* apex;
	struct auth_rrset* soa;
	struct packed_rrset_data* d;
	apex = az_find_name(z, z->name, z->namelen);
	if(!apex) return 0;
	soa = az_domain_rrset(apex, LDNS_RR_TYPE_SOA);
	if(!soa || soa->data->count==0)
		return 0; /* no RRset or no RRs in rrset */
	if(soa->data->rr_len[0] < 2+4*5) return 0; /* SOA too short */
	/* SOA record ends with serial, refresh, retry, expiry, minimum,
	 * as 4 byte fields */
	d = soa->data;
	xfr->have_zone = 1;
	xfr->serial = sldns_read_uint32(d->rr_data[0]+(d->rr_len[0]-20));
	xfr->refresh = sldns_read_uint32(d->rr_data[0]+(d->rr_len[0]-16));
	xfr->retry = sldns_read_uint32(d->rr_data[0]+(d->rr_len[0]-12));
	xfr->expiry = sldns_read_uint32(d->rr_data[0]+(d->rr_len[0]-8));
	/* soa minimum at d->rr_len[0]-4 */
	return 1;
}

/** 
 * Setup auth_xfer zone
 * This populates the have_zone, soa values, and so on times.
 * Doesn't do network traffic yet, can set option flags.
 * @param z: locked by caller, and modified for setup
 * @param x: locked by caller, and modified.
 * @return false on failure.
 */
static int
auth_xfer_setup(struct auth_zone* z, struct auth_xfer* x)
{
	/* for a zone without zone transfers, x==NULL, so skip them,
	 * i.e. the zone config is fixed with no masters or urls */
	if(!z || !x) return 1;
	if(!xfr_find_soa(z, x)) {
		return 1;
	}
	/* nothing for probe, nextprobe and transfer tasks */
	return 1;
}

/**
 * Setup all zones
 * @param az: auth zones structure
 * @return false on failure.
 */
static int
auth_zones_setup_zones(struct auth_zones* az)
{
	struct auth_zone* z;
	struct auth_xfer* x;
	lock_rw_wrlock(&az->lock);
	RBTREE_FOR(z, struct auth_zone*, &az->ztree) {
		lock_rw_wrlock(&z->lock);
		x = auth_xfer_find(az, z->name, z->namelen, z->dclass);
		if(x) {
			lock_basic_lock(&x->lock);
		}
		if(!auth_xfer_setup(z, x)) {
			if(x) {
				lock_basic_unlock(&x->lock);
			}
			lock_rw_unlock(&z->lock);
			lock_rw_unlock(&az->lock);
			return 0;
		}
		if(x) {
			lock_basic_unlock(&x->lock);
		}
		lock_rw_unlock(&z->lock);
	}
	lock_rw_unlock(&az->lock);
	return 1;
}

/** set config items and create zones */
static int
auth_zones_cfg(struct auth_zones* az, struct config_auth* c)
{
	struct auth_zone* z;
	struct auth_xfer* x = NULL;

	/* create zone */
	lock_rw_wrlock(&az->lock);
	if(!(z=auth_zones_find_or_add_zone(az, c->name))) {
		lock_rw_unlock(&az->lock);
		return 0;
	}
	if(c->masters || c->urls) {
		if(!(x=auth_zones_find_or_add_xfer(az, z))) {
			lock_rw_unlock(&az->lock);
			lock_rw_unlock(&z->lock);
			return 0;
		}
	}
	if(c->for_downstream)
		az->have_downstream = 1;
	lock_rw_unlock(&az->lock);

	/* set options */
	z->zone_deleted = 0;
	if(!auth_zone_set_zonefile(z, c->zonefile)) {
		if(x) {
			lock_basic_unlock(&x->lock);
		}
		lock_rw_unlock(&z->lock);
		return 0;
	}
	z->for_downstream = c->for_downstream;
	z->for_upstream = c->for_upstream;
	z->fallback_enabled = c->fallback_enabled;

	/* xfer zone */
	if(x) {
		z->zone_is_slave = 1;
		/* set options on xfer zone */
		if(!xfer_set_masters(&x->task_probe->masters, c, 0)) {
			lock_basic_unlock(&x->lock);
			lock_rw_unlock(&z->lock);
			return 0;
		}
		if(!xfer_set_masters(&x->task_transfer->masters, c, 1)) {
			lock_basic_unlock(&x->lock);
			lock_rw_unlock(&z->lock);
			return 0;
		}
		lock_basic_unlock(&x->lock);
	}

	lock_rw_unlock(&z->lock);
	return 1;
}

/** set all auth zones deleted, then in auth_zones_cfg, it marks them
 * as nondeleted (if they are still in the config), and then later
 * we can find deleted zones */
static void
az_setall_deleted(struct auth_zones* az)
{
	struct auth_zone* z;
	lock_rw_wrlock(&az->lock);
	RBTREE_FOR(z, struct auth_zone*, &az->ztree) {
		lock_rw_wrlock(&z->lock);
		z->zone_deleted = 1;
		lock_rw_unlock(&z->lock);
	}
	lock_rw_unlock(&az->lock);
}

/** find zones that are marked deleted and delete them.
 * This is called from apply_cfg, and there are no threads and no
 * workers, so the xfr can just be deleted. */
static void
az_delete_deleted_zones(struct auth_zones* az)
{
	struct auth_zone* z;
	struct auth_zone* delete_list = NULL, *next;
	struct auth_xfer* xfr;
	lock_rw_wrlock(&az->lock);
	RBTREE_FOR(z, struct auth_zone*, &az->ztree) {
		lock_rw_wrlock(&z->lock);
		if(z->zone_deleted) {
			/* we cannot alter the rbtree right now, but
			 * we can put it on a linked list and then
			 * delete it */
			z->delete_next = delete_list;
			delete_list = z;
		}
		lock_rw_unlock(&z->lock);
	}
	/* now we are out of the tree loop and we can loop and delete
	 * the zones */
	z = delete_list;
	while(z) {
		next = z->delete_next;
		xfr = auth_xfer_find(az, z->name, z->namelen, z->dclass);
		if(xfr) {
			(void)rbtree_delete(&az->xtree, &xfr->node);
			auth_xfer_delete(xfr);
		}
		(void)rbtree_delete(&az->ztree, &z->node);
		auth_zone_delete(z);
		z = next;
	}
	lock_rw_unlock(&az->lock);
}

int auth_zones_apply_cfg(struct auth_zones* az, struct config_file* cfg,
	int setup)
{
	struct config_auth* p;
	az_setall_deleted(az);
	for(p = cfg->auths; p; p = p->next) {
		if(!p->name || p->name[0] == 0) {
			log_warn("auth-zone without a name, skipped");
			continue;
		}
		if(!auth_zones_cfg(az, p)) {
			log_err("cannot config auth zone %s", p->name);
			return 0;
		}
	}
	az_delete_deleted_zones(az);
	if(!auth_zones_read_zones(az))
		return 0;
	if(setup) {
		if(!auth_zones_setup_zones(az))
			return 0;
	}
	return 1;
}

/** delete chunks
 * @param at: transfer structure with chunks list.  The chunks and their
 * 	data are freed.
 */
static void
auth_chunks_delete(struct auth_transfer* at)
{
	if(at->chunks_first) {
		struct auth_chunk* c, *cn;
		c = at->chunks_first;
		while(c) {
			cn = c->next;
			free(c->data);
			free(c);
			c = cn;
		}
	}
	at->chunks_first = NULL;
	at->chunks_last = NULL;
}

/** free master addr list */
static void
auth_free_master_addrs(struct auth_addr* list)
{
	struct auth_addr *n;
	while(list) {
		n = list->next;
		free(list);
		list = n;
	}
}

/** free the masters list */
static void
auth_free_masters(struct auth_master* list)
{
	struct auth_master* n;
	while(list) {
		n = list->next;
		auth_free_master_addrs(list->list);
		free(list->host);
		free(list->file);
		free(list);
		list = n;
	}
}

/** delete auth xfer structure
 * @param xfr: delete this xfer and its tasks.
 */
static void
auth_xfer_delete(struct auth_xfer* xfr)
{
	if(!xfr) return;
	lock_basic_destroy(&xfr->lock);
	free(xfr->name);
	if(xfr->task_nextprobe) {
		comm_timer_delete(xfr->task_nextprobe->timer);
		free(xfr->task_nextprobe);
	}
	if(xfr->task_probe) {
		auth_free_masters(xfr->task_probe->masters);
		comm_point_delete(xfr->task_probe->cp);
		free(xfr->task_probe);
	}
	if(xfr->task_transfer) {
		auth_free_masters(xfr->task_transfer->masters);
		comm_point_delete(xfr->task_transfer->cp);
		if(xfr->task_transfer->chunks_first) {
			auth_chunks_delete(xfr->task_transfer);
		}
		free(xfr->task_transfer);
	}
	auth_free_masters(xfr->allow_notify_list);
	free(xfr);
}

/** helper traverse to delete zones */
static void
auth_zone_del(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct auth_zone* z = (struct auth_zone*)n->key;
	auth_zone_delete(z);
}

/** helper traverse to delete xfer zones */
static void
auth_xfer_del(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct auth_xfer* z = (struct auth_xfer*)n->key;
	auth_xfer_delete(z);
}

void auth_zones_delete(struct auth_zones* az)
{
	if(!az) return;
	lock_rw_destroy(&az->lock);
	traverse_postorder(&az->ztree, auth_zone_del, NULL);
	traverse_postorder(&az->xtree, auth_xfer_del, NULL);
	free(az);
}

/** true if domain has only nsec3 */
static int
domain_has_only_nsec3(struct auth_data* n)
{
	struct auth_rrset* rrset = n->rrsets;
	int nsec3_seen = 0;
	while(rrset) {
		if(rrset->type == LDNS_RR_TYPE_NSEC3) {
			nsec3_seen = 1;
		} else if(rrset->type != LDNS_RR_TYPE_RRSIG) {
			return 0;
		}
		rrset = rrset->next;
	}
	return nsec3_seen;
}

/** see if the domain has a wildcard child '*.domain' */
static struct auth_data*
az_find_wildcard_domain(struct auth_zone* z, uint8_t* nm, size_t nmlen)
{
	uint8_t wc[LDNS_MAX_DOMAINLEN];
	if(nmlen+2 > sizeof(wc))
		return NULL; /* result would be too long */
	wc[0] = 1; /* length of wildcard label */
	wc[1] = (uint8_t)'*'; /* wildcard label */
	memmove(wc+2, nm, nmlen);
	return az_find_name(z, wc, nmlen+2);
}

/** find wildcard between qname and cename */
static struct auth_data*
az_find_wildcard(struct auth_zone* z, struct query_info* qinfo,
	struct auth_data* ce)
{
	uint8_t* nm = qinfo->qname;
	size_t nmlen = qinfo->qname_len;
	struct auth_data* node;
	if(!dname_subdomain_c(nm, z->name))
		return NULL; /* out of zone */
	while((node=az_find_wildcard_domain(z, nm, nmlen))==NULL) {
		/* see if we can go up to find the wildcard */
		if(nmlen == z->namelen)
			return NULL; /* top of zone reached */
		if(ce && nmlen == ce->namelen)
			return NULL; /* ce reached */
		if(dname_is_root(nm))
			return NULL; /* cannot go up */
		dname_remove_label(&nm, &nmlen);
	}
	return node;
}

/** domain is not exact, find first candidate ce (name that matches
 * a part of qname) in tree */
static struct auth_data*
az_find_candidate_ce(struct auth_zone* z, struct query_info* qinfo,
	struct auth_data* n)
{
	uint8_t* nm;
	size_t nmlen;
	if(n) {
		nm = dname_get_shared_topdomain(qinfo->qname, n->name);
	} else {
		nm = qinfo->qname;
	}
	dname_count_size_labels(nm, &nmlen);
	n = az_find_name(z, nm, nmlen);
	/* delete labels and go up on name */
	while(!n) {
		if(dname_is_root(nm))
			return NULL; /* cannot go up */
		dname_remove_label(&nm, &nmlen);
		n = az_find_name(z, nm, nmlen);
	}
	return n;
}

/** go up the auth tree to next existing name. */
static struct auth_data*
az_domain_go_up(struct auth_zone* z, struct auth_data* n)
{
	uint8_t* nm = n->name;
	size_t nmlen = n->namelen;
	while(!dname_is_root(nm)) {
		dname_remove_label(&nm, &nmlen);
		if((n=az_find_name(z, nm, nmlen)) != NULL)
			return n;
	}
	return NULL;
}

/** Find the closest encloser, an name that exists and is above the
 * qname.
 * return true if the node (param node) is existing, nonobscured and
 * 	can be used to generate answers from.  It is then also node_exact.
 * returns false if the node is not good enough (or it wasn't node_exact)
 *	in this case the ce can be filled.
 *	if ce is NULL, no ce exists, and likely the zone is completely empty,
 *	not even with a zone apex.
 *	if ce is nonNULL it is the closest enclosing upper name (that exists
 *	itself for answer purposes).  That name may have DNAME, NS or wildcard
 *	rrset is the closest DNAME or NS rrset that was found.
 */
static int
az_find_ce(struct auth_zone* z, struct query_info* qinfo,
	struct auth_data* node, int node_exact, struct auth_data** ce,
	struct auth_rrset** rrset)
{
	struct auth_data* n = node;
	*ce = NULL;
	*rrset = NULL;
	if(!node_exact) {
		/* if not exact, lookup closest exact match */
		n = az_find_candidate_ce(z, qinfo, n);
	} else {
		/* if exact, the node itself is the first candidate ce */
		*ce = n;
	}

	/* no direct answer from nsec3-only domains */
	if(n && domain_has_only_nsec3(n)) {
		node_exact = 0;
		*ce = NULL;
	}

	/* with exact matches, walk up the labels until we find the
	 * delegation, or DNAME or zone end */
	while(n) {
		/* see if the current candidate has issues */
		/* not zone apex and has type NS */
		if(n->namelen != z->namelen &&
			(*rrset=az_domain_rrset(n, LDNS_RR_TYPE_NS)) &&
			/* delegate here, but DS at exact the dp has notype */
			(qinfo->qtype != LDNS_RR_TYPE_DS || 
			n->namelen != qinfo->qname_len)) {
			/* referral */
			/* this is ce and the lowernode is nonexisting */
			*ce = n;
			return 0;
		}
		/* not equal to qname and has type DNAME */
		if(n->namelen != qinfo->qname_len &&
			(*rrset=az_domain_rrset(n, LDNS_RR_TYPE_DNAME))) {
			/* this is ce and the lowernode is nonexisting */
			*ce = n;
			return 0;
		}

		if(*ce == NULL && !domain_has_only_nsec3(n)) {
			/* if not found yet, this exact name must be
			 * our lowest match (but not nsec3onlydomain) */
			*ce = n;
		}

		/* walk up the tree by removing labels from name and lookup */
		n = az_domain_go_up(z, n);
	}
	/* found no problems, if it was an exact node, it is fine to use */
	return node_exact;
}

/** add additional A/AAAA from domain names in rrset rdata (+offset)
 * offset is number of bytes in rdata where the dname is located. */
static int
az_add_additionals_from(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_rrset* rrset, size_t offset)
{
	struct packed_rrset_data* d = rrset->data;
	size_t i;
	if(!d) return 0;
	for(i=0; i<d->count; i++) {
		size_t dlen;
		struct auth_data* domain;
		struct auth_rrset* ref;
		if(d->rr_len[i] < 2+offset)
			continue; /* too short */
		if(!(dlen = dname_valid(d->rr_data[i]+2+offset,
			d->rr_len[i]-2-offset)))
			continue; /* malformed */
		domain = az_find_name(z, d->rr_data[i]+2+offset, dlen);
		if(!domain)
			continue;
		if((ref=az_domain_rrset(domain, LDNS_RR_TYPE_A)) != NULL) {
			if(!msg_add_rrset_ar(z, region, msg, domain, ref))
				return 0;
		}
		if((ref=az_domain_rrset(domain, LDNS_RR_TYPE_AAAA)) != NULL) {
			if(!msg_add_rrset_ar(z, region, msg, domain, ref))
				return 0;
		}
	}
	return 1;
}

/** add negative SOA record (with negative TTL) */
static int
az_add_negative_soa(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg)
{
	uint32_t minimum;
	struct packed_rrset_data* d;
	struct auth_rrset* soa;
	struct auth_data* apex = az_find_name(z, z->name, z->namelen);
	if(!apex) return 0;
	soa = az_domain_rrset(apex, LDNS_RR_TYPE_SOA);
	if(!soa) return 0;
	/* must be first to put in message; we want to fix the TTL with
	 * one RRset here, otherwise we'd need to loop over the RRs to get
	 * the resulting lower TTL */
	log_assert(msg->rep->rrset_count == 0);
	if(!msg_add_rrset_ns(z, region, msg, apex, soa)) return 0;
	/* fixup TTL */
	d = (struct packed_rrset_data*)msg->rep->rrsets[msg->rep->rrset_count-1]->entry.data;
	/* last 4 bytes are minimum ttl in network format */
	if(d->count == 0) return 0;
	if(d->rr_len[0] < 2+4) return 0;
	minimum = sldns_read_uint32(d->rr_data[0]+(d->rr_len[0]-4));
	d->ttl = (time_t)minimum;
	d->rr_ttl[0] = (time_t)minimum;
	msg->rep->ttl = get_rrset_ttl(msg->rep->rrsets[0]);
	msg->rep->prefetch_ttl = PREFETCH_TTL_CALC(msg->rep->ttl);
	msg->rep->serve_expired_ttl = msg->rep->ttl + SERVE_EXPIRED_TTL;
	return 1;
}

/** See if the query goes to empty nonterminal (that has no auth_data,
 * but there are nodes underneath.  We already checked that there are
 * not NS, or DNAME above, so that we only need to check if some node
 * exists below (with nonempty rr list), return true if emptynonterminal */
static int
az_empty_nonterminal(struct auth_zone* z, struct query_info* qinfo,
	struct auth_data* node)
{
	struct auth_data* next;
	if(!node) {
		/* no smaller was found, use first (smallest) node as the
		 * next one */
		next = (struct auth_data*)rbtree_first(&z->data);
	} else {
		next = (struct auth_data*)rbtree_next(&node->node);
	}
	while(next && (rbnode_type*)next != RBTREE_NULL && next->rrsets == NULL) {
		/* the next name has empty rrsets, is an empty nonterminal
		 * itself, see if there exists something below it */
		next = (struct auth_data*)rbtree_next(&node->node);
	}
	if((rbnode_type*)next == RBTREE_NULL || !next) {
		/* there is no next node, so something below it cannot
		 * exist */
		return 0;
	}
	/* a next node exists, if there was something below the query,
	 * this node has to be it.  See if it is below the query name */
	if(dname_strict_subdomain_c(next->name, qinfo->qname))
		return 1;
	return 0;
}

/** create synth cname target name in buffer, or fail if too long */
static size_t
synth_cname_buf(uint8_t* qname, size_t qname_len, size_t dname_len,
	uint8_t* dtarg, size_t dtarglen, uint8_t* buf, size_t buflen)
{
	size_t newlen = qname_len + dtarglen - dname_len;
	if(newlen > buflen) {
		/* YXDOMAIN error */
		return 0;
	}
	/* new name is concatenation of qname front (without DNAME owner)
	 * and DNAME target name */
	memcpy(buf, qname, qname_len-dname_len);
	memmove(buf+(qname_len-dname_len), dtarg, dtarglen);
	return newlen;
}

/** create synthetic CNAME rrset for in a DNAME answer in region,
 * false on alloc failure, cname==NULL when name too long. */
static int
create_synth_cname(uint8_t* qname, size_t qname_len, struct regional* region,
	struct auth_data* node, struct auth_rrset* dname, uint16_t dclass,
	struct ub_packed_rrset_key** cname)
{
	uint8_t buf[LDNS_MAX_DOMAINLEN];
	uint8_t* dtarg;
	size_t dtarglen, newlen;
	struct packed_rrset_data* d;

	/* get DNAME target name */
	if(dname->data->count < 1) return 0;
	if(dname->data->rr_len[0] < 3) return 0; /* at least rdatalen +1 */
	dtarg = dname->data->rr_data[0]+2;
	dtarglen = dname->data->rr_len[0]-2;
	if(sldns_read_uint16(dname->data->rr_data[0]) != dtarglen)
		return 0; /* rdatalen in DNAME rdata is malformed */
	if(dname_valid(dtarg, dtarglen) != dtarglen)
		return 0; /* DNAME RR has malformed rdata */

	/* synthesize a CNAME */
	newlen = synth_cname_buf(qname, qname_len, node->namelen,
		dtarg, dtarglen, buf, sizeof(buf));
	if(newlen == 0) {
		/* YXDOMAIN error */
		*cname = NULL;
		return 1;
	}
	*cname = (struct ub_packed_rrset_key*)regional_alloc(region,
		sizeof(struct ub_packed_rrset_key));
	if(!*cname)
		return 0; /* out of memory */
	memset(&(*cname)->entry, 0, sizeof((*cname)->entry));
	(*cname)->entry.key = (*cname);
	(*cname)->rk.type = htons(LDNS_RR_TYPE_CNAME);
	(*cname)->rk.rrset_class = htons(dclass);
	(*cname)->rk.flags = 0;
	(*cname)->rk.dname = regional_alloc_init(region, qname, qname_len);
	if(!(*cname)->rk.dname)
		return 0; /* out of memory */
	(*cname)->rk.dname_len = qname_len;
	(*cname)->entry.hash = rrset_key_hash(&(*cname)->rk);
	d = (struct packed_rrset_data*)regional_alloc_zero(region,
		sizeof(struct packed_rrset_data) + sizeof(size_t) +
		sizeof(uint8_t*) + sizeof(time_t) + sizeof(uint16_t)
		+ newlen);
	if(!d)
		return 0; /* out of memory */
	(*cname)->entry.data = d;
	d->ttl = 0; /* 0 for synthesized CNAME TTL */
	d->count = 1;
	d->rrsig_count = 0;
	d->trust = rrset_trust_ans_noAA;
	d->rr_len = (size_t*)((uint8_t*)d +
		sizeof(struct packed_rrset_data));
	d->rr_len[0] = newlen + sizeof(uint16_t);
	packed_rrset_ptr_fixup(d);
	d->rr_ttl[0] = d->ttl;
	sldns_write_uint16(d->rr_data[0], newlen);
	memmove(d->rr_data[0] + sizeof(uint16_t), buf, newlen);
	return 1;
}

/** add a synthesized CNAME to the answer section */
static int
add_synth_cname(struct auth_zone* z, uint8_t* qname, size_t qname_len,
	struct regional* region, struct dns_msg* msg, struct auth_data* dname,
	struct auth_rrset* rrset)
{
	struct ub_packed_rrset_key* cname;
	/* synthesize a CNAME */
	if(!create_synth_cname(qname, qname_len, region, dname, rrset,
		z->dclass, &cname)) {
		/* out of memory */
		return 0;
	}
	if(!cname) {
		/* cname cannot be create because of YXDOMAIN */
		msg->rep->flags |= LDNS_RCODE_YXDOMAIN;
		return 1;
	}
	/* add cname to message */
	if(!msg_grow_array(region, msg))
		return 0;
	msg->rep->rrsets[msg->rep->rrset_count] = cname;
	msg->rep->rrset_count++;
	msg->rep->an_numrrsets++;
	msg_ttl(msg);
	return 1;
}

/** Change a dname to a different one, for wildcard namechange */
static void
az_change_dnames(struct dns_msg* msg, uint8_t* oldname, uint8_t* newname,
	size_t newlen, int an_only)
{
	size_t i;
	size_t start = 0, end = msg->rep->rrset_count;
	if(!an_only) start = msg->rep->an_numrrsets;
	if(an_only) end = msg->rep->an_numrrsets;
	for(i=start; i<end; i++) {
		/* allocated in region so we can change the ptrs */
		if(query_dname_compare(msg->rep->rrsets[i]->rk.dname, oldname)
			== 0) {
			msg->rep->rrsets[i]->rk.dname = newname;
			msg->rep->rrsets[i]->rk.dname_len = newlen;
		}
	}
}

/** find NSEC record covering the query */
static struct auth_rrset*
az_find_nsec_cover(struct auth_zone* z, struct auth_data** node)
{
	uint8_t* nm = (*node)->name;
	size_t nmlen = (*node)->namelen;
	struct auth_rrset* rrset;
	/* find the NSEC for the smallest-or-equal node */
	/* if node == NULL, we did not find a smaller name.  But the zone
	 * name is the smallest name and should have an NSEC. So there is
	 * no NSEC to return (for a properly signed zone) */
	/* for empty nonterminals, the auth-data node should not exist,
	 * and thus we don't need to go rbtree_previous here to find
	 * a domain with an NSEC record */
	/* but there could be glue, and if this is node, then it has no NSEC.
	 * Go up to find nonglue (previous) NSEC-holding nodes */
	while((rrset=az_domain_rrset(*node, LDNS_RR_TYPE_NSEC)) == NULL) {
		if(dname_is_root(nm)) return NULL;
		if(nmlen == z->namelen) return NULL;
		dname_remove_label(&nm, &nmlen);
		/* adjust *node for the nsec rrset to find in */
		*node = az_find_name(z, nm, nmlen);
	}
	return rrset;
}

/** Find NSEC and add for wildcard denial */
static int
az_nsec_wildcard_denial(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, uint8_t* cenm, size_t cenmlen)
{
	struct query_info qinfo;
	int node_exact;
	struct auth_data* node;
	struct auth_rrset* nsec;
	uint8_t wc[LDNS_MAX_DOMAINLEN];
	if(cenmlen+2 > sizeof(wc))
		return 0; /* result would be too long */
	wc[0] = 1; /* length of wildcard label */
	wc[1] = (uint8_t)'*'; /* wildcard label */
	memmove(wc+2, cenm, cenmlen);

	/* we have '*.ce' in wc wildcard name buffer */
	/* get nsec cover for that */
	qinfo.qname = wc;
	qinfo.qname_len = cenmlen+2;
	qinfo.qtype = 0;
	qinfo.qclass = 0;
	az_find_domain(z, &qinfo, &node_exact, &node);
	if((nsec=az_find_nsec_cover(z, &node)) != NULL) {
		if(!msg_add_rrset_ns(z, region, msg, node, nsec)) return 0;
	}
	return 1;
}

/** Find the NSEC3PARAM rrset (if any) and if true you have the parameters */
static int
az_nsec3_param(struct auth_zone* z, int* algo, size_t* iter, uint8_t** salt,
	size_t* saltlen)
{
	struct auth_data* apex;
	struct auth_rrset* param;
	size_t i;
	apex = az_find_name(z, z->name, z->namelen);
	if(!apex) return 0;
	param = az_domain_rrset(apex, LDNS_RR_TYPE_NSEC3PARAM);
	if(!param || param->data->count==0)
		return 0; /* no RRset or no RRs in rrset */
	/* find out which NSEC3PARAM RR has supported parameters */
	/* skip unknown flags (dynamic signer is recalculating nsec3 chain) */
	for(i=0; i<param->data->count; i++) {
		uint8_t* rdata = param->data->rr_data[i]+2;
		size_t rdatalen = param->data->rr_len[i];
		if(rdatalen < 2+5)
			continue; /* too short */
		if(!nsec3_hash_algo_size_supported((int)(rdata[0])))
			continue; /* unsupported algo */
		if(rdatalen < (size_t)(2+5+(size_t)rdata[4]))
			continue; /* salt missing */
		if((rdata[1]&NSEC3_UNKNOWN_FLAGS)!=0)
			continue; /* unknown flags */
		*algo = (int)(rdata[0]);
		*iter = sldns_read_uint16(rdata+2);
		*saltlen = rdata[4];
		if(*saltlen == 0)
			*salt = NULL;
		else	*salt = rdata+5;
		return 1;
	}
	/* no supported params */
	return 0;
}

/** Hash a name with nsec3param into buffer, it has zone name appended.
 * return length of hash */
static size_t
az_nsec3_hash(uint8_t* buf, size_t buflen, uint8_t* nm, size_t nmlen,
	int algo, size_t iter, uint8_t* salt, size_t saltlen)
{
	size_t hlen = nsec3_hash_algo_size_supported(algo);
	/* buffer has domain name, nsec3hash, and 256 is for max saltlen
	 * (salt has 0-255 length) */
	unsigned char p[LDNS_MAX_DOMAINLEN+1+N3HASHBUFLEN+256];
	size_t i;
	if(nmlen+saltlen > sizeof(p) || hlen+saltlen > sizeof(p))
		return 0;
	if(hlen > buflen)
		return 0; /* somehow too large for destination buffer */
	/* hashfunc(name, salt) */
	memmove(p, nm, nmlen);
	query_dname_tolower(p);
	memmove(p+nmlen, salt, saltlen);
	(void)secalgo_nsec3_hash(algo, p, nmlen+saltlen, (unsigned char*)buf);
	for(i=0; i<iter; i++) {
		/* hashfunc(hash, salt) */
		memmove(p, buf, hlen);
		memmove(p+hlen, salt, saltlen);
		(void)secalgo_nsec3_hash(algo, p, hlen+saltlen,
			(unsigned char*)buf);
	}
	return hlen;
}

/** Hash name and return b32encoded hashname for lookup, zone name appended */
static int
az_nsec3_hashname(struct auth_zone* z, uint8_t* hashname, size_t* hashnmlen,
	uint8_t* nm, size_t nmlen, int algo, size_t iter, uint8_t* salt,
	size_t saltlen)
{
	uint8_t hash[N3HASHBUFLEN];
	size_t hlen;
	int ret;
	hlen = az_nsec3_hash(hash, sizeof(hash), nm, nmlen, algo, iter,
		salt, saltlen);
	if(!hlen) return 0;
	/* b32 encode */
	if(*hashnmlen < hlen*2+1+z->namelen) /* approx b32 as hexb16 */
		return 0;
	ret = sldns_b32_ntop_extended_hex(hash, hlen, (char*)(hashname+1),
		(*hashnmlen)-1);
	if(ret<1)
		return 0;
	hashname[0] = (uint8_t)ret;
	ret++;
	if((*hashnmlen) - ret < z->namelen)
		return 0;
	memmove(hashname+ret, z->name, z->namelen);
	*hashnmlen = z->namelen+(size_t)ret;
	return 1;
}

/** Find the datanode that covers the nsec3hash-name */
static struct auth_data*
az_nsec3_findnode(struct auth_zone* z, uint8_t* hashnm, size_t hashnmlen)
{
	struct query_info qinfo;
	struct auth_data* node;
	int node_exact;
	qinfo.qclass = 0;
	qinfo.qtype = 0;
	qinfo.qname = hashnm;
	qinfo.qname_len = hashnmlen;
	/* because canonical ordering and b32 nsec3 ordering are the same.
	 * this is a good lookup to find the nsec3 name. */
	az_find_domain(z, &qinfo, &node_exact, &node);
	/* but we may have to skip non-nsec3 nodes */
	/* this may be a lot, the way to speed that up is to have a
	 * separate nsec3 tree with nsec3 nodes */
	while(node && (rbnode_type*)node != RBTREE_NULL &&
		!az_domain_rrset(node, LDNS_RR_TYPE_NSEC3)) {
		node = (struct auth_data*)rbtree_previous(&node->node);
	}
	if((rbnode_type*)node == RBTREE_NULL)
		node = NULL;
	return node;
}

/** Find cover for hashed(nm, nmlen) (or NULL) */
static struct auth_data*
az_nsec3_find_cover(struct auth_zone* z, uint8_t* nm, size_t nmlen,
	int algo, size_t iter, uint8_t* salt, size_t saltlen)
{
	struct auth_data* node;
	uint8_t hname[LDNS_MAX_DOMAINLEN];
	size_t hlen = sizeof(hname);
	if(!az_nsec3_hashname(z, hname, &hlen, nm, nmlen, algo, iter,
		salt, saltlen))
		return NULL;
	node = az_nsec3_findnode(z, hname, hlen);
	if(node)
		return node;
	/* we did not find any, perhaps because the NSEC3 hash is before
	 * the first hash, we have to find the 'last hash' in the zone */
	node = (struct auth_data*)rbtree_last(&z->data);
	while(node && (rbnode_type*)node != RBTREE_NULL &&
		!az_domain_rrset(node, LDNS_RR_TYPE_NSEC3)) {
		node = (struct auth_data*)rbtree_previous(&node->node);
	}
	if((rbnode_type*)node == RBTREE_NULL)
		node = NULL;
	return node;
}

/** Find exact match for hashed(nm, nmlen) NSEC3 record or NULL */
static struct auth_data*
az_nsec3_find_exact(struct auth_zone* z, uint8_t* nm, size_t nmlen,
	int algo, size_t iter, uint8_t* salt, size_t saltlen)
{
	struct auth_data* node;
	uint8_t hname[LDNS_MAX_DOMAINLEN];
	size_t hlen = sizeof(hname);
	if(!az_nsec3_hashname(z, hname, &hlen, nm, nmlen, algo, iter,
		salt, saltlen))
		return NULL;
	node = az_find_name(z, hname, hlen);
	if(az_domain_rrset(node, LDNS_RR_TYPE_NSEC3))
		return node;
	return NULL;
}

/** Return nextcloser name (as a ref into the qname).  This is one label
 * more than the cenm (cename must be a suffix of qname) */
static void
az_nsec3_get_nextcloser(uint8_t* cenm, uint8_t* qname, size_t qname_len,
	uint8_t** nx, size_t* nxlen)
{
	int celabs = dname_count_labels(cenm);
	int qlabs = dname_count_labels(qname);
	int strip = qlabs - celabs -1;
	log_assert(dname_strict_subdomain(qname, qlabs, cenm, celabs));
	*nx = qname;
	*nxlen = qname_len;
	if(strip>0)
		dname_remove_labels(nx, nxlen, strip);
}

/** Find the closest encloser that has exact NSEC3.
 * updated cenm to the new name. If it went up no-exact-ce is true. */
static struct auth_data*
az_nsec3_find_ce(struct auth_zone* z, uint8_t** cenm, size_t* cenmlen,
	int* no_exact_ce, int algo, size_t iter, uint8_t* salt, size_t saltlen)
{
	struct auth_data* node;
	while((node = az_nsec3_find_exact(z, *cenm, *cenmlen,
		algo, iter, salt, saltlen)) == NULL) {
		if(*cenmlen == z->namelen) {
			/* next step up would take us out of the zone. fail */
			return NULL;
		}
		*no_exact_ce = 1;
		dname_remove_label(cenm, cenmlen);
	}
	return node;
}

/* Insert NSEC3 record in authority section, if NULL does nothing */
static int
az_nsec3_insert(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* node)
{
	struct auth_rrset* nsec3;
	if(!node) return 1; /* no node, skip this */
	nsec3 = az_domain_rrset(node, LDNS_RR_TYPE_NSEC3);
	if(!nsec3) return 1; /* if no nsec3 RR, skip it */
	if(!msg_add_rrset_ns(z, region, msg, node, nsec3)) return 0;
	return 1;
}

/** add NSEC3 records to the zone for the nsec3 proof.
 * Specify with the flags with parts of the proof are required.
 * the ce is the exact matching name (for notype) but also delegation points.
 * qname is the one where the nextcloser name can be derived from.
 * If NSEC3 is not properly there (in the zone) nothing is added.
 * always enabled: include nsec3 proving about the Closest Encloser.
 * 	that is an exact match that should exist for it.
 * 	If that does not exist, a higher exact match + nxproof is enabled
 * 	(for some sort of opt-out empty nonterminal cases).
 * nxproof: include denial of the qname.
 * wcproof: include denial of wildcard (wildcard.ce).
 */
static int
az_add_nsec3_proof(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, uint8_t* cenm, size_t cenmlen, uint8_t* qname,
	size_t qname_len, int nxproof, int wcproof)
{
	int algo;
	size_t iter, saltlen;
	uint8_t* salt;
	int no_exact_ce = 0;
	struct auth_data* node;

	/* find parameters of nsec3 proof */
	if(!az_nsec3_param(z, &algo, &iter, &salt, &saltlen))
		return 1; /* no nsec3 */
	/* find ce that has an NSEC3 */
	node = az_nsec3_find_ce(z, &cenm, &cenmlen, &no_exact_ce,
		algo, iter, salt, saltlen);
	if(no_exact_ce) nxproof = 1;
	if(!az_nsec3_insert(z, region, msg, node))
		return 0;

	if(nxproof) {
		uint8_t* nx;
		size_t nxlen;
		/* create nextcloser domain name */
		az_nsec3_get_nextcloser(cenm, qname, qname_len, &nx, &nxlen);
		/* find nsec3 that matches or covers it */
		node = az_nsec3_find_cover(z, nx, nxlen, algo, iter, salt,
			saltlen);
		if(!az_nsec3_insert(z, region, msg, node))
			return 0;
	}
	if(wcproof) {
		/* create wildcard name *.ce */
		uint8_t wc[LDNS_MAX_DOMAINLEN];
		size_t wclen;
		if(cenmlen+2 > sizeof(wc))
			return 0; /* result would be too long */
		wc[0] = 1; /* length of wildcard label */
		wc[1] = (uint8_t)'*'; /* wildcard label */
		memmove(wc+2, cenm, cenmlen);
		wclen = cenmlen+2;
		/* find nsec3 that matches or covers it */
		node = az_nsec3_find_cover(z, wc, wclen, algo, iter, salt,
			saltlen);
		if(!az_nsec3_insert(z, region, msg, node))
			return 0;
	}
	return 1;
}

/** generate answer for positive answer */
static int
az_generate_positive_answer(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* node, struct auth_rrset* rrset)
{
	if(!msg_add_rrset_an(z, region, msg, node, rrset)) return 0;
	/* see if we want additional rrs */
	if(rrset->type == LDNS_RR_TYPE_MX) {
		if(!az_add_additionals_from(z, region, msg, rrset, 2))
			return 0;
	} else if(rrset->type == LDNS_RR_TYPE_SRV) {
		if(!az_add_additionals_from(z, region, msg, rrset, 6))
			return 0;
	} else if(rrset->type == LDNS_RR_TYPE_NS) {
		if(!az_add_additionals_from(z, region, msg, rrset, 0))
			return 0;
	}
	return 1;
}

/** generate answer for type ANY answer */
static int
az_generate_any_answer(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* node)
{
	struct auth_rrset* rrset;
	int added = 0;
	/* add a couple (at least one) RRs */
	if((rrset=az_domain_rrset(node, LDNS_RR_TYPE_SOA)) != NULL) {
		if(!msg_add_rrset_an(z, region, msg, node, rrset)) return 0;
		added++;
	}
	if((rrset=az_domain_rrset(node, LDNS_RR_TYPE_MX)) != NULL) {
		if(!msg_add_rrset_an(z, region, msg, node, rrset)) return 0;
		added++;
	}
	if((rrset=az_domain_rrset(node, LDNS_RR_TYPE_A)) != NULL) {
		if(!msg_add_rrset_an(z, region, msg, node, rrset)) return 0;
		added++;
	}
	if((rrset=az_domain_rrset(node, LDNS_RR_TYPE_AAAA)) != NULL) {
		if(!msg_add_rrset_an(z, region, msg, node, rrset)) return 0;
		added++;
	}
	if(added == 0 && node->rrsets) {
		if(!msg_add_rrset_an(z, region, msg, node,
			node->rrsets)) return 0;
	}
	return 1;
}

/** follow cname chain and add more data to the answer section */
static int
follow_cname_chain(struct auth_zone* z, uint16_t qtype,
	struct regional* region, struct dns_msg* msg,
	struct packed_rrset_data* d)
{
	int maxchain = 0;
	/* see if we can add the target of the CNAME into the answer */
	while(maxchain++ < MAX_CNAME_CHAIN) {
		struct auth_data* node;
		struct auth_rrset* rrset;
		size_t clen;
		/* d has cname rdata */
		if(d->count == 0) break; /* no CNAME */
		if(d->rr_len[0] < 2+1) break; /* too small */
		if((clen=dname_valid(d->rr_data[0]+2, d->rr_len[0]-2))==0)
			break; /* malformed */
		if(!dname_subdomain_c(d->rr_data[0]+2, z->name))
			break; /* target out of zone */
		if((node = az_find_name(z, d->rr_data[0]+2, clen))==NULL)
			break; /* no such target name */
		if((rrset=az_domain_rrset(node, qtype))!=NULL) {
			/* done we found the target */
			if(!msg_add_rrset_an(z, region, msg, node, rrset))
				return 0;
			break;
		}
		if((rrset=az_domain_rrset(node, LDNS_RR_TYPE_CNAME))==NULL)
			break; /* no further CNAME chain, notype */
		if(!msg_add_rrset_an(z, region, msg, node, rrset)) return 0;
		d = rrset->data;
	}
	return 1;
}

/** generate answer for cname answer */
static int
az_generate_cname_answer(struct auth_zone* z, struct query_info* qinfo,
	struct regional* region, struct dns_msg* msg,
	struct auth_data* node, struct auth_rrset* rrset)
{
	if(!msg_add_rrset_an(z, region, msg, node, rrset)) return 0;
	if(!rrset) return 1;
	if(!follow_cname_chain(z, qinfo->qtype, region, msg, rrset->data))
		return 0;
	return 1;
}

/** generate answer for notype answer */
static int
az_generate_notype_answer(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* node)
{
	struct auth_rrset* rrset;
	if(!az_add_negative_soa(z, region, msg)) return 0;
	/* DNSSEC denial NSEC */
	if((rrset=az_domain_rrset(node, LDNS_RR_TYPE_NSEC))!=NULL) {
		if(!msg_add_rrset_ns(z, region, msg, node, rrset)) return 0;
	} else if(node) {
		/* DNSSEC denial NSEC3 */
		if(!az_add_nsec3_proof(z, region, msg, node->name,
			node->namelen, msg->qinfo.qname,
			msg->qinfo.qname_len, 0, 0))
			return 0;
	}
	return 1;
}

/** generate answer for referral answer */
static int
az_generate_referral_answer(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* ce, struct auth_rrset* rrset)
{
	struct auth_rrset* ds, *nsec;
	/* turn off AA flag, referral is nonAA because it leaves the zone */
	log_assert(ce);
	msg->rep->flags &= ~BIT_AA;
	if(!msg_add_rrset_ns(z, region, msg, ce, rrset)) return 0;
	/* add DS or deny it */
	if((ds=az_domain_rrset(ce, LDNS_RR_TYPE_DS))!=NULL) {
		if(!msg_add_rrset_ns(z, region, msg, ce, ds)) return 0;
	} else {
		/* deny the DS */
		if((nsec=az_domain_rrset(ce, LDNS_RR_TYPE_NSEC))!=NULL) {
			if(!msg_add_rrset_ns(z, region, msg, ce, nsec))
				return 0;
		} else {
			if(!az_add_nsec3_proof(z, region, msg, ce->name,
				ce->namelen, msg->qinfo.qname,
				msg->qinfo.qname_len, 0, 0))
				return 0;
		}
	}
	/* add additional rrs for type NS */
	if(!az_add_additionals_from(z, region, msg, rrset, 0)) return 0;
	return 1;
}

/** generate answer for DNAME answer */
static int
az_generate_dname_answer(struct auth_zone* z, struct query_info* qinfo,
	struct regional* region, struct dns_msg* msg, struct auth_data* ce,
	struct auth_rrset* rrset)
{
	log_assert(ce);
	/* add the DNAME and then a CNAME */
	if(!msg_add_rrset_an(z, region, msg, ce, rrset)) return 0;
	if(!add_synth_cname(z, qinfo->qname, qinfo->qname_len, region,
		msg, ce, rrset)) return 0;
	if(FLAGS_GET_RCODE(msg->rep->flags) == LDNS_RCODE_YXDOMAIN)
		return 1;
	if(msg->rep->rrset_count == 0 ||
		!msg->rep->rrsets[msg->rep->rrset_count-1])
		return 0;
	if(!follow_cname_chain(z, qinfo->qtype, region, msg, 
		(struct packed_rrset_data*)msg->rep->rrsets[
		msg->rep->rrset_count-1]->entry.data))
		return 0;
	return 1;
}

/** generate answer for wildcard answer */
static int
az_generate_wildcard_answer(struct auth_zone* z, struct query_info* qinfo,
	struct regional* region, struct dns_msg* msg, struct auth_data* ce,
	struct auth_data* wildcard, struct auth_data* node)
{
	struct auth_rrset* rrset, *nsec;
	if((rrset=az_domain_rrset(wildcard, qinfo->qtype)) != NULL) {
		/* wildcard has type, add it */
		if(!msg_add_rrset_an(z, region, msg, wildcard, rrset))
			return 0;
		az_change_dnames(msg, wildcard->name, msg->qinfo.qname,
			msg->qinfo.qname_len, 1);
	} else if((rrset=az_domain_rrset(wildcard, LDNS_RR_TYPE_CNAME))!=NULL) {
		/* wildcard has cname instead, do that */
		if(!msg_add_rrset_an(z, region, msg, wildcard, rrset))
			return 0;
		az_change_dnames(msg, wildcard->name, msg->qinfo.qname,
			msg->qinfo.qname_len, 1);
		if(!follow_cname_chain(z, qinfo->qtype, region, msg,
			rrset->data))
			return 0;
	} else if(qinfo->qtype == LDNS_RR_TYPE_ANY && wildcard->rrsets) {
		/* add ANY rrsets from wildcard node */
		if(!az_generate_any_answer(z, region, msg, wildcard))
			return 0;
		az_change_dnames(msg, wildcard->name, msg->qinfo.qname,
			msg->qinfo.qname_len, 1);
	} else {
		/* wildcard has nodata, notype answer */
		/* call other notype routine for dnssec notype denials */
		if(!az_generate_notype_answer(z, region, msg, wildcard))
			return 0;
	}

	/* ce and node for dnssec denial of wildcard original name */
	if((nsec=az_find_nsec_cover(z, &node)) != NULL) {
		if(!msg_add_rrset_ns(z, region, msg, node, nsec)) return 0;
	} else if(ce) {
		if(!az_add_nsec3_proof(z, region, msg, ce->name,
			ce->namelen, msg->qinfo.qname,
			msg->qinfo.qname_len, 1, 0))
			return 0;
	}

	/* fixup name of wildcard from *.zone to qname, use already allocated
	 * pointer to msg qname */
	az_change_dnames(msg, wildcard->name, msg->qinfo.qname,
		msg->qinfo.qname_len, 0);
	return 1;
}

/** generate answer for nxdomain answer */
static int
az_generate_nxdomain_answer(struct auth_zone* z, struct regional* region,
	struct dns_msg* msg, struct auth_data* ce, struct auth_data* node)
{
	struct auth_rrset* nsec;
	msg->rep->flags |= LDNS_RCODE_NXDOMAIN;
	if(!az_add_negative_soa(z, region, msg)) return 0;
	if((nsec=az_find_nsec_cover(z, &node)) != NULL) {
		if(!msg_add_rrset_ns(z, region, msg, node, nsec)) return 0;
		if(ce && !az_nsec_wildcard_denial(z, region, msg, ce->name,
			ce->namelen)) return 0;
	} else if(ce) {
		if(!az_add_nsec3_proof(z, region, msg, ce->name,
			ce->namelen, msg->qinfo.qname,
			msg->qinfo.qname_len, 1, 1))
			return 0;
	}
	return 1;
}

/** Create answers when an exact match exists for the domain name */
static int
az_generate_answer_with_node(struct auth_zone* z, struct query_info* qinfo,
	struct regional* region, struct dns_msg* msg, struct auth_data* node)
{
	struct auth_rrset* rrset;
	/* positive answer, rrset we are looking for exists */
	if((rrset=az_domain_rrset(node, qinfo->qtype)) != NULL) {
		return az_generate_positive_answer(z, region, msg, node, rrset);
	}
	/* CNAME? */
	if((rrset=az_domain_rrset(node, LDNS_RR_TYPE_CNAME)) != NULL) {
		return az_generate_cname_answer(z, qinfo, region, msg,
			node, rrset);
	}
	/* type ANY ? */
	if(qinfo->qtype == LDNS_RR_TYPE_ANY) {
		return az_generate_any_answer(z, region, msg, node);
	}
	/* NOERROR/NODATA (no such type at domain name) */
	return az_generate_notype_answer(z, region, msg, node);
}

/** Generate answer without an existing-node that we can use.
 * So it'll be a referral, DNAME or nxdomain */
static int
az_generate_answer_nonexistnode(struct auth_zone* z, struct query_info* qinfo,
	struct regional* region, struct dns_msg* msg, struct auth_data* ce,
	struct auth_rrset* rrset, struct auth_data* node)
{
	struct auth_data* wildcard;

	/* we do not have an exact matching name (that exists) */
	/* see if we have a NS or DNAME in the ce */
	if(ce && rrset && rrset->type == LDNS_RR_TYPE_NS) {
		return az_generate_referral_answer(z, region, msg, ce, rrset);
	}
	if(ce && rrset && rrset->type == LDNS_RR_TYPE_DNAME) {
		return az_generate_dname_answer(z, qinfo, region, msg, ce,
			rrset);
	}
	/* if there is an empty nonterminal, wildcard and nxdomain don't
	 * happen, it is a notype answer */
	if(az_empty_nonterminal(z, qinfo, node)) {
		return az_generate_notype_answer(z, region, msg, node);
	}
	/* see if we have a wildcard under the ce */
	if((wildcard=az_find_wildcard(z, qinfo, ce)) != NULL) {
		return az_generate_wildcard_answer(z, qinfo, region, msg,
			ce, wildcard, node);
	}
	/* generate nxdomain answer */
	return az_generate_nxdomain_answer(z, region, msg, ce, node);
}

/** Lookup answer in a zone. */
static int
auth_zone_generate_answer(struct auth_zone* z, struct query_info* qinfo,
	struct regional* region, struct dns_msg** msg, int* fallback)
{
	struct auth_data* node, *ce;
	struct auth_rrset* rrset;
	int node_exact, node_exists;
	/* does the zone want fallback in case of failure? */
	*fallback = z->fallback_enabled;
	if(!(*msg=msg_create(region, qinfo))) return 0;

	/* lookup if there is a matching domain name for the query */
	az_find_domain(z, qinfo, &node_exact, &node);

	/* see if node exists for generating answers from (i.e. not glue and
	 * obscured by NS or DNAME or NSEC3-only), and also return the
	 * closest-encloser from that, closest node that should be used
	 * to generate answers from that is above the query */
	node_exists = az_find_ce(z, qinfo, node, node_exact, &ce, &rrset);

	if(verbosity >= VERB_ALGO) {
		char zname[256], qname[256], nname[256], cename[256],
			tpstr[32], rrstr[32];
		sldns_wire2str_dname_buf(qinfo->qname, qinfo->qname_len, qname,
			sizeof(qname));
		sldns_wire2str_type_buf(qinfo->qtype, tpstr, sizeof(tpstr));
		sldns_wire2str_dname_buf(z->name, z->namelen, zname,
			sizeof(zname));
		if(node)
			sldns_wire2str_dname_buf(node->name, node->namelen,
				nname, sizeof(nname));
		else	snprintf(nname, sizeof(nname), "NULL");
		if(ce)
			sldns_wire2str_dname_buf(ce->name, ce->namelen,
				cename, sizeof(cename));
		else	snprintf(cename, sizeof(cename), "NULL");
		if(rrset) sldns_wire2str_type_buf(rrset->type, rrstr,
			sizeof(rrstr));
		else	snprintf(rrstr, sizeof(rrstr), "NULL");
		log_info("auth_zone %s query %s %s, domain %s %s %s, "
			"ce %s, rrset %s", zname, qname, tpstr, nname,
			(node_exact?"exact":"notexact"),
			(node_exists?"exist":"notexist"), cename, rrstr);
	}

	if(node_exists) {
		/* the node is fine, generate answer from node */
		return az_generate_answer_with_node(z, qinfo, region, *msg,
			node);
	}
	return az_generate_answer_nonexistnode(z, qinfo, region, *msg,
		ce, rrset, node);
}

int auth_zones_lookup(struct auth_zones* az, struct query_info* qinfo,
	struct regional* region, struct dns_msg** msg, int* fallback,
	uint8_t* dp_nm, size_t dp_nmlen)
{
	int r;
	struct auth_zone* z;
	/* find the zone that should contain the answer. */
	lock_rw_rdlock(&az->lock);
	z = auth_zone_find(az, dp_nm, dp_nmlen, qinfo->qclass);
	if(!z) {
		lock_rw_unlock(&az->lock);
		/* no auth zone, fallback to internet */
		*fallback = 1;
		return 0;
	}
	lock_rw_rdlock(&z->lock);
	lock_rw_unlock(&az->lock);

	/* if not for upstream queries, fallback */
	if(!z->for_upstream) {
		lock_rw_unlock(&z->lock);
		*fallback = 1;
		return 0;
	}
	/* see what answer that zone would generate */
	r = auth_zone_generate_answer(z, qinfo, region, msg, fallback);
	lock_rw_unlock(&z->lock);
	return r;
}

/** encode auth answer */
static void
auth_answer_encode(struct query_info* qinfo, struct module_env* env,
	struct edns_data* edns, struct comm_reply* repinfo, sldns_buffer* buf,
	struct regional* temp, struct dns_msg* msg)
{
	uint16_t udpsize;
	udpsize = edns->udp_size;
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;

	if(!inplace_cb_reply_local_call(env, qinfo, NULL, msg->rep,
		(int)FLAGS_GET_RCODE(msg->rep->flags), edns, repinfo, temp)
		|| !reply_info_answer_encode(qinfo, msg->rep,
		*(uint16_t*)sldns_buffer_begin(buf),
		sldns_buffer_read_u16_at(buf, 2),
		buf, 0, 0, temp, udpsize, edns,
		(int)(edns->bits&EDNS_DO), 0)) {
		error_encode(buf, (LDNS_RCODE_SERVFAIL|BIT_AA), qinfo,
			*(uint16_t*)sldns_buffer_begin(buf),
			sldns_buffer_read_u16_at(buf, 2), edns);
	}
}

/** encode auth error answer */
static void
auth_error_encode(struct query_info* qinfo, struct module_env* env,
	struct edns_data* edns, struct comm_reply* repinfo, sldns_buffer* buf,
	struct regional* temp, int rcode)
{
	edns->edns_version = EDNS_ADVERTISED_VERSION;
	edns->udp_size = EDNS_ADVERTISED_SIZE;
	edns->ext_rcode = 0;
	edns->bits &= EDNS_DO;

	if(!inplace_cb_reply_local_call(env, qinfo, NULL, NULL,
		rcode, edns, repinfo, temp))
		edns->opt_list = NULL;
	error_encode(buf, rcode|BIT_AA, qinfo,
		*(uint16_t*)sldns_buffer_begin(buf),
		sldns_buffer_read_u16_at(buf, 2), edns);
}

int auth_zones_answer(struct auth_zones* az, struct module_env* env,
	struct query_info* qinfo, struct edns_data* edns,
	struct comm_reply* repinfo, struct sldns_buffer* buf, struct regional* temp)
{
	struct dns_msg* msg = NULL;
	struct auth_zone* z;
	int r;
	int fallback = 0;

	lock_rw_rdlock(&az->lock);
	if(!az->have_downstream) {
		/* no downstream auth zones */
		lock_rw_unlock(&az->lock);
		return 0;
	}
	if(qinfo->qtype == LDNS_RR_TYPE_DS) {
		uint8_t* delname = qinfo->qname;
		size_t delnamelen = qinfo->qname_len;
		dname_remove_label(&delname, &delnamelen);
		z = auth_zones_find_zone(az, delname, delnamelen,
			qinfo->qclass);
	} else {
		z = auth_zones_find_zone(az, qinfo->qname, qinfo->qname_len,
			qinfo->qclass);
	}
	if(!z) {
		/* no zone above it */
		lock_rw_unlock(&az->lock);
		return 0;
	}
	lock_rw_rdlock(&z->lock);
	lock_rw_unlock(&az->lock);
	if(!z->for_downstream) {
		lock_rw_unlock(&z->lock);
		return 0;
	}

	/* answer it from zone z */
	r = auth_zone_generate_answer(z, qinfo, temp, &msg, &fallback);
	lock_rw_unlock(&z->lock);
	if(!r && fallback) {
		/* fallback to regular answering (recursive) */
		return 0;
	}
	lock_rw_wrlock(&az->lock);
	az->num_query_down++;
	lock_rw_unlock(&az->lock);

	/* encode answer */
	if(!r)
		auth_error_encode(qinfo, env, edns, repinfo, buf, temp,
			LDNS_RCODE_SERVFAIL);
	else	auth_answer_encode(qinfo, env, edns, repinfo, buf, temp, msg);

	return 1;
}

int auth_zones_can_fallback(struct auth_zones* az, uint8_t* nm, size_t nmlen,
	uint16_t dclass)
{
	int r;
	struct auth_zone* z;
	lock_rw_rdlock(&az->lock);
	z = auth_zone_find(az, nm, nmlen, dclass);
	if(!z) {
		lock_rw_unlock(&az->lock);
		/* no such auth zone, fallback */
		return 1;
	}
	lock_rw_rdlock(&z->lock);
	lock_rw_unlock(&az->lock);
	r = z->fallback_enabled || (!z->for_upstream);
	lock_rw_unlock(&z->lock);
	return r;
}

int
auth_zone_parse_notify_serial(sldns_buffer* pkt, uint32_t *serial)
{
	struct query_info q;
	uint16_t rdlen;
	memset(&q, 0, sizeof(q));
	sldns_buffer_set_position(pkt, 0);
	if(!query_info_parse(&q, pkt)) return 0;
	if(LDNS_ANCOUNT(sldns_buffer_begin(pkt)) == 0) return 0;
	/* skip name of RR in answer section */
	if(sldns_buffer_remaining(pkt) < 1) return 0;
	if(pkt_dname_len(pkt) == 0) return 0;
	/* check type */
	if(sldns_buffer_remaining(pkt) < 10 /* type,class,ttl,rdatalen*/)
		return 0;
	if(sldns_buffer_read_u16(pkt) != LDNS_RR_TYPE_SOA) return 0;
	sldns_buffer_skip(pkt, 2); /* class */
	sldns_buffer_skip(pkt, 4); /* ttl */
	rdlen = sldns_buffer_read_u16(pkt); /* rdatalen */
	if(sldns_buffer_remaining(pkt) < rdlen) return 0;
	if(rdlen < 22) return 0; /* bad soa length */
	sldns_buffer_skip(pkt, (ssize_t)(rdlen-20));
	*serial = sldns_buffer_read_u32(pkt);
	/* return true when has serial in answer section */
	return 1;
}

/** see if addr appears in the list */
static int
addr_in_list(struct auth_addr* list, struct sockaddr_storage* addr,
	socklen_t addrlen)
{
	struct auth_addr* p;
	for(p=list; p; p=p->next) {
		if(sockaddr_cmp_addr(addr, addrlen, &p->addr, p->addrlen)==0)
			return 1;
	}
	return 0;
}

/** check if an address matches a master specification (or one of its
 * addresses in the addr list) */
static int
addr_matches_master(struct auth_master* master, struct sockaddr_storage* addr,
	socklen_t addrlen, struct auth_master** fromhost)
{
	struct sockaddr_storage a;
	socklen_t alen = 0;
	int net = 0;
	if(addr_in_list(master->list, addr, addrlen)) {
		*fromhost = master;
		return 1;	
	}
	/* compare address (but not port number, that is the destination
	 * port of the master, the port number of the received notify is
	 * allowed to by any port on that master) */
	if(extstrtoaddr(master->host, &a, &alen) &&
		sockaddr_cmp_addr(addr, addrlen, &a, alen)==0) {
		*fromhost = master;
		return 1;
	}
	/* prefixes, addr/len, like 10.0.0.0/8 */
	/* not http and has a / and there is one / */
	if(master->allow_notify && !master->http &&
		strchr(master->host, '/') != NULL &&
		strchr(master->host, '/') == strrchr(master->host, '/') &&
		netblockstrtoaddr(master->host, UNBOUND_DNS_PORT, &a, &alen,
		&net) && alen == addrlen) {
		if(addr_in_common(addr, (addr_is_ip6(addr, addrlen)?128:32),
			&a, net, alen) >= net) {
			*fromhost = NULL; /* prefix does not have destination
				to send the probe or transfer with */
			return 1; /* matches the netblock */
		}
	}
	return 0;
}

/** check access list for notifies */
static int
az_xfr_allowed_notify(struct auth_xfer* xfr, struct sockaddr_storage* addr,
	socklen_t addrlen, struct auth_master** fromhost)
{
	struct auth_master* p;
	for(p=xfr->allow_notify_list; p; p=p->next) {
		if(addr_matches_master(p, addr, addrlen, fromhost)) {
			return 1;
		}
	}
	return 0;
}

/** see if the serial means the zone has to be updated, i.e. the serial
 * is newer than the zone serial, or we have no zone */
static int
xfr_serial_means_update(struct auth_xfer* xfr, uint32_t serial)
{
	if(!xfr->have_zone)
		return 1; /* no zone, anything is better */
	if(xfr->zone_expired)
		return 1; /* expired, the sent serial is better than expired
			data */
	if(compare_serial(xfr->serial, serial) < 0)
		return 1; /* our serial is smaller than the sent serial,
			the data is newer, fetch it */
	return 0;
}

/** note notify serial, updates the notify information in the xfr struct */
static void
xfr_note_notify_serial(struct auth_xfer* xfr, int has_serial, uint32_t serial)
{
	if(xfr->notify_received && xfr->notify_has_serial && has_serial) {
		/* see if this serial is newer */
		if(compare_serial(xfr->notify_serial, serial) < 0)
			xfr->notify_serial = serial;
	} else if(xfr->notify_received && xfr->notify_has_serial &&
		!has_serial) {
		/* remove serial, we have notify without serial */
		xfr->notify_has_serial = 0;
		xfr->notify_serial = 0;
	} else if(xfr->notify_received && !xfr->notify_has_serial) {
		/* we already have notify without serial, keep it
		 * that way; no serial check when current operation
		 * is done */
	} else {
		xfr->notify_received = 1;
		xfr->notify_has_serial = has_serial;
		xfr->notify_serial = serial;
	}
}

/** process a notify serial, start new probe or note serial. xfr is locked */
static void
xfr_process_notify(struct auth_xfer* xfr, struct module_env* env,
	int has_serial, uint32_t serial, struct auth_master* fromhost)
{
	/* if the serial of notify is older than we have, don't fetch
	 * a zone, we already have it */
	if(has_serial && !xfr_serial_means_update(xfr, serial)) {
		lock_basic_unlock(&xfr->lock);
		return;
	}
	/* start new probe with this addr src, or note serial */
	if(!xfr_start_probe(xfr, env, fromhost)) {
		/* not started because already in progress, note the serial */
		xfr_note_notify_serial(xfr, has_serial, serial);
		lock_basic_unlock(&xfr->lock);
	}
	/* successful end of start_probe unlocked xfr->lock */
}

int auth_zones_notify(struct auth_zones* az, struct module_env* env,
	uint8_t* nm, size_t nmlen, uint16_t dclass,
	struct sockaddr_storage* addr, socklen_t addrlen, int has_serial,
	uint32_t serial, int* refused)
{
	struct auth_xfer* xfr;
	struct auth_master* fromhost = NULL;
	/* see which zone this is */
	lock_rw_rdlock(&az->lock);
	xfr = auth_xfer_find(az, nm, nmlen, dclass);
	if(!xfr) {
		lock_rw_unlock(&az->lock);
		/* no such zone, refuse the notify */
		*refused = 1;
		return 0;
	}
	lock_basic_lock(&xfr->lock);
	lock_rw_unlock(&az->lock);
	
	/* check access list for notifies */
	if(!az_xfr_allowed_notify(xfr, addr, addrlen, &fromhost)) {
		lock_basic_unlock(&xfr->lock);
		/* notify not allowed, refuse the notify */
		*refused = 1;
		return 0;
	}

	/* process the notify */
	xfr_process_notify(xfr, env, has_serial, serial, fromhost);
	return 1;
}

int auth_zones_startprobesequence(struct auth_zones* az,
	struct module_env* env, uint8_t* nm, size_t nmlen, uint16_t dclass)
{
	struct auth_xfer* xfr;
	lock_rw_rdlock(&az->lock);
	xfr = auth_xfer_find(az, nm, nmlen, dclass);
	if(!xfr) {
		lock_rw_unlock(&az->lock);
		return 0;
	}
	lock_basic_lock(&xfr->lock);
	lock_rw_unlock(&az->lock);

	xfr_process_notify(xfr, env, 0, 0, NULL);
	return 1;
}

/** set a zone expired */
static void
auth_xfer_set_expired(struct auth_xfer* xfr, struct module_env* env,
	int expired)
{
	struct auth_zone* z;

	/* expire xfr */
	lock_basic_lock(&xfr->lock);
	xfr->zone_expired = expired;
	lock_basic_unlock(&xfr->lock);

	/* find auth_zone */
	lock_rw_rdlock(&env->auth_zones->lock);
	z = auth_zone_find(env->auth_zones, xfr->name, xfr->namelen,
		xfr->dclass);
	if(!z) {
		lock_rw_unlock(&env->auth_zones->lock);
		return;
	}
	lock_rw_wrlock(&z->lock);
	lock_rw_unlock(&env->auth_zones->lock);

	/* expire auth_zone */
	z->zone_expired = expired;
	lock_rw_unlock(&z->lock);
}

/** find master (from notify or probe) in list of masters */
static struct auth_master*
find_master_by_host(struct auth_master* list, char* host)
{
	struct auth_master* p;
	for(p=list; p; p=p->next) {
		if(strcmp(p->host, host) == 0)
			return p;
	}
	return NULL;
}

/** delete the looked up auth_addrs for all the masters in the list */
static void
xfr_masterlist_free_addrs(struct auth_master* list)
{
	struct auth_master* m;
	for(m=list; m; m=m->next) {
		if(m->list) {
			auth_free_master_addrs(m->list);
			m->list = NULL;
		}
	}
}

/** copy a list of auth_addrs */
static struct auth_addr*
auth_addr_list_copy(struct auth_addr* source)
{
	struct auth_addr* list = NULL, *last = NULL;
	struct auth_addr* p;
	for(p=source; p; p=p->next) {
		struct auth_addr* a = (struct auth_addr*)memdup(p, sizeof(*p));
		if(!a) {
			log_err("malloc failure");
			auth_free_master_addrs(list);
			return NULL;
		}
		a->next = NULL;
		if(last) last->next = a;
		if(!list) list = a;
		last = a;
	}
	return list;
}

/** copy a master to a new structure, NULL on alloc failure */
static struct auth_master*
auth_master_copy(struct auth_master* o)
{
	struct auth_master* m;
	if(!o) return NULL;
	m = (struct auth_master*)memdup(o, sizeof(*o));
	if(!m) {
		log_err("malloc failure");
		return NULL;
	}
	m->next = NULL;
	if(m->host) {
		m->host = strdup(m->host);
		if(!m->host) {
			free(m);
			log_err("malloc failure");
			return NULL;
		}
	}
	if(m->file) {
		m->file = strdup(m->file);
		if(!m->file) {
			free(m->host);
			free(m);
			log_err("malloc failure");
			return NULL;
		}
	}
	if(m->list) {
		m->list = auth_addr_list_copy(m->list);
		if(!m->list) {
			free(m->file);
			free(m->host);
			free(m);
			return NULL;
		}
	}
	return m;
}

/** copy the master addresses from the task_probe lookups to the allow_notify
 * list of masters */
static void
probe_copy_masters_for_allow_notify(struct auth_xfer* xfr)
{
	struct auth_master* list = NULL, *last = NULL;
	struct auth_master* p;
	/* build up new list with copies */
	for(p = xfr->task_probe->masters; p; p=p->next) {
		struct auth_master* m = auth_master_copy(p);
		if(!m) {
			auth_free_masters(list);
			/* failed because of malloc failure, use old list */
			return;
		}
		m->next = NULL;
		if(last) last->next = m;
		if(!list) list = m;
		last = m;
	}
	/* success, replace list */
	auth_free_masters(xfr->allow_notify_list);
	xfr->allow_notify_list = list;
}

/** start the lookups for task_transfer */
static void
xfr_transfer_start_lookups(struct auth_xfer* xfr)
{
	/* delete all the looked up addresses in the list */
	xfr_masterlist_free_addrs(xfr->task_transfer->masters);

	/* start lookup at the first master */
	xfr->task_transfer->lookup_target = xfr->task_transfer->masters;
	xfr->task_transfer->lookup_aaaa = 0;
}

/** move to the next lookup of hostname for task_transfer */
static void
xfr_transfer_move_to_next_lookup(struct auth_xfer* xfr, struct module_env* env)
{
	if(!xfr->task_transfer->lookup_target)
		return; /* already at end of list */
	if(!xfr->task_transfer->lookup_aaaa && env->cfg->do_ip6) {
		/* move to lookup AAAA */
		xfr->task_transfer->lookup_aaaa = 1;
		return;
	}
	xfr->task_transfer->lookup_target = 
		xfr->task_transfer->lookup_target->next;
	xfr->task_transfer->lookup_aaaa = 0;
	if(!env->cfg->do_ip4 && xfr->task_transfer->lookup_target!=NULL)
		xfr->task_transfer->lookup_aaaa = 1;
}

/** start the lookups for task_probe */
static void
xfr_probe_start_lookups(struct auth_xfer* xfr)
{
	/* delete all the looked up addresses in the list */
	xfr_masterlist_free_addrs(xfr->task_probe->masters);

	/* start lookup at the first master */
	xfr->task_probe->lookup_target = xfr->task_probe->masters;
	xfr->task_probe->lookup_aaaa = 0;
}

/** move to the next lookup of hostname for task_probe */
static void
xfr_probe_move_to_next_lookup(struct auth_xfer* xfr, struct module_env* env)
{
	if(!xfr->task_probe->lookup_target)
		return; /* already at end of list */
	if(!xfr->task_probe->lookup_aaaa && env->cfg->do_ip6) {
		/* move to lookup AAAA */
		xfr->task_probe->lookup_aaaa = 1;
		return;
	}
	xfr->task_probe->lookup_target = xfr->task_probe->lookup_target->next;
	xfr->task_probe->lookup_aaaa = 0;
	if(!env->cfg->do_ip4 && xfr->task_probe->lookup_target!=NULL)
		xfr->task_probe->lookup_aaaa = 1;
}

/** start the iteration of the task_transfer list of masters */
static void
xfr_transfer_start_list(struct auth_xfer* xfr, struct auth_master* spec) 
{
	if(spec) {
		xfr->task_transfer->scan_specific = find_master_by_host(
			xfr->task_transfer->masters, spec->host);
		if(xfr->task_transfer->scan_specific) {
			xfr->task_transfer->scan_target = NULL;
			xfr->task_transfer->scan_addr = NULL;
			if(xfr->task_transfer->scan_specific->list)
				xfr->task_transfer->scan_addr =
					xfr->task_transfer->scan_specific->list;
			return;
		}
	}
	/* no specific (notified) host to scan */
	xfr->task_transfer->scan_specific = NULL;
	xfr->task_transfer->scan_addr = NULL;
	/* pick up first scan target */
	xfr->task_transfer->scan_target = xfr->task_transfer->masters;
	if(xfr->task_transfer->scan_target && xfr->task_transfer->
		scan_target->list)
		xfr->task_transfer->scan_addr =
			xfr->task_transfer->scan_target->list;
}

/** start the iteration of the task_probe list of masters */
static void
xfr_probe_start_list(struct auth_xfer* xfr, struct auth_master* spec) 
{
	if(spec) {
		xfr->task_probe->scan_specific = find_master_by_host(
			xfr->task_probe->masters, spec->host);
		if(xfr->task_probe->scan_specific) {
			xfr->task_probe->scan_target = NULL;
			xfr->task_probe->scan_addr = NULL;
			if(xfr->task_probe->scan_specific->list)
				xfr->task_probe->scan_addr =
					xfr->task_probe->scan_specific->list;
			return;
		}
	}
	/* no specific (notified) host to scan */
	xfr->task_probe->scan_specific = NULL;
	xfr->task_probe->scan_addr = NULL;
	/* pick up first scan target */
	xfr->task_probe->scan_target = xfr->task_probe->masters;
	if(xfr->task_probe->scan_target && xfr->task_probe->scan_target->list)
		xfr->task_probe->scan_addr =
			xfr->task_probe->scan_target->list;
}

/** pick up the master that is being scanned right now, task_transfer */
static struct auth_master*
xfr_transfer_current_master(struct auth_xfer* xfr)
{
	if(xfr->task_transfer->scan_specific)
		return xfr->task_transfer->scan_specific;
	return xfr->task_transfer->scan_target;
}

/** pick up the master that is being scanned right now, task_probe */
static struct auth_master*
xfr_probe_current_master(struct auth_xfer* xfr)
{
	if(xfr->task_probe->scan_specific)
		return xfr->task_probe->scan_specific;
	return xfr->task_probe->scan_target;
}

/** true if at end of list, task_transfer */
static int
xfr_transfer_end_of_list(struct auth_xfer* xfr)
{
	return !xfr->task_transfer->scan_specific &&
		!xfr->task_transfer->scan_target;
}

/** true if at end of list, task_probe */
static int
xfr_probe_end_of_list(struct auth_xfer* xfr)
{
	return !xfr->task_probe->scan_specific && !xfr->task_probe->scan_target;
}

/** move to next master in list, task_transfer */
static void
xfr_transfer_nextmaster(struct auth_xfer* xfr)
{
	if(!xfr->task_transfer->scan_specific &&
		!xfr->task_transfer->scan_target)
		return;
	if(xfr->task_transfer->scan_addr) {
		xfr->task_transfer->scan_addr =
			xfr->task_transfer->scan_addr->next;
		if(xfr->task_transfer->scan_addr)
			return;
	}
	if(xfr->task_transfer->scan_specific) {
		xfr->task_transfer->scan_specific = NULL;
		xfr->task_transfer->scan_target = xfr->task_transfer->masters;
		if(xfr->task_transfer->scan_target && xfr->task_transfer->
			scan_target->list)
			xfr->task_transfer->scan_addr =
				xfr->task_transfer->scan_target->list;
		return;
	}
	if(!xfr->task_transfer->scan_target)
		return;
	xfr->task_transfer->scan_target = xfr->task_transfer->scan_target->next;
	if(xfr->task_transfer->scan_target && xfr->task_transfer->
		scan_target->list)
		xfr->task_transfer->scan_addr =
			xfr->task_transfer->scan_target->list;
	return;
}

/** move to next master in list, task_probe */
static void
xfr_probe_nextmaster(struct auth_xfer* xfr)
{
	if(!xfr->task_probe->scan_specific && !xfr->task_probe->scan_target)
		return;
	if(xfr->task_probe->scan_addr) {
		xfr->task_probe->scan_addr = xfr->task_probe->scan_addr->next;
		if(xfr->task_probe->scan_addr)
			return;
	}
	if(xfr->task_probe->scan_specific) {
		xfr->task_probe->scan_specific = NULL;
		xfr->task_probe->scan_target = xfr->task_probe->masters;
		if(xfr->task_probe->scan_target && xfr->task_probe->
			scan_target->list)
			xfr->task_probe->scan_addr =
				xfr->task_probe->scan_target->list;
		return;
	}
	if(!xfr->task_probe->scan_target)
		return;
	xfr->task_probe->scan_target = xfr->task_probe->scan_target->next;
	if(xfr->task_probe->scan_target && xfr->task_probe->
		scan_target->list)
		xfr->task_probe->scan_addr =
			xfr->task_probe->scan_target->list;
	return;
}

/** create SOA probe packet for xfr */
static void
xfr_create_soa_probe_packet(struct auth_xfer* xfr, sldns_buffer* buf, 
	uint16_t id)
{
	struct query_info qinfo;

	memset(&qinfo, 0, sizeof(qinfo));
	qinfo.qname = xfr->name;
	qinfo.qname_len = xfr->namelen;
	qinfo.qtype = LDNS_RR_TYPE_SOA;
	qinfo.qclass = xfr->dclass;
	qinfo_query_encode(buf, &qinfo);
	sldns_buffer_write_u16_at(buf, 0, id);
}

/** create IXFR/AXFR packet for xfr */
static void
xfr_create_ixfr_packet(struct auth_xfer* xfr, sldns_buffer* buf, uint16_t id,
	struct auth_master* master)
{
	struct query_info qinfo;
	uint32_t serial;
	int have_zone;
	have_zone = xfr->have_zone;
	serial = xfr->serial;

	memset(&qinfo, 0, sizeof(qinfo));
	qinfo.qname = xfr->name;
	qinfo.qname_len = xfr->namelen;
	xfr->task_transfer->got_xfr_serial = 0;
	xfr->task_transfer->rr_scan_num = 0;
	xfr->task_transfer->incoming_xfr_serial = 0;
	xfr->task_transfer->on_ixfr_is_axfr = 0;
	xfr->task_transfer->on_ixfr = 1;
	qinfo.qtype = LDNS_RR_TYPE_IXFR;
	if(!have_zone || xfr->task_transfer->ixfr_fail || !master->ixfr) {
		qinfo.qtype = LDNS_RR_TYPE_AXFR;
		xfr->task_transfer->ixfr_fail = 0;
		xfr->task_transfer->on_ixfr = 0;
	}

	qinfo.qclass = xfr->dclass;
	qinfo_query_encode(buf, &qinfo);
	sldns_buffer_write_u16_at(buf, 0, id);

	/* append serial for IXFR */
	if(qinfo.qtype == LDNS_RR_TYPE_IXFR) {
		size_t end = sldns_buffer_limit(buf);
		sldns_buffer_clear(buf);
		sldns_buffer_set_position(buf, end);
		/* auth section count 1 */
		sldns_buffer_write_u16_at(buf, LDNS_NSCOUNT_OFF, 1);
		/* write SOA */
		sldns_buffer_write_u8(buf, 0xC0); /* compressed ptr to qname */
		sldns_buffer_write_u8(buf, 0x0C);
		sldns_buffer_write_u16(buf, LDNS_RR_TYPE_SOA);
		sldns_buffer_write_u16(buf, qinfo.qclass);
		sldns_buffer_write_u32(buf, 0); /* ttl */
		sldns_buffer_write_u16(buf, 22); /* rdata length */
		sldns_buffer_write_u8(buf, 0); /* . */
		sldns_buffer_write_u8(buf, 0); /* . */
		sldns_buffer_write_u32(buf, serial); /* serial */
		sldns_buffer_write_u32(buf, 0); /* refresh */
		sldns_buffer_write_u32(buf, 0); /* retry */
		sldns_buffer_write_u32(buf, 0); /* expire */
		sldns_buffer_write_u32(buf, 0); /* minimum */
		sldns_buffer_flip(buf);
	}
}

/** check if returned packet is OK */
static int
check_packet_ok(sldns_buffer* pkt, uint16_t qtype, struct auth_xfer* xfr,
	uint32_t* serial)
{
	/* parse to see if packet worked, valid reply */

	/* check serial number of SOA */
	if(sldns_buffer_limit(pkt) < LDNS_HEADER_SIZE)
		return 0;

	/* check ID */
	if(LDNS_ID_WIRE(sldns_buffer_begin(pkt)) != xfr->task_probe->id)
		return 0;

	/* check flag bits and rcode */
	if(!LDNS_QR_WIRE(sldns_buffer_begin(pkt)))
		return 0;
	if(LDNS_OPCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_PACKET_QUERY)
		return 0;
	if(LDNS_RCODE_WIRE(sldns_buffer_begin(pkt)) != LDNS_RCODE_NOERROR)
		return 0;

	/* check qname */
	if(LDNS_QDCOUNT(sldns_buffer_begin(pkt)) != 1)
		return 0;
	sldns_buffer_skip(pkt, LDNS_HEADER_SIZE);
	if(sldns_buffer_remaining(pkt) < xfr->namelen)
		return 0;
	if(query_dname_compare(sldns_buffer_current(pkt), xfr->name) != 0)
		return 0;
	sldns_buffer_skip(pkt, (ssize_t)xfr->namelen);

	/* check qtype, qclass */
	if(sldns_buffer_remaining(pkt) < 4)
		return 0;
	if(sldns_buffer_read_u16(pkt) != qtype)
		return 0;
	if(sldns_buffer_read_u16(pkt) != xfr->dclass)
		return 0;

	if(serial) {
		uint16_t rdlen;
		/* read serial number, from answer section SOA */
		if(LDNS_ANCOUNT(sldns_buffer_begin(pkt)) == 0)
			return 0;
		/* read from first record SOA record */
		if(sldns_buffer_remaining(pkt) < 1)
			return 0;
		if(dname_pkt_compare(pkt, sldns_buffer_current(pkt),
			xfr->name) != 0)
			return 0;
		if(!pkt_dname_len(pkt))
			return 0;
		/* type, class, ttl, rdatalen */
		if(sldns_buffer_remaining(pkt) < 4+4+2)
			return 0;
		if(sldns_buffer_read_u16(pkt) != qtype)
			return 0;
		if(sldns_buffer_read_u16(pkt) != xfr->dclass)
			return 0;
		sldns_buffer_skip(pkt, 4); /* ttl */
		rdlen = sldns_buffer_read_u16(pkt);
		if(sldns_buffer_remaining(pkt) < rdlen)
			return 0;
		if(sldns_buffer_remaining(pkt) < 1)
			return 0;
		if(!pkt_dname_len(pkt)) /* soa name */
			return 0;
		if(sldns_buffer_remaining(pkt) < 1)
			return 0;
		if(!pkt_dname_len(pkt)) /* soa name */
			return 0;
		if(sldns_buffer_remaining(pkt) < 20)
			return 0;
		*serial = sldns_buffer_read_u32(pkt);
	}
	return 1;
}

/** read one line from chunks into buffer at current position */
static int
chunkline_get_line(struct auth_chunk** chunk, size_t* chunk_pos,
	sldns_buffer* buf)
{
	int readsome = 0;
	while(*chunk) {
		/* more text in this chunk? */
		if(*chunk_pos < (*chunk)->len) {
			readsome = 1;
			while(*chunk_pos < (*chunk)->len) {
				char c = (char)((*chunk)->data[*chunk_pos]);
				(*chunk_pos)++;
				if(sldns_buffer_remaining(buf) < 2) {
					/* buffer too short */
					verbose(VERB_ALGO, "http chunkline, "
						"line too long");
					return 0;
				}
				sldns_buffer_write_u8(buf, (uint8_t)c);
				if(c == '\n') {
					/* we are done */
					return 1;
				}
			}
		}
		/* move to next chunk */
		*chunk = (*chunk)->next;
		*chunk_pos = 0;
	}
	/* no more text */
	if(readsome) return 1;
	return 0;
}

/** count number of open and closed parenthesis in a chunkline */
static int
chunkline_count_parens(sldns_buffer* buf, size_t start)
{
	size_t end = sldns_buffer_position(buf);
	size_t i;
	int count = 0;
	int squote = 0, dquote = 0;
	for(i=start; i<end; i++) {
		char c = (char)sldns_buffer_read_u8_at(buf, i);
		if(squote && c != '\'') continue;
		if(dquote && c != '"') continue;
		if(c == '"')
			dquote = !dquote; /* skip quoted part */
		else if(c == '\'')
			squote = !squote; /* skip quoted part */
		else if(c == '(')
			count ++;
		else if(c == ')')
			count --;
		else if(c == ';') {
			/* rest is a comment */
			return count;
		}
	}
	return count;
}

/** remove trailing ;... comment from a line in the chunkline buffer */
static void
chunkline_remove_trailcomment(sldns_buffer* buf, size_t start)
{
	size_t end = sldns_buffer_position(buf);
	size_t i;
	int squote = 0, dquote = 0;
	for(i=start; i<end; i++) {
		char c = (char)sldns_buffer_read_u8_at(buf, i);
		if(squote && c != '\'') continue;
		if(dquote && c != '"') continue;
		if(c == '"')
			dquote = !dquote; /* skip quoted part */
		else if(c == '\'')
			squote = !squote; /* skip quoted part */
		else if(c == ';') {
			/* rest is a comment */
			sldns_buffer_set_position(buf, i);
			return;
		}
	}
	/* nothing to remove */
}

/** see if a chunkline is a comment line (or empty line) */
static int
chunkline_is_comment_line_or_empty(sldns_buffer* buf)
{
	size_t i, end = sldns_buffer_limit(buf);
	for(i=0; i<end; i++) {
		char c = (char)sldns_buffer_read_u8_at(buf, i);
		if(c == ';')
			return 1; /* comment */
		else if(c != ' ' && c != '\t' && c != '\r' && c != '\n')
			return 0; /* not a comment */
	}
	return 1; /* empty */
}

/** find a line with ( ) collated */
static int
chunkline_get_line_collated(struct auth_chunk** chunk, size_t* chunk_pos,
	sldns_buffer* buf)
{
	size_t pos;
	int parens = 0;
	sldns_buffer_clear(buf);
	pos = sldns_buffer_position(buf);
	if(!chunkline_get_line(chunk, chunk_pos, buf)) {
		if(sldns_buffer_position(buf) < sldns_buffer_limit(buf))
			sldns_buffer_write_u8_at(buf, sldns_buffer_position(buf), 0);
		else sldns_buffer_write_u8_at(buf, sldns_buffer_position(buf)-1, 0);
		sldns_buffer_flip(buf);
		return 0;
	}
	parens += chunkline_count_parens(buf, pos);
	while(parens > 0) {
		chunkline_remove_trailcomment(buf, pos);
		pos = sldns_buffer_position(buf);
		if(!chunkline_get_line(chunk, chunk_pos, buf)) {
			if(sldns_buffer_position(buf) < sldns_buffer_limit(buf))
				sldns_buffer_write_u8_at(buf, sldns_buffer_position(buf), 0);
			else sldns_buffer_write_u8_at(buf, sldns_buffer_position(buf)-1, 0);
			sldns_buffer_flip(buf);
			return 0;
		}
		parens += chunkline_count_parens(buf, pos);
	}

	if(sldns_buffer_remaining(buf) < 1) {
		verbose(VERB_ALGO, "http chunkline: "
			"line too long");
		return 0;
	}
	sldns_buffer_write_u8_at(buf, sldns_buffer_position(buf), 0);
	sldns_buffer_flip(buf);
	return 1;
}

/** process $ORIGIN for http */
static int
http_parse_origin(sldns_buffer* buf, struct sldns_file_parse_state* pstate)
{
	char* line = (char*)sldns_buffer_begin(buf);
	if(strncmp(line, "$ORIGIN", 7) == 0 &&
		isspace((unsigned char)line[7])) {
		int s;
		pstate->origin_len = sizeof(pstate->origin);
		s = sldns_str2wire_dname_buf(sldns_strip_ws(line+8),
			pstate->origin, &pstate->origin_len);
		if(s) pstate->origin_len = 0;
		return 1;
	}
	return 0;
}

/** process $TTL for http */
static int
http_parse_ttl(sldns_buffer* buf, struct sldns_file_parse_state* pstate)
{
	char* line = (char*)sldns_buffer_begin(buf);
	if(strncmp(line, "$TTL", 4) == 0 &&
		isspace((unsigned char)line[4])) {
		const char* end = NULL;
		pstate->default_ttl = sldns_str2period(
			sldns_strip_ws(line+5), &end);
		return 1;
	}
	return 0;
}

/** find noncomment RR line in chunks, collates lines if ( ) format */
static int
chunkline_non_comment_RR(struct auth_chunk** chunk, size_t* chunk_pos,
	sldns_buffer* buf, struct sldns_file_parse_state* pstate)
{
	while(chunkline_get_line_collated(chunk, chunk_pos, buf)) {
		if(chunkline_is_comment_line_or_empty(buf)) {
			/* a comment, go to next line */
			continue;
		}
		if(http_parse_origin(buf, pstate)) {
			continue; /* $ORIGIN has been handled */
		}
		if(http_parse_ttl(buf, pstate)) {
			continue; /* $TTL has been handled */
		}
		return 1;
	}
	/* no noncomments, fail */
	return 0;
}

/** check syntax of chunklist zonefile, parse first RR, return false on
 * failure and return a string in the scratch buffer (first RR string)
 * on failure. */
static int
http_zonefile_syntax_check(struct auth_xfer* xfr, sldns_buffer* buf)
{
	uint8_t rr[LDNS_RR_BUF_SIZE];
	size_t rr_len, dname_len = 0;
	struct sldns_file_parse_state pstate;
	struct auth_chunk* chunk;
	size_t chunk_pos;
	int e;
	memset(&pstate, 0, sizeof(pstate));
	pstate.default_ttl = 3600;
	if(xfr->namelen < sizeof(pstate.origin)) {
		pstate.origin_len = xfr->namelen;
		memmove(pstate.origin, xfr->name, xfr->namelen);
	}
	chunk = xfr->task_transfer->chunks_first;
	chunk_pos = 0;
	if(!chunkline_non_comment_RR(&chunk, &chunk_pos, buf, &pstate)) {
		return 0;
	}
	rr_len = sizeof(rr);
	e=sldns_str2wire_rr_buf((char*)sldns_buffer_begin(buf), rr, &rr_len,
		&dname_len, pstate.default_ttl,
		pstate.origin_len?pstate.origin:NULL, pstate.origin_len,
		pstate.prev_rr_len?pstate.prev_rr:NULL, pstate.prev_rr_len);
	if(e != 0) {
		log_err("parse failure on first RR[%d]: %s",
			LDNS_WIREPARSE_OFFSET(e),
			sldns_get_errorstr_parse(LDNS_WIREPARSE_ERROR(e)));
		return 0;
	}
	/* check that class is correct */
	if(sldns_wirerr_get_class(rr, rr_len, dname_len) != xfr->dclass) {
		log_err("parse failure: first record in downloaded zonefile "
			"from wrong RR class");
		return 0;
	}
	return 1;
}

/** sum sizes of chunklist */
static size_t
chunklist_sum(struct auth_chunk* list)
{
	struct auth_chunk* p;
	size_t s = 0;
	for(p=list; p; p=p->next) {
		s += p->len;
	}
	return s;
}

/** remove newlines from collated line */
static void
chunkline_newline_removal(sldns_buffer* buf)
{
	size_t i, end=sldns_buffer_limit(buf);
	for(i=0; i<end; i++) {
		char c = (char)sldns_buffer_read_u8_at(buf, i);
		if(c == '\n' && i==end-1) {
			sldns_buffer_write_u8_at(buf, i, 0);
			sldns_buffer_set_limit(buf, end-1);
			return;
		}
		if(c == '\n')
			sldns_buffer_write_u8_at(buf, i, (uint8_t)' ');
	}
}

/** for http download, parse and add RR to zone */
static int
http_parse_add_rr(struct auth_xfer* xfr, struct auth_zone* z,
	sldns_buffer* buf, struct sldns_file_parse_state* pstate)
{
	uint8_t rr[LDNS_RR_BUF_SIZE];
	size_t rr_len, dname_len = 0;
	int e;
	char* line = (char*)sldns_buffer_begin(buf);
	rr_len = sizeof(rr);
	e = sldns_str2wire_rr_buf(line, rr, &rr_len, &dname_len,
		pstate->default_ttl,
		pstate->origin_len?pstate->origin:NULL, pstate->origin_len,
		pstate->prev_rr_len?pstate->prev_rr:NULL, pstate->prev_rr_len);
	if(e != 0) {
		log_err("%s/%s parse failure RR[%d]: %s in '%s'",
			xfr->task_transfer->master->host,
			xfr->task_transfer->master->file,
			LDNS_WIREPARSE_OFFSET(e),
			sldns_get_errorstr_parse(LDNS_WIREPARSE_ERROR(e)),
			line);
		return 0;
	}
	if(rr_len == 0)
		return 1; /* empty line or so */

	/* set prev */
	if(dname_len < sizeof(pstate->prev_rr)) {
		memmove(pstate->prev_rr, rr, dname_len);
		pstate->prev_rr_len = dname_len;
	}

	return az_insert_rr(z, rr, rr_len, dname_len, NULL);
}

/** RR list iterator, returns RRs from answer section one by one from the
 * dns packets in the chunklist */
static void
chunk_rrlist_start(struct auth_xfer* xfr, struct auth_chunk** rr_chunk,
	int* rr_num, size_t* rr_pos)
{
	*rr_chunk = xfr->task_transfer->chunks_first;
	*rr_num = 0;
	*rr_pos = 0;
}

/** RR list iterator, see if we are at the end of the list */
static int
chunk_rrlist_end(struct auth_chunk* rr_chunk, int rr_num)
{
	while(rr_chunk) {
		if(rr_chunk->len < LDNS_HEADER_SIZE)
			return 1;
		if(rr_num < (int)LDNS_ANCOUNT(rr_chunk->data))
			return 0;
		/* no more RRs in this chunk */
		/* continue with next chunk, see if it has RRs */
		rr_chunk = rr_chunk->next;
		rr_num = 0;
	}
	return 1;
}

/** RR list iterator, move to next RR */
static void
chunk_rrlist_gonext(struct auth_chunk** rr_chunk, int* rr_num,
	size_t* rr_pos, size_t rr_nextpos)
{
	/* already at end of chunks? */
	if(!*rr_chunk)
		return;
	/* move within this chunk */
	if((*rr_chunk)->len >= LDNS_HEADER_SIZE &&
		(*rr_num)+1 < (int)LDNS_ANCOUNT((*rr_chunk)->data)) {
		(*rr_num) += 1;
		*rr_pos = rr_nextpos;
		return;
	}
	/* no more RRs in this chunk */
	/* continue with next chunk, see if it has RRs */
	if(*rr_chunk)
		*rr_chunk = (*rr_chunk)->next;
	while(*rr_chunk) {
		*rr_num = 0;
		*rr_pos = 0;
		if((*rr_chunk)->len >= LDNS_HEADER_SIZE &&
			LDNS_ANCOUNT((*rr_chunk)->data) > 0) {
			return;
		}
		*rr_chunk = (*rr_chunk)->next;
	}
}

/** RR iterator, get current RR information, false on parse error */
static int
chunk_rrlist_get_current(struct auth_chunk* rr_chunk, int rr_num,
	size_t rr_pos, uint8_t** rr_dname, uint16_t* rr_type,
	uint16_t* rr_class, uint32_t* rr_ttl, uint16_t* rr_rdlen,
	uint8_t** rr_rdata, size_t* rr_nextpos)
{
	sldns_buffer pkt;
	/* integrity checks on position */
	if(!rr_chunk) return 0;
	if(rr_chunk->len < LDNS_HEADER_SIZE) return 0;
	if(rr_num >= (int)LDNS_ANCOUNT(rr_chunk->data)) return 0;
	if(rr_pos >= rr_chunk->len) return 0;

	/* fetch rr information */
	sldns_buffer_init_frm_data(&pkt, rr_chunk->data, rr_chunk->len);
	if(rr_pos == 0) {
		size_t i;
		/* skip question section */
		sldns_buffer_set_position(&pkt, LDNS_HEADER_SIZE);
		for(i=0; i<LDNS_QDCOUNT(rr_chunk->data); i++) {
			if(pkt_dname_len(&pkt) == 0) return 0;
			if(sldns_buffer_remaining(&pkt) < 4) return 0;
			sldns_buffer_skip(&pkt, 4); /* type and class */
		}
	} else	{
		sldns_buffer_set_position(&pkt, rr_pos);
	}
	*rr_dname = sldns_buffer_current(&pkt);
	if(pkt_dname_len(&pkt) == 0) return 0;
	if(sldns_buffer_remaining(&pkt) < 10) return 0;
	*rr_type = sldns_buffer_read_u16(&pkt);
	*rr_class = sldns_buffer_read_u16(&pkt);
	*rr_ttl = sldns_buffer_read_u32(&pkt);
	*rr_rdlen = sldns_buffer_read_u16(&pkt);
	if(sldns_buffer_remaining(&pkt) < (*rr_rdlen)) return 0;
	*rr_rdata = sldns_buffer_current(&pkt);
	sldns_buffer_skip(&pkt, (ssize_t)(*rr_rdlen));
	*rr_nextpos = sldns_buffer_position(&pkt);
	return 1;
}

/** print log message where we are in parsing the zone transfer */
static void
log_rrlist_position(const char* label, struct auth_chunk* rr_chunk,
	uint8_t* rr_dname, uint16_t rr_type, size_t rr_counter)
{
	sldns_buffer pkt;
	size_t dlen;
	uint8_t buf[256];
	char str[256];
	char typestr[32];
	sldns_buffer_init_frm_data(&pkt, rr_chunk->data, rr_chunk->len);
	sldns_buffer_set_position(&pkt, (size_t)(rr_dname -
		sldns_buffer_begin(&pkt)));
	if((dlen=pkt_dname_len(&pkt)) == 0) return;
	if(dlen >= sizeof(buf)) return;
	dname_pkt_copy(&pkt, buf, rr_dname);
	dname_str(buf, str);
	(void)sldns_wire2str_type_buf(rr_type, typestr, sizeof(typestr));
	verbose(VERB_ALGO, "%s at[%d] %s %s", label, (int)rr_counter,
		str, typestr);
}

/** check that start serial is OK for ixfr. we are at rr_counter == 0,
 * and we are going to check rr_counter == 1 (has to be type SOA) serial */
static int
ixfr_start_serial(struct auth_chunk* rr_chunk, int rr_num, size_t rr_pos,
	uint8_t* rr_dname, uint16_t rr_type, uint16_t rr_class,
	uint32_t rr_ttl, uint16_t rr_rdlen, uint8_t* rr_rdata,
	size_t rr_nextpos, uint32_t transfer_serial, uint32_t xfr_serial)
{
	uint32_t startserial;
	/* move forward on RR */
	chunk_rrlist_gonext(&rr_chunk, &rr_num, &rr_pos, rr_nextpos);
	if(chunk_rrlist_end(rr_chunk, rr_num)) {
		/* no second SOA */
		verbose(VERB_OPS, "IXFR has no second SOA record");
		return 0;
	}
	if(!chunk_rrlist_get_current(rr_chunk, rr_num, rr_pos,
		&rr_dname, &rr_type, &rr_class, &rr_ttl, &rr_rdlen,
		&rr_rdata, &rr_nextpos)) {
		verbose(VERB_OPS, "IXFR cannot parse second SOA record");
		/* failed to parse RR */
		return 0;
	}
	if(rr_type != LDNS_RR_TYPE_SOA) {
		verbose(VERB_OPS, "IXFR second record is not type SOA");
		return 0;
	}
	if(rr_rdlen < 22) {
		verbose(VERB_OPS, "IXFR, second SOA has short rdlength");
		return 0; /* bad SOA rdlen */
	}
	startserial = sldns_read_uint32(rr_rdata+rr_rdlen-20);
	if(startserial == transfer_serial) {
		/* empty AXFR, not an IXFR */
		verbose(VERB_OPS, "IXFR second serial same as first");
		return 0;
	}
	if(startserial != xfr_serial) {
		/* wrong start serial, it does not match the serial in
		 * memory */
		verbose(VERB_OPS, "IXFR is from serial %u to %u but %u "
			"in memory, rejecting the zone transfer",
			(unsigned)startserial, (unsigned)transfer_serial,
			(unsigned)xfr_serial);
		return 0;
	}
	/* everything OK in second SOA serial */
	return 1;
}

/** apply IXFR to zone in memory. z is locked. false on failure(mallocfail) */
static int
apply_ixfr(struct auth_xfer* xfr, struct auth_zone* z,
	struct sldns_buffer* scratch_buffer)
{
	struct auth_chunk* rr_chunk;
	int rr_num;
	size_t rr_pos;
	uint8_t* rr_dname, *rr_rdata;
	uint16_t rr_type, rr_class, rr_rdlen;
	uint32_t rr_ttl;
	size_t rr_nextpos;
	int have_transfer_serial = 0;
	uint32_t transfer_serial = 0;
	size_t rr_counter = 0;
	int delmode = 0;
	int softfail = 0;

	/* start RR iterator over chunklist of packets */
	chunk_rrlist_start(xfr, &rr_chunk, &rr_num, &rr_pos);
	while(!chunk_rrlist_end(rr_chunk, rr_num)) {
		if(!chunk_rrlist_get_current(rr_chunk, rr_num, rr_pos,
			&rr_dname, &rr_type, &rr_class, &rr_ttl, &rr_rdlen,
			&rr_rdata, &rr_nextpos)) {
			/* failed to parse RR */
			return 0;
		}
		if(verbosity>=7) log_rrlist_position("apply ixfr",
			rr_chunk, rr_dname, rr_type, rr_counter);
		/* twiddle add/del mode and check for start and end */
		if(rr_counter == 0 && rr_type != LDNS_RR_TYPE_SOA)
			return 0;
		if(rr_counter == 1 && rr_type != LDNS_RR_TYPE_SOA) {
			/* this is an AXFR returned from the IXFR master */
			/* but that should already have been detected, by
			 * on_ixfr_is_axfr */
			return 0;
		}
		if(rr_type == LDNS_RR_TYPE_SOA) {
			uint32_t serial;
			if(rr_rdlen < 22) return 0; /* bad SOA rdlen */
			serial = sldns_read_uint32(rr_rdata+rr_rdlen-20);
			if(have_transfer_serial == 0) {
				have_transfer_serial = 1;
				transfer_serial = serial;
				delmode = 1; /* gets negated below */
				/* check second RR before going any further */
				if(!ixfr_start_serial(rr_chunk, rr_num, rr_pos,
					rr_dname, rr_type, rr_class, rr_ttl,
					rr_rdlen, rr_rdata, rr_nextpos,
					transfer_serial, xfr->serial)) {
					return 0;
				}
			} else if(transfer_serial == serial) {
				have_transfer_serial++;
				if(rr_counter == 1) {
					/* empty AXFR, with SOA; SOA; */
					/* should have been detected by
					 * on_ixfr_is_axfr */
					return 0;
				}
				if(have_transfer_serial == 3) {
					/* see serial three times for end */
					/* eg. IXFR:
					 *  SOA 3 start
					 *  SOA 1 second RR, followed by del
					 *  SOA 2 followed by add
					 *  SOA 2 followed by del
					 *  SOA 3 followed by add
					 *  SOA 3 end */
					/* ended by SOA record */
					xfr->serial = transfer_serial;
					break;
				}
			}
			/* twiddle add/del mode */
			/* switch from delete part to add part and back again
			 * just before the soa, it gets deleted and added too
			 * this means we switch to delete mode for the final
			 * SOA(so skip that one) */
			delmode = !delmode;
		}
		/* process this RR */
		/* if the RR is deleted twice or added twice, then we 
		 * softfail, and continue with the rest of the IXFR, so
		 * that we serve something fairly nice during the refetch */
		if(verbosity>=7) log_rrlist_position((delmode?"del":"add"),
			rr_chunk, rr_dname, rr_type, rr_counter);
		if(delmode) {
			/* delete this RR */
			int nonexist = 0;
			if(!az_remove_rr_decompress(z, rr_chunk->data,
				rr_chunk->len, scratch_buffer, rr_dname,
				rr_type, rr_class, rr_ttl, rr_rdata, rr_rdlen,
				&nonexist)) {
				/* failed, malloc error or so */
				return 0;
			}
			if(nonexist) {
				/* it was removal of a nonexisting RR */
				if(verbosity>=4) log_rrlist_position(
					"IXFR error nonexistent RR",
					rr_chunk, rr_dname, rr_type, rr_counter);
				softfail = 1;
			}
		} else if(rr_counter != 0) {
			/* skip first SOA RR for addition, it is added in
			 * the addition part near the end of the ixfr, when
			 * that serial is seen the second time. */
			int duplicate = 0;
			/* add this RR */
			if(!az_insert_rr_decompress(z, rr_chunk->data,
				rr_chunk->len, scratch_buffer, rr_dname,
				rr_type, rr_class, rr_ttl, rr_rdata, rr_rdlen,
				&duplicate)) {
				/* failed, malloc error or so */
				return 0;
			}
			if(duplicate) {
				/* it was a duplicate */
				if(verbosity>=4) log_rrlist_position(
					"IXFR error duplicate RR",
					rr_chunk, rr_dname, rr_type, rr_counter);
				softfail = 1;
			}
		}

		rr_counter++;
		chunk_rrlist_gonext(&rr_chunk, &rr_num, &rr_pos, rr_nextpos);
	}
	if(softfail) {
		verbose(VERB_ALGO, "IXFR did not apply cleanly, fetching full zone");
		return 0;
	}
	return 1;
}

/** apply AXFR to zone in memory. z is locked. false on failure(mallocfail) */
static int
apply_axfr(struct auth_xfer* xfr, struct auth_zone* z,
	struct sldns_buffer* scratch_buffer)
{
	struct auth_chunk* rr_chunk;
	int rr_num;
	size_t rr_pos;
	uint8_t* rr_dname, *rr_rdata;
	uint16_t rr_type, rr_class, rr_rdlen;
	uint32_t rr_ttl;
	uint32_t serial = 0;
	size_t rr_nextpos;
	size_t rr_counter = 0;
	int have_end_soa = 0;

	/* clear the data tree */
	traverse_postorder(&z->data, auth_data_del, NULL);
	rbtree_init(&z->data, &auth_data_cmp);
	xfr->have_zone = 0;
	xfr->serial = 0;

	/* insert all RRs in to the zone */
	/* insert the SOA only once, skip the last one */
	/* start RR iterator over chunklist of packets */
	chunk_rrlist_start(xfr, &rr_chunk, &rr_num, &rr_pos);
	while(!chunk_rrlist_end(rr_chunk, rr_num)) {
		if(!chunk_rrlist_get_current(rr_chunk, rr_num, rr_pos,
			&rr_dname, &rr_type, &rr_class, &rr_ttl, &rr_rdlen,
			&rr_rdata, &rr_nextpos)) {
			/* failed to parse RR */
			return 0;
		}
		if(verbosity>=7) log_rrlist_position("apply_axfr",
			rr_chunk, rr_dname, rr_type, rr_counter);
		if(rr_type == LDNS_RR_TYPE_SOA) {
			if(rr_counter != 0) {
				/* end of the axfr */
				have_end_soa = 1;
				break;
			}
			if(rr_rdlen < 22) return 0; /* bad SOA rdlen */
			serial = sldns_read_uint32(rr_rdata+rr_rdlen-20);
		}

		/* add this RR */
		if(!az_insert_rr_decompress(z, rr_chunk->data, rr_chunk->len,
			scratch_buffer, rr_dname, rr_type, rr_class, rr_ttl,
			rr_rdata, rr_rdlen, NULL)) {
			/* failed, malloc error or so */
			return 0;
		}

		rr_counter++;
		chunk_rrlist_gonext(&rr_chunk, &rr_num, &rr_pos, rr_nextpos);
	}
	if(!have_end_soa) {
		log_err("no end SOA record for AXFR");
		return 0;
	}

	xfr->serial = serial;
	xfr->have_zone = 1;
	return 1;
}

/** apply HTTP to zone in memory. z is locked. false on failure(mallocfail) */
static int
apply_http(struct auth_xfer* xfr, struct auth_zone* z,
	struct sldns_buffer* scratch_buffer)
{
	/* parse data in chunks */
	/* parse RR's and read into memory. ignore $INCLUDE from the
	 * downloaded file*/
	struct sldns_file_parse_state pstate;
	struct auth_chunk* chunk;
	size_t chunk_pos;
	memset(&pstate, 0, sizeof(pstate));
	pstate.default_ttl = 3600;
	if(xfr->namelen < sizeof(pstate.origin)) {
		pstate.origin_len = xfr->namelen;
		memmove(pstate.origin, xfr->name, xfr->namelen);
	}

	if(verbosity >= VERB_ALGO)
		verbose(VERB_ALGO, "http download %s of size %d",
		xfr->task_transfer->master->file,
		(int)chunklist_sum(xfr->task_transfer->chunks_first));
	if(xfr->task_transfer->chunks_first && verbosity >= VERB_ALGO) {
		char preview[1024];
		if(xfr->task_transfer->chunks_first->len+1 > sizeof(preview)) {
			memmove(preview, xfr->task_transfer->chunks_first->data,
				sizeof(preview)-1);
			preview[sizeof(preview)-1]=0;
		} else {
			memmove(preview, xfr->task_transfer->chunks_first->data,
				xfr->task_transfer->chunks_first->len);
			preview[xfr->task_transfer->chunks_first->len]=0;
		}
		log_info("auth zone http downloaded content preview: %s",
			preview);
	}

	/* perhaps a little syntax check before we try to apply the data? */
	if(!http_zonefile_syntax_check(xfr, scratch_buffer)) {
		log_err("http download %s/%s does not contain a zonefile, "
			"but got '%s'", xfr->task_transfer->master->host,
			xfr->task_transfer->master->file,
			sldns_buffer_begin(scratch_buffer));
		return 0;
	}

	/* clear the data tree */
	traverse_postorder(&z->data, auth_data_del, NULL);
	rbtree_init(&z->data, &auth_data_cmp);
	xfr->have_zone = 0;
	xfr->serial = 0;

	chunk = xfr->task_transfer->chunks_first;
	chunk_pos = 0;
	pstate.lineno = 0;
	while(chunkline_get_line_collated(&chunk, &chunk_pos, scratch_buffer)) {
		/* process this line */
		pstate.lineno++;
		chunkline_newline_removal(scratch_buffer);
		if(chunkline_is_comment_line_or_empty(scratch_buffer)) {
			continue;
		}
		/* parse line and add RR */
		if(http_parse_origin(scratch_buffer, &pstate)) {
			continue; /* $ORIGIN has been handled */
		}
		if(http_parse_ttl(scratch_buffer, &pstate)) {
			continue; /* $TTL has been handled */
		}
		if(!http_parse_add_rr(xfr, z, scratch_buffer, &pstate)) {
			verbose(VERB_ALGO, "error parsing line [%s:%d] %s",
				xfr->task_transfer->master->file,
				pstate.lineno,
				sldns_buffer_begin(scratch_buffer));
			return 0;
		}
	}
	return 1;
}

/** write http chunks to zonefile to create downloaded file */
static int
auth_zone_write_chunks(struct auth_xfer* xfr, const char* fname)
{
	FILE* out;
	struct auth_chunk* p;
	out = fopen(fname, "w");
	if(!out) {
		log_err("could not open %s: %s", fname, strerror(errno));
		return 0;
	}
	for(p = xfr->task_transfer->chunks_first; p ; p = p->next) {
		if(!write_out(out, (char*)p->data, p->len)) {
			log_err("could not write http download to %s", fname);
			fclose(out);
			return 0;
		}
	}
	fclose(out);
	return 1;
}

/** write to zonefile after zone has been updated */
static void
xfr_write_after_update(struct auth_xfer* xfr, struct module_env* env)
{
	struct auth_zone* z;
	char tmpfile[1024];
	lock_basic_unlock(&xfr->lock);

	/* get lock again, so it is a readlock and concurrently queries
	 * can be answered */
	lock_rw_rdlock(&env->auth_zones->lock);
	z = auth_zone_find(env->auth_zones, xfr->name, xfr->namelen,
		xfr->dclass);
	if(!z) {
		lock_rw_unlock(&env->auth_zones->lock);
		/* the zone is gone, ignore xfr results */
		lock_basic_lock(&xfr->lock);
		return;
	}
	lock_rw_rdlock(&z->lock);
	lock_basic_lock(&xfr->lock);
	lock_rw_unlock(&env->auth_zones->lock);

	if(z->zonefile == NULL) {
		lock_rw_unlock(&z->lock);
		/* no write needed, no zonefile set */
		return;
	}

	/* write to tempfile first */
	if((size_t)strlen(z->zonefile) + 16 > sizeof(tmpfile)) {
		verbose(VERB_ALGO, "tmpfilename too long, cannot update "
			" zonefile %s", z->zonefile);
		lock_rw_unlock(&z->lock);
		return;
	}
	snprintf(tmpfile, sizeof(tmpfile), "%s.tmp%u", z->zonefile,
		(unsigned)getpid());
	if(xfr->task_transfer->master->http) {
		/* use the stored chunk list to write them */
		if(!auth_zone_write_chunks(xfr, tmpfile)) {
			unlink(tmpfile);
			lock_rw_unlock(&z->lock);
		}
	} else if(!auth_zone_write_file(z, tmpfile)) {
		unlink(tmpfile);
		lock_rw_unlock(&z->lock);
		return;
	}
	if(rename(tmpfile, z->zonefile) < 0) {
		log_err("could not rename(%s, %s): %s", tmpfile, z->zonefile,
			strerror(errno));
		unlink(tmpfile);
		lock_rw_unlock(&z->lock);
		return;
	}
	lock_rw_unlock(&z->lock);
}

/** process chunk list and update zone in memory,
 * return false if it did not work */
static int
xfr_process_chunk_list(struct auth_xfer* xfr, struct module_env* env,
	int* ixfr_fail)
{
	struct auth_zone* z;

	/* obtain locks and structures */
	/* release xfr lock, then, while holding az->lock grab both
	 * z->lock and xfr->lock */
	lock_basic_unlock(&xfr->lock);
	lock_rw_rdlock(&env->auth_zones->lock);
	z = auth_zone_find(env->auth_zones, xfr->name, xfr->namelen,
		xfr->dclass);
	if(!z) {
		lock_rw_unlock(&env->auth_zones->lock);
		/* the zone is gone, ignore xfr results */
		lock_basic_lock(&xfr->lock);
		return 0;
	}
	lock_rw_wrlock(&z->lock);
	lock_basic_lock(&xfr->lock);
	lock_rw_unlock(&env->auth_zones->lock);

	/* apply data */
	if(xfr->task_transfer->master->http) {
		if(!apply_http(xfr, z, env->scratch_buffer)) {
			lock_rw_unlock(&z->lock);
			verbose(VERB_ALGO, "http from %s: could not store data",
				xfr->task_transfer->master->host);
			return 0;
		}
	} else if(xfr->task_transfer->on_ixfr &&
		!xfr->task_transfer->on_ixfr_is_axfr) {
		if(!apply_ixfr(xfr, z, env->scratch_buffer)) {
			lock_rw_unlock(&z->lock);
			verbose(VERB_ALGO, "xfr from %s: could not store IXFR"
				" data", xfr->task_transfer->master->host);
			*ixfr_fail = 1;
			return 0;
		}
	} else {
		if(!apply_axfr(xfr, z, env->scratch_buffer)) {
			lock_rw_unlock(&z->lock);
			verbose(VERB_ALGO, "xfr from %s: could not store AXFR"
				" data", xfr->task_transfer->master->host);
			return 0;
		}
	}
	xfr->zone_expired = 0;
	z->zone_expired = 0;
	if(!xfr_find_soa(z, xfr)) {
		lock_rw_unlock(&z->lock);
		verbose(VERB_ALGO, "xfr from %s: no SOA in zone after update"
			" (or malformed RR)", xfr->task_transfer->master->host);
		return 0;
	}
	if(xfr->have_zone)
		xfr->lease_time = *env->now;

	/* unlock */
	lock_rw_unlock(&z->lock);

	if(verbosity >= VERB_QUERY && xfr->have_zone) {
		char zname[256];
		dname_str(xfr->name, zname);
		verbose(VERB_QUERY, "auth zone %s updated to serial %u", zname,
			(unsigned)xfr->serial);
	}
	/* see if we need to write to a zonefile */
	xfr_write_after_update(xfr, env);
	return 1;
}

/** disown task_transfer.  caller must hold xfr.lock */
static void
xfr_transfer_disown(struct auth_xfer* xfr)
{
	/* remove the commpoint */
	comm_point_delete(xfr->task_transfer->cp);
	xfr->task_transfer->cp = NULL;
	/* we don't own this item anymore */
	xfr->task_transfer->worker = NULL;
	xfr->task_transfer->env = NULL;
}

/** lookup a host name for its addresses, if needed */
static int
xfr_transfer_lookup_host(struct auth_xfer* xfr, struct module_env* env)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = 0;
	struct auth_master* master = xfr->task_transfer->lookup_target;
	struct query_info qinfo;
	uint16_t qflags = BIT_RD;
	uint8_t dname[LDNS_MAX_DOMAINLEN+1];
	struct edns_data edns;
	sldns_buffer* buf = env->scratch_buffer;
	if(!master) return 0;
	if(extstrtoaddr(master->host, &addr, &addrlen)) {
		/* not needed, host is in IP addr format */
		return 0;
	}
	if(master->allow_notify)
		return 0; /* allow-notifies are not transferred from, no
		lookup is needed */

	/* use mesh_new_callback to probe for non-addr hosts,
	 * and then wait for them to be looked up (in cache, or query) */
	qinfo.qname_len = sizeof(dname);
	if(sldns_str2wire_dname_buf(master->host, dname, &qinfo.qname_len)
		!= 0) {
		log_err("cannot parse host name of master %s", master->host);
		return 0;
	}
	qinfo.qname = dname;
	qinfo.qclass = xfr->dclass;
	qinfo.qtype = LDNS_RR_TYPE_A;
	if(xfr->task_transfer->lookup_aaaa)
		qinfo.qtype = LDNS_RR_TYPE_AAAA;
	qinfo.local_alias = NULL;
	if(verbosity >= VERB_ALGO) {
		char buf[512];
		char buf2[LDNS_MAX_DOMAINLEN+1];
		dname_str(xfr->name, buf2);
		snprintf(buf, sizeof(buf), "auth zone %s: master lookup"
			" for task_transfer", buf2);
		log_query_info(VERB_ALGO, buf, &qinfo);
	}
	edns.edns_present = 1;
	edns.ext_rcode = 0;
	edns.edns_version = 0;
	edns.bits = EDNS_DO;
	edns.opt_list = NULL;
	if(sldns_buffer_capacity(buf) < 65535)
		edns.udp_size = (uint16_t)sldns_buffer_capacity(buf);
	else	edns.udp_size = 65535;

	/* unlock xfr during mesh_new_callback() because the callback can be
	 * called straight away */
	lock_basic_unlock(&xfr->lock);
	if(!mesh_new_callback(env->mesh, &qinfo, qflags, &edns, buf, 0,
		&auth_xfer_transfer_lookup_callback, xfr)) {
		lock_basic_lock(&xfr->lock);
		log_err("out of memory lookup up master %s", master->host);
		return 0;
	}
	lock_basic_lock(&xfr->lock);
	return 1;
}

/** initiate TCP to the target and fetch zone.
 * returns true if that was successfully started, and timeout setup. */
static int
xfr_transfer_init_fetch(struct auth_xfer* xfr, struct module_env* env)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = 0;
	struct auth_master* master = xfr->task_transfer->master;
	if(!master) return 0;
	if(master->allow_notify) return 0; /* only for notify */

	/* get master addr */
	if(xfr->task_transfer->scan_addr) {
		addrlen = xfr->task_transfer->scan_addr->addrlen;
		memmove(&addr, &xfr->task_transfer->scan_addr->addr, addrlen);
	} else {
		if(!extstrtoaddr(master->host, &addr, &addrlen)) {
			/* the ones that are not in addr format are supposed
			 * to be looked up.  The lookup has failed however,
			 * so skip them */
			char zname[255+1];
			dname_str(xfr->name, zname);
			log_err("%s: failed lookup, cannot transfer from master %s",
				zname, master->host);
			return 0;
		}
	}

	/* remove previous TCP connection (if any) */
	if(xfr->task_transfer->cp) {
		comm_point_delete(xfr->task_transfer->cp);
		xfr->task_transfer->cp = NULL;
	}

	if(master->http) {
		/* perform http fetch */
		/* store http port number into sockaddr,
		 * unless someone used unbound's host@port notation */
		if(strchr(master->host, '@') == NULL)
			sockaddr_store_port(&addr, addrlen, master->port);
		xfr->task_transfer->cp = outnet_comm_point_for_http(
			env->outnet, auth_xfer_transfer_http_callback, xfr,
			&addr, addrlen, AUTH_TRANSFER_TIMEOUT, master->ssl,
			master->host, master->file);
		if(!xfr->task_transfer->cp) {
			char zname[255+1];
			dname_str(xfr->name, zname);
			verbose(VERB_ALGO, "cannot create http cp "
				"connection for %s to %s", zname,
				master->host);
			return 0;
		}
		return 1;
	}

	/* perform AXFR/IXFR */
	/* set the packet to be written */
	/* create new ID */
	xfr->task_transfer->id = (uint16_t)(ub_random(env->rnd)&0xffff);
	xfr_create_ixfr_packet(xfr, env->scratch_buffer,
		xfr->task_transfer->id, master);

	/* connect on fd */
	xfr->task_transfer->cp = outnet_comm_point_for_tcp(env->outnet,
		auth_xfer_transfer_tcp_callback, xfr, &addr, addrlen,
		env->scratch_buffer, AUTH_TRANSFER_TIMEOUT);
	if(!xfr->task_transfer->cp) {
		char zname[255+1];
		dname_str(xfr->name, zname);
		verbose(VERB_ALGO, "cannot create tcp cp connection for "
			"xfr %s to %s", zname, master->host);
		return 0;
	}
	return 1;
}

/** perform next lookup, next transfer TCP, or end and resume wait time task */
static void
xfr_transfer_nexttarget_or_end(struct auth_xfer* xfr, struct module_env* env)
{
	log_assert(xfr->task_transfer->worker == env->worker);

	/* are we performing lookups? */
	while(xfr->task_transfer->lookup_target) {
		if(xfr_transfer_lookup_host(xfr, env)) {
			/* wait for lookup to finish,
			 * note that the hostname may be in unbound's cache
			 * and we may then get an instant cache response,
			 * and that calls the callback just like a full
			 * lookup and lookup failures also call callback */
			lock_basic_unlock(&xfr->lock);
			return;
		}
		xfr_transfer_move_to_next_lookup(xfr, env);
	}

	/* initiate TCP and fetch the zone from the master */
	/* and set timeout on it */
	while(!xfr_transfer_end_of_list(xfr)) {
		xfr->task_transfer->master = xfr_transfer_current_master(xfr);
		if(xfr_transfer_init_fetch(xfr, env)) {
			/* successfully started, wait for callback */
			lock_basic_unlock(&xfr->lock);
			return;
		}
		/* failed to fetch, next master */
		xfr_transfer_nextmaster(xfr);
	}

	/* we failed to fetch the zone, move to wait task
	 * use the shorter retry timeout */
	xfr_transfer_disown(xfr);

	/* pick up the nextprobe task and wait */
	if(xfr->task_nextprobe->worker == NULL)
		xfr_set_timeout(xfr, env, 1, 0);
	lock_basic_unlock(&xfr->lock);
}

/** add addrs from A or AAAA rrset to the master */
static void
xfr_master_add_addrs(struct auth_master* m, struct ub_packed_rrset_key* rrset,
	uint16_t rrtype)
{
	size_t i;
	struct packed_rrset_data* data;
	if(!m || !rrset) return;
	if(rrtype != LDNS_RR_TYPE_A && rrtype != LDNS_RR_TYPE_AAAA)
		return;
	data = (struct packed_rrset_data*)rrset->entry.data;
	for(i=0; i<data->count; i++) {
		struct auth_addr* a;
		size_t len = data->rr_len[i] - 2;
		uint8_t* rdata = data->rr_data[i]+2;
		if(rrtype == LDNS_RR_TYPE_A && len != INET_SIZE)
			continue; /* wrong length for A */
		if(rrtype == LDNS_RR_TYPE_AAAA && len != INET6_SIZE)
			continue; /* wrong length for AAAA */
		
		/* add and alloc it */
		a = (struct auth_addr*)calloc(1, sizeof(*a));
		if(!a) {
			log_err("out of memory");
			return;
		}
		if(rrtype == LDNS_RR_TYPE_A) {
			struct sockaddr_in* sa;
			a->addrlen = (socklen_t)sizeof(*sa);
			sa = (struct sockaddr_in*)&a->addr;
			sa->sin_family = AF_INET;
			sa->sin_port = (in_port_t)htons(UNBOUND_DNS_PORT);
			memmove(&sa->sin_addr, rdata, INET_SIZE);
		} else {
			struct sockaddr_in6* sa;
			a->addrlen = (socklen_t)sizeof(*sa);
			sa = (struct sockaddr_in6*)&a->addr;
			sa->sin6_family = AF_INET6;
			sa->sin6_port = (in_port_t)htons(UNBOUND_DNS_PORT);
			memmove(&sa->sin6_addr, rdata, INET6_SIZE);
		}
		if(verbosity >= VERB_ALGO) {
			char s[64];
			addr_to_str(&a->addr, a->addrlen, s, sizeof(s));
			verbose(VERB_ALGO, "auth host %s lookup %s",
				m->host, s);
		}
		/* append to list */
		a->next = m->list;
		m->list = a;
	}
}

/** callback for task_transfer lookup of host name, of A or AAAA */
void auth_xfer_transfer_lookup_callback(void* arg, int rcode, sldns_buffer* buf,
	enum sec_status ATTR_UNUSED(sec), char* ATTR_UNUSED(why_bogus),
	int ATTR_UNUSED(was_ratelimited))
{
	struct auth_xfer* xfr = (struct auth_xfer*)arg;
	struct module_env* env;
	log_assert(xfr->task_transfer);
	lock_basic_lock(&xfr->lock);
	env = xfr->task_transfer->env;
	if(env->outnet->want_to_quit) {
		lock_basic_unlock(&xfr->lock);
		return; /* stop on quit */
	}

	/* process result */
	if(rcode == LDNS_RCODE_NOERROR) {
		uint16_t wanted_qtype = LDNS_RR_TYPE_A;
		struct regional* temp = env->scratch;
		struct query_info rq;
		struct reply_info* rep;
		if(xfr->task_transfer->lookup_aaaa)
			wanted_qtype = LDNS_RR_TYPE_AAAA;
		memset(&rq, 0, sizeof(rq));
		rep = parse_reply_in_temp_region(buf, temp, &rq);
		if(rep && rq.qtype == wanted_qtype &&
			FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NOERROR) {
			/* parsed successfully */
			struct ub_packed_rrset_key* answer =
				reply_find_answer_rrset(&rq, rep);
			if(answer) {
				xfr_master_add_addrs(xfr->task_transfer->
					lookup_target, answer, wanted_qtype);
			}
		}
	}
	if(xfr->task_transfer->lookup_target->list &&
		xfr->task_transfer->lookup_target == xfr_transfer_current_master(xfr))
		xfr->task_transfer->scan_addr = xfr->task_transfer->lookup_target->list;

	/* move to lookup AAAA after A lookup, move to next hostname lookup,
	 * or move to fetch the zone, or, if nothing to do, end task_transfer */
	xfr_transfer_move_to_next_lookup(xfr, env);
	xfr_transfer_nexttarget_or_end(xfr, env);
}

/** check if xfer (AXFR or IXFR) packet is OK.
 * return false if we lost connection (SERVFAIL, or unreadable).
 * return false if we need to move from IXFR to AXFR, with gonextonfail
 * 	set to false, so the same master is tried again, but with AXFR.
 * return true if fine to link into data.
 * return true with transferdone=true when the transfer has ended.
 */
static int
check_xfer_packet(sldns_buffer* pkt, struct auth_xfer* xfr,
	int* gonextonfail, int* transferdone)
{
	uint8_t* wire = sldns_buffer_begin(pkt);
	int i;
	if(sldns_buffer_limit(pkt) < LDNS_HEADER_SIZE) {
		verbose(VERB_ALGO, "xfr to %s failed, packet too small",
			xfr->task_transfer->master->host);
		return 0;
	}
	if(!LDNS_QR_WIRE(wire)) {
		verbose(VERB_ALGO, "xfr to %s failed, packet has no QR flag",
			xfr->task_transfer->master->host);
		return 0;
	}
	if(LDNS_TC_WIRE(wire)) {
		verbose(VERB_ALGO, "xfr to %s failed, packet has TC flag",
			xfr->task_transfer->master->host);
		return 0;
	}
	/* check ID */
	if(LDNS_ID_WIRE(wire) != xfr->task_transfer->id) {
		verbose(VERB_ALGO, "xfr to %s failed, packet wrong ID",
			xfr->task_transfer->master->host);
		return 0;
	}
	if(LDNS_RCODE_WIRE(wire) != LDNS_RCODE_NOERROR) {
		char rcode[32];
		sldns_wire2str_rcode_buf((int)LDNS_RCODE_WIRE(wire), rcode,
			sizeof(rcode));
		/* if we are doing IXFR, check for fallback */
		if(xfr->task_transfer->on_ixfr) {
			if(LDNS_RCODE_WIRE(wire) == LDNS_RCODE_NOTIMPL ||
				LDNS_RCODE_WIRE(wire) == LDNS_RCODE_SERVFAIL ||
				LDNS_RCODE_WIRE(wire) == LDNS_RCODE_REFUSED ||
				LDNS_RCODE_WIRE(wire) == LDNS_RCODE_FORMERR) {
				verbose(VERB_ALGO, "xfr to %s, fallback "
					"from IXFR to AXFR (with rcode %s)",
					xfr->task_transfer->master->host,
					rcode);
				xfr->task_transfer->ixfr_fail = 1;
				*gonextonfail = 0;
				return 0;
			}
		}
		verbose(VERB_ALGO, "xfr to %s failed, packet with rcode %s",
			xfr->task_transfer->master->host, rcode);
		return 0;
	}
	if(LDNS_OPCODE_WIRE(wire) != LDNS_PACKET_QUERY) {
		verbose(VERB_ALGO, "xfr to %s failed, packet with bad opcode",
			xfr->task_transfer->master->host);
		return 0;
	}
	if(LDNS_QDCOUNT(wire) > 1) {
		verbose(VERB_ALGO, "xfr to %s failed, packet has qdcount %d",
			xfr->task_transfer->master->host,
			(int)LDNS_QDCOUNT(wire));
		return 0;
	}

	/* check qname */
	sldns_buffer_set_position(pkt, LDNS_HEADER_SIZE);
	for(i=0; i<(int)LDNS_QDCOUNT(wire); i++) {
		size_t pos = sldns_buffer_position(pkt);
		uint16_t qtype, qclass;
		if(pkt_dname_len(pkt) == 0) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"malformed dname",
				xfr->task_transfer->master->host);
			return 0;
		}
		if(dname_pkt_compare(pkt, sldns_buffer_at(pkt, pos),
			xfr->name) != 0) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"wrong qname",
				xfr->task_transfer->master->host);
			return 0;
		}
		if(sldns_buffer_remaining(pkt) < 4) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"truncated query RR",
				xfr->task_transfer->master->host);
			return 0;
		}
		qtype = sldns_buffer_read_u16(pkt);
		qclass = sldns_buffer_read_u16(pkt);
		if(qclass != xfr->dclass) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"wrong qclass",
				xfr->task_transfer->master->host);
			return 0;
		}
		if(xfr->task_transfer->on_ixfr) {
			if(qtype != LDNS_RR_TYPE_IXFR) {
				verbose(VERB_ALGO, "xfr to %s failed, packet "
					"with wrong qtype, expected IXFR",
				xfr->task_transfer->master->host);
				return 0;
			}
		} else {
			if(qtype != LDNS_RR_TYPE_AXFR) {
				verbose(VERB_ALGO, "xfr to %s failed, packet "
					"with wrong qtype, expected AXFR",
				xfr->task_transfer->master->host);
				return 0;
			}
		}
	}

	/* check parse of RRs in packet, store first SOA serial
	 * to be able to detect last SOA (with that serial) to see if done */
	/* also check for IXFR 'zone up to date' reply */
	for(i=0; i<(int)LDNS_ANCOUNT(wire); i++) {
		size_t pos = sldns_buffer_position(pkt);
		uint16_t tp, rdlen;
		if(pkt_dname_len(pkt) == 0) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"malformed dname in answer section",
				xfr->task_transfer->master->host);
			return 0;
		}
		if(sldns_buffer_remaining(pkt) < 10) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"truncated RR",
				xfr->task_transfer->master->host);
			return 0;
		}
		tp = sldns_buffer_read_u16(pkt);
		(void)sldns_buffer_read_u16(pkt); /* class */
		(void)sldns_buffer_read_u32(pkt); /* ttl */
		rdlen = sldns_buffer_read_u16(pkt);
		if(sldns_buffer_remaining(pkt) < rdlen) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"truncated RR rdata",
				xfr->task_transfer->master->host);
			return 0;
		}

		/* RR parses (haven't checked rdata itself), now look at
		 * SOA records to see serial number */
		if(xfr->task_transfer->rr_scan_num == 0 &&
			tp != LDNS_RR_TYPE_SOA) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"malformed zone transfer, no start SOA",
				xfr->task_transfer->master->host);
			return 0;
		}
		if(xfr->task_transfer->rr_scan_num == 1 &&
			tp != LDNS_RR_TYPE_SOA) {
			/* second RR is not a SOA record, this is not an IXFR
			 * the master is replying with an AXFR */
			xfr->task_transfer->on_ixfr_is_axfr = 1;
		}
		if(tp == LDNS_RR_TYPE_SOA) {
			uint32_t serial;
			if(rdlen < 22) {
				verbose(VERB_ALGO, "xfr to %s failed, packet "
					"with SOA with malformed rdata",
					xfr->task_transfer->master->host);
				return 0;
			}
			if(dname_pkt_compare(pkt, sldns_buffer_at(pkt, pos),
				xfr->name) != 0) {
				verbose(VERB_ALGO, "xfr to %s failed, packet "
					"with SOA with wrong dname",
					xfr->task_transfer->master->host);
				return 0;
			}

			/* read serial number of SOA */
			serial = sldns_buffer_read_u32_at(pkt,
				sldns_buffer_position(pkt)+rdlen-20);

			/* check for IXFR 'zone has SOA x' reply */
			if(xfr->task_transfer->on_ixfr &&
				xfr->task_transfer->rr_scan_num == 0 &&
				LDNS_ANCOUNT(wire)==1) {
				verbose(VERB_ALGO, "xfr to %s ended, "
					"IXFR reply that zone has serial %u",
					xfr->task_transfer->master->host,
					(unsigned)serial);
				return 0;
			}

			/* if first SOA, store serial number */
			if(xfr->task_transfer->got_xfr_serial == 0) {
				xfr->task_transfer->got_xfr_serial = 1;
				xfr->task_transfer->incoming_xfr_serial =
					serial;
				verbose(VERB_ALGO, "xfr %s: contains "
					"SOA serial %u",
					xfr->task_transfer->master->host,
					(unsigned)serial);
			/* see if end of AXFR */
			} else if(!xfr->task_transfer->on_ixfr ||
				xfr->task_transfer->on_ixfr_is_axfr) {
				/* second SOA with serial is the end
				 * for AXFR */
				*transferdone = 1;
				verbose(VERB_ALGO, "xfr %s: last AXFR packet",
					xfr->task_transfer->master->host);
			/* for IXFR, count SOA records with that serial */
			} else if(xfr->task_transfer->incoming_xfr_serial ==
				serial && xfr->task_transfer->got_xfr_serial
				== 1) {
				xfr->task_transfer->got_xfr_serial++;
			/* if not first soa, if serial==firstserial, the
			 * third time we are at the end, for IXFR */
			} else if(xfr->task_transfer->incoming_xfr_serial ==
				serial && xfr->task_transfer->got_xfr_serial
				== 2) {
				verbose(VERB_ALGO, "xfr %s: last IXFR packet",
					xfr->task_transfer->master->host);
				*transferdone = 1;
				/* continue parse check, if that succeeds,
				 * transfer is done */
			}
		}
		xfr->task_transfer->rr_scan_num++;

		/* skip over RR rdata to go to the next RR */
		sldns_buffer_skip(pkt, (ssize_t)rdlen);
	}

	/* check authority section */
	/* we skip over the RRs checking packet format */
	for(i=0; i<(int)LDNS_NSCOUNT(wire); i++) {
		uint16_t rdlen;
		if(pkt_dname_len(pkt) == 0) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"malformed dname in authority section",
				xfr->task_transfer->master->host);
			return 0;
		}
		if(sldns_buffer_remaining(pkt) < 10) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"truncated RR",
				xfr->task_transfer->master->host);
			return 0;
		}
		(void)sldns_buffer_read_u16(pkt); /* type */
		(void)sldns_buffer_read_u16(pkt); /* class */
		(void)sldns_buffer_read_u32(pkt); /* ttl */
		rdlen = sldns_buffer_read_u16(pkt);
		if(sldns_buffer_remaining(pkt) < rdlen) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"truncated RR rdata",
				xfr->task_transfer->master->host);
			return 0;
		}
		/* skip over RR rdata to go to the next RR */
		sldns_buffer_skip(pkt, (ssize_t)rdlen);
	}

	/* check additional section */
	for(i=0; i<(int)LDNS_ARCOUNT(wire); i++) {
		uint16_t rdlen;
		if(pkt_dname_len(pkt) == 0) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"malformed dname in additional section",
				xfr->task_transfer->master->host);
			return 0;
		}
		if(sldns_buffer_remaining(pkt) < 10) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"truncated RR",
				xfr->task_transfer->master->host);
			return 0;
		}
		(void)sldns_buffer_read_u16(pkt); /* type */
		(void)sldns_buffer_read_u16(pkt); /* class */
		(void)sldns_buffer_read_u32(pkt); /* ttl */
		rdlen = sldns_buffer_read_u16(pkt);
		if(sldns_buffer_remaining(pkt) < rdlen) {
			verbose(VERB_ALGO, "xfr to %s failed, packet with "
				"truncated RR rdata",
				xfr->task_transfer->master->host);
			return 0;
		}
		/* skip over RR rdata to go to the next RR */
		sldns_buffer_skip(pkt, (ssize_t)rdlen);
	}

	return 1;
}

/** Link the data from this packet into the worklist of transferred data */
static int
xfer_link_data(sldns_buffer* pkt, struct auth_xfer* xfr)
{
	/* alloc it */
	struct auth_chunk* e;
	e = (struct auth_chunk*)calloc(1, sizeof(*e));
	if(!e) return 0;
	e->next = NULL;
	e->len = sldns_buffer_limit(pkt);
	e->data = memdup(sldns_buffer_begin(pkt), e->len);
	if(!e->data) {
		free(e);
		return 0;
	}

	/* alloc succeeded, link into list */
	if(!xfr->task_transfer->chunks_first)
		xfr->task_transfer->chunks_first = e;
	if(xfr->task_transfer->chunks_last)
		xfr->task_transfer->chunks_last->next = e;
	xfr->task_transfer->chunks_last = e;
	return 1;
}

/** task transfer.  the list of data is complete. process it and if failed
 * move to next master, if succeeded, end the task transfer */
static void
process_list_end_transfer(struct auth_xfer* xfr, struct module_env* env)
{
	int ixfr_fail = 0;
	if(xfr_process_chunk_list(xfr, env, &ixfr_fail)) {
		/* it worked! */
		auth_chunks_delete(xfr->task_transfer);

		/* we fetched the zone, move to wait task */
		xfr_transfer_disown(xfr);

		if(xfr->notify_received && (!xfr->notify_has_serial ||
			(xfr->notify_has_serial && 
			xfr_serial_means_update(xfr, xfr->notify_serial)))) {
			uint32_t sr = xfr->notify_serial;
			int has_sr = xfr->notify_has_serial;
			/* we received a notify while probe/transfer was
			 * in progress.  start a new probe and transfer */
			xfr->notify_received = 0;
			xfr->notify_has_serial = 0;
			xfr->notify_serial = 0;
			if(!xfr_start_probe(xfr, env, NULL)) {
				/* if we couldn't start it, already in
				 * progress; restore notify serial,
				 * while xfr still locked */
				xfr->notify_received = 1;
				xfr->notify_has_serial = has_sr;
				xfr->notify_serial = sr;
				lock_basic_unlock(&xfr->lock);
			}
			return;
		} else {
			/* pick up the nextprobe task and wait (normail wait time) */
			if(xfr->task_nextprobe->worker == NULL)
				xfr_set_timeout(xfr, env, 0, 0);
		}
		lock_basic_unlock(&xfr->lock);
		return;
	}
	/* processing failed */
	/* when done, delete data from list */
	auth_chunks_delete(xfr->task_transfer);
	if(ixfr_fail) {
		xfr->task_transfer->ixfr_fail = 1;
	} else {
		xfr_transfer_nextmaster(xfr);
	}
	xfr_transfer_nexttarget_or_end(xfr, env);
}

/** callback for task_transfer tcp connections */
int
auth_xfer_transfer_tcp_callback(struct comm_point* c, void* arg, int err,
	struct comm_reply* ATTR_UNUSED(repinfo))
{
	struct auth_xfer* xfr = (struct auth_xfer*)arg;
	struct module_env* env;
	int gonextonfail = 1;
	int transferdone = 0;
	log_assert(xfr->task_transfer);
	lock_basic_lock(&xfr->lock);
	env = xfr->task_transfer->env;
	if(env->outnet->want_to_quit) {
		lock_basic_unlock(&xfr->lock);
		return 0; /* stop on quit */
	}

	if(err != NETEVENT_NOERROR) {
		/* connection failed, closed, or timeout */
		/* stop this transfer, cleanup 
		 * and continue task_transfer*/
		verbose(VERB_ALGO, "xfr stopped, connection lost to %s",
			xfr->task_transfer->master->host);
	failed:
		/* delete transferred data from list */
		auth_chunks_delete(xfr->task_transfer);
		comm_point_delete(xfr->task_transfer->cp);
		xfr->task_transfer->cp = NULL;
		xfr_transfer_nextmaster(xfr);
		xfr_transfer_nexttarget_or_end(xfr, env);
		return 0;
	}

	/* handle returned packet */
	/* if it fails, cleanup and end this transfer */
	/* if it needs to fallback from IXFR to AXFR, do that */
	if(!check_xfer_packet(c->buffer, xfr, &gonextonfail, &transferdone)) {
		goto failed;
	}
	/* if it is good, link it into the list of data */
	/* if the link into list of data fails (malloc fail) cleanup and end */
	if(!xfer_link_data(c->buffer, xfr)) {
		verbose(VERB_ALGO, "xfr stopped to %s, malloc failed",
			xfr->task_transfer->master->host);
		goto failed;
	}
	/* if the transfer is done now, disconnect and process the list */
	if(transferdone) {
		comm_point_delete(xfr->task_transfer->cp);
		xfr->task_transfer->cp = NULL;
		process_list_end_transfer(xfr, env);
		return 0;
	}

	/* if we want to read more messages, setup the commpoint to read
	 * a DNS packet, and the timeout */
	lock_basic_unlock(&xfr->lock);
	c->tcp_is_reading = 1;
	sldns_buffer_clear(c->buffer);
	comm_point_start_listening(c, -1, AUTH_TRANSFER_TIMEOUT);
	return 0;
}

/** callback for task_transfer http connections */
int
auth_xfer_transfer_http_callback(struct comm_point* c, void* arg, int err,
	struct comm_reply* repinfo)
{
	struct auth_xfer* xfr = (struct auth_xfer*)arg;
	struct module_env* env;
	log_assert(xfr->task_transfer);
	lock_basic_lock(&xfr->lock);
	env = xfr->task_transfer->env;
	if(env->outnet->want_to_quit) {
		lock_basic_unlock(&xfr->lock);
		return 0; /* stop on quit */
	}
	verbose(VERB_ALGO, "auth zone transfer http callback");

	if(err != NETEVENT_NOERROR && err != NETEVENT_DONE) {
		/* connection failed, closed, or timeout */
		/* stop this transfer, cleanup 
		 * and continue task_transfer*/
		verbose(VERB_ALGO, "http stopped, connection lost to %s",
			xfr->task_transfer->master->host);
	failed:
		/* delete transferred data from list */
		auth_chunks_delete(xfr->task_transfer);
		if(repinfo) repinfo->c = NULL; /* signal cp deleted to
				the routine calling this callback */
		comm_point_delete(xfr->task_transfer->cp);
		xfr->task_transfer->cp = NULL;
		xfr_transfer_nextmaster(xfr);
		xfr_transfer_nexttarget_or_end(xfr, env);
		return 0;
	}

	/* if it is good, link it into the list of data */
	/* if the link into list of data fails (malloc fail) cleanup and end */
	if(sldns_buffer_limit(c->buffer) > 0) {
		verbose(VERB_ALGO, "auth zone http queued up %d bytes",
			(int)sldns_buffer_limit(c->buffer));
		if(!xfer_link_data(c->buffer, xfr)) {
			verbose(VERB_ALGO, "http stopped to %s, malloc failed",
				xfr->task_transfer->master->host);
			goto failed;
		}
	}
	/* if the transfer is done now, disconnect and process the list */
	if(err == NETEVENT_DONE) {
		if(repinfo) repinfo->c = NULL; /* signal cp deleted to
				the routine calling this callback */
		comm_point_delete(xfr->task_transfer->cp);
		xfr->task_transfer->cp = NULL;
		process_list_end_transfer(xfr, env);
		return 0;
	}

	/* if we want to read more messages, setup the commpoint to read
	 * a DNS packet, and the timeout */
	lock_basic_unlock(&xfr->lock);
	c->tcp_is_reading = 1;
	sldns_buffer_clear(c->buffer);
	comm_point_start_listening(c, -1, AUTH_TRANSFER_TIMEOUT);
	return 0;
}


/** start transfer task by this worker , xfr is locked. */
static void
xfr_start_transfer(struct auth_xfer* xfr, struct module_env* env,
	struct auth_master* master)
{
	log_assert(xfr->task_transfer != NULL);
	log_assert(xfr->task_transfer->worker == NULL);
	log_assert(xfr->task_transfer->chunks_first == NULL);
	log_assert(xfr->task_transfer->chunks_last == NULL);
	xfr->task_transfer->worker = env->worker;
	xfr->task_transfer->env = env;

	/* init transfer process */
	/* find that master in the transfer's list of masters? */
	xfr_transfer_start_list(xfr, master);
	/* start lookup for hostnames in transfer master list */
	xfr_transfer_start_lookups(xfr);

	/* initiate TCP, and set timeout on it */
	xfr_transfer_nexttarget_or_end(xfr, env);
}

/** disown task_probe.  caller must hold xfr.lock */
static void
xfr_probe_disown(struct auth_xfer* xfr)
{
	/* remove timer (from this worker's event base) */
	comm_timer_delete(xfr->task_probe->timer);
	xfr->task_probe->timer = NULL;
	/* remove the commpoint */
	comm_point_delete(xfr->task_probe->cp);
	xfr->task_probe->cp = NULL;
	/* we don't own this item anymore */
	xfr->task_probe->worker = NULL;
	xfr->task_probe->env = NULL;
}

/** send the UDP probe to the master, this is part of task_probe */
static int
xfr_probe_send_probe(struct auth_xfer* xfr, struct module_env* env,
	int timeout)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = 0;
	struct timeval t;
	/* pick master */
	struct auth_master* master = xfr_probe_current_master(xfr);
	if(!master) return 0;
	if(master->allow_notify) return 0; /* only for notify */
	if(master->http) return 0; /* only masters get SOA UDP probe,
		not urls, if those are in this list */

	/* get master addr */
	if(xfr->task_probe->scan_addr) {
		addrlen = xfr->task_probe->scan_addr->addrlen;
		memmove(&addr, &xfr->task_probe->scan_addr->addr, addrlen);
	} else {
		if(!extstrtoaddr(master->host, &addr, &addrlen)) {
			/* the ones that are not in addr format are supposed
			 * to be looked up.  The lookup has failed however,
			 * so skip them */
			char zname[255+1];
			dname_str(xfr->name, zname);
			log_err("%s: failed lookup, cannot probe to master %s",
				zname, master->host);
			return 0;
		}
	}

	/* create packet */
	/* create new ID for new probes, but not on timeout retries,
	 * this means we'll accept replies to previous retries to same ip */
	if(timeout == AUTH_PROBE_TIMEOUT)
		xfr->task_probe->id = (uint16_t)(ub_random(env->rnd)&0xffff);
	xfr_create_soa_probe_packet(xfr, env->scratch_buffer, 
		xfr->task_probe->id);
	if(!xfr->task_probe->cp) {
		xfr->task_probe->cp = outnet_comm_point_for_udp(env->outnet,
			auth_xfer_probe_udp_callback, xfr, &addr, addrlen);
		if(!xfr->task_probe->cp) {
			char zname[255+1];
			dname_str(xfr->name, zname);
			verbose(VERB_ALGO, "cannot create udp cp for "
				"probe %s to %s", zname, master->host);
			return 0;
		}
	}
	if(!xfr->task_probe->timer) {
		xfr->task_probe->timer = comm_timer_create(env->worker_base,
			auth_xfer_probe_timer_callback, xfr);
		if(!xfr->task_probe->timer) {
			log_err("malloc failure");
			return 0;
		}
	}

	/* send udp packet */
	if(!comm_point_send_udp_msg(xfr->task_probe->cp, env->scratch_buffer,
		(struct sockaddr*)&addr, addrlen)) {
		char zname[255+1];
		dname_str(xfr->name, zname);
		verbose(VERB_ALGO, "failed to send soa probe for %s to %s",
			zname, master->host);
		return 0;
	}
	xfr->task_probe->timeout = timeout;
#ifndef S_SPLINT_S
	t.tv_sec = timeout/1000;
	t.tv_usec = (timeout%1000)*1000;
#endif
	comm_timer_set(xfr->task_probe->timer, &t);

	return 1;
}

/** callback for task_probe timer */
void
auth_xfer_probe_timer_callback(void* arg)
{
	struct auth_xfer* xfr = (struct auth_xfer*)arg;
	struct module_env* env;
	log_assert(xfr->task_probe);
	lock_basic_lock(&xfr->lock);
	env = xfr->task_probe->env;
	if(env->outnet->want_to_quit) {
		lock_basic_unlock(&xfr->lock);
		return; /* stop on quit */
	}

	if(xfr->task_probe->timeout <= AUTH_PROBE_TIMEOUT_STOP) {
		/* try again with bigger timeout */
		if(xfr_probe_send_probe(xfr, env, xfr->task_probe->timeout*2)) {
			lock_basic_unlock(&xfr->lock);
			return;
		}
	}
	/* delete commpoint so a new one is created, with a fresh port nr */
	comm_point_delete(xfr->task_probe->cp);
	xfr->task_probe->cp = NULL;

	/* too many timeouts (or fail to send), move to next or end */
	xfr_probe_nextmaster(xfr);
	xfr_probe_send_or_end(xfr, env);
}

/** callback for task_probe udp packets */
int
auth_xfer_probe_udp_callback(struct comm_point* c, void* arg, int err,
	struct comm_reply* repinfo)
{
	struct auth_xfer* xfr = (struct auth_xfer*)arg;
	struct module_env* env;
	log_assert(xfr->task_probe);
	lock_basic_lock(&xfr->lock);
	env = xfr->task_probe->env;
	if(env->outnet->want_to_quit) {
		lock_basic_unlock(&xfr->lock);
		return 0; /* stop on quit */
	}

	/* the comm_point_udp_callback is in a for loop for NUM_UDP_PER_SELECT
	 * and we set rep.c=NULL to stop if from looking inside the commpoint*/
	repinfo->c = NULL;
	/* stop the timer */
	comm_timer_disable(xfr->task_probe->timer);

	/* see if we got a packet and what that means */
	if(err == NETEVENT_NOERROR) {
		uint32_t serial = 0;
		if(check_packet_ok(c->buffer, LDNS_RR_TYPE_SOA, xfr,
			&serial)) {
			/* successful lookup */
			if(verbosity >= VERB_ALGO) {
				char buf[256];
				dname_str(xfr->name, buf);
				verbose(VERB_ALGO, "auth zone %s: soa probe "
					"serial is %u", buf, (unsigned)serial);
			}
			/* see if this serial indicates that the zone has
			 * to be updated */
			if(xfr_serial_means_update(xfr, serial)) {
				/* if updated, start the transfer task, if needed */
				verbose(VERB_ALGO, "auth_zone updated, start transfer");
				if(xfr->task_transfer->worker == NULL) {
					struct auth_master* master =
						xfr_probe_current_master(xfr);
					/* if we have download URLs use them
					 * in preference to this master we
					 * just probed the SOA from */
					if(xfr->task_transfer->masters &&
						xfr->task_transfer->masters->http)
						master = NULL;
					xfr_probe_disown(xfr);
					xfr_start_transfer(xfr, env, master);
					return 0;

				}
				/* other tasks are running, we don't do this anymore */
				xfr_probe_disown(xfr);
				lock_basic_unlock(&xfr->lock);
				/* return, we don't sent a reply to this udp packet,
				 * and we setup the tasks to do next */
				return 0;
			} else {
				verbose(VERB_ALGO, "auth_zone master reports unchanged soa serial");
				/* we if cannot find updates amongst the
				 * masters, this means we then have a new lease
				 * on the zone */
				xfr->task_probe->have_new_lease = 1;
			}
		} else {
			if(verbosity >= VERB_ALGO) {
				char buf[256];
				dname_str(xfr->name, buf);
				verbose(VERB_ALGO, "auth zone %s: bad reply to soa probe", buf);
			}
		}
	} else {
		if(verbosity >= VERB_ALGO) {
			char buf[256];
			dname_str(xfr->name, buf);
			verbose(VERB_ALGO, "auth zone %s: soa probe failed", buf);
		}
	}
	
	/* failed lookup or not an update */
	/* delete commpoint so a new one is created, with a fresh port nr */
	comm_point_delete(xfr->task_probe->cp);
	xfr->task_probe->cp = NULL;

	/* if the result was not a successfull probe, we need
	 * to send the next one */
	xfr_probe_nextmaster(xfr);
	xfr_probe_send_or_end(xfr, env);
	return 0;
}

/** lookup a host name for its addresses, if needed */
static int
xfr_probe_lookup_host(struct auth_xfer* xfr, struct module_env* env)
{
	struct sockaddr_storage addr;
	socklen_t addrlen = 0;
	struct auth_master* master = xfr->task_probe->lookup_target;
	struct query_info qinfo;
	uint16_t qflags = BIT_RD;
	uint8_t dname[LDNS_MAX_DOMAINLEN+1];
	struct edns_data edns;
	sldns_buffer* buf = env->scratch_buffer;
	if(!master) return 0;
	if(extstrtoaddr(master->host, &addr, &addrlen)) {
		/* not needed, host is in IP addr format */
		return 0;
	}
	if(master->allow_notify && !master->http &&
		strchr(master->host, '/') != NULL &&
		strchr(master->host, '/') == strrchr(master->host, '/')) {
		return 0; /* is IP/prefix format, not something to look up */
	}

	/* use mesh_new_callback to probe for non-addr hosts,
	 * and then wait for them to be looked up (in cache, or query) */
	qinfo.qname_len = sizeof(dname);
	if(sldns_str2wire_dname_buf(master->host, dname, &qinfo.qname_len)
		!= 0) {
		log_err("cannot parse host name of master %s", master->host);
		return 0;
	}
	qinfo.qname = dname;
	qinfo.qclass = xfr->dclass;
	qinfo.qtype = LDNS_RR_TYPE_A;
	if(xfr->task_probe->lookup_aaaa)
		qinfo.qtype = LDNS_RR_TYPE_AAAA;
	qinfo.local_alias = NULL;
	if(verbosity >= VERB_ALGO) {
		char buf[512];
		char buf2[LDNS_MAX_DOMAINLEN+1];
		dname_str(xfr->name, buf2);
		snprintf(buf, sizeof(buf), "auth zone %s: master lookup"
			" for task_probe", buf2);
		log_query_info(VERB_ALGO, buf, &qinfo);
	}
	edns.edns_present = 1;
	edns.ext_rcode = 0;
	edns.edns_version = 0;
	edns.bits = EDNS_DO;
	edns.opt_list = NULL;
	if(sldns_buffer_capacity(buf) < 65535)
		edns.udp_size = (uint16_t)sldns_buffer_capacity(buf);
	else	edns.udp_size = 65535;

	/* unlock xfr during mesh_new_callback() because the callback can be
	 * called straight away */
	lock_basic_unlock(&xfr->lock);
	if(!mesh_new_callback(env->mesh, &qinfo, qflags, &edns, buf, 0,
		&auth_xfer_probe_lookup_callback, xfr)) {
		lock_basic_lock(&xfr->lock);
		log_err("out of memory lookup up master %s", master->host);
		return 0;
	}
	lock_basic_lock(&xfr->lock);
	return 1;
}

/** move to sending the probe packets, next if fails. task_probe */
static void
xfr_probe_send_or_end(struct auth_xfer* xfr, struct module_env* env)
{
	/* are we doing hostname lookups? */
	while(xfr->task_probe->lookup_target) {
		if(xfr_probe_lookup_host(xfr, env)) {
			/* wait for lookup to finish,
			 * note that the hostname may be in unbound's cache
			 * and we may then get an instant cache response,
			 * and that calls the callback just like a full
			 * lookup and lookup failures also call callback */
			lock_basic_unlock(&xfr->lock);
			return;
		}
		xfr_probe_move_to_next_lookup(xfr, env);
	}
	/* probe of list has ended.  Create or refresh the list of of
	 * allow_notify addrs */
	probe_copy_masters_for_allow_notify(xfr);
	if(xfr->task_probe->only_lookup) {
		/* only wanted lookups for copy, stop probe and start wait */
		xfr->task_probe->only_lookup = 0;
		xfr_probe_disown(xfr);
		if(xfr->task_nextprobe->worker == NULL)
			xfr_set_timeout(xfr, env, 0, 0);
		lock_basic_unlock(&xfr->lock);
		return;
	}

	/* send probe packets */
	while(!xfr_probe_end_of_list(xfr)) {
		if(xfr_probe_send_probe(xfr, env, AUTH_PROBE_TIMEOUT)) {
			/* successfully sent probe, wait for callback */
			lock_basic_unlock(&xfr->lock);
			return;
		}
		/* failed to send probe, next master */
		xfr_probe_nextmaster(xfr);
	}

	/* done with probe sequence, wait */
	if(xfr->task_probe->have_new_lease) {
		/* if zone not updated, start the wait timer again */
		verbose(VERB_ALGO, "auth_zone unchanged, new lease, wait");
		xfr_probe_disown(xfr);
		if(xfr->have_zone)
			xfr->lease_time = *env->now;
		if(xfr->task_nextprobe->worker == NULL)
			xfr_set_timeout(xfr, env, 0, 0);
	} else {
		/* we failed to send this as well, move to the wait task,
		 * use the shorter retry timeout */
		xfr_probe_disown(xfr);
		/* pick up the nextprobe task and wait */
		if(xfr->task_nextprobe->worker == NULL)
			xfr_set_timeout(xfr, env, 1, 0);
	}

	lock_basic_unlock(&xfr->lock);
}

/** callback for task_probe lookup of host name, of A or AAAA */
void auth_xfer_probe_lookup_callback(void* arg, int rcode, sldns_buffer* buf,
	enum sec_status ATTR_UNUSED(sec), char* ATTR_UNUSED(why_bogus),
	int ATTR_UNUSED(was_ratelimited))
{
	struct auth_xfer* xfr = (struct auth_xfer*)arg;
	struct module_env* env;
	log_assert(xfr->task_probe);
	lock_basic_lock(&xfr->lock);
	env = xfr->task_probe->env;
	if(env->outnet->want_to_quit) {
		lock_basic_unlock(&xfr->lock);
		return; /* stop on quit */
	}

	/* process result */
	if(rcode == LDNS_RCODE_NOERROR) {
		uint16_t wanted_qtype = LDNS_RR_TYPE_A;
		struct regional* temp = env->scratch;
		struct query_info rq;
		struct reply_info* rep;
		if(xfr->task_probe->lookup_aaaa)
			wanted_qtype = LDNS_RR_TYPE_AAAA;
		memset(&rq, 0, sizeof(rq));
		rep = parse_reply_in_temp_region(buf, temp, &rq);
		if(rep && rq.qtype == wanted_qtype &&
			FLAGS_GET_RCODE(rep->flags) == LDNS_RCODE_NOERROR) {
			/* parsed successfully */
			struct ub_packed_rrset_key* answer =
				reply_find_answer_rrset(&rq, rep);
			if(answer) {
				xfr_master_add_addrs(xfr->task_probe->
					lookup_target, answer, wanted_qtype);
			}
		}
	}
	if(xfr->task_probe->lookup_target->list &&
		xfr->task_probe->lookup_target == xfr_probe_current_master(xfr))
		xfr->task_probe->scan_addr = xfr->task_probe->lookup_target->list;

	/* move to lookup AAAA after A lookup, move to next hostname lookup,
	 * or move to send the probes, or, if nothing to do, end task_probe */
	xfr_probe_move_to_next_lookup(xfr, env);
	xfr_probe_send_or_end(xfr, env);
}

/** disown task_nextprobe.  caller must hold xfr.lock */
static void
xfr_nextprobe_disown(struct auth_xfer* xfr)
{
	/* delete the timer, because the next worker to pick this up may
	 * not have the same event base */
	comm_timer_delete(xfr->task_nextprobe->timer);
	xfr->task_nextprobe->timer = NULL;
	xfr->task_nextprobe->next_probe = 0;
	/* we don't own this item anymore */
	xfr->task_nextprobe->worker = NULL;
	xfr->task_nextprobe->env = NULL;
}

/** xfer nextprobe timeout callback, this is part of task_nextprobe */
void
auth_xfer_timer(void* arg)
{
	struct auth_xfer* xfr = (struct auth_xfer*)arg;
	struct module_env* env;
	log_assert(xfr->task_nextprobe);
	lock_basic_lock(&xfr->lock);
	env = xfr->task_nextprobe->env;
	if(env->outnet->want_to_quit) {
		lock_basic_unlock(&xfr->lock);
		return; /* stop on quit */
	}

	/* see if zone has expired, and if so, also set auth_zone expired */
	if(xfr->have_zone && !xfr->zone_expired &&
	   *env->now >= xfr->lease_time + xfr->expiry) {
		lock_basic_unlock(&xfr->lock);
		auth_xfer_set_expired(xfr, env, 1);
		lock_basic_lock(&xfr->lock);
	}

	xfr_nextprobe_disown(xfr);

	if(!xfr_start_probe(xfr, env, NULL)) {
		/* not started because already in progress */
		lock_basic_unlock(&xfr->lock);
	}
}

/** return true if there are probe (SOA UDP query) targets in the master list*/
static int
have_probe_targets(struct auth_master* list)
{
	struct auth_master* p;
	for(p=list; p; p = p->next) {
		if(!p->allow_notify && p->host)
			return 1;
	}
	return 0;
}

/** start task_probe if possible, if no masters for probe start task_transfer
 * returns true if task has been started, and false if the task is already
 * in progress. */
static int
xfr_start_probe(struct auth_xfer* xfr, struct module_env* env,
	struct auth_master* spec)
{
	/* see if we need to start a probe (or maybe it is already in
	 * progress (due to notify)) */
	if(xfr->task_probe->worker == NULL) {
		if(!have_probe_targets(xfr->task_probe->masters) &&
			!(xfr->task_probe->only_lookup &&
			xfr->task_probe->masters != NULL)) {
			/* useless to pick up task_probe, no masters to
			 * probe. Instead attempt to pick up task transfer */
			if(xfr->task_transfer->worker == NULL) {
				xfr_start_transfer(xfr, env, spec);
				return 1;
			}
			/* task transfer already in progress */
			return 0;
		}

		/* pick up the probe task ourselves */
		xfr->task_probe->worker = env->worker;
		xfr->task_probe->env = env;
		xfr->task_probe->cp = NULL;

		/* start the task */
		/* have not seen a new lease yet, this scan */
		xfr->task_probe->have_new_lease = 0;
		/* if this was a timeout, no specific first master to scan */
		/* otherwise, spec is nonNULL the notified master, scan
		 * first and also transfer first from it */
		xfr_probe_start_list(xfr, spec);
		/* setup to start the lookup of hostnames of masters afresh */
		xfr_probe_start_lookups(xfr);
		/* send the probe packet or next send, or end task */
		xfr_probe_send_or_end(xfr, env);
		return 1;
	}
	return 0;
}

/** for task_nextprobe.
 * determine next timeout for auth_xfer. Also (re)sets timer.
 * @param xfr: task structure
 * @param env: module environment, with worker and time.
 * @param failure: set true if timer should be set for failure retry.
 * @param lookup_only: only perform lookups when timer done, 0 sec timeout
 */
static void
xfr_set_timeout(struct auth_xfer* xfr, struct module_env* env,
	int failure, int lookup_only)
{
	struct timeval tv;
	log_assert(xfr->task_nextprobe != NULL);
	log_assert(xfr->task_nextprobe->worker == NULL ||
		xfr->task_nextprobe->worker == env->worker);
	/* normally, nextprobe = startoflease + refresh,
	 * but if expiry is sooner, use that one.
	 * after a failure, use the retry timer instead. */
	xfr->task_nextprobe->next_probe = *env->now;
	if(xfr->lease_time && !failure)
		xfr->task_nextprobe->next_probe = xfr->lease_time;
	
	if(!failure) {
		xfr->task_nextprobe->backoff = 0;
	} else {
		if(xfr->task_nextprobe->backoff == 0)
				xfr->task_nextprobe->backoff = 3;
		else	xfr->task_nextprobe->backoff *= 2;
		if(xfr->task_nextprobe->backoff > AUTH_TRANSFER_MAX_BACKOFF)
			xfr->task_nextprobe->backoff =
				AUTH_TRANSFER_MAX_BACKOFF;
	}

	if(xfr->have_zone) {
		time_t wait = xfr->refresh;
		if(failure) wait = xfr->retry;
		if(xfr->expiry < wait)
			xfr->task_nextprobe->next_probe += xfr->expiry;
		else	xfr->task_nextprobe->next_probe += wait;
		if(failure)
			xfr->task_nextprobe->next_probe +=
				xfr->task_nextprobe->backoff;
		/* put the timer exactly on expiry, if possible */
		if(xfr->lease_time && xfr->lease_time+xfr->expiry <
			xfr->task_nextprobe->next_probe &&
			xfr->lease_time+xfr->expiry > *env->now)
			xfr->task_nextprobe->next_probe =
				xfr->lease_time+xfr->expiry;
	} else {
		xfr->task_nextprobe->next_probe +=
			xfr->task_nextprobe->backoff;
	}

	if(!xfr->task_nextprobe->timer) {
		xfr->task_nextprobe->timer = comm_timer_create(
			env->worker_base, auth_xfer_timer, xfr);
		if(!xfr->task_nextprobe->timer) {
			/* failed to malloc memory. likely zone transfer
			 * also fails for that. skip the timeout */
			char zname[255+1];
			dname_str(xfr->name, zname);
			log_err("cannot allocate timer, no refresh for %s",
				zname);
			return;
		}
	}
	xfr->task_nextprobe->worker = env->worker;
	xfr->task_nextprobe->env = env;
	if(*(xfr->task_nextprobe->env->now) <= xfr->task_nextprobe->next_probe)
		tv.tv_sec = xfr->task_nextprobe->next_probe - 
			*(xfr->task_nextprobe->env->now);
	else	tv.tv_sec = 0;
	if(tv.tv_sec != 0 && lookup_only && xfr->task_probe->masters) {
		/* don't lookup_only, if lookup timeout is 0 anyway,
		 * or if we don't have masters to lookup */
		tv.tv_sec = 0;
		if(xfr->task_probe && xfr->task_probe->worker == NULL)
			xfr->task_probe->only_lookup = 1;
	}
	if(verbosity >= VERB_ALGO) {
		char zname[255+1];
		dname_str(xfr->name, zname);
		verbose(VERB_ALGO, "auth zone %s timeout in %d seconds",
			zname, (int)tv.tv_sec);
	}
	tv.tv_usec = 0;
	comm_timer_set(xfr->task_nextprobe->timer, &tv);
}

/** initial pick up of worker timeouts, ties events to worker event loop */
void
auth_xfer_pickup_initial(struct auth_zones* az, struct module_env* env)
{
	struct auth_xfer* x;
	lock_rw_wrlock(&az->lock);
	RBTREE_FOR(x, struct auth_xfer*, &az->xtree) {
		lock_basic_lock(&x->lock);
		/* set lease_time, because we now have timestamp in env,
		 * (not earlier during startup and apply_cfg), and this
		 * notes the start time when the data was acquired */
		if(x->have_zone)
			x->lease_time = *env->now;
		if(x->task_nextprobe && x->task_nextprobe->worker == NULL) {
			xfr_set_timeout(x, env, 0, 1);
		}
		lock_basic_unlock(&x->lock);
	}
	lock_rw_unlock(&az->lock);
}

void auth_zones_cleanup(struct auth_zones* az)
{
	struct auth_xfer* x;
	lock_rw_wrlock(&az->lock);
	RBTREE_FOR(x, struct auth_xfer*, &az->xtree) {
		lock_basic_lock(&x->lock);
		if(x->task_nextprobe && x->task_nextprobe->worker != NULL) {
			xfr_nextprobe_disown(x);
		}
		if(x->task_probe && x->task_probe->worker != NULL) {
			xfr_probe_disown(x);
		}
		if(x->task_transfer && x->task_transfer->worker != NULL) {
			auth_chunks_delete(x->task_transfer);
			xfr_transfer_disown(x);
		}
		lock_basic_unlock(&x->lock);
	}
	lock_rw_unlock(&az->lock);
}

/**
 * malloc the xfer and tasks
 * @param z: auth_zone with name of zone.
 */
static struct auth_xfer*
auth_xfer_new(struct auth_zone* z)
{
	struct auth_xfer* xfr;
	xfr = (struct auth_xfer*)calloc(1, sizeof(*xfr));
	if(!xfr) return NULL;
	xfr->name = memdup(z->name, z->namelen);
	if(!xfr->name) {
		free(xfr);
		return NULL;
	}
	xfr->node.key = xfr;
	xfr->namelen = z->namelen;
	xfr->namelabs = z->namelabs;
	xfr->dclass = z->dclass;

	xfr->task_nextprobe = (struct auth_nextprobe*)calloc(1,
		sizeof(struct auth_nextprobe));
	if(!xfr->task_nextprobe) {
		free(xfr->name);
		free(xfr);
		return NULL;
	}
	xfr->task_probe = (struct auth_probe*)calloc(1,
		sizeof(struct auth_probe));
	if(!xfr->task_probe) {
		free(xfr->task_nextprobe);
		free(xfr->name);
		free(xfr);
		return NULL;
	}
	xfr->task_transfer = (struct auth_transfer*)calloc(1,
		sizeof(struct auth_transfer));
	if(!xfr->task_transfer) {
		free(xfr->task_probe);
		free(xfr->task_nextprobe);
		free(xfr->name);
		free(xfr);
		return NULL;
	}

	lock_basic_init(&xfr->lock);
	lock_protect(&xfr->lock, &xfr->name, sizeof(xfr->name));
	lock_protect(&xfr->lock, &xfr->namelen, sizeof(xfr->namelen));
	lock_protect(&xfr->lock, xfr->name, xfr->namelen);
	lock_protect(&xfr->lock, &xfr->namelabs, sizeof(xfr->namelabs));
	lock_protect(&xfr->lock, &xfr->dclass, sizeof(xfr->dclass));
	lock_protect(&xfr->lock, &xfr->notify_received, sizeof(xfr->notify_received));
	lock_protect(&xfr->lock, &xfr->notify_serial, sizeof(xfr->notify_serial));
	lock_protect(&xfr->lock, &xfr->zone_expired, sizeof(xfr->zone_expired));
	lock_protect(&xfr->lock, &xfr->have_zone, sizeof(xfr->have_zone));
	lock_protect(&xfr->lock, &xfr->serial, sizeof(xfr->serial));
	lock_protect(&xfr->lock, &xfr->retry, sizeof(xfr->retry));
	lock_protect(&xfr->lock, &xfr->refresh, sizeof(xfr->refresh));
	lock_protect(&xfr->lock, &xfr->expiry, sizeof(xfr->expiry));
	lock_protect(&xfr->lock, &xfr->lease_time, sizeof(xfr->lease_time));
	lock_protect(&xfr->lock, &xfr->task_nextprobe->worker,
		sizeof(xfr->task_nextprobe->worker));
	lock_protect(&xfr->lock, &xfr->task_probe->worker,
		sizeof(xfr->task_probe->worker));
	lock_protect(&xfr->lock, &xfr->task_transfer->worker,
		sizeof(xfr->task_transfer->worker));
	lock_basic_lock(&xfr->lock);
	return xfr;
}

/** Create auth_xfer structure.
 * This populates the have_zone, soa values, and so on times.
 * and sets the timeout, if a zone transfer is needed a short timeout is set.
 * For that the auth_zone itself must exist (and read in zonefile)
 * returns false on alloc failure. */
struct auth_xfer*
auth_xfer_create(struct auth_zones* az, struct auth_zone* z)
{
	struct auth_xfer* xfr;

	/* malloc it */
	xfr = auth_xfer_new(z);
	if(!xfr) {
		log_err("malloc failure");
		return NULL;
	}
	/* insert in tree */
	(void)rbtree_insert(&az->xtree, &xfr->node);
	return xfr;
}

/** create new auth_master structure */
static struct auth_master*
auth_master_new(struct auth_master*** list)
{
	struct auth_master *m;
	m = (struct auth_master*)calloc(1, sizeof(*m));
	if(!m) {
		log_err("malloc failure");
		return NULL;
	}
	/* set first pointer to m, or next pointer of previous element to m */
	(**list) = m;
	/* store m's next pointer as future point to store at */
	(*list) = &(m->next);
	return m;
}

/** dup_prefix : create string from initial part of other string, malloced */
static char*
dup_prefix(char* str, size_t num)
{
	char* result;
	size_t len = strlen(str);
	if(len < num) num = len; /* not more than strlen */
	result = (char*)malloc(num+1);
	if(!result) {
		log_err("malloc failure");
		return result;
	}
	memmove(result, str, num);
	result[num] = 0;
	return result;
}

/** dup string and print error on error */
static char*
dup_all(char* str)
{
	char* result = strdup(str);
	if(!result) {
		log_err("malloc failure");
		return NULL;
	}
	return result;
}

/** find first of two characters */
static char*
str_find_first_of_chars(char* s, char a, char b)
{
	char* ra = strchr(s, a);
	char* rb = strchr(s, b);
	if(!ra) return rb;
	if(!rb) return ra;
	if(ra < rb) return ra;
	return rb;
}

/** parse URL into host and file parts, false on malloc or parse error */
static int
parse_url(char* url, char** host, char** file, int* port, int* ssl)
{
	char* p = url;
	/* parse http://www.example.com/file.htm
	 * or http://127.0.0.1   (index.html)
	 * or https://[::1@1234]/a/b/c/d */
	*ssl = 1;
	*port = AUTH_HTTPS_PORT;

	/* parse http:// or https:// */
	if(strncmp(p, "http://", 7) == 0) {
		p += 7;
		*ssl = 0;
		*port = AUTH_HTTP_PORT;
	} else if(strncmp(p, "https://", 8) == 0) {
		p += 8;
	} else if(strstr(p, "://") && strchr(p, '/') > strstr(p, "://") &&
		strchr(p, ':') >= strstr(p, "://")) {
		char* uri = dup_prefix(p, (size_t)(strstr(p, "://")-p));
		log_err("protocol %s:// not supported (for url %s)",
			uri?uri:"", p);
		free(uri);
		return 0;
	}

	/* parse hostname part */
	if(p[0] == '[') {
		char* end = strchr(p, ']');
		p++; /* skip over [ */
		if(end) {
			*host = dup_prefix(p, (size_t)(end-p));
			if(!*host) return 0;
			p = end+1; /* skip over ] */
		} else {
			*host = dup_all(p);
			if(!*host) return 0;
			p = end;
		}
	} else {
		char* end = str_find_first_of_chars(p, ':', '/');
		if(end) {
			*host = dup_prefix(p, (size_t)(end-p));
			if(!*host) return 0;
		} else {
			*host = dup_all(p);
			if(!*host) return 0;
		}
		p = end; /* at next : or / or NULL */
	}

	/* parse port number */
	if(p && p[0] == ':') {
		char* end = NULL;
		*port = strtol(p+1, &end, 10);
		p = end;
	}

	/* parse filename part */
	while(p && *p == '/')
		p++;
	if(!p || p[0] == 0)
		*file = strdup("index.html");
	else	*file = strdup(p);
	if(!*file) {
		log_err("malloc failure");
		return 0;
	}
	return 1;
}

int
xfer_set_masters(struct auth_master** list, struct config_auth* c,
	int with_http)
{
	struct auth_master* m;
	struct config_strlist* p;
	/* list points to the first, or next pointer for the new element */
	while(*list) {
		list = &( (*list)->next );
	}
	if(with_http)
	  for(p = c->urls; p; p = p->next) {
		m = auth_master_new(&list);
		m->http = 1;
		if(!parse_url(p->str, &m->host, &m->file, &m->port, &m->ssl))
			return 0;
	}
	for(p = c->masters; p; p = p->next) {
		m = auth_master_new(&list);
		m->ixfr = 1; /* this flag is not configurable */
		m->host = strdup(p->str);
		if(!m->host) {
			log_err("malloc failure");
			return 0;
		}
	}
	for(p = c->allow_notify; p; p = p->next) {
		m = auth_master_new(&list);
		m->allow_notify = 1;
		m->host = strdup(p->str);
		if(!m->host) {
			log_err("malloc failure");
			return 0;
		}
	}
	return 1;
}

#define SERIAL_BITS	32
int
compare_serial(uint32_t a, uint32_t b)
{
	const uint32_t cutoff = ((uint32_t) 1 << (SERIAL_BITS - 1));

	if (a == b) {
		return 0;
	} else if ((a < b && b - a < cutoff) || (a > b && a - b > cutoff)) {
		return -1;
	} else {
		return 1;
	}
}
