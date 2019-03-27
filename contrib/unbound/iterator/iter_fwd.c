/*
 * iterator/iter_fwd.c - iterative resolver module forward zones.
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
 * This file contains functions to assist the iterator module.
 * Keep track of forward zones and config settings.
 */
#include "config.h"
#include "iterator/iter_fwd.h"
#include "iterator/iter_delegpt.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "util/data/dname.h"
#include "sldns/rrdef.h"
#include "sldns/str2wire.h"

int
fwd_cmp(const void* k1, const void* k2)
{
	int m;
	struct iter_forward_zone* n1 = (struct iter_forward_zone*)k1;
	struct iter_forward_zone* n2 = (struct iter_forward_zone*)k2;
	if(n1->dclass != n2->dclass) {
		if(n1->dclass < n2->dclass)
			return -1;
		return 1;
	}
	return dname_lab_cmp(n1->name, n1->namelabs, n2->name, n2->namelabs, 
		&m);
}

struct iter_forwards* 
forwards_create(void)
{
	struct iter_forwards* fwd = (struct iter_forwards*)calloc(1,
		sizeof(struct iter_forwards));
	if(!fwd)
		return NULL;
	return fwd;
}

static void fwd_zone_free(struct iter_forward_zone* n)
{
	if(!n) return;
	delegpt_free_mlc(n->dp);
	free(n->name);
	free(n);
}

static void delfwdnode(rbnode_type* n, void* ATTR_UNUSED(arg))
{
	struct iter_forward_zone* node = (struct iter_forward_zone*)n;
	fwd_zone_free(node);
}

static void fwd_del_tree(struct iter_forwards* fwd)
{
	if(fwd->tree)
		traverse_postorder(fwd->tree, &delfwdnode, NULL);
	free(fwd->tree);
}

void 
forwards_delete(struct iter_forwards* fwd)
{
	if(!fwd) 
		return;
	fwd_del_tree(fwd);
	free(fwd);
}

/** insert info into forward structure */
static int
forwards_insert_data(struct iter_forwards* fwd, uint16_t c, uint8_t* nm, 
	size_t nmlen, int nmlabs, struct delegpt* dp)
{
	struct iter_forward_zone* node = (struct iter_forward_zone*)malloc(
		sizeof(struct iter_forward_zone));
	if(!node) {
		delegpt_free_mlc(dp);
		return 0;
	}
	node->node.key = node;
	node->dclass = c;
	node->name = memdup(nm, nmlen);
	if(!node->name) {
		delegpt_free_mlc(dp);
		free(node);
		return 0;
	}
	node->namelen = nmlen;
	node->namelabs = nmlabs;
	node->dp = dp;
	if(!rbtree_insert(fwd->tree, &node->node)) {
		char buf[257];
		dname_str(nm, buf);
		log_err("duplicate forward zone %s ignored.", buf);
		delegpt_free_mlc(dp);
		free(node->name);
		free(node);
	}
	return 1;
}

/** insert new info into forward structure given dp */
static int
forwards_insert(struct iter_forwards* fwd, uint16_t c, struct delegpt* dp)
{
	return forwards_insert_data(fwd, c, dp->name, dp->namelen,
		dp->namelabs, dp);
}

/** initialise parent pointers in the tree */
static void
fwd_init_parents(struct iter_forwards* fwd)
{
	struct iter_forward_zone* node, *prev = NULL, *p;
	int m;
	RBTREE_FOR(node, struct iter_forward_zone*, fwd->tree) {
		node->parent = NULL;
		if(!prev || prev->dclass != node->dclass) {
			prev = node;
			continue;
		}
		(void)dname_lab_cmp(prev->name, prev->namelabs, node->name,
			node->namelabs, &m); /* we know prev is smaller */
		/* sort order like: . com. bla.com. zwb.com. net. */
		/* find the previous, or parent-parent-parent */
		for(p = prev; p; p = p->parent)
			/* looking for name with few labels, a parent */
			if(p->namelabs <= m) {
				/* ==: since prev matched m, this is closest*/
				/* <: prev matches more, but is not a parent,
				 * this one is a (grand)parent */
				node->parent = p;
				break;
			}
		prev = node;
	}
}

/** set zone name */
static struct delegpt* 
read_fwds_name(struct config_stub* s)
{
	struct delegpt* dp;
	uint8_t* dname;
	size_t dname_len;
	if(!s->name) {
		log_err("forward zone without a name (use name \".\" to forward everything)");
		return NULL;
	}
	dname = sldns_str2wire_dname(s->name, &dname_len);
	if(!dname) {
		log_err("cannot parse forward zone name %s", s->name);
		return NULL;
	}
	if(!(dp=delegpt_create_mlc(dname))) {
		free(dname);
		log_err("out of memory");
		return NULL;
	}
	free(dname);
	return dp;
}

