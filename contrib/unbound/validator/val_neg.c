/*
 * validator/val_neg.c - validator aggressive negative caching functions.
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
 * This file contains helper functions for the validator module.
 * The functions help with aggressive negative caching.
 * This creates new denials of existence, and proofs for absence of types
 * from cached NSEC records.
 */
#include "config.h"
#ifdef HAVE_OPENSSL_SSL_H
#include "openssl/ssl.h"
#define NSEC3_SHA_LEN SHA_DIGEST_LENGTH
#else
#define NSEC3_SHA_LEN 20
#endif
#include "validator/val_neg.h"
#include "validator/val_nsec.h"
#include "validator/val_nsec3.h"
#include "validator/val_utils.h"
#include "util/data/dname.h"
#include "util/data/msgreply.h"
#include "util/log.h"
#include "util/net_help.h"
#include "util/config_file.h"
#include "services/cache/rrset.h"
#include "services/cache/dns.h"
#include "sldns/rrdef.h"
#include "sldns/sbuffer.h"

int val_neg_data_compare(const void* a, const void* b)
{
	struct val_neg_data* x = (struct val_neg_data*)a;
	struct val_neg_data* y = (struct val_neg_data*)b;
	int m;
	return dname_canon_lab_cmp(x->name, x->labs, y->name, y->labs, &m);
}

int val_neg_zone_compare(const void* a, const void* b)
{
	struct val_neg_zone* x = (struct val_neg_zone*)a;
	struct val_neg_zone* y = (struct val_neg_zone*)b;
	int m;
	if(x->dclass != y->dclass) {
		if(x->dclass < y->dclass)
			return -1;
		return 1;
	}
	return dname_canon_lab_cmp(x->name, x->labs, y->name, y->labs, &m);
}

struct val_neg_cache* val_neg_create(struct config_file* cfg, size_t maxiter)
{
	struct val_neg_cache* neg = (struct val_neg_cache*)calloc(1, 
		sizeof(*neg));
	if(!neg) {
		log_err("Could not create neg cache: out of memory");
		return NULL;
	}
	neg->nsec3_max_iter = maxiter;
	neg->max = 1024*1024; /* 1 M is thousands of entries */
	if(cfg) neg->max = cfg->neg_cache_size;
	rbtree_init(&neg->tree, &val_neg_zone_compare);
	lock_basic_init(&neg->lock);
	lock_protect(&neg->lock, neg, sizeof(*neg));
	return neg;
}

size_t val_neg_get_mem(struct val_neg_cache* neg)
{
	size_t result;
	lock_basic_lock(&neg->lock);
	result = sizeof(*neg) + neg->use;
	lock_basic_unlock(&neg->lock);
	return result;
}

/** clear datas on cache deletion */
static void
neg_clear_datas(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct val_neg_data* d = (struct val_neg_data*)n;
	free(d->name);
	free(d);
}

/** clear zones on cache deletion */
static void
neg_clear_zones(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct val_neg_zone* z = (struct val_neg_zone*)n;
	/* delete all the rrset entries in the tree */
	traverse_postorder(&z->tree, &neg_clear_datas, NULL);
	free(z->nsec3_salt);
	free(z->name);
	free(z);
}

void neg_cache_delete(struct val_neg_cache* neg)
{
	if(!neg) return;
	lock_basic_destroy(&neg->lock);
	/* delete all the zones in the tree */
	traverse_postorder(&neg->tree, &neg_clear_zones, NULL);
	free(neg);
}

/**
 * Put data element at the front of the LRU list.
 * @param neg: negative cache with LRU start and end.
 * @param data: this data is fronted.
 */
static void neg_lru_front(struct val_neg_cache* neg, 
	struct val_neg_data* data)
{
	data->prev = NULL;
	data->next = neg->first;
	if(!neg->first)
		neg->last = data;
	else	neg->first->prev = data;
	neg->first = data;
}

/**
 * Remove data element from LRU list.
 * @param neg: negative cache with LRU start and end.
 * @param data: this data is removed from the list.
 */
static void neg_lru_remove(struct val_neg_cache* neg, 
	struct val_neg_data* data)
{
	if(data->prev)
		data->prev->next = data->next;
	else	neg->first = data->next;
	if(data->next)
		data->next->prev = data->prev;
	else	neg->last = data->prev;
}

/**
 * Touch LRU for data element, put it at the start of the LRU list.
 * @param neg: negative cache with LRU start and end.
 * @param data: this data is used.
 */
static void neg_lru_touch(struct val_neg_cache* neg, 
	struct val_neg_data* data)
{
	if(data == neg->first)
		return; /* nothing to do */
	/* remove from current lru position */
	neg_lru_remove(neg, data);
	/* add at front */
	neg_lru_front(neg, data);
}

/**
 * Delete a zone element from the negative cache.
 * May delete other zone elements to keep tree coherent, or
 * only mark the element as 'not in use'.
 * @param neg: negative cache.
 * @param z: zone element to delete.
 */
static void neg_delete_zone(struct val_neg_cache* neg, struct val_neg_zone* z)
{
	struct val_neg_zone* p, *np;
	if(!z) return;
	log_assert(z->in_use);
	log_assert(z->count > 0);
	z->in_use = 0;

	/* go up the tree and reduce counts */
	p = z;
	while(p) {
		log_assert(p->count > 0);
		p->count --;
		p = p->parent;
	}

	/* remove zones with zero count */
	p = z;
	while(p && p->count == 0) {
		np = p->parent;
		(void)rbtree_delete(&neg->tree, &p->node);
		neg->use -= p->len + sizeof(*p);
		free(p->nsec3_salt);
		free(p->name);
		free(p);
		p = np;
	}
}
	
void neg_delete_data(struct val_neg_cache* neg, struct val_neg_data* el)
{
	struct val_neg_zone* z;
	struct val_neg_data* p, *np;
	if(!el) return;
	z = el->zone;
	log_assert(el->in_use);
	log_assert(el->count > 0);
	el->in_use = 0;

	/* remove it from the lru list */
	neg_lru_remove(neg, el);
	
	/* go up the tree and reduce counts */
	p = el;
	while(p) {
		log_assert(p->count > 0);
		p->count --;
		p = p->parent;
	}

	/* delete 0 count items from tree */
	p = el;
	while(p && p->count == 0) {
		np = p->parent;
		(void)rbtree_delete(&z->tree, &p->node);
		neg->use -= p->len + sizeof(*p);
		free(p->name);
		free(p);
		p = np;
	}

	/* check if the zone is now unused */
	if(z->tree.count == 0) {
		neg_delete_zone(neg, z);
	}
}

/**
 * Create more space in negative cache
 * The oldest elements are deleted until enough space is present.
 * Empty zones are deleted.
 * @param neg: negative cache.
 * @param need: how many bytes are needed.
 */
static void neg_make_space(struct val_neg_cache* neg, size_t need)
{
	/* delete elements until enough space or its empty */
	while(neg->last && neg->max < neg->use + need) {
		neg_delete_data(neg, neg->last);
	}
}

