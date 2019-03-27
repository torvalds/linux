/*
 * ipsecmod/ipsecmod.h - facilitate opportunistic IPsec module
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
 * This file contains a module that facilitates opportunistic IPsec. It does so
 * by also quering for the IPSECKEY for A/AAAA queries and calling a
 * configurable hook (eg. signaling an IKE daemon) before replying.
 */

#ifndef IPSECMOD_H
#define IPSECMOD_H
#include "util/module.h"
#include "util/rbtree.h"

/**
 * The global variable environment contents for the ipsecmod
 * Shared between threads, this represents long term information.
 */
struct ipsecmod_env {
	/** White listed domains for ipsecmod. */
	rbtree_type* whitelist;
};

/**
 * Per query state for the ipsecmod module.
 */
struct ipsecmod_qstate {
	/** State of the IPsec module. */
	/** NOTE: This value is copied here from the configuration so that a change
	 *  with unbound-control would not complicate an already running mesh. */
	int enabled;
	/** If the qname is whitelisted or not. */
	/** NOTE: No whitelist means all qnames are whitelisted. */
	int is_whitelisted;
	/** Pointer to IPSECKEY rrset allocated in the qstate region. NULL if there
	 *  was no IPSECKEY reply from the subquery. */
	struct ub_packed_rrset_key* ipseckey_rrset;
	/** If the IPSECKEY subquery has finished. */
	int ipseckey_done;
};

/** Init the ipsecmod module */
int ipsecmod_init(struct module_env* env, int id);
/** Deinit the ipsecmod module */
void ipsecmod_deinit(struct module_env* env, int id);
/** Operate on an event on a query (in qstate). */
void ipsecmod_operate(struct module_qstate* qstate, enum module_ev event,
	int id, struct outbound_entry* outbound);
/** Subordinate query done, inform this super request of its conclusion */
void ipsecmod_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);
/** clear the ipsecmod query-specific contents out of qstate */
void ipsecmod_clear(struct module_qstate* qstate, int id);
/** return memory estimate for the ipsecmod module */
size_t ipsecmod_get_mem(struct module_env* env, int id);

/**
 * Get the function block with pointers to the ipsecmod functions
 * @return the function block for "ipsecmod".
 */
struct module_func_block* ipsecmod_get_funcblock(void);

#endif /* IPSECMOD_H */
