/*
 * iterator/iterator.h - iterative resolver DNS query response module
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
 * This file contains a module that performs recursive iterative DNS query
 * processing.
 */

#ifndef ITERATOR_ITERATOR_H
#define ITERATOR_ITERATOR_H
#include "services/outbound_list.h"
#include "util/data/msgreply.h"
#include "util/module.h"
struct delegpt;
struct iter_hints;
struct iter_forwards;
struct iter_donotq;
struct iter_prep_list;
struct iter_priv;
struct rbtree_type;

/** max number of targets spawned for a query and its subqueries */
#define MAX_TARGET_COUNT	64
/** max number of query restarts. Determines max number of CNAME chain. */
#define MAX_RESTART_COUNT       8
/** max number of referrals. Makes sure resolver does not run away */
#define MAX_REFERRAL_COUNT	130
/** max number of queries-sent-out.  Make sure large NS set does not loop */
#define MAX_SENT_COUNT		32
/** max number of queries for which to perform dnsseclameness detection,
 * (rrsigs missing detection) after that, just pick up that response */
#define DNSSEC_LAME_DETECT_COUNT 4
/**
 * max number of QNAME minimisation iterations. Limits number of queries for
 * QNAMEs with a lot of labels.
*/
#define MAX_MINIMISE_COUNT	10
/* max number of time-outs for minimised query. Prevents resolving failures
 * when the QNAME minimisation QTYPE is blocked. */
#define MAX_MINIMISE_TIMEOUT_COUNT 3
/**
 * number of labels from QNAME that are always send individually when using
 * QNAME minimisation, even when the number of labels of the QNAME is bigger
 * tham MAX_MINIMISE_COUNT */
#define MINIMISE_ONE_LAB	4
#define MINIMISE_MULTIPLE_LABS	(MAX_MINIMISE_COUNT - MINIMISE_ONE_LAB)
/** at what query-sent-count to stop target fetch policy */
#define TARGET_FETCH_STOP	3
/** how nice is a server without further information, in msec 
 * Equals rtt initial timeout value.
 */
#define UNKNOWN_SERVER_NICENESS 376
/** maximum timeout before a host is deemed unsuitable, in msec. 
 * After host_ttl this will be timed out and the host will be tried again. 
 * Equals RTT_MAX_TIMEOUT
 */
#define USEFUL_SERVER_TOP_TIMEOUT	120000
/** number of retries on outgoing queries */
#define OUTBOUND_MSG_RETRY 5
/** RTT band, within this amount from the best, servers are chosen randomly.
 * Chosen so that the UNKNOWN_SERVER_NICENESS falls within the band of a 
 * fast server, this causes server exploration as a side benefit. msec. */
#define RTT_BAND 400
/** Start value for blacklisting a host, 2*USEFUL_SERVER_TOP_TIMEOUT in sec */
#define INFRA_BACKOFF_INITIAL 240

/**
 * Global state for the iterator. 
 */
struct iter_env {
	/** A flag to indicate whether or not we have an IPv6 route */
	int supports_ipv6;

	/** A flag to indicate whether or not we have an IPv4 route */
	int supports_ipv4;

	/** A set of inetaddrs that should never be queried. */
	struct iter_donotq* donotq;

	/** private address space and private domains */
	struct iter_priv* priv;

	/** whitelist for capsforid names */
	struct rbtree_type* caps_white;

	/** The maximum dependency depth that this resolver will pursue. */
	int max_dependency_depth;

	/**
	 * The target fetch policy for each dependency level. This is 
	 * described as a simple number (per dependency level): 
	 *	negative numbers (usually just -1) mean fetch-all, 
	 *	0 means only fetch on demand, and 
	 *	positive numbers mean to fetch at most that many targets.
	 * array of max_dependency_depth+1 size.
	 */
	int* target_fetch_policy;

	/** lock on ratelimit counter */
	lock_basic_type queries_ratelimit_lock;
	/** number of queries that have been ratelimited */
	size_t num_queries_ratelimited;
};

/**
 * QNAME minimisation state
 */
enum minimisation_state {
	/**
	 * (Re)start minimisation. Outgoing QNAME should be set to dp->name.
	 * State entered on new query or after following referral or CNAME.
	 */
	INIT_MINIMISE_STATE = 0,
	/**
	 * QNAME minimisation ongoing. Increase QNAME on every iteration.
	 */
	MINIMISE_STATE,
	/**
	 * Don't increment QNAME this iteration
	 */
	SKIP_MINIMISE_STATE,
	/**
	 * Send out full QNAME + original QTYPE
	 */
	DONOT_MINIMISE_STATE,
};