struct val_neg_zone* neg_find_zone(struct val_neg_cache* neg, 
	uint8_t* nm, size_t len, uint16_t dclass)
{
	struct val_neg_zone lookfor;
	struct val_neg_zone* result;
	lookfor.node.key = &lookfor;
	lookfor.name = nm;
	lookfor.len = len;
	lookfor.labs = dname_count_labels(lookfor.name);
	lookfor.dclass = dclass;

	result = (struct val_neg_zone*)
		rbtree_search(&neg->tree, lookfor.node.key);
	return result;
}

/**
 * Find the given data
 * @param zone: negative zone
 * @param nm: what to look for.
 * @param len: length of nm
 * @param labs: labels in nm
 * @return data or NULL if not found.
 */
static struct val_neg_data* neg_find_data(struct val_neg_zone* zone, 
	uint8_t* nm, size_t len, int labs)
{
	struct val_neg_data lookfor;
	struct val_neg_data* result;
	lookfor.node.key = &lookfor;
	lookfor.name = nm;
	lookfor.len = len;
	lookfor.labs = labs;

	result = (struct val_neg_data*)
		rbtree_search(&zone->tree, lookfor.node.key);
	return result;
}

/**
 * Calculate space needed for the data and all its parents
 * @param rep: NSEC entries.
 * @return size.
 */
static size_t calc_data_need(struct reply_info* rep)
{
	uint8_t* d;
	size_t i, len, res = 0;

	for(i=rep->an_numrrsets; i<rep->an_numrrsets+rep->ns_numrrsets; i++) {
		if(ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_NSEC) {
			d = rep->rrsets[i]->rk.dname;
			len = rep->rrsets[i]->rk.dname_len;
			res = sizeof(struct val_neg_data) + len;
			while(!dname_is_root(d)) {
				log_assert(len > 1); /* not root label */
				dname_remove_label(&d, &len);
				res += sizeof(struct val_neg_data) + len;
			}
		}
	}
	return res;
}

/**
 * Calculate space needed for zone and all its parents
 * @param d: name of zone
 * @param len: length of name
 * @return size.
 */
static size_t calc_zone_need(uint8_t* d, size_t len)
{
	size_t res = sizeof(struct val_neg_zone) + len;
	while(!dname_is_root(d)) {
		log_assert(len > 1); /* not root label */
		dname_remove_label(&d, &len);
		res += sizeof(struct val_neg_zone) + len;
	}
	return res;
}

/**
 * Find closest existing parent zone of the given name.
 * @param neg: negative cache.
 * @param nm: name to look for
 * @param nm_len: length of nm
 * @param labs: labelcount of nm.
 * @param qclass: class.
 * @return the zone or NULL if none found.
 */
static struct val_neg_zone* neg_closest_zone_parent(struct val_neg_cache* neg,
	uint8_t* nm, size_t nm_len, int labs, uint16_t qclass)
{
	struct val_neg_zone key;
	struct val_neg_zone* result;
	rbnode_type* res = NULL;
	key.node.key = &key;
	key.name = nm;
	key.len = nm_len;
	key.labs = labs;
	key.dclass = qclass;
	if(rbtree_find_less_equal(&neg->tree, &key, &res)) {
		/* exact match */
		result = (struct val_neg_zone*)res;
	} else {
		/* smaller element (or no element) */
		int m;
		result = (struct val_neg_zone*)res;
		if(!result || result->dclass != qclass)
			return NULL;
		/* count number of labels matched */
		(void)dname_lab_cmp(result->name, result->labs, key.name,
			key.labs, &m);
		while(result) { /* go up until qname is subdomain of stub */
			if(result->labs <= m)
				break;
			result = result->parent;
		}
	}
	return result;
}

/**
 * Find closest existing parent data for the given name.
 * @param zone: to look in.
 * @param nm: name to look for
 * @param nm_len: length of nm
 * @param labs: labelcount of nm.
 * @return the data or NULL if none found.
 */
static struct val_neg_data* neg_closest_data_parent(
	struct val_neg_zone* zone, uint8_t* nm, size_t nm_len, int labs)
{
	struct val_neg_data key;
	struct val_neg_data* result;
	rbnode_type* res = NULL;
	key.node.key = &key;
	key.name = nm;
	key.len = nm_len;
	key.labs = labs;
	if(rbtree_find_less_equal(&zone->tree, &key, &res)) {
		/* exact match */
		result = (struct val_neg_data*)res;
	} else {
		/* smaller element (or no element) */
		int m;
		result = (struct val_neg_data*)res;
		if(!result)
			return NULL;
		/* count number of labels matched */
		(void)dname_lab_cmp(result->name, result->labs, key.name,
			key.labs, &m);
		while(result) { /* go up until qname is subdomain of stub */
			if(result->labs <= m)
				break;
			result = result->parent;
		}
	}
	return result;
}

/**
 * Create a single zone node
 * @param nm: name for zone (copied)
 * @param nm_len: length of name
 * @param labs: labels in name.
 * @param dclass: class of zone, host order.
 * @return new zone or NULL on failure
 */
static struct val_neg_zone* neg_setup_zone_node(
	uint8_t* nm, size_t nm_len, int labs, uint16_t dclass)
{
	struct val_neg_zone* zone = 
		(struct val_neg_zone*)calloc(1, sizeof(*zone));
	if(!zone) {
		return NULL;
	}
	zone->node.key = zone;
	zone->name = memdup(nm, nm_len);
	if(!zone->name) {
		free(zone);
		return NULL;
	}
	zone->len = nm_len;
	zone->labs = labs;
	zone->dclass = dclass;

	rbtree_init(&zone->tree, &val_neg_data_compare);
	return zone;
}

/**
 * Create a linked list of parent zones, starting at longname ending on
 * the parent (can be NULL, creates to the root).
 * @param nm: name for lowest in chain
 * @param nm_len: length of name
 * @param labs: labels in name.
 * @param dclass: class of zone.
 * @param parent: NULL for to root, else so it fits under here.
 * @return zone; a chain of zones and their parents up to the parent.
 *  	or NULL on malloc failure
 */
static struct val_neg_zone* neg_zone_chain(
	uint8_t* nm, size_t nm_len, int labs, uint16_t dclass,
	struct val_neg_zone* parent)
{
	int i;
	int tolabs = parent?parent->labs:0;
	struct val_neg_zone* zone, *prev = NULL, *first = NULL;

	/* create the new subtree, i is labelcount of current creation */
	/* this creates a 'first' to z->parent=NULL list of zones */
	for(i=labs; i!=tolabs; i--) {
		/* create new item */
		zone = neg_setup_zone_node(nm, nm_len, i, dclass);
		if(!zone) {
			/* need to delete other allocations in this routine!*/
			struct val_neg_zone* p=first, *np;
			while(p) {
				np = p->parent;
				free(p->name);
				free(p);
				p = np;
			}
			return NULL;
		}
		if(i == labs) {
			first = zone;
		} else {
			prev->parent = zone;
		}
		/* prepare for next name */
		prev = zone;
		dname_remove_label(&nm, &nm_len);
	}
	return first;
}	

