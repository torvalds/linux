/*
 * iterator/iter_priv.c - iterative resolver private address and domain store
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
 * This file contains functions to assist the iterator module.
 * Keep track of the private addresses and lookup fast.
 */

#include "config.h"
#include "iterator/iter_priv.h"
#include "util/regional.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/data/dname.h"
#include "util/data/msgparse.h"
#include "util/net_help.h"
#include "util/storage/dnstree.h"
#include "sldns/str2wire.h"
#include "sldns/sbuffer.h"

struct iter_priv* priv_create(void)
{
	struct iter_priv* priv = (struct iter_priv*)calloc(1, sizeof(*priv));
	if(!priv)
		return NULL;
	priv->region = regional_create();
	if(!priv->region) {
		priv_delete(priv);
		return NULL;
	}
	addr_tree_init(&priv->a);
	name_tree_init(&priv->n);
	return priv;
}

void priv_delete(struct iter_priv* priv)
{
	if(!priv) return;
	regional_destroy(priv->region);
	free(priv);
}

/** Read private-addr declarations from config */
static int read_addrs(struct iter_priv* priv, struct config_file* cfg)
{
	/* parse addresses, report errors, insert into tree */
	struct config_strlist* p;
	struct addr_tree_node* n;
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;

	for(p = cfg->private_address; p; p = p->next) {
		log_assert(p->str);
		if(!netblockstrtoaddr(p->str, UNBOUND_DNS_PORT, &addr, 
			&addrlen, &net)) {
			log_err("cannot parse private-address: %s", p->str);
			return 0;
		}
		n = (struct addr_tree_node*)regional_alloc(priv->region,
			sizeof(*n));
		if(!n) {
			log_err("out of memory");
			return 0;
		}
		if(!addr_tree_insert(&priv->a, n, &addr, addrlen, net)) {
			verbose(VERB_QUERY, "ignoring duplicate "
				"private-address: %s", p->str);
		}
	}
	return 1;
}

/** Read private-domain declarations from config */
static int read_names(struct iter_priv* priv, struct config_file* cfg)
{
	/* parse names, report errors, insert into tree */
	struct config_strlist* p;
	struct name_tree_node* n;
	uint8_t* nm, *nmr;
	size_t nm_len;
	int nm_labs;

	for(p = cfg->private_domain; p; p = p->next) {
		log_assert(p->str);
		nm = sldns_str2wire_dname(p->str, &nm_len);
		if(!nm) {
			log_err("cannot parse private-domain: %s", p->str);
			return 0;
		}
		nm_labs = dname_count_size_labels(nm, &nm_len);
		nmr = (uint8_t*)regional_alloc_init(priv->region, nm, nm_len);
		free(nm);
		if(!nmr) {
			log_err("out of memory");
			return 0;
		}
		n = (struct name_tree_node*)regional_alloc(priv->region,
			sizeof(*n));
		if(!n) {
			log_err("out of memory");
			return 0;
		}
		if(!name_tree_insert(&priv->n, n, nmr, nm_len, nm_labs,
			LDNS_RR_CLASS_IN)) {
			verbose(VERB_QUERY, "ignoring duplicate "
				"private-domain: %s", p->str);
		}
	}
	return 1;
}

int priv_apply_cfg(struct iter_priv* priv, struct config_file* cfg)
{
	/* empty the current contents */
	regional_free_all(priv->region);
	addr_tree_init(&priv->a);
	name_tree_init(&priv->n);

	/* read new contents */
	if(!read_addrs(priv, cfg))
		return 0;
	if(!read_names(priv, cfg))
		return 0;

	/* prepare for lookups */
	addr_tree_init_parents(&priv->a);
	name_tree_init_parents(&priv->n);
	return 1;
}

/**
 * See if an address is blocked.
 * @param priv: structure for address storage.
 * @param addr: address to check
 * @param addrlen: length of addr.
 * @return: true if the address must not be queried. false if unlisted.
 */
static int 
priv_lookup_addr(struct iter_priv* priv, struct sockaddr_storage* addr,
	socklen_t addrlen)
{
	return addr_tree_lookup(&priv->a, addr, addrlen) != NULL;
}

/**
 * See if a name is whitelisted.
 * @param priv: structure for address storage.
 * @param pkt: the packet (for compression ptrs).
 * @param name: name to check.
 * @param name_len: uncompressed length of the name to check.
 * @param dclass: class to check.
 * @return: true if the name is OK. false if unlisted.
 */
