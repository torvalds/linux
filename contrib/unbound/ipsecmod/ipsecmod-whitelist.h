/*
 * ipsecmod/ipsecmod-whitelist.h - White listed domains for the ipsecmod to
 * operate on.
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
 * Keep track of the white listed domains for ipsecmod.
 */

#ifndef IPSECMOD_WHITELIST_H
#define IPSECMOD_WHITELIST_H
#include "util/storage/dnstree.h"

struct config_file;
struct regional;

/**
 * Process ipsecmod_whitelist config.
 * @param ie: ipsecmod environment.
 * @param cfg: config options.
 * @return 0 on error.
 */
int ipsecmod_whitelist_apply_cfg(struct ipsecmod_env* ie,
	struct config_file* cfg);

/**
 * Delete the ipsecmod whitelist.
 * @param whitelist: ipsecmod whitelist.
 */
void ipsecmod_whitelist_delete(rbtree_type* whitelist);

/**
 * See if a domain is whitelisted.
 * @param ie: ipsecmod environment.
 * @param dname: domain name to check.
 * @param dname_len: length of domain name.
 * @param qclass: query CLASS.
 * @return: true if the domain is whitelisted for the ipsecmod.
 */
int ipsecmod_domain_is_whitelisted(struct ipsecmod_env* ie, uint8_t* dname,
	size_t dname_len, uint16_t qclass);

/**
 * Get memory used by ipsecmod whitelist.
 * @param whitelist: structure for domain storage.
 * @return bytes in use.
 */
size_t ipsecmod_whitelist_get_mem(rbtree_type* whitelist);

#endif /* IPSECMOD_WHITELIST_H */