void val_neg_zone_take_inuse(struct val_neg_zone* zone)
{
	if(!zone->in_use) {
		struct val_neg_zone* p;
		zone->in_use = 1;
		/* increase usage count of all parents */
		for(p=zone; p; p = p->parent) {
			p->count++;
		}
	}
}

struct val_neg_zone* neg_create_zone(struct val_neg_cache* neg,
	uint8_t* nm, size_t nm_len, uint16_t dclass)
{
	struct val_neg_zone* zone;
	struct val_neg_zone* parent;
	struct val_neg_zone* p, *np;
	int labs = dname_count_labels(nm);

	/* find closest enclosing parent zone that (still) exists */
	parent = neg_closest_zone_parent(neg, nm, nm_len, labs, dclass);
	if(parent && query_dname_compare(parent->name, nm) == 0)
		return parent; /* already exists, weird */
	/* if parent exists, it is in use */
	log_assert(!parent || parent->count > 0);
	zone = neg_zone_chain(nm, nm_len, labs, dclass, parent);
	if(!zone) {
		return NULL;
	}

	/* insert the list of zones into the tree */
	p = zone;
	while(p) {
		np = p->parent;
		/* mem use */
		neg->use += sizeof(struct val_neg_zone) + p->len;
		/* insert in tree */
		(void)rbtree_insert(&neg->tree, &p->node);
		/* last one needs proper parent pointer */
		if(np == NULL)
			p->parent = parent;
		p = np;
	}
	return zone;
}

/** find zone name of message, returns the SOA record */
static struct ub_packed_rrset_key* reply_find_soa(struct reply_info* rep)
{
	size_t i;
	for(i=rep->an_numrrsets; i< rep->an_numrrsets+rep->ns_numrrsets; i++){
		if(ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_SOA)
			return rep->rrsets[i];
	}
	return NULL;
}

/** see if the reply has NSEC records worthy of caching */
static int reply_has_nsec(struct reply_info* rep)
{
	size_t i;
	struct packed_rrset_data* d;
	if(rep->security != sec_status_secure)
		return 0;
	for(i=rep->an_numrrsets; i< rep->an_numrrsets+rep->ns_numrrsets; i++){
		if(ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_NSEC) {
			d = (struct packed_rrset_data*)rep->rrsets[i]->
				entry.data;
			if(d->security == sec_status_secure)
				return 1;
		}
	}
	return 0;
}


/**
 * Create single node of data element.
 * @param nm: name (copied)
 * @param nm_len: length of name
 * @param labs: labels in name.
 * @return element with name nm, or NULL malloc failure.
 */
static struct val_neg_data* neg_setup_data_node(
	uint8_t* nm, size_t nm_len, int labs)
{
	struct val_neg_data* el;
	el = (struct val_neg_data*)calloc(1, sizeof(*el));
	if(!el) {
		return NULL;
	}
	el->node.key = el;
	el->name = memdup(nm, nm_len);
	if(!el->name) {
		free(el);
		return NULL;
	}
	el->len = nm_len;
	el->labs = labs;
	return el;
}

/**
 * Create chain of data element and parents
 * @param nm: name
 * @param nm_len: length of name
 * @param labs: labels in name.
 * @param parent: up to where to make, if NULL up to root label.
 * @return lowest element with name nm, or NULL malloc failure.
 */
static struct val_neg_data* neg_data_chain(
	uint8_t* nm, size_t nm_len, int labs, struct val_neg_data* parent)
{
	int i;
	int tolabs = parent?parent->labs:0;
	struct val_neg_data* el, *first = NULL, *prev = NULL;

	/* create the new subtree, i is labelcount of current creation */
	/* this creates a 'first' to z->parent=NULL list of zones */
	for(i=labs; i!=tolabs; i--) {
		/* create new item */
		el = neg_setup_data_node(nm, nm_len, i);
		if(!el) {
			/* need to delete other allocations in this routine!*/
			struct val_neg_data* p = first, *np;
			while(p) {
				np = p->parent;
				free(p->name);
				free(p);
				p = np;
			}
			return NULL;
		}
		if(i == labs) {
			first = el;
		} else {
			prev->parent = el;
		}

		/* prepare for next name */
		prev = el;
		dname_remove_label(&nm, &nm_len);
	}
	return first;
}

/**
 * Remove NSEC records between start and end points.
 * By walking the tree, the tree is sorted canonically.
 * @param neg: negative cache.
 * @param zone: the zone
 * @param el: element to start walking at.
 * @param nsec: the nsec record with the end point
 */
static void wipeout(struct val_neg_cache* neg, struct val_neg_zone* zone, 
	struct val_neg_data* el, struct ub_packed_rrset_key* nsec)
{
	struct packed_rrset_data* d = (struct packed_rrset_data*)nsec->
		entry.data;
	uint8_t* end;
	size_t end_len;
	int end_labs, m;
	rbnode_type* walk, *next;
	struct val_neg_data* cur;
	uint8_t buf[257];
	/* get endpoint */
	if(!d || d->count == 0 || d->rr_len[0] < 2+1)
		return;
	if(ntohs(nsec->rk.type) == LDNS_RR_TYPE_NSEC) {
		end = d->rr_data[0]+2;
		end_len = dname_valid(end, d->rr_len[0]-2);
		end_labs = dname_count_labels(end);
	} else {
		/* NSEC3 */
		if(!nsec3_get_nextowner_b32(nsec, 0, buf, sizeof(buf)))
			return;
		end = buf;
		end_labs = dname_count_size_labels(end, &end_len);
	}

	/* sanity check, both owner and end must be below the zone apex */
	if(!dname_subdomain_c(el->name, zone->name) || 
		!dname_subdomain_c(end, zone->name))
		return;

	/* detect end of zone NSEC ; wipe until the end of zone */
	if(query_dname_compare(end, zone->name) == 0) {
		end = NULL;
	}

	walk = rbtree_next(&el->node);
	while(walk && walk != RBTREE_NULL) {
		cur = (struct val_neg_data*)walk;
		/* sanity check: must be larger than start */
		if(dname_canon_lab_cmp(cur->name, cur->labs, 
			el->name, el->labs, &m) <= 0) {
			/* r == 0 skip original record. */
			/* r < 0  too small! */
			walk = rbtree_next(walk);
			continue;
		}
		/* stop at endpoint, also data at empty nonterminals must be
		 * removed (no NSECs there) so everything between 
		 * start and end */
		if(end && dname_canon_lab_cmp(cur->name, cur->labs,
			end, end_labs, &m) >= 0) {
			break;
		}
		/* this element has to be deleted, but we cannot do it
		 * now, because we are walking the tree still ... */
		/* get the next element: */
		next = rbtree_next(walk);
		/* now delete the original element, this may trigger
		 * rbtree rebalances, but really, the next element is
		 * the one we need.
		 * But it may trigger delete of other data and the
		 * entire zone. However, if that happens, this is done
		 * by deleting the *parents* of the element for deletion,
		 * and maybe also the entire zone if it is empty. 
		 * But parents are smaller in canonical compare, thus,
		 * if a larger element exists, then it is not a parent,
		 * it cannot get deleted, the zone cannot get empty.
		 * If the next==NULL, then zone can be empty. */
		if(cur->in_use)
			neg_delete_data(neg, cur);
		walk = next;
	}
}