/**
 * State of the iterator for a query.
 */
enum iter_state {
	/**
	 * Externally generated queries start at this state. Query restarts are
	 * reset to this state.
	 */
	INIT_REQUEST_STATE = 0,

	/**
	 * Root priming events reactivate here, most other events pass 
	 * through this naturally as the 2nd part of the INIT_REQUEST_STATE.
	 */
	INIT_REQUEST_2_STATE,

	/**
	 * Stub priming events reactivate here, most other events pass 
	 * through this naturally as the 3rd part of the INIT_REQUEST_STATE.
	 */
	INIT_REQUEST_3_STATE,

	/**
	 * Each time a delegation point changes for a given query or a 
	 * query times out and/or wakes up, this state is (re)visited. 
	 * This state is responsible for iterating through a list of 
	 * nameserver targets.
	 */
	QUERYTARGETS_STATE,

	/**
	 * Responses to queries start at this state. This state handles 
	 * the decision tree associated with handling responses.
	 */
	QUERY_RESP_STATE,

	/** Responses to priming queries finish at this state. */
	PRIME_RESP_STATE,

	/** Collecting query class information, for qclass=ANY, when
	 * it spawns off queries for every class, it returns here. */
	COLLECT_CLASS_STATE,

	/** Find NS record to resolve DS record from, walking to the right
	 * NS spot until we find it */
	DSNS_FIND_STATE,

	/** Responses that are to be returned upstream end at this state. 
	 * As well as responses to target queries. */
	FINISHED_STATE
};

/**
 * Per query state for the iterator module.
 */
struct iter_qstate {
	/** 
	 * State of the iterator module.
	 * This is the state that event is in or should sent to -- all 
	 * requests should start with the INIT_REQUEST_STATE. All 
	 * responses should start with QUERY_RESP_STATE. Subsequent 
	 * processing of the event will change this state.
	 */
	enum iter_state state;

	/** 
	 * Final state for the iterator module.
	 * This is the state that responses should be routed to once the 
	 * response is final. For externally initiated queries, this 
	 * will be FINISHED_STATE, locally initiated queries will have 
	 * different final states.
	 */
	enum iter_state final_state;

	/** 
	 * The depth of this query, this means the depth of recursion.
	 * This address is needed for another query, which is an address
	 * needed for another query, etc. Original client query has depth 0.
	 */
	int depth;

	/**
	 * The response
	 */
	struct dns_msg* response;

	/** 
	 * This is a list of RRsets that must be prepended to the 
	 * ANSWER section of a response before being sent upstream.
	 */
	struct iter_prep_list* an_prepend_list;
	/** Last element of the prepend list */
	struct iter_prep_list* an_prepend_last;

	/**
	 * This is the list of RRsets that must be prepended to the
	 * AUTHORITY section of the response before being sent upstream.
	 */
	struct iter_prep_list* ns_prepend_list;
	/** Last element of the authority prepend list */
	struct iter_prep_list* ns_prepend_last;

	/** query name used for chasing the results. Initially the same as
	 * the state qinfo, but after CNAMEs this will be different. 
	 * The query info used to elicit the results needed. */
	struct query_info qchase;
	/** query flags to use when chasing the answer (i.e. RD flag) */
	uint16_t chase_flags;
	/** true if we set RD bit because of last resort recursion lame query*/
	int chase_to_rd;

	/** 
	 * This is the current delegation point for an in-progress query. This
	 * object retains state as to which delegation targets need to be
	 * (sub)queried for vs which ones have already been visited.
	 */
	struct delegpt* dp;

	/** state for 0x20 fallback when capsfail happens, 0 not a fallback */
	int caps_fallback;
	/** state for capsfail: current server number to try */
	size_t caps_server;
	/** state for capsfail: stored query for comparisons. Can be NULL if
	 * no response had been seen prior to starting the fallback. */
	struct reply_info* caps_reply;
	struct dns_msg* caps_response;

	/** Current delegation message - returned for non-RD queries */
	struct dns_msg* deleg_msg;

	/** number of outstanding target sub queries */
	int num_target_queries;

	/** outstanding direct queries */
	int num_current_queries;

	/** the number of times this query has been restarted. */
	int query_restart_count;

	/** the number of times this query as followed a referral. */
	int referral_count;

	/** number of queries fired off */
	int sent_count;
	