static int 
priv_lookup_name(struct iter_priv* priv, sldns_buffer* pkt,
	uint8_t* name, size_t name_len, uint16_t dclass)
{
	size_t len;
	uint8_t decomp[256];
	int labs;
	if(name_len >= sizeof(decomp))
		return 0;
	dname_pkt_copy(pkt, decomp, name);
	labs = dname_count_size_labels(decomp, &len);
	log_assert(name_len == len);
	return name_tree_lookup(&priv->n, decomp, len, labs, dclass) != NULL;
}

size_t priv_get_mem(struct iter_priv* priv)
{
	if(!priv) return 0;
	return sizeof(*priv) + regional_get_mem(priv->region);
}

/** remove RR from msgparse RRset, return true if rrset is entirely bad */
static int
remove_rr(const char* str, sldns_buffer* pkt, struct rrset_parse* rrset,
	struct rr_parse* prev, struct rr_parse** rr, struct sockaddr_storage* addr, socklen_t addrlen)
{
	if(verbosity >= VERB_QUERY && rrset->dname_len <= LDNS_MAX_DOMAINLEN && str) {
		uint8_t buf[LDNS_MAX_DOMAINLEN+1];
		dname_pkt_copy(pkt, buf, rrset->dname);
		log_name_addr(VERB_QUERY, str, buf, addr, addrlen);
	}
	if(prev)
		prev->next = (*rr)->next;
	else	rrset->rr_first = (*rr)->next;
	if(rrset->rr_last == *rr)
		rrset->rr_last = prev;
	rrset->rr_count --;
	rrset->size -= (*rr)->size;
	/* rr struct still exists, but is unlinked, so that in the for loop
	 * the rr->next works fine to continue. */
	return rrset->rr_count == 0;
}

int priv_rrset_bad(struct iter_priv* priv, sldns_buffer* pkt,
	struct rrset_parse* rrset)
{
	if(priv->a.count == 0) 
		return 0; /* there are no blocked addresses */

	/* see if it is a private name, that is allowed to have any */
	if(priv_lookup_name(priv, pkt, rrset->dname, rrset->dname_len,
		ntohs(rrset->rrset_class))) {
		return 0;
	} else {
		/* so its a public name, check the address */
		socklen_t len;
		struct rr_parse* rr, *prev = NULL;
		if(rrset->type == LDNS_RR_TYPE_A) {
			struct sockaddr_storage addr;
			struct sockaddr_in sa;

			len = (socklen_t)sizeof(sa);
			memset(&sa, 0, len);
			sa.sin_family = AF_INET;
			sa.sin_port = (in_port_t)htons(UNBOUND_DNS_PORT);
			for(rr = rrset->rr_first; rr; rr = rr->next) {
				if(sldns_read_uint16(rr->ttl_data+4) 
					!= INET_SIZE) {
					prev = rr;
					continue;
				}
				memmove(&sa.sin_addr, rr->ttl_data+4+2, 
					INET_SIZE);
				memmove(&addr, &sa, len);
				if(priv_lookup_addr(priv, &addr, len)) {
					if(remove_rr("sanitize: removing public name with private address", pkt, rrset, prev, &rr, &addr, len))
						return 1;
					continue;
				}
				prev = rr;
			}
		} else if(rrset->type == LDNS_RR_TYPE_AAAA) {
			struct sockaddr_storage addr;
			struct sockaddr_in6 sa;
			len = (socklen_t)sizeof(sa);
			memset(&sa, 0, len);
			sa.sin6_family = AF_INET6;
			sa.sin6_port = (in_port_t)htons(UNBOUND_DNS_PORT);
			for(rr = rrset->rr_first; rr; rr = rr->next) {
				if(sldns_read_uint16(rr->ttl_data+4) 
					!= INET6_SIZE) {
					prev = rr;
					continue;
				}
				memmove(&sa.sin6_addr, rr->ttl_data+4+2, 
					INET6_SIZE);
				memmove(&addr, &sa, len);
				if(priv_lookup_addr(priv, &addr, len)) {
					if(remove_rr("sanitize: removing public name with private address", pkt, rrset, prev, &rr, &addr, len))
						return 1;
					continue;
				}
				prev = rr;
			}
		} 
	}
	return 0;
}
