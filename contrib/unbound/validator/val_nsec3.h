/*
 * validator/val_nsec3.h - validator NSEC3 denial of existence functions.
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
 * This file contains helper functions for the validator module.
 * The functions help with NSEC3 checking, the different NSEC3 proofs
 * for denial of existence, and proofs for presence of types.
 *
 * NSEC3
 *                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Hash Alg.   |     Flags     |          Iterations           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Salt Length  |                     Salt                      /
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Hash Length  |             Next Hashed Owner Name            /
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * /                         Type Bit Maps                         /
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * NSEC3PARAM
 *                      1 1 1 1 1 1 1 1 1 1 2 2 2 2 2 2 2 2 2 2 3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Hash Alg.   |     Flags     |          Iterations           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |  Salt Length  |                     Salt                      /
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

#ifndef VALIDATOR_VAL_NSEC3_H
#define VALIDATOR_VAL_NSEC3_H
#include "util/rbtree.h"
#include "util/data/packed_rrset.h"
struct val_env;
struct regional;
struct module_env;
struct module_qstate;
struct ub_packed_rrset_key;
struct reply_info;
struct query_info;
struct key_entry_key;
struct sldns_buffer;

/**
 *     0 1 2 3 4 5 6 7
 *    +-+-+-+-+-+-+-+-+
 *    |             |O|
 *    +-+-+-+-+-+-+-+-+
 * The OPT-OUT bit in the NSEC3 flags field.
 * If enabled, there can be zero or more unsigned delegations in the span.
 * If disabled, there are zero unsigned delegations in the span.
 */
#define NSEC3_OPTOUT	0x01
/**
 * The unknown flags in the NSEC3 flags field.
 * They must be zero, or the NSEC3 is ignored.
 */
#define NSEC3_UNKNOWN_FLAGS 0xFE

/** The SHA1 hash algorithm for NSEC3 */
#define NSEC3_HASH_SHA1	0x01

/**
 * Determine if the set of NSEC3 records provided with a response prove NAME
 * ERROR. This means that the NSEC3s prove a) the closest encloser exists,
 * b) the direct child of the closest encloser towards qname doesn't exist,
 * and c) *.closest encloser does not exist.
 *
 * @param env: module environment with temporary region and buffer.
 * @param ve: validator environment, with iteration count settings.
 * @param list: array of RRsets, some of which are NSEC3s.
 * @param num: number of RRsets in the array to examine.
 * @param qinfo: query that is verified for.
 * @param kkey: key entry that signed the NSEC3s.
 * @return:
 * 	sec_status SECURE of the Name Error is proven by the NSEC3 RRs, 
 * 	BOGUS if not, INSECURE if all of the NSEC3s could be validly ignored.
 */
enum sec_status
nsec3_prove_nameerror(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num, 
	struct query_info* qinfo, struct key_entry_key* kkey);

/**
 * Determine if the NSEC3s provided in a response prove the NOERROR/NODATA
 * status. There are a number of different variants to this:
 * 
 * 1) Normal NODATA -- qname is matched to an NSEC3 record, type is not
 * present.
 * 
 * 2) ENT NODATA -- because there must be NSEC3 record for
 * empty-non-terminals, this is the same as #1.
 * 
 * 3) NSEC3 ownername NODATA -- qname matched an existing, lone NSEC3
 * ownername, but qtype was not NSEC3. NOTE: as of nsec-05, this case no
 * longer exists.
 * 
 * 4) Wildcard NODATA -- A wildcard matched the name, but not the type.
 * 
 * 5) Opt-In DS NODATA -- the qname is covered by an opt-in span and qtype ==
 * DS. (or maybe some future record with the same parent-side-only property)
 *
 * @param env: module environment with temporary region and buffer.
 * @param ve: validator environment, with iteration count settings.
 * @param list: array of RRsets, some of which are NSEC3s.
 * @param num: number of RRsets in the array to examine.
 * @param qinfo: query that is verified for.
 * @param kkey: key entry that signed the NSEC3s.
 * @return:
 * 	sec_status SECURE of the proposition is proven by the NSEC3 RRs, 
 * 	BOGUS if not, INSECURE if all of the NSEC3s could be validly ignored.
 */
enum sec_status
nsec3_prove_nodata(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num, 
	struct query_info* qinfo, struct key_entry_key* kkey);