	/** number of target queries spawned in [1], for this query and its
	 * subqueries, the malloced-array is shared, [0] refcount. */
	int* target_count;

	/** if true, already tested for ratelimiting and passed the test */
	int ratelimit_ok;

	/**
	 * The query must store NS records from referrals as parentside RRs
	 * Enabled once it hits resolution problems, to throttle retries.
	 * If enabled it is the pointer to the old delegation point with
	 * the old retry counts for bad-nameserver-addresses.
	 */
	struct delegpt* store_parent_NS;

	/**
	 * The query is for parent-side glue(A or AAAA) for a nameserver.
	 * If the item is seen as glue in a referral, and pside_glue is NULL,
	 * then it is stored in pside_glue for later.
	 * If it was never seen, at the end, then a negative caching element 
	 * must be created.  
	 * The (data or negative) RR cache element then throttles retries.
	 */
	int query_for_pside_glue;
	/** the parent-side-glue element (NULL if none, its first match) */
	struct ub_packed_rrset_key* pside_glue;

	/** If nonNULL we are walking upwards from DS query to find NS */
	uint8_t* dsns_point;
	/** length of the dname in dsns_point */
	size_t dsns_point_len;

	/** 
	 * expected dnssec information for this iteration step. 
	 * If dnssec rrsigs are expected and not given, the server is marked
	 * lame (dnssec-lame).
	 */
	int dnssec_expected;

	/**
	 * We are expecting dnssec information, but we also know the server
	 * is DNSSEC lame.  The response need not be marked dnssec-lame again.
	 */
	int dnssec_lame_query;

	/**
	 * This is flag that, if true, means that this event is 
	 * waiting for a stub priming query. 
	 */
	int wait_priming_stub;

	/**
	 * This is a flag that, if true, means that this query is
	 * for (re)fetching glue from a zone. Since the address should
	 * have been glue, query again to the servers that should have
	 * been returning it as glue.
	 * The delegation point must be set to the one that should *not*
	 * be used when creating the state. A higher one will be attempted.
	 */
	int refetch_glue;

	/** list of pending queries to authoritative servers. */
	struct outbound_list outlist;

	/** QNAME minimisation state, RFC7816 */
	enum minimisation_state minimisation_state;

	/** State for capsfail: QNAME minimisation state for comparisons. */
	enum minimisation_state caps_minimisation_state;

	/**
	 * The query info that is sent upstream. Will be a subset of qchase
	 * when qname minimisation is enabled.
	 */
	struct query_info qinfo_out;

	/**
	 * Count number of QNAME minimisation iterations. Used to limit number of
	 * outgoing queries when QNAME minimisation is enabled.
	 */
	int minimise_count;

	/**
	 * Count number of time-outs. Used to prevent resolving failures when
	 * the QNAME minimisation QTYPE is blocked. */
	int minimise_timeout_count;

	/** True if the current response is from auth_zone */
	int auth_zone_response;
	/** True if the auth_zones should not be consulted for the query */
	int auth_zone_avoid;
};

/**
 * List of prepend items
 */
struct iter_prep_list {
	/** next in list */
	struct iter_prep_list* next;
	/** rrset */
	struct ub_packed_rrset_key* rrset;
};

/**
 * Get the iterator function block.
 * @return: function block with function pointers to iterator methods.
 */
struct module_func_block* iter_get_funcblock(void);

/**
 * Get iterator state as a string
 * @param state: to convert
 * @return constant string that is printable.
 */
const char* iter_state_to_string(enum iter_state state);

/**
 * See if iterator state is a response state
 * @param s: to inspect
 * @return true if response state.
 */
int iter_state_is_responsestate(enum iter_state s);

/** iterator init */
int iter_init(struct module_env* env, int id);

/** iterator deinit */
void iter_deinit(struct module_env* env, int id);

/** iterator operate on a query */
void iter_operate(struct module_qstate* qstate, enum module_ev event, int id,
	struct outbound_entry* outbound);

/**
 * Return priming query results to interested super querystates.
 * 
 * Sets the delegation point and delegation message (not nonRD queries).
 * This is a callback from walk_supers.
 *
 * @param qstate: query state that finished.
 * @param id: module id.
 * @param super: the qstate to inform.
 */
void iter_inform_super(struct module_qstate* qstate, int id, 
	struct module_qstate* super);

/** iterator cleanup query state */
void iter_clear(struct module_qstate* qstate, int id);

/** iterator alloc size routine */
size_t iter_get_mem(struct module_env* env, int id);

#endif /* ITERATOR_ITERATOR_H */