/** set fwd host names */
static int 
read_fwds_host(struct config_stub* s, struct delegpt* dp)
{
	struct config_strlist* p;
	uint8_t* dname;
	size_t dname_len;
	for(p = s->hosts; p; p = p->next) {
		log_assert(p->str);
		dname = sldns_str2wire_dname(p->str, &dname_len);
		if(!dname) {
			log_err("cannot parse forward %s server name: '%s'", 
				s->name, p->str);
			return 0;
		}
		if(!delegpt_add_ns_mlc(dp, dname, 0)) {
			free(dname);
			log_err("out of memory");
			return 0;
		}
		free(dname);
	}
	return 1;
}

/** set fwd server addresses */
static int 
read_fwds_addr(struct config_stub* s, struct delegpt* dp)
{
	struct config_strlist* p;
	struct sockaddr_storage addr;
	socklen_t addrlen;
	char* tls_auth_name;
	for(p = s->addrs; p; p = p->next) {
		log_assert(p->str);
		if(!authextstrtoaddr(p->str, &addr, &addrlen, &tls_auth_name)) {
			log_err("cannot parse forward %s ip address: '%s'", 
				s->name, p->str);
			return 0;
		}
#ifndef HAVE_SSL_SET1_HOST
		if(tls_auth_name)
			log_err("no name verification functionality in "
				"ssl library, ignored name for %s", p->str);
#endif
		if(!delegpt_add_addr_mlc(dp, &addr, addrlen, 0, 0,
			tls_auth_name)) {
			log_err("out of memory");
			return 0;
		}
	}
	return 1;
}

/** read forwards config */
static int 
read_forwards(struct iter_forwards* fwd, struct config_file* cfg)
{
	struct config_stub* s;
	for(s = cfg->forwards; s; s = s->next) {
		struct delegpt* dp;
		if(!(dp=read_fwds_name(s)))
			return 0;
		if(!read_fwds_host(s, dp) || !read_fwds_addr(s, dp)) {
			delegpt_free_mlc(dp);
			return 0;
		}
		/* set flag that parent side NS information is included.
		 * Asking a (higher up) server on the internet is not useful */
		/* the flag is turned off for 'forward-first' so that the
		 * last resort will ask for parent-side NS record and thus
		 * fallback to the internet name servers on a failure */
		dp->has_parent_side_NS = (uint8_t)!s->isfirst;
		/* Do not cache if set. */
		dp->no_cache = s->no_cache;
		/* use SSL for queries to this forwarder */
		dp->ssl_upstream = (uint8_t)s->ssl_upstream;
		verbose(VERB_QUERY, "Forward zone server list:");
		delegpt_log(VERB_QUERY, dp);
		if(!forwards_insert(fwd, LDNS_RR_CLASS_IN, dp))
			return 0;
	}
	return 1;
}

/** insert a stub hole (if necessary) for stub name */
static int
fwd_add_stub_hole(struct iter_forwards* fwd, uint16_t c, uint8_t* nm)
{
	struct iter_forward_zone key;
	key.node.key = &key;
	key.dclass = c;
	key.name = nm;
	key.namelabs = dname_count_size_labels(key.name, &key.namelen);
	return forwards_insert_data(fwd, key.dclass, key.name,
		key.namelen, key.namelabs, NULL);
}

/** make NULL entries for stubs */
static int
make_stub_holes(struct iter_forwards* fwd, struct config_file* cfg)
{
	struct config_stub* s;
	uint8_t* dname;
	size_t dname_len;
	for(s = cfg->stubs; s; s = s->next) {
		if(!s->name) continue;
		dname = sldns_str2wire_dname(s->name, &dname_len);
		if(!dname) {
			log_err("cannot parse stub name '%s'", s->name);
			return 0;
		}
		if(!fwd_add_stub_hole(fwd, LDNS_RR_CLASS_IN, dname)) {
			free(dname);
			log_err("out of memory");
			return 0;
		}
		free(dname);
	}
	return 1;
}

int 
forwards_apply_cfg(struct iter_forwards* fwd, struct config_file* cfg)
{
	fwd_del_tree(fwd);
	fwd->tree = rbtree_create(fwd_cmp);
	if(!fwd->tree)
		return 0;

	/* read forward zones */
	if(!read_forwards(fwd, cfg))
		return 0;
	if(!make_stub_holes(fwd, cfg))
		return 0;
	fwd_init_parents(fwd);
	return 1;
}

struct delegpt* 
forwards_find(struct iter_forwards* fwd, uint8_t* qname, uint16_t qclass)
{
	rbnode_type* res = NULL;
	struct iter_forward_zone key;
	key.node.key = &key;
	key.dclass = qclass;
	key.name = qname;
	key.namelabs = dname_count_size_labels(qname, &key.namelen);
	res = rbtree_search(fwd->tree, &key);
	if(res) return ((struct iter_forward_zone*)res)->dp;
	return NULL;
}

