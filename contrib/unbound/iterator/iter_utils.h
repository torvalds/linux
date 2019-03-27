/*
 * iterator/iter_utils.h - iterative resolver module utility functions.
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
 * Configuration options. Forward zones. 
 */

#ifndef ITERATOR_ITER_UTILS_H
#define ITERATOR_ITER_UTILS_H
#include "iterator/iter_resptype.h"
struct sldns_buffer;
struct iter_env;
struct iter_hints;
struct iter_forwards;
struct config_file;
struct module_env;
struct delegpt_addr;
struct delegpt;
struct regional;
struct msg_parse;
struct ub_randstate;
struct query_info;
struct reply_info;
struct module_qstate;
struct sock_list;
struct ub_packed_rrset_key;

/**
 * Process config options and set iterator module state.
 * Sets default values if no config is found.
 * @param iter_env: iterator module state.
 * @param cfg: config options.
 * @return 0 on error.
 */
int iter_apply_cfg(struct iter_env* iter_env, struct config_file* cfg);

/**
 * Select a valid, nice target to send query to.
 * Sorting and removing unsuitable targets is combined.
 *
 * @param iter_env: iterator module global state, with ip6 enabled and 
 *	do-not-query-addresses.
 * @param env: environment with infra cache (lameness, rtt info).
 * @param dp: delegation point with result list.
 * @param name: zone name (for lameness check).
 * @param namelen: length of name.
 * @param qtype: query type that we want to send.
 * @param dnssec_lame: set to 1, if a known dnssec-lame server is selected
 *	these are not preferred, but are used as a last resort.
 * @param chase_to_rd: set to 1 if a known recursion lame server is selected
 * 	these are not preferred, but are used as a last resort.
 * @param open_target: number of currently outstanding target queries.
 * 	If we wait for these, perhaps more server addresses become available.
 * @param blacklist: the IP blacklist to use.
 * @param prefetch: if not 0, prefetch is in use for this query.
 * 	This means the query can have different timing, because prefetch is
 * 	not waited upon by the downstream client, and thus a good time to
 * 	perform exploration of other targets.
 * @return best target or NULL if no target.
 *	if not null, that target is removed from the result list in the dp.
 */
struct delegpt_addr* iter_server_selection(struct iter_env* iter_env, 
	struct module_env* env, struct delegpt* dp, uint8_t* name, 
	size_t namelen, uint16_t qtype, int* dnssec_lame,
	int* chase_to_rd, int open_target, struct sock_list* blacklist,
	time_t prefetch);

/**
 * Allocate dns_msg from parsed msg, in regional.
 * @param pkt: packet.
 * @param msg: parsed message (cleaned and ready for regional allocation).
 * @param regional: regional to use for allocation.
 * @return newly allocated dns_msg, or NULL on memory error.
 */
struct dns_msg* dns_alloc_msg(struct sldns_buffer* pkt, struct msg_parse* msg, 
	struct regional* regional);

/**
 * Copy a dns_msg to this regional.
 * @param from: dns message, also in regional.
 * @param regional: regional to use for allocation.
 * @return newly allocated dns_msg, or NULL on memory error.
 */
struct dns_msg* dns_copy_msg(struct dns_msg* from, struct regional* regional);

/**
 * Allocate a dns_msg with malloc/alloc structure and store in dns cache.
 * @param env: environment, with alloc structure and dns cache.
 * @param qinf: query info, the query for which answer is stored.
 * @param rep: reply in dns_msg from dns_alloc_msg for example.
 * @param is_referral: If true, then the given message to be stored is a
 *	referral. The cache implementation may use this as a hint.
 * @param leeway: prefetch TTL leeway to expire old rrsets quicker.
 * @param pside: true if dp is parentside, thus message is 'fresh' and NS
 * 	can be prefetch-updates.
 * @param region: to copy modified (cache is better) rrs back to.
 * @param flags: with BIT_CD for dns64 AAAA translated queries.
 * @return void, because we are not interested in alloc errors,
 * 	the iterator and validator can operate on the results in their
 * 	scratch space (the qstate.region) and are not dependent on the cache.
 * 	It is useful to log the alloc failure (for the server operator),
 * 	but the query resolution can continue without cache storage.
 */
void iter_dns_store(struct module_env* env, struct query_info* qinf,
	struct reply_info* rep, int is_referral, time_t leeway, int pside,
	struct regional* region, uint16_t flags);