void neg_insert_data(struct val_neg_cache* neg, 
	struct val_neg_zone* zone, struct ub_packed_rrset_key* nsec)
{
	struct packed_rrset_data* d;
	struct val_neg_data* parent;
	struct val_neg_data* el;
	uint8_t* nm = nsec->rk.dname;
	size_t nm_len = nsec->rk.dname_len;
	int labs = dname_count_labels(nsec->rk.dname);

	d = (struct packed_rrset_data*)nsec->entry.data;
	if( !(d->security == sec_status_secure ||
		(d->security == sec_status_unchecked && d->rrsig_count > 0)))
		return;
	log_nametypeclass(VERB_ALGO, "negcache rr", 
		nsec->rk.dname, ntohs(nsec->rk.type), 
		ntohs(nsec->rk.rrset_class));

	/* find closest enclosing parent data that (still) exists */
	parent = neg_closest_data_parent(zone, nm, nm_len, labs);
	if(parent && query_dname_compare(parent->name, nm) == 0) {
		/* perfect match already exists */
		log_assert(parent->count > 0);
		el = parent;
	} else { 
		struct val_neg_data* p, *np;

		/* create subtree for perfect match */
		/* if parent exists, it is in use */
		log_assert(!parent || parent->count > 0);

		el = neg_data_chain(nm, nm_len, labs, parent);
		if(!el) {
			log_err("out of memory inserting NSEC negative cache");
			return;
		}
		el->in_use = 0; /* set on below */

		/* insert the list of zones into the tree */
		p = el;
		while(p) {
			np = p->parent;
			/* mem use */
			neg->use += sizeof(struct val_neg_data) + p->len;
			/* insert in tree */
			p->zone = zone;
			(void)rbtree_insert(&zone->tree, &p->node);
			/* last one needs proper parent pointer */
			if(np == NULL)
				p->parent = parent;
			p = np;
		}
	}

	if(!el->in_use) {
		struct val_neg_data* p;

		el->in_use = 1;
		/* increase usage count of all parents */
		for(p=el; p; p = p->parent) {
			p->count++;
		}

		neg_lru_front(neg, el);
	} else {
		/* in use, bring to front, lru */
		neg_lru_touch(neg, el);
	}

	/* if nsec3 store last used parameters */
	if(ntohs(nsec->rk.type) == LDNS_RR_TYPE_NSEC3) {
		int h;
		uint8_t* s;
		size_t slen, it;
		if(nsec3_get_params(nsec, 0, &h, &it, &s, &slen) &&
			it <= neg->nsec3_max_iter &&
			(h != zone->nsec3_hash || it != zone->nsec3_iter ||
			slen != zone->nsec3_saltlen || 
			memcmp(zone->nsec3_salt, s, slen) != 0)) {

			if(slen > 0) {
				uint8_t* sa = memdup(s, slen);
				if(sa) {
					free(zone->nsec3_salt);
					zone->nsec3_salt = sa;
					zone->nsec3_saltlen = slen;
					zone->nsec3_iter = it;
					zone->nsec3_hash = h;
				}
			} else {
				free(zone->nsec3_salt);
				zone->nsec3_salt = NULL;
				zone->nsec3_saltlen = 0;
				zone->nsec3_iter = it;
				zone->nsec3_hash = h;
			}
		}
	}

	/* wipe out the cache items between NSEC start and end */
	wipeout(neg, zone, el, nsec);
}

/** see if the reply has signed NSEC records and return the signer */
static uint8_t* reply_nsec_signer(struct reply_info* rep, size_t* signer_len,
	uint16_t* dclass)
{
	size_t i;
	struct packed_rrset_data* d;
	uint8_t* s;
	for(i=rep->an_numrrsets; i< rep->an_numrrsets+rep->ns_numrrsets; i++){
		if(ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_NSEC ||
			ntohs(rep->rrsets[i]->rk.type) == LDNS_RR_TYPE_NSEC3) {
			d = (struct packed_rrset_data*)rep->rrsets[i]->
				entry.data;
			/* return first signer name of first NSEC */
			if(d->rrsig_count != 0) {
				val_find_rrset_signer(rep->rrsets[i],
					&s, signer_len);
				if(s && *signer_len) {
					*dclass = ntohs(rep->rrsets[i]->
						rk.rrset_class);
					return s;
				}
			}
		}
	}
	return 0;
}

void val_neg_addreply(struct val_neg_cache* neg, struct reply_info* rep)
{
	size_t i, need;
	struct ub_packed_rrset_key* soa;
	uint8_t* dname = NULL;
	size_t dname_len;
	uint16_t rrset_class;
	struct val_neg_zone* zone;
	/* see if secure nsecs inside */
	if(!reply_has_nsec(rep))
		return;
	/* find the zone name in message */
	if((soa = reply_find_soa(rep))) {
		dname = soa->rk.dname;
		dname_len = soa->rk.dname_len;
		rrset_class = ntohs(soa->rk.rrset_class);
	}
	else {
		/* No SOA in positive (wildcard) answer. Use signer from the 
		 * validated answer RRsets' signature. */
		if(!(dname = reply_nsec_signer(rep, &dname_len, &rrset_class)))
			return;
	}

	log_nametypeclass(VERB_ALGO, "negcache insert for zone",
		dname, LDNS_RR_TYPE_SOA, rrset_class);
	
	/* ask for enough space to store all of it */
	need = calc_data_need(rep) + 
		calc_zone_need(dname, dname_len);
	lock_basic_lock(&neg->lock);
	neg_make_space(neg, need);

	/* find or create the zone entry */
	zone = neg_find_zone(neg, dname, dname_len, rrset_class);
	if(!zone) {
		if(!(zone = neg_create_zone(neg, dname, dname_len,
			rrset_class))) {
			lock_basic_unlock(&neg->lock);
			log_err("out of memory adding negative zone");
			return;
		}
	}
	val_neg_zone_take_inuse(zone);

	/* insert the NSECs */
	for(i=rep->an_numrrsets; i< rep->an_numrrsets+rep->ns_numrrsets; i++){
		if(ntohs(rep->rrsets[i]->rk.type) != LDNS_RR_TYPE_NSEC)
			continue;
		if(!dname_subdomain_c(rep->rrsets[i]->rk.dname, 
			zone->name)) continue;
		/* insert NSEC into this zone's tree */
		neg_insert_data(neg, zone, rep->rrsets[i]);
	}
	if(zone->tree.count == 0) {
		/* remove empty zone if inserts failed */
		neg_delete_zone(neg, zone);
	}
	lock_basic_unlock(&neg->lock);
}