struct delegpt* 
forwards_lookup(struct iter_forwards* fwd, uint8_t* qname, uint16_t qclass)
{
	/* lookup the forward zone in the tree */
	rbnode_type* res = NULL;
	struct iter_forward_zone *result;
	struct iter_forward_zone key;
	key.node.key = &key;
	key.dclass = qclass;
	key.name = qname;
	key.namelabs = dname_count_size_labels(qname, &key.namelen);
	if(rbtree_find_less_equal(fwd->tree, &key, &res)) {
		/* exact */
		result = (struct iter_forward_zone*)res;
	} else {
		/* smaller element (or no element) */
		int m;
		result = (struct iter_forward_zone*)res;
		if(!result || result->dclass != qclass)
			return NULL;
		/* count number of labels matched */
		(void)dname_lab_cmp(result->name, result->namelabs, key.name,
			key.namelabs, &m);
		while(result) { /* go up until qname is subdomain of stub */
			if(result->namelabs <= m)
				break;
			result = result->parent;
		}
	}
	if(result)
		return result->dp;
	return NULL;
}

struct delegpt* 
forwards_lookup_root(struct iter_forwards* fwd, uint16_t qclass)
{
	uint8_t root = 0;
	return forwards_lookup(fwd, &root, qclass);
}

int
forwards_next_root(struct iter_forwards* fwd, uint16_t* dclass)
{
	struct iter_forward_zone key;
	rbnode_type* n;
	struct iter_forward_zone* p;
	if(*dclass == 0) {
		/* first root item is first item in tree */
		n = rbtree_first(fwd->tree);
		if(n == RBTREE_NULL)
			return 0;
		p = (struct iter_forward_zone*)n;
		if(dname_is_root(p->name)) {
			*dclass = p->dclass;
			return 1;
		}
		/* root not first item? search for higher items */
		*dclass = p->dclass + 1;
		return forwards_next_root(fwd, dclass);
	}
	/* find class n in tree, we may get a direct hit, or if we don't
	 * this is the last item of the previous class so rbtree_next() takes
	 * us to the next root (if any) */
	key.node.key = &key;
	key.name = (uint8_t*)"\000";
	key.namelen = 1;
	key.namelabs = 0;
	key.dclass = *dclass;
	n = NULL;
	if(rbtree_find_less_equal(fwd->tree, &key, &n)) {
		/* exact */
		return 1;
	} else {
		/* smaller element */
		if(!n || n == RBTREE_NULL)
			return 0; /* nothing found */
		n = rbtree_next(n);
		if(n == RBTREE_NULL)
			return 0; /* no higher */
		p = (struct iter_forward_zone*)n;
		if(dname_is_root(p->name)) {
			*dclass = p->dclass;
			return 1;
		}
		/* not a root node, return next higher item */
		*dclass = p->dclass+1;
		return forwards_next_root(fwd, dclass);
	}
}

size_t 
forwards_get_mem(struct iter_forwards* fwd)
{
	struct iter_forward_zone* p;
	size_t s;
	if(!fwd)
		return 0;
	s = sizeof(*fwd) + sizeof(*fwd->tree);
	RBTREE_FOR(p, struct iter_forward_zone*, fwd->tree) {
		s += sizeof(*p) + p->namelen + delegpt_get_mem(p->dp);
	}
	return s;
}

static struct iter_forward_zone*
fwd_zone_find(struct iter_forwards* fwd, uint16_t c, uint8_t* nm)
{
	struct iter_forward_zone key;
	key.node.key = &key;
	key.dclass = c;
	key.name = nm;
	key.namelabs = dname_count_size_labels(nm, &key.namelen);
	return (struct iter_forward_zone*)rbtree_search(fwd->tree, &key);
}

int 
forwards_add_zone(struct iter_forwards* fwd, uint16_t c, struct delegpt* dp)
{
	struct iter_forward_zone *z;
	if((z=fwd_zone_find(fwd, c, dp->name)) != NULL) {
		(void)rbtree_delete(fwd->tree, &z->node);
		fwd_zone_free(z);
	}
	if(!forwards_insert(fwd, c, dp))
		return 0;
	fwd_init_parents(fwd);
	return 1;
}

void 
forwards_delete_zone(struct iter_forwards* fwd, uint16_t c, uint8_t* nm)
{
	struct iter_forward_zone *z;
	if(!(z=fwd_zone_find(fwd, c, nm)))
		return; /* nothing to do */
	(void)rbtree_delete(fwd->tree, &z->node);
	fwd_zone_free(z);
	fwd_init_parents(fwd);
}

int
forwards_add_stub_hole(struct iter_forwards* fwd, uint16_t c, uint8_t* nm)
{
	if(!fwd_add_stub_hole(fwd, c, nm)) {
		return 0;
	}
	fwd_init_parents(fwd);
	return 1;
}

void
forwards_delete_stub_hole(struct iter_forwards* fwd, uint16_t c, uint8_t* nm)
{
	struct iter_forward_zone *z;
	if(!(z=fwd_zone_find(fwd, c, nm)))
		return; /* nothing to do */
	if(z->dp != NULL)
		return; /* not a stub hole */
	(void)rbtree_delete(fwd->tree, &z->node);
	fwd_zone_free(z);
	fwd_init_parents(fwd);
}

