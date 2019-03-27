/*
 * validator/validator.h - secure validator DNS query response module
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
 * This file contains a module that performs validation of DNS queries.
 * According to RFC 4034.
 */

#ifndef VALIDATOR_VALIDATOR_H
#define VALIDATOR_VALIDATOR_H
#include "util/module.h"
#include "util/data/msgreply.h"
#include "validator/val_utils.h"
struct val_anchors;
struct key_cache;
struct key_entry_key;
struct val_neg_cache;
struct config_strlist;

/**
 * This is the TTL to use when a trust anchor fails to prime. A trust anchor
 * will be primed no more often than this interval.  Used when harden-
 * dnssec-stripped is off and the trust anchor fails.
 */
#define NULL_KEY_TTL	60 /* seconds */

/**
 * TTL for bogus key entries.  When a DS or DNSKEY fails in the chain of
 * trust the entire zone for that name is blacked out for this TTL.
 */
#define BOGUS_KEY_TTL	60 /* seconds */

/** max number of query restarts, number of IPs to probe */
#define VAL_MAX_RESTART_COUNT 5

/** Root key sentinel is ta preamble */
#define SENTINEL_IS		"root-key-sentinel-is-ta-"
/** Root key sentinel is not ta preamble */
#define SENTINEL_NOT		"root-key-sentinel-not-ta-"
/** Root key sentinal keytag length */
#define SENTINEL_KEYTAG_LEN	5

/**
 * Global state for the validator. 
 */
struct val_env {
	/** key cache; these are validated keys. trusted keys only
	 * end up here after being primed. */
	struct key_cache* kcache;

	/** aggressive negative cache. index into NSECs in rrset cache. */
	struct val_neg_cache* neg_cache;

	/** for debug testing a fixed validation date can be entered.
	 * if 0, current time is used for rrsig validation */
	int32_t date_override;

	/** clock skew min for signatures */
	int32_t skew_min;

	/** clock skew max for signatures */
	int32_t skew_max;

	/** TTL for bogus data; used instead of untrusted TTL from data.
	 * Bogus data will not be verified more often than this interval. 
	 * seconds. */
	uint32_t bogus_ttl;

	/**
	 * Number of entries in the NSEC3 maximum iteration count table.
	 * Keep this table short, and sorted by size
	 */
	int nsec3_keyiter_count;

	/**
	 * NSEC3 maximum iteration count per signing key size.
	 * This array contains key size values (in increasing order)
	 */
	size_t* nsec3_keysize;

	/**
	 * NSEC3 maximum iteration count per signing key size.
	 * This array contains the maximum iteration count for the keysize
	 * in the keysize array.
	 */
	size_t* nsec3_maxiter;

	/** lock on bogus counter */
	lock_basic_type bogus_lock;
	/** number of times rrsets marked bogus */
	size_t num_rrset_bogus;
};

/**
 * State of the validator for a query.
 */
enum val_state {
	/** initial state for validation */
	VAL_INIT_STATE = 0,
	/** find the proper keys for validation, follow trust chain */
	VAL_FINDKEY_STATE,
	/** validate the answer, using found key entry */
	VAL_VALIDATE_STATE,
	/** finish up */
	VAL_FINISHED_STATE,
	/** DLV lookup state, processing DLV queries */
	VAL_DLVLOOKUP_STATE
};

/**
 * Per query state for the validator module.
 */
struct val_qstate {
	/** 
	 * State of the validator module.
	 */
	enum val_state state;

	/**
	 * The original message we have been given to validate.
	 */
	struct dns_msg* orig_msg;

	/**
	 * The query restart count
	 */
	int restart_count;
	/** The blacklist saved for chainoftrust elements */
	struct sock_list* chain_blacklist;

	/**
	 * The query name we have chased to; qname after following CNAMEs
	 */
	struct query_info qchase;

	/**
	 * The chased reply, extract from original message. Can be:
	 * 	o CNAME
	 * 	o DNAME + CNAME
	 * 	o answer 
	 * 	plus authority, additional (nsecs) that have same signature.
	 */
	struct reply_info* chase_reply;

	/**
	 * The cname skip value; the number of rrsets that have been skipped
	 * due to chasing cnames. This is the offset into the 
	 * orig_msg->rep->rrsets array, into the answer section.
	 * starts at 0 - for the full original message.
	 * if it is >0 - qchase followed the cname, chase_reply setup to be
	 * that message and relevant authority rrsets.
	 *
	 * The skip is also used for referral messages, where it will
	 * range from 0, over the answer, authority and additional sections.
	 */
	size_t rrset_skip;

	/** trust anchor name */
	uint8_t* trust_anchor_name;
	/** trust anchor labels */
	int trust_anchor_labs;
	/** trust anchor length */
	size_t trust_anchor_len;

	/** the DS rrset */
	struct ub_packed_rrset_key* ds_rrset;

	/** domain name for empty nonterminal detection */
	uint8_t* empty_DS_name;
	/** length of empty_DS_name */
	size_t empty_DS_len;

	/** the current key entry */
	struct key_entry_key* key_entry;

	/** subtype */
	enum val_classification subtype;

	/** signer name */
	uint8_t* signer_name;
	/** length of signer_name */
	size_t signer_len;

	/** true if this state is waiting to prime a trust anchor */
	int wait_prime_ta;

	/** have we already checked the DLV? */
	int dlv_checked;
	/** The name for which the DLV is looked up. For the current message
	 * or for the current RRset (for CNAME, REFERRAL types).
	 * If there is signer name, that may be it, else a domain name */
	uint8_t* dlv_lookup_name;
	/** length of dlv lookup name */
	size_t dlv_lookup_name_len;
	/** Name at which chain of trust stopped with insecure, starting DLV
	 * DLV must result in chain going further down */
	uint8_t* dlv_insecure_at;
	/** length of dlv insecure point name */
	size_t dlv_insecure_at_len;
	/** status of DLV lookup. Indication to VAL_DLV_STATE what to do */
	enum dlv_status {
		dlv_error, /* server failure */
		dlv_success, /* got a DLV */
		dlv_ask_higher, /* ask again */
		dlv_there_is_no_dlv /* got no DLV, sure of it */
	} dlv_status;
};

/**
 * Get the validator function block.
 * @return: function block with function pointers to validator methods.
 */
struct module_func_block* val_get_funcblock(void);

/**
 * Get validator state as a string
 * @param state: to convert
 * @return constant string that is printable.
 */
const char* val_state_to_string(enum val_state state);

/** validator init */
int val_init(struct module_env* env, int id);

/** validator deinit */
void val_deinit(struct module_env* env, int id);

/** validator operate on a query */
void val_operate(struct module_qstate* qstate, enum module_ev event, int id,
        struct outbound_entry* outbound);

/** 
 * inform validator super.
 * 
 * @param qstate: query state that finished.
 * @param id: module id.
 * @param super: the qstate to inform.
 */
void val_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);

/** validator cleanup query state */
void val_clear(struct module_qstate* qstate, int id);

/**
 * Debug helper routine that assists worker in determining memory in 
 * use.
 * @param env: module environment 
 * @param id: module id.
 * @return memory in use in bytes.
 */
size_t val_get_mem(struct module_env* env, int id);

#endif /* VALIDATOR_VALIDATOR_H */