/**
 * Lookup closest data record. For NSEC denial.
 * @param zone: zone to look in
 * @param qname: name to look for.
 * @param len: length of name
 * @param labs: labels in name
 * @param data: data element, exact or smaller or NULL
 * @return true if exact match.
 */
static int neg_closest_data(struct val_neg_zone* zone,
	uint8_t* qname, size_t len, int labs, struct val_neg_data** data)
{
	struct val_neg_data key;
	rbnode_type* r;
	key.node.key = &key;
	key.name = qname;
	key.len = len;
	key.labs = labs;
	if(rbtree_find_less_equal(&zone->tree, &key, &r)) {
		/* exact match */
		*data = (struct val_neg_data*)r;
		return 1;
	} else {
		/* smaller match */
		*data = (struct val_neg_data*)r;
		return 0;
	}
}

int val_neg_dlvlookup(struct val_neg_cache* neg, uint8_t* qname, size_t len,
        uint16_t qclass, struct rrset_cache* rrset_cache, time_t now)
{
	/* lookup closest zone */
	struct val_neg_zone* zone;
	struct val_neg_data* data;
	int labs;
	struct ub_packed_rrset_key* nsec;
	struct packed_rrset_data* d;
	uint32_t flags;
	uint8_t* wc;
	struct query_info qinfo;
	if(!neg) return 0;

	log_nametypeclass(VERB_ALGO, "negcache dlvlookup", qname, 
		LDNS_RR_TYPE_DLV, qclass);
	
	labs = dname_count_labels(qname);
	lock_basic_lock(&neg->lock);
	zone = neg_closest_zone_parent(neg, qname, len, labs, qclass);
	while(zone && !zone->in_use)
		zone = zone->parent;
	if(!zone) {
		lock_basic_unlock(&neg->lock);
		return 0;
	}
	log_nametypeclass(VERB_ALGO, "negcache zone", zone->name, 0, 
		zone->dclass);

	/* DLV is defined to use NSEC only */
	if(zone->nsec3_hash) {
		lock_basic_unlock(&neg->lock);
		return 0;
	}

	/* lookup closest data record */
	(void)neg_closest_data(zone, qname, len, labs, &data);
	while(data && !data->in_use)
		data = data->parent;
	if(!data) {
		lock_basic_unlock(&neg->lock);
		return 0;
	}
	log_nametypeclass(VERB_ALGO, "negcache rr", data->name, 
		LDNS_RR_TYPE_NSEC, zone->dclass);

	/* lookup rrset in rrset cache */
	flags = 0;
	if(query_dname_compare(data->name, zone->name) == 0)
		flags = PACKED_RRSET_NSEC_AT_APEX;
	nsec = rrset_cache_lookup(rrset_cache, data->name, data->len,
		LDNS_RR_TYPE_NSEC, zone->dclass, flags, now, 0);

	/* check if secure and TTL ok */
	if(!nsec) {
		lock_basic_unlock(&neg->lock);
		return 0;
	}
	d = (struct packed_rrset_data*)nsec->entry.data;
	if(!d || now > d->ttl) {
		lock_rw_unlock(&nsec->entry.lock);
		/* delete data record if expired */
		neg_delete_data(neg, data);
		lock_basic_unlock(&neg->lock);
		return 0;
	}
	if(d->security != sec_status_secure) {
		lock_rw_unlock(&nsec->entry.lock);
		neg_delete_data(neg, data);
		lock_basic_unlock(&neg->lock);
		return 0;
	}
	verbose(VERB_ALGO, "negcache got secure rrset");

	/* check NSEC security */
	/* check if NSEC proves no DLV type exists */
	/* check if NSEC proves NXDOMAIN for qname */
	qinfo.qname = qname;
	qinfo.qtype = LDNS_RR_TYPE_DLV;
	qinfo.qclass = qclass;
	qinfo.local_alias = NULL;
	if(!nsec_proves_nodata(nsec, &qinfo, &wc) &&
		!val_nsec_proves_name_error(nsec, qname)) {
		/* the NSEC is not a denial for the DLV */
		lock_rw_unlock(&nsec->entry.lock);
		lock_basic_unlock(&neg->lock);
		verbose(VERB_ALGO, "negcache not proven");
		return 0;
	}
	/* so the NSEC was a NODATA proof, or NXDOMAIN proof. */

	/* no need to check for wildcard NSEC; no wildcards in DLV repos */
	/* no need to lookup SOA record for client; no response message */

	lock_rw_unlock(&nsec->entry.lock);
	/* if OK touch the LRU for neg_data element */
	neg_lru_touch(neg, data);
	lock_basic_unlock(&neg->lock);
	verbose(VERB_ALGO, "negcache DLV denial proven");
	return 1;
}

void val_neg_addreferral(struct val_neg_cache* neg, struct reply_info* rep,
	uint8_t* zone_name)
{
	size_t i, need;
	uint8_t* signer;
	size_t signer_len;
	uint16_t dclass;
	struct val_neg_zone* zone;
	/* no SOA in this message, find RRSIG over NSEC's signer name.
	 * note the NSEC records are maybe not validated yet */
	signer = reply_nsec_signer(rep, &signer_len, &dclass);
	if(!signer) 
		return;
	if(!dname_subdomain_c(signer, zone_name)) {
		/* the signer is not in the bailiwick, throw it out */
		return;
	}

	log_nametypeclass(VERB_ALGO, "negcache insert referral ",
		signer, LDNS_RR_TYPE_NS, dclass);
	
	/* ask for enough space to store all of it */
	need = calc_data_need(rep) + calc_zone_need(signer, signer_len);
	lock_basic_lock(&neg->lock);
	neg_make_space(neg, need);

	/* find or create the zone entry */
	zone = neg_find_zone(neg, signer, signer_len, dclass);
	if(!zone) {
		if(!(zone = neg_create_zone(neg, signer, signer_len, 
			dclass))) {
			lock_basic_unlock(&neg->lock);
			log_err("out of memory adding negative zone");
			return;
		}
	}
	val_neg_zone_take_inuse(zone);

	/* insert the NSECs */
	for(i=rep->an_numrrsets; i< rep->an_numrrsets+rep->ns_numrrsets; i++){
		if(ntohs(rep->rrsets[i]->rk.type) != LDNS_RR_TYPE_NSEC &&
			ntohs(rep->rrsets[i]->rk.type) != LDNS_RR_TYPE_NSEC3)
			continue;
		if(!dname_subdomain_c(rep->rrsets[i]->rk.dname, 
			zone->name)) continue;
		/* insert NSEC into this zone's tree */
		neg_insert_data(neg, zone, rep->rrsets[i]);
	}
	if(zone->tree.count == 0) {
		/* remove empty zone if inserts failed */
		neg_delete_zone(neg, zone);
	}
	lock_basic_unlock(&neg->lock);
}