/**
 * Select randomly with n/m probability.
 * For shuffle NS records for address fetching.
 * @param rnd: random table
 * @param n: probability.
 * @param m: divisor for probability.
 * @return true with n/m probability.
 */
int iter_ns_probability(struct ub_randstate* rnd, int n, int m);

/**
 * Mark targets that result in a dependency cycle as done, so they
 * will not get selected as targets.
 * @param qstate: query state.
 * @param dp: delegpt to mark ns in.
 */
void iter_mark_cycle_targets(struct module_qstate* qstate, struct delegpt* dp);

/**
 * Mark targets that result in a dependency cycle as done, so they
 * will not get selected as targets.  For the parent-side lookups.
 * @param qstate: query state.
 * @param dp: delegpt to mark ns in.
 */
void iter_mark_pside_cycle_targets(struct module_qstate* qstate,
	struct delegpt* dp);

/**
 * See if delegation is useful or offers immediately no targets for 
 * further recursion.
 * @param qinfo: query name and type
 * @param qflags: query flags with RD flag
 * @param dp: delegpt to check.
 * @return true if dp is useless.
 */
int iter_dp_is_useless(struct query_info* qinfo, uint16_t qflags, 
	struct delegpt* dp);

/**
 * See if qname has DNSSEC needs.  This is true if there is a trust anchor above
 * it.  Whether there is an insecure delegation to the data is unknown.
 * @param env: environment with anchors.
 * @param qinfo: query name and class.
 * @return true if trust anchor above qname, false if no anchor or insecure
 * point above qname.
 */
int iter_qname_indicates_dnssec(struct module_env* env,
	struct query_info *qinfo);

/**
 * See if delegation is expected to have DNSSEC information (RRSIGs) in 
 * its answers, or not. Inspects delegation point (name), trust anchors,
 * and delegation message (DS RRset) to determine this.
 * @param env: module env with trust anchors.
 * @param dp: delegation point.
 * @param msg: delegation message, with DS if a secure referral.
 * @param dclass: class of query.
 * @return 1 if dnssec is expected, 0 if not or insecure point above qname.
 */
int iter_indicates_dnssec(struct module_env* env, struct delegpt* dp,
	struct dns_msg* msg, uint16_t dclass);

/**
 * See if a message contains DNSSEC.
 * This is examined by looking for RRSIGs. With DNSSEC a valid answer, 
 * nxdomain, nodata, referral or cname reply has RRSIGs in answer or auth 
 * sections, sigs on answer data, SOA, DS, or NSEC/NSEC3 records.
 * @param msg: message to examine.
 * @return true if DNSSEC information was found.
 */
int iter_msg_has_dnssec(struct dns_msg* msg);

/**
 * See if a message is known to be from a certain zone.
 * This looks for SOA or NS rrsets, for answers.
 * For referrals, when one label is delegated, the zone is detected.
 * Does not look at signatures.
 * @param msg: the message to inspect.
 * @param dp: delegation point with zone name to look for.
 * @param type: type of message.
 * @param dclass: class of query.
 * @return true if message is certain to be from zone in dp->name.
 *	false if not sure (empty msg), or not from the zone.
 */
int iter_msg_from_zone(struct dns_msg* msg, struct delegpt* dp, 
	enum response_type type, uint16_t dclass);

/**
 * Check if two replies are equal
 * For fallback procedures
 * @param p: reply one. The reply has rrset data pointers in region.
 * 	Does not check rrset-IDs
 * @param q: reply two
 * @param region: scratch buffer.
 * @return if one and two are equal.
 */
int reply_equal(struct reply_info* p, struct reply_info* q, struct regional* region);

/**
 * Remove unused bits from the reply if possible.
 * So that caps-for-id (0x20) fallback is more likely to be successful.
 * This removes like, the additional section, and NS record in the authority
 * section if those records are gratuitous (not for a referral).
 * @param rep: the reply to strip stuff out of.
 */
void caps_strip_reply(struct reply_info* rep);

/**
 * see if reply has a 'useful' rcode for capsforid comparison, so
 * not SERVFAIL or REFUSED, and thus NOERROR or NXDOMAIN.
 * @param rep: reply to check.
 * @return true if the rcode is a bad type of message.
 */
int caps_failed_rcode(struct reply_info* rep);

/**
 * Store parent-side rrset in separate rrset cache entries for later 
 * last-resort * lookups in case the child-side versions of this information 
 * fails.
 * @param env: environment with cache, time, ...
 * @param rrset: the rrset to store (copied).
 * Failure to store is logged, but otherwise ignored.
 */