/**
 * Prove that a positive wildcard match was appropriate (no direct match
 * RRset).
 *
 * @param env: module environment with temporary region and buffer.
 * @param ve: validator environment, with iteration count settings.
 * @param list: array of RRsets, some of which are NSEC3s.
 * @param num: number of RRsets in the array to examine.
 * @param qinfo: query that is verified for.
 * @param kkey: key entry that signed the NSEC3s.
 * @param wc: The purported wildcard that matched. This is the wildcard name
 * 	as *.wildcard.name., with the *. label already removed.
 * @return:
 * 	sec_status SECURE of the proposition is proven by the NSEC3 RRs, 
 * 	BOGUS if not, INSECURE if all of the NSEC3s could be validly ignored.
 */
enum sec_status
nsec3_prove_wildcard(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num, 
	struct query_info* qinfo, struct key_entry_key* kkey, uint8_t* wc);

/**
 * Prove that a DS response either had no DS, or wasn't a delegation point.
 *
 * Fundamentally there are two cases here: normal NODATA and Opt-In NODATA.
 *
 * @param env: module environment with temporary region and buffer.
 * @param ve: validator environment, with iteration count settings.
 * @param list: array of RRsets, some of which are NSEC3s.
 * @param num: number of RRsets in the array to examine.
 * @param qinfo: query that is verified for.
 * @param kkey: key entry that signed the NSEC3s.
 * @param reason: string for bogus result.
 * @param qstate: qstate with region.
 * @return:
 * 	sec_status SECURE of the proposition is proven by the NSEC3 RRs, 
 * 	BOGUS if not, INSECURE if all of the NSEC3s could be validly ignored.
 * 	or if there was no DS in an insecure (i.e., opt-in) way,
 * 	INDETERMINATE if it was clear that this wasn't a delegation point.
 */
enum sec_status
nsec3_prove_nods(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num, 
	struct query_info* qinfo, struct key_entry_key* kkey, char** reason,
	struct module_qstate* qstate);

/**
 * Prove NXDOMAIN or NODATA.
 *
 * @param env: module environment with temporary region and buffer.
 * @param ve: validator environment, with iteration count settings.
 * @param list: array of RRsets, some of which are NSEC3s.
 * @param num: number of RRsets in the array to examine.
 * @param qinfo: query that is verified for.
 * @param kkey: key entry that signed the NSEC3s.
 * @param nodata: if return value is secure, this indicates if nodata or
 * 	nxdomain was proven.
 * @return:
 * 	sec_status SECURE of the proposition is proven by the NSEC3 RRs, 
 * 	BOGUS if not, INSECURE if all of the NSEC3s could be validly ignored.
 */
enum sec_status
nsec3_prove_nxornodata(struct module_env* env, struct val_env* ve,
	struct ub_packed_rrset_key** list, size_t num, 
	struct query_info* qinfo, struct key_entry_key* kkey, int* nodata);

/**
 * The NSEC3 hash result storage.
 * Consists of an rbtree, with these nodes in it.
 * The nodes detail how a set of parameters (from nsec3 rr) plus
 * a dname result in a hash.
 */
struct nsec3_cached_hash {
	/** rbtree node, key is this structure */
	rbnode_type node;
	/** where are the parameters for conversion, in this rrset data */
	struct ub_packed_rrset_key* nsec3;
	/** where are the parameters for conversion, this RR number in data */
	int rr;
	/** the name to convert */
	uint8_t* dname;
	/** length of the dname */
	size_t dname_len;
	/** the hash result (not base32 encoded) */
	uint8_t* hash;
	/** length of hash in bytes */
	size_t hash_len;
	/** the hash result in base32 encoding */
	uint8_t* b32;
	/** length of base32 encoding (as a label) */
	size_t b32_len;
};

/**
 * Rbtree for hash cache comparison function.
 * @param c1: key 1.
 * @param c2: key 2.
 * @return: comparison code, -1, 0, 1, of the keys.
 */
int nsec3_hash_cmp(const void* c1, const void* c2);

/**
 * Obtain the hash of an owner name.
 * Used internally by the nsec3 proof functions in this file.
 * published to enable unit testing of hash algorithms and cache.
 *
 * @param table: the cache table. Must be initialised at start.
 * @param region: scratch region to use for allocation.
 * 	This region holds the tree, if you wipe the region, reinit the tree.
 * @param buf: temporary buffer.
 * @param nsec3: the rrset with parameters
 * @param rr: rr number from d that has the NSEC3 parameters to hash to.
 * @param dname: name to hash
 * 	This pointer is used inside the tree, assumed region-alloced.
 * @param dname_len: the length of the name.
 * @param hash: the hash node is returned on success.
 * @return:
 * 	1 on success, either from cache or newly hashed hash is returned.
 * 	0 on a malloc failure.
 * 	-1 if the NSEC3 rr was badly formatted (i.e. formerr).
 */
