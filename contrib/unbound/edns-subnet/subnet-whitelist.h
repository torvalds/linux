/*
 * edns-subnet/subnet-whitelist.h - Hosts we actively try to send subnet option
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
 * Keep track of the white listed servers and domain names for subnet option.
 * Based on acl_list.c|h
 */

#ifndef EDNSSUBNET_WHITELIST_H
#define EDNSSUBNET_WHITELIST_H
#include "util/storage/dnstree.h"

struct config_file;
struct regional;

/**
 * ecs_whitelist structure
 */
struct ecs_whitelist {
	/** regional for allocation */
	struct regional* region;
	/** 
	 * Tree of the address spans that are whitelisted.
	 * contents of type addr_tree_node. Each node is an address span 
	 * Unbound will append subnet option for.
	 */
	rbtree_type upstream;
	/**
	 * Tree of domain names for which Unbound will append an ECS option.
	 * rbtree of struct name_tree_node.
	 */
	rbtree_type dname;
};

/**
 * Create ecs_whitelist structure 
 * @return new structure or NULL on error.
 */
struct ecs_whitelist* ecs_whitelist_create(void);

/**
 * Delete ecs_whitelist structure.
 * @param whitelist: to delete.
 */
void ecs_whitelist_delete(struct ecs_whitelist* whitelist);

/**
 * Process ecs_whitelist config.
 * @param whitelist: where to store.
 * @param cfg: config options.
 * @return 0 on error.
 */
int ecs_whitelist_apply_cfg(struct ecs_whitelist* whitelist,
	struct config_file* cfg);

/**
 * See if an address or domain is whitelisted.
 * @param whitelist: structure for address storage.
 * @param addr: address to check
 * @param addrlen: length of addr.
 * @param qname: dname in query
 * @param qname_len: length of dname
 * @param qclass: class in query
 * @return: true if the address is whitelisted for subnet option. 
 */
int ecs_is_whitelisted(struct ecs_whitelist* whitelist,
	struct sockaddr_storage* addr, socklen_t addrlen, uint8_t* qname,
	size_t qname_len, uint16_t qclass);

/**
 * Get memory used by ecs_whitelist structure.
 * @param whitelist: structure for address storage.
 * @return bytes in use.
 */
size_t ecs_whitelist_get_mem(struct ecs_whitelist* whitelist);

#endif /* EDNSSUBNET_WHITELIST_H */
