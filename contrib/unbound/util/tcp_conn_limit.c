/*
 * daemon/tcp_conn_limit.c - client TCP connection limit storage for the server.
 *
 * Copyright (c) 2018, NLnet Labs. All rights reserved.
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
 * This file helps the server discard excess TCP connections.
 */
#include "config.h"
#include "util/regional.h"
#include "util/log.h"
#include "util/config_file.h"
#include "util/net_help.h"
#include "util/tcp_conn_limit.h"
#include "services/localzone.h"
#include "sldns/str2wire.h"

struct tcl_list*
tcl_list_create(void)
{
	struct tcl_list* tcl = (struct tcl_list*)calloc(1,
		sizeof(struct tcl_list));
	if(!tcl)
		return NULL;
	tcl->region = regional_create();
	if(!tcl->region) {
		tcl_list_delete(tcl);
		return NULL;
	}
	return tcl;
}

static void
tcl_list_free_node(rbnode_type* node, void* ATTR_UNUSED(arg))
{
	struct tcl_addr* n = (struct tcl_addr*) node;
	lock_quick_destroy(&n->lock);
#ifdef THREADS_DISABLED
	(void)n;
#endif
}

void
tcl_list_delete(struct tcl_list* tcl)
{
	if(!tcl)
		return;
	traverse_postorder(&tcl->tree, tcl_list_free_node, NULL);
	regional_destroy(tcl->region);
	free(tcl);
}

/** insert new address into tcl_list structure */
static struct tcl_addr*
tcl_list_insert(struct tcl_list* tcl, struct sockaddr_storage* addr,
	socklen_t addrlen, int net, uint32_t limit,
	int complain_duplicates)
{
	struct tcl_addr* node = regional_alloc_zero(tcl->region,
		sizeof(struct tcl_addr));
	if(!node)
		return NULL;
	lock_quick_init(&node->lock);
	node->limit = limit;
	if(!addr_tree_insert(&tcl->tree, &node->node, addr, addrlen, net)) {
		if(complain_duplicates)
			verbose(VERB_QUERY, "duplicate tcl address ignored.");
	}
	return node;
}

/** apply tcl_list string */
static int
tcl_list_str_cfg(struct tcl_list* tcl, const char* str, const char* s2,
	int complain_duplicates)
{
	struct sockaddr_storage addr;
	int net;
	socklen_t addrlen;
	uint32_t limit;
	if(atoi(s2) < 0) {
		log_err("bad connection limit %s", s2);
		return 0;
	}
	limit = (uint32_t)atoi(s2);
	if(!netblockstrtoaddr(str, UNBOUND_DNS_PORT, &addr, &addrlen, &net)) {
		log_err("cannot parse connection limit netblock: %s", str);
		return 0;
	}
	if(!tcl_list_insert(tcl, &addr, addrlen, net, limit,
		complain_duplicates)) {
		log_err("out of memory");
		return 0;
	}
	return 1;
}

/** read tcl_list config */
static int
read_tcl_list(struct tcl_list* tcl, struct config_file* cfg)
{
	struct config_str2list* p;
	for(p = cfg->tcp_connection_limits; p; p = p->next) {
		log_assert(p->str && p->str2);
		if(!tcl_list_str_cfg(tcl, p->str, p->str2, 1))
			return 0;
	}
	return 1;
}

int
tcl_list_apply_cfg(struct tcl_list* tcl, struct config_file* cfg)
{
	regional_free_all(tcl->region);
	addr_tree_init(&tcl->tree);
	if(!read_tcl_list(tcl, cfg))
		return 0;
	addr_tree_init_parents(&tcl->tree);
	return 1;
}

int
tcl_new_connection(struct tcl_addr* tcl)
{
	if(tcl) {
		int res = 1;
		lock_quick_lock(&tcl->lock);
		if(tcl->count >= tcl->limit)
			res = 0;
		else
			tcl->count++;
		lock_quick_unlock(&tcl->lock);
		return res;
	}
	return 1;
}

void
tcl_close_connection(struct tcl_addr* tcl)
{
	if(tcl) {
		lock_quick_lock(&tcl->lock);
		log_assert(tcl->count > 0);
		tcl->count--;
		lock_quick_unlock(&tcl->lock);
	}
}

struct tcl_addr*
tcl_addr_lookup(struct tcl_list* tcl, struct sockaddr_storage* addr,
        socklen_t addrlen)
{
	return (struct tcl_addr*)addr_tree_lookup(&tcl->tree,
		addr, addrlen);
}

size_t
tcl_list_get_mem(struct tcl_list* tcl)
{
	if(!tcl) return 0;
	return sizeof(*tcl) + regional_get_mem(tcl->region);
}