/**
 * Check that an NSEC3 rrset does not have a type set.
 * None of the nsec3s in a hash-collision are allowed to have the type.
 * (since we do not know which one is the nsec3 looked at, flags, ..., we
 * ignore the cached item and let it bypass negative caching).
 * @param k: the nsec3 rrset to check.
 * @param t: type to check
 * @return true if no RRs have the type.
 */
static int nsec3_no_type(struct ub_packed_rrset_key* k, uint16_t t)
{
	int count = (int)((struct packed_rrset_data*)k->entry.data)->count;
	int i;
	for(i=0; i<count; i++)
		if(nsec3_has_type(k, i, t))
			return 0;
	return 1;
}

/**
 * See if rrset exists in rrset cache.
 * If it does, the bit is checked, and if not expired, it is returned
 * allocated in region.
 * @param rrset_cache: rrset cache
 * @param qname: to lookup rrset name
 * @param qname_len: length of qname.
 * @param qtype: type of rrset to lookup, host order
 * @param qclass: class of rrset to lookup, host order
 * @param flags: flags for rrset to lookup
 * @param region: where to alloc result
 * @param checkbit: if true, a bit in the nsec typemap is checked for absence.
 * @param checktype: which bit to check
 * @param now: to check ttl against
 * @return rrset or NULL
 */
static struct ub_packed_rrset_key*
grab_nsec(struct rrset_cache* rrset_cache, uint8_t* qname, size_t qname_len,
	uint16_t qtype, uint16_t qclass, uint32_t flags, 
	struct regional* region, int checkbit, uint16_t checktype, 
	time_t now)
{
	struct ub_packed_rrset_key* r, *k = rrset_cache_lookup(rrset_cache,
		qname, qname_len, qtype, qclass, flags, now, 0);
	struct packed_rrset_data* d;
	if(!k) return NULL;
	d = (struct packed_rrset_data*)k->entry.data;
	if(d->ttl < now) {
		lock_rw_unlock(&k->entry.lock);
		return NULL;
	}
	/* only secure or unchecked records that have signatures. */
	if( ! ( d->security == sec_status_secure ||
		(d->security == sec_status_unchecked &&
		d->rrsig_count > 0) ) ) {
		lock_rw_unlock(&k->entry.lock);
		return NULL;
	}
	/* check if checktype is absent */
	if(checkbit && (
		(qtype == LDNS_RR_TYPE_NSEC && nsec_has_type(k, checktype)) ||
		(qtype == LDNS_RR_TYPE_NSEC3 && !nsec3_no_type(k, checktype))
		)) {
		lock_rw_unlock(&k->entry.lock);
		return NULL;
	}
	/* looks OK! copy to region and return it */
	r = packed_rrset_copy_region(k, region, now);
	/* if it failed, we return the NULL */
	lock_rw_unlock(&k->entry.lock);
	return r;
}

/**
 * Get best NSEC record for qname. Might be matching, covering or totally
 * useless.
 * @param neg_cache: neg cache
 * @param qname: to lookup rrset name
 * @param qname_len: length of qname.
 * @param qclass: class of rrset to lookup, host order
 * @param rrset_cache: rrset cache
 * @param now: to check ttl against
 * @param region: where to alloc result
 * @return rrset or NULL
 */
static struct ub_packed_rrset_key*
neg_find_nsec(struct val_neg_cache* neg_cache, uint8_t* qname, size_t qname_len,
	uint16_t qclass, struct rrset_cache* rrset_cache, time_t now,
	struct regional* region)
{
	int labs;
	uint32_t flags;
	struct val_neg_zone* zone;
	struct val_neg_data* data;
	struct ub_packed_rrset_key* nsec;

	labs = dname_count_labels(qname);
	lock_basic_lock(&neg_cache->lock);
	zone = neg_closest_zone_parent(neg_cache, qname, qname_len, labs,
		qclass);
	while(zone && !zone->in_use)
		zone = zone->parent;
	if(!zone) {
		lock_basic_unlock(&neg_cache->lock);
		return NULL;
	}

	/* NSEC only for now */
	if(zone->nsec3_hash) {
		lock_basic_unlock(&neg_cache->lock);
		return NULL;
	}

	/* ignore return value, don't care if it is an exact or smaller match */
	(void)neg_closest_data(zone, qname, qname_len, labs, &data);
	if(!data) {
		lock_basic_unlock(&neg_cache->lock);
		return NULL;
	}

	/* ENT nodes are not in use, try the previous node. If the previous node
	 * is not in use, we don't have an useful NSEC and give up. */
	if(!data->in_use) {
		data = (struct val_neg_data*)rbtree_previous((rbnode_type*)data);
		if((rbnode_type*)data == RBTREE_NULL || !data->in_use) {
			lock_basic_unlock(&neg_cache->lock);
			return NULL;
		}
	}

	flags = 0;
	if(query_dname_compare(data->name, zone->name) == 0)
		flags = PACKED_RRSET_NSEC_AT_APEX;

	nsec = grab_nsec(rrset_cache, data->name, data->len, LDNS_RR_TYPE_NSEC,
		zone->dclass, flags, region, 0, 0, now);
	lock_basic_unlock(&neg_cache->lock);
	return nsec;
}

/** find nsec3 closest encloser in neg cache */
static struct val_neg_data*
neg_find_nsec3_ce(struct val_neg_zone* zone, uint8_t* qname, size_t qname_len,
		int qlabs, sldns_buffer* buf, uint8_t* hashnc, size_t* nclen)
{
	struct val_neg_data* data;
	uint8_t hashce[NSEC3_SHA_LEN];
	uint8_t b32[257];
	size_t celen, b32len;

	*nclen = 0;
	while(qlabs > 0) {
		/* hash */
		if(!(celen=nsec3_get_hashed(buf, qname, qname_len, 
			zone->nsec3_hash, zone->nsec3_iter, zone->nsec3_salt, 
			zone->nsec3_saltlen, hashce, sizeof(hashce))))
			return NULL;
		if(!(b32len=nsec3_hash_to_b32(hashce, celen, zone->name,
			zone->len, b32, sizeof(b32))))
			return NULL;

		/* lookup (exact match only) */
		data = neg_find_data(zone, b32, b32len, zone->labs+1);
		if(data && data->in_use) {
			/* found ce match! */
			return data;
		}

		*nclen = celen;
		memmove(hashnc, hashce, celen);
		dname_remove_label(&qname, &qname_len);
		qlabs --;
	}
	return NULL;
}

/** check nsec3 parameters on nsec3 rrset with current zone values */
static int
neg_params_ok(struct val_neg_zone* zone, struct ub_packed_rrset_key* rrset)
{
	int h;
	uint8_t* s;
	size_t slen, it;
	if(!nsec3_get_params(rrset, 0, &h, &it, &s, &slen))
		return 0;
	return (h == zone->nsec3_hash && it == zone->nsec3_iter &&
		slen == zone->nsec3_saltlen &&
		memcmp(zone->nsec3_salt, s, slen) == 0);
}