void iter_store_parentside_rrset(struct module_env* env, 
	struct ub_packed_rrset_key* rrset);

/**
 * Store parent-side NS records from a referral message
 * @param env: environment with cache, time, ...
 * @param rep: response with NS rrset.
 * Failure to store is logged, but otherwise ignored.
 */
void iter_store_parentside_NS(struct module_env* env, struct reply_info* rep);

/**
 * Store parent-side negative element, the parentside rrset does not exist,
 * creates an rrset with empty rdata in the rrset cache with PARENTSIDE flag.
 * @param env: environment with cache, time, ...
 * @param qinfo: the identity of the rrset that is missing.
 * @param rep: delegation response or answer response, to glean TTL from.
 * (malloc) failure is logged but otherwise ignored.
 */
void iter_store_parentside_neg(struct module_env* env, 
	struct query_info* qinfo, struct reply_info* rep);

/**
 * Add parent NS record if that exists in the cache.  This is both new
 * information and acts like a timeout throttle on retries.
 * @param env: query env with rrset cache and time.
 * @param dp: delegation point to store result in.  Also this dp is used to
 *	see which NS name is needed.
 * @param region: region to alloc result in.
 * @param qinfo: pertinent information, the qclass.
 * @return false on malloc failure.
 *	if true, the routine worked and if such cached information 
 *	existed dp->has_parent_side_NS is set true.
 */
int iter_lookup_parent_NS_from_cache(struct module_env* env,
	struct delegpt* dp, struct regional* region, struct query_info* qinfo);

/**
 * Add parent-side glue if that exists in the cache.  This is both new
 * information and acts like a timeout throttle on retries to fetch them.
 * @param env: query env with rrset cache and time.
 * @param dp: delegation point to store result in.  Also this dp is used to
 *	see which NS name is needed.
 * @param region: region to alloc result in.
 * @param qinfo: pertinent information, the qclass.
 * @return: true, it worked, no malloc failures, and new addresses (lame)
 *	have been added, giving extra options as query targets.
 */
int iter_lookup_parent_glue_from_cache(struct module_env* env,
	struct delegpt* dp, struct regional* region, struct query_info* qinfo);

/**
 * Lookup next root-hint or root-forward entry.
 * @param hints: the hints.
 * @param fwd: the forwards.
 * @param c: the class to start searching at. 0 means find first one.
 * @return false if no classes found, true if found and returned in c.
 */
int iter_get_next_root(struct iter_hints* hints, struct iter_forwards* fwd,
	uint16_t* c);

/**
 * Remove DS records that are inappropriate before they are cached.
 * @param msg: the response to scrub.
 * @param ns: RRSET that is the NS record for the referral.
 * 	if NULL, then all DS records are removed from the authority section.
 * @param z: zone name that the response is from.
 */
void iter_scrub_ds(struct dns_msg* msg, struct ub_packed_rrset_key* ns,
	uint8_t* z);

/**
 * Remove query attempts from all available ips. For 0x20.
 * @param dp: delegpt.
 * @param d: decrease.
 */
void iter_dec_attempts(struct delegpt* dp, int d);

/**
 * Add retry counts from older delegpt to newer delegpt.
 * Does not waste time on timeout'd (or other failing) addresses.
 * @param dp: new delegationpoint.
 * @param old: old delegationpoint.
 */
void iter_merge_retry_counts(struct delegpt* dp, struct delegpt* old);

/**
 * See if a DS response (type ANSWER) is too low: a nodata answer with 
 * a SOA record in the authority section at-or-below the qchase.qname.
 * Also returns true if we are not sure (i.e. empty message, CNAME nosig).
 * @param msg: the response.
 * @param dp: the dp name is used to check if the RRSIG gives a clue that
 * 	it was originated from the correct nameserver.
 * @return true if too low.
 */
int iter_ds_toolow(struct dns_msg* msg, struct delegpt* dp);

/**
 * See if delegpt can go down a step to the qname or not
 * @param qinfo: the query name looked up.
 * @param dp: checked if the name can go lower to the qname
 * @return true if can go down, false if that would not be possible.
 * the current response seems to be the one and only, best possible, response.
 */
int iter_dp_cangodown(struct query_info* qinfo, struct delegpt* dp);

#endif /* ITERATOR_ITER_UTILS_H */
