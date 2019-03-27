/*
 * edns-subnet/subnet-whitelist.c - Hosts we actively try to send subnet option
 * to.
 *
 * Copyright (c) 2013, NLnet Labs. All rights reserved.
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
 * Keep track of the white listed servers for subnet option. Based
 * on acl_list.c|h
 */

#include "config.h"

#ifdef CLIENT_SUBNET /* keeps splint happy */
#include "edns-subnet/edns-subnet.h"
#include "edns-subnet/subnet-whitelist.h"
#include "util/regional.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "util/storage/dnstree.h"
#include "sldns/str2wire.h"
#include "util/data/dname.h"

struct ecs_whitelist* 
ecs_whitelist_create(void)
{
	struct ecs_whitelist* whitelist = 
		(struct ecs_whitelist*)calloc(1,
		sizeof(struct ecs_whitelist));
	if(!whitelist)
		return NULL;
	whitelist->region = regional_create();
	if(!whitelist->region) {
		ecs_whitelist_delete(whitelist);
		return NULL;
	}
	return whitelist;
}

void 
ecs_whitelist_delete(struct ecs_whitelist* whitelist)
{
	if(!whitelist) 
		return;
	regional_destroy(whitelist->region);
	free(whitelist);
}

/** insert new address into whitelist structure */
static int
upstream_insert(struct ecs_whitelist* whitelist, 
	struct sockaddr_storage* addr, socklen_t addrlen, int net)
{
	struct addr_tree_node* node = (struct addr_tree_node*)regional_alloc(
		whitelist->region, sizeof(*node));
	if(!node)
		return 0;
	if(!addr_tree_insert(&whitelist->upstream, node, addr, addrlen, net)) {
		verbose(VERB_QUERY,
			"duplicate send-client-subnet address ignored.");
	}
	return 1;
}

/** apply edns-subnet string */
static int
upstream_str_cfg(struct ecs_whitelist* whitelist, const char* str)
{
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;
	verbose(VERB_ALGO, "send-client-subnet: %s", str);
	if(!netblockstrtoaddr(str, UNBOUND_DNS_PORT, &addr, &addrlen, &net)) {
		log_err("cannot parse send-client-subnet netblock: %s", str);
		return 0;
	}
	if(!upstream_insert(whitelist, &addr, addrlen, net)) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** read client_subnet config */
static int 
read_upstream(struct ecs_whitelist* whitelist, struct config_file* cfg)
{
	struct config_strlist* p;
	for(p = cfg->client_subnet; p; p = p->next) {
		log_assert(p->str);
		if(!upstream_str_cfg(whitelist, p->str))
			return 0;
	}
	return 1;
}

/** read client_subnet_zone config */
static int 
read_names(struct ecs_whitelist* whitelist, struct config_file* cfg)
{
	/* parse names, report errors, insert into tree */
	struct config_strlist* p;
	struct name_tree_node* n;
	uint8_t* nm, *nmr;
	size_t nm_len;
	int nm_labs;

	for(p = cfg->client_subnet_zone; p; p = p->next) {
		log_assert(p->str);
		nm = sldns_str2wire_dname(p->str, &nm_len);
		if(!nm) {
			log_err("cannot parse client-subnet-zone: %s", p->str);
			return 0;
		}
		nm_labs = dname_count_size_labels(nm, &nm_len);
		nmr = (uint8_t*)regional_alloc_init(whitelist->region, nm,
			nm_len);
		free(nm);
		if(!nmr) {
			log_err("out of memory");
			return 0;
		}
		n = (struct name_tree_node*)regional_alloc(whitelist->region,
			sizeof(*n));
		if(!n) {
			log_err("out of memory");
			return 0;
		}
		if(!name_tree_insert(&whitelist->dname, n, nmr, nm_len, nm_labs,
			LDNS_RR_CLASS_IN)) {
			verbose(VERB_QUERY, "ignoring duplicate "
				"client-subnet-zone: %s", p->str);
		}
	}
	return 1;
}

int 
ecs_whitelist_apply_cfg(struct ecs_whitelist* whitelist,
	struct config_file* cfg)
{
	regional_free_all(whitelist->region);
	addr_tree_init(&whitelist->upstream);
	name_tree_init(&whitelist->dname);
	if(!read_upstream(whitelist, cfg))
		return 0;
	if(!read_names(whitelist, cfg))
		return 0;
	addr_tree_init_parents(&whitelist->upstream);
	name_tree_init_parents(&whitelist->dname);
	return 1;
}

int 
ecs_is_whitelisted(struct ecs_whitelist* whitelist,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* qname,
	size_t qname_len, uint16_t qclass)
{
	int labs;
	if(addr_tree_lookup(&whitelist->upstream, addr, addrlen))
		return 1;
	/* Not in upstream whitelist, check dname whitelist. */
	labs = dname_count_labels(qname);
	return name_tree_lookup(&whitelist->dname, qname, qname_len, labs,
		qclass) != NULL;
}

size_t 
ecs_whitelist_get_mem(struct ecs_whitelist* whitelist)
{
	if(!whitelist) return 0;
	return sizeof(*whitelist) + regional_get_mem(whitelist->region);
}

#endif /* CLIENT_SUBNET */