/** get next closer for nsec3 proof */
static struct ub_packed_rrset_key*
neg_nsec3_getnc(struct val_neg_zone* zone, uint8_t* hashnc, size_t nclen,
	struct rrset_cache* rrset_cache, struct regional* region, 
	time_t now, uint8_t* b32, size_t maxb32)
{
	struct ub_packed_rrset_key* nc_rrset;
	struct val_neg_data* data;
	size_t b32len;

	if(!(b32len=nsec3_hash_to_b32(hashnc, nclen, zone->name,
		zone->len, b32, maxb32)))
		return NULL;
	(void)neg_closest_data(zone, b32, b32len, zone->labs+1, &data);
	if(!data && zone->tree.count != 0) {
		/* could be before the first entry ; return the last
		 * entry (possibly the rollover nsec3 at end) */
		data = (struct val_neg_data*)rbtree_last(&zone->tree);
	}
	while(data && !data->in_use)
		data = data->parent;
	if(!data)
		return NULL;
	/* got a data element in tree, grab it */
	nc_rrset = grab_nsec(rrset_cache, data->name, data->len, 
		LDNS_RR_TYPE_NSEC3, zone->dclass, 0, region, 0, 0, now);
	if(!nc_rrset)
		return NULL;
	if(!neg_params_ok(zone, nc_rrset))
		return NULL;
	return nc_rrset;
}

/** neg cache nsec3 proof procedure*/
static struct dns_msg*
neg_nsec3_proof_ds(struct val_neg_zone* zone, uint8_t* qname, size_t qname_len,
		int qlabs, sldns_buffer* buf, struct rrset_cache* rrset_cache,
		struct regional* region, time_t now, uint8_t* topname)
{
	struct dns_msg* msg;
	struct val_neg_data* data;
	uint8_t hashnc[NSEC3_SHA_LEN];
	size_t nclen;
	struct ub_packed_rrset_key* ce_rrset, *nc_rrset;
	struct nsec3_cached_hash c;
	uint8_t nc_b32[257];

	/* for NSEC3 ; determine the closest encloser for which we
	 * can find an exact match. Remember the hashed lower name,
	 * since that is the one we need a closest match for. 
	 * If we find a match straight away, then it becomes NODATA.
	 * Otherwise, NXDOMAIN or if OPTOUT, an insecure delegation.
	 * Also check that parameters are the same on closest encloser
	 * and on closest match.
	 */
	if(!zone->nsec3_hash) 
		return NULL; /* not nsec3 zone */

	if(!(data=neg_find_nsec3_ce(zone, qname, qname_len, qlabs, buf,
		hashnc, &nclen))) {
		return NULL;
	}

	/* grab the ce rrset */
	ce_rrset = grab_nsec(rrset_cache, data->name, data->len, 
		LDNS_RR_TYPE_NSEC3, zone->dclass, 0, region, 1, 
		LDNS_RR_TYPE_DS, now);
	if(!ce_rrset)
		return NULL;
	if(!neg_params_ok(zone, ce_rrset))
		return NULL;

	if(nclen == 0) {
		/* exact match, just check the type bits */
		/* need: -SOA, -DS, +NS */
		if(nsec3_has_type(ce_rrset, 0, LDNS_RR_TYPE_SOA) ||
			nsec3_has_type(ce_rrset, 0, LDNS_RR_TYPE_DS) ||
			!nsec3_has_type(ce_rrset, 0, LDNS_RR_TYPE_NS))
			return NULL;
		if(!(msg = dns_msg_create(qname, qname_len, 
			LDNS_RR_TYPE_DS, zone->dclass, region, 1))) 
			return NULL;
		/* TTL reduced in grab_nsec */
		if(!dns_msg_authadd(msg, region, ce_rrset, 0)) 
			return NULL;
		return msg;
	}

	/* optout is not allowed without knowing the trust-anchor in use,
	 * otherwise the optout could spoof away that anchor */
	if(!topname)
		return NULL;

	/* if there is no exact match, it must be in an optout span
	 * (an existing DS implies an NSEC3 must exist) */
	nc_rrset = neg_nsec3_getnc(zone, hashnc, nclen, rrset_cache, 
		region, now, nc_b32, sizeof(nc_b32));
	if(!nc_rrset) 
		return NULL;
	if(!neg_params_ok(zone, nc_rrset))
		return NULL;
	if(!nsec3_has_optout(nc_rrset, 0))
		return NULL;
	c.hash = hashnc;
	c.hash_len = nclen;
	c.b32 = nc_b32+1;
	c.b32_len = (size_t)nc_b32[0];
	if(nsec3_covers(zone->name, &c, nc_rrset, 0, buf)) {
		/* nc_rrset covers the next closer name.
		 * ce_rrset equals a closer encloser.
		 * nc_rrset is optout.
		 * No need to check wildcard for type DS */
		/* capacity=3: ce + nc + soa(if needed) */
		if(!(msg = dns_msg_create(qname, qname_len, 
			LDNS_RR_TYPE_DS, zone->dclass, region, 3))) 
			return NULL;
		/* now=0 because TTL was reduced in grab_nsec */
		if(!dns_msg_authadd(msg, region, ce_rrset, 0)) 
			return NULL;
		if(!dns_msg_authadd(msg, region, nc_rrset, 0)) 
			return NULL;
		return msg;
	}
	return NULL;
}

/**
 * Add SOA record for external responses.
 * @param rrset_cache: to look into.
 * @param now: current time.
 * @param region: where to perform the allocation
 * @param msg: current msg with NSEC.
 * @param zone: val_neg_zone if we have one.
 * @return false on lookup or alloc failure.
 */
static int add_soa(struct rrset_cache* rrset_cache, time_t now,
	struct regional* region, struct dns_msg* msg, struct val_neg_zone* zone)
{
	struct ub_packed_rrset_key* soa;
	uint8_t* nm;
	size_t nmlen;
	uint16_t dclass;
	if(zone) {
		nm = zone->name;
		nmlen = zone->len;
		dclass = zone->dclass;
	} else {
		/* Assumes the signer is the zone SOA to add */
		nm = reply_nsec_signer(msg->rep, &nmlen, &dclass);
		if(!nm) 
			return 0;
	}
	soa = rrset_cache_lookup(rrset_cache, nm, nmlen, LDNS_RR_TYPE_SOA, 
		dclass, PACKED_RRSET_SOA_NEG, now, 0);
	if(!soa)
		return 0;
	if(!dns_msg_authadd(msg, region, soa, now)) {
		lock_rw_unlock(&soa->entry.lock);
		return 0;
	}
	lock_rw_unlock(&soa->entry.lock);
	return 1;
}

struct dns_msg* 
val_neg_getmsg(struct val_neg_cache* neg, struct query_info* qinfo, 
	struct regional* region, struct rrset_cache* rrset_cache, 
	sldns_buffer* buf, time_t now, int addsoa, uint8_t* topname,
	struct config_file* cfg)
{
	struct dns_msg* msg;
	struct ub_packed_rrset_key* nsec; /* qname matching/covering nsec */
	struct ub_packed_rrset_key* wcrr; /* wildcard record or nsec */
	uint8_t* nodata_wc = NULL;
	uint8_t* ce = NULL;
	size_t ce_len;
	uint8_t wc_ce[LDNS_MAX_DOMAINLEN+3];
	struct query_info wc_qinfo;
	struct ub_packed_rrset_key* cache_wc;
	struct packed_rrset_data* wcrr_data;
	int rcode = LDNS_RCODE_NOERROR;
	uint8_t* zname;
	size_t zname_len;
	int zname_labs;
	struct val_neg_zone* zone;