int nsec3_hash_name(rbtree_type* table, struct regional* region,
	struct sldns_buffer* buf, struct ub_packed_rrset_key* nsec3, int rr,
	uint8_t* dname, size_t dname_len, struct nsec3_cached_hash** hash);

/**
 * Get next owner name, converted to base32 encoding and with the
 * zone name (taken from the nsec3 owner name) appended.
 * @param rrset: the NSEC3 rrset.
 * @param r: the rr num of the nsec3 in the rrset.
 * @param buf: buffer to store name in
 * @param max: size of buffer.
 * @return length of name on success. 0 on failure (buffer too short or
 *	bad format nsec3 record).
 */
size_t nsec3_get_nextowner_b32(struct ub_packed_rrset_key* rrset, int r,
	uint8_t* buf, size_t max);

/**
 * Convert hash into base32 encoding and with the
 * zone name appended.
 * @param hash: hashed buffer
 * @param hashlen: length of hash
 * @param zone: name of zone
 * @param zonelen: length of zonename.
 * @param buf: buffer to store name in
 * @param max: size of buffer.
 * @return length of name on success. 0 on failure (buffer too short or
 *	bad format nsec3 record).
 */
size_t nsec3_hash_to_b32(uint8_t* hash, size_t hashlen, uint8_t* zone,
	size_t zonelen, uint8_t* buf, size_t max);

/** 
 * Get NSEC3 parameters out of rr.
 * @param rrset: the NSEC3 rrset.
 * @param r: the rr num of the nsec3 in the rrset.
 * @param algo: nsec3 hash algo.
 * @param iter: iteration count.
 * @param salt: ptr to salt inside rdata.
 * @param saltlen: length of salt.
 * @return 0 if bad formatted, unknown nsec3 hash algo, or unknown flags set.
 */
int nsec3_get_params(struct ub_packed_rrset_key* rrset, int r,
	int* algo, size_t* iter, uint8_t** salt, size_t* saltlen);

/**
 * Get NSEC3 hashed in a buffer
 * @param buf: buffer for temp use.
 * @param nm: name to hash
 * @param nmlen: length of nm.
 * @param algo: algo to use, must be known.
 * @param iter: iterations
 * @param salt: salt for nsec3
 * @param saltlen: length of salt.
 * @param res: result of hash stored here.
 * @param max: maximum space for result.
 * @return 0 on failure, otherwise bytelength stored.
 */
size_t nsec3_get_hashed(struct sldns_buffer* buf, uint8_t* nm, size_t nmlen,
	int algo, size_t iter, uint8_t* salt, size_t saltlen, uint8_t* res,
	size_t max);

/** 
 * see if NSEC3 RR contains given type
 * @param rrset: NSEC3 rrset
 * @param r: RR in rrset
 * @param type: in host order to check bit for.
 * @return true if bit set, false if not or error.
 */
int nsec3_has_type(struct ub_packed_rrset_key* rrset, int r, uint16_t type);

/** 
 * return if nsec3 RR has the optout flag 
 * @param rrset: NSEC3 rrset
 * @param r: RR in rrset
 * @return true if optout, false on error or not optout
 */
int nsec3_has_optout(struct ub_packed_rrset_key* rrset, int r);

/** 
 * Return nsec3 RR next hashed owner name 
 * @param rrset: NSEC3 rrset
 * @param r: RR in rrset
 * @param next: ptr into rdata to next owner hash
 * @param nextlen: length of hash.
 * @return false on malformed
 */
int nsec3_get_nextowner(struct ub_packed_rrset_key* rrset, int r,
	uint8_t** next, size_t* nextlen);

/**
 * nsec3Covers
 * Given a hash and a candidate NSEC3Record, determine if that NSEC3Record
 * covers the hash. Covers specifically means that the hash is in between
 * the owner and next hashes and does not equal either.
 *
 * @param zone: the zone name.
 * @param hash: the hash of the name
 * @param rrset: the rrset of the NSEC3.
 * @param rr: which rr in the rrset.
 * @param buf: temporary buffer.
 * @return true if covers, false if not.
 */
int nsec3_covers(uint8_t* zone, struct nsec3_cached_hash* hash,
	struct ub_packed_rrset_key* rrset, int rr, struct sldns_buffer* buf);

#endif /* VALIDATOR_VAL_NSEC3_H */