	/* only for DS queries when aggressive use of NSEC is disabled */
	if(qinfo->qtype != LDNS_RR_TYPE_DS && !cfg->aggressive_nsec)
		return NULL;
	log_assert(!topname || dname_subdomain_c(qinfo->qname, topname));

	/* Get best available NSEC for qname */
	nsec = neg_find_nsec(neg, qinfo->qname, qinfo->qname_len, qinfo->qclass,
		rrset_cache, now, region);

	/* Matching NSEC, use to generate No Data answer. Not creating answers
	 * yet for No Data proven using wildcard. */
	if(nsec && nsec_proves_nodata(nsec, qinfo, &nodata_wc) && !nodata_wc) {
		if(!(msg = dns_msg_create(qinfo->qname, qinfo->qname_len, 
			qinfo->qtype, qinfo->qclass, region, 2))) 
			return NULL;
		if(!dns_msg_authadd(msg, region, nsec, 0)) 
			return NULL;
		if(addsoa && !add_soa(rrset_cache, now, region, msg, NULL))
			return NULL;

		lock_basic_lock(&neg->lock);
		neg->num_neg_cache_noerror++;
		lock_basic_unlock(&neg->lock);
		return msg;
	} else if(nsec && val_nsec_proves_name_error(nsec, qinfo->qname)) {
		if(!(msg = dns_msg_create(qinfo->qname, qinfo->qname_len, 
			qinfo->qtype, qinfo->qclass, region, 3))) 
			return NULL;
		if(!(ce = nsec_closest_encloser(qinfo->qname, nsec)))
			return NULL;
		dname_count_size_labels(ce, &ce_len);

		/* No extra extra NSEC required if both nameerror qname and
		 * nodata *.ce. are proven already. */
		if(!nodata_wc || query_dname_compare(nodata_wc, ce) != 0) {
			/* Qname proven non existing, get wildcard record for
			 * QTYPE or NSEC covering or matching wildcard. */

			/* Num labels in ce is always smaller than in qname,
			 * therefore adding the wildcard label cannot overflow
			 * buffer. */
			wc_ce[0] = 1;
			wc_ce[1] = (uint8_t)'*';
			memmove(wc_ce+2, ce, ce_len);
			wc_qinfo.qname = wc_ce;
			wc_qinfo.qname_len = ce_len + 2;
			wc_qinfo.qtype = qinfo->qtype;


			if((cache_wc = rrset_cache_lookup(rrset_cache, wc_qinfo.qname,
				wc_qinfo.qname_len, wc_qinfo.qtype,
				qinfo->qclass, 0/*flags*/, now, 0/*read only*/))) {
				/* Synthesize wildcard answer */
				wcrr_data = (struct packed_rrset_data*)cache_wc->entry.data;
				if(!(wcrr_data->security == sec_status_secure ||
					(wcrr_data->security == sec_status_unchecked &&
					wcrr_data->rrsig_count > 0))) {
					lock_rw_unlock(&cache_wc->entry.lock);
					return NULL;
				}
				if(!(wcrr = packed_rrset_copy_region(cache_wc,
					region, now))) {
					lock_rw_unlock(&cache_wc->entry.lock);
					return NULL;
				};
				lock_rw_unlock(&cache_wc->entry.lock);
				wcrr->rk.dname = qinfo->qname;
				wcrr->rk.dname_len = qinfo->qname_len;
				if(!dns_msg_ansadd(msg, region, wcrr, 0))
					return NULL;
				/* No SOA needed for wildcard synthesised
				 * answer. */
				addsoa = 0;
			} else {
				/* Get wildcard NSEC for possible non existence
				 * proof */
				if(!(wcrr = neg_find_nsec(neg, wc_qinfo.qname,
					wc_qinfo.qname_len, qinfo->qclass,
					rrset_cache, now, region)))
					return NULL;

				nodata_wc = NULL;
				if(val_nsec_proves_name_error(wcrr, wc_ce))
					rcode = LDNS_RCODE_NXDOMAIN;
				else if(!nsec_proves_nodata(wcrr, &wc_qinfo,
					&nodata_wc) || nodata_wc)
					/* &nodata_wc shouldn't be set, wc_qinfo
					 * already contains wildcard domain. */
					/* NSEC doesn't prove anything for
					 * wildcard. */
					return NULL;
				if(query_dname_compare(wcrr->rk.dname,
					nsec->rk.dname) != 0)
					if(!dns_msg_authadd(msg, region, wcrr, 0))
						return NULL;
			}
		}

		if(!dns_msg_authadd(msg, region, nsec, 0))
			return NULL;
		if(addsoa && !add_soa(rrset_cache, now, region, msg, NULL))
			return NULL;

		/* Increment statistic counters */
		lock_basic_lock(&neg->lock);
		if(rcode == LDNS_RCODE_NOERROR)
			neg->num_neg_cache_noerror++;
		else if(rcode == LDNS_RCODE_NXDOMAIN)
			neg->num_neg_cache_nxdomain++;
		lock_basic_unlock(&neg->lock);

		FLAGS_SET_RCODE(msg->rep->flags, rcode);
		return msg;
	}

	/* No aggressive use of NSEC3 for now, only proceed for DS types. */
	if(qinfo->qtype != LDNS_RR_TYPE_DS){
		return NULL;
	}
	/* check NSEC3 neg cache for type DS */
	/* need to look one zone higher for DS type */
	zname = qinfo->qname;
	zname_len = qinfo->qname_len;
	dname_remove_label(&zname, &zname_len);
	zname_labs = dname_count_labels(zname);

	/* lookup closest zone */
	lock_basic_lock(&neg->lock);
	zone = neg_closest_zone_parent(neg, zname, zname_len, zname_labs, 
		qinfo->qclass);
	while(zone && !zone->in_use)
		zone = zone->parent;
	/* check that the zone is not too high up so that we do not pick data
	 * out of a zone that is above the last-seen key (or trust-anchor). */
	if(zone && topname) {
		if(!dname_subdomain_c(zone->name, topname))
			zone = NULL;
	}
	if(!zone) {
		lock_basic_unlock(&neg->lock);
		return NULL;
	}

	msg = neg_nsec3_proof_ds(zone, qinfo->qname, qinfo->qname_len, 
		zname_labs+1, buf, rrset_cache, region, now, topname);
	if(msg && addsoa && !add_soa(rrset_cache, now, region, msg, zone)) {
		lock_basic_unlock(&neg->lock);
		return NULL;
	}
	lock_basic_unlock(&neg->lock);
	return msg;
}
