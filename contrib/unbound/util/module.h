/*
 * util/module.h - DNS handling module interface
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
 * This file contains the interface for DNS handling modules.
 *
 * The module interface uses the DNS modules as state machines.  The
 * state machines are activated in sequence to operate on queries.  Once
 * they are done, the reply is passed back.  In the usual setup the mesh
 * is the caller of the state machines and once things are done sends replies
 * and invokes result callbacks.
 *
 * The module provides a number of functions, listed in the module_func_block.
 * The module is inited and destroyed and memory usage queries, for the
 * module as a whole, for entire-module state (such as a cache).  And per-query
 * functions are called, operate to move the state machine and cleanup of
 * the per-query state.
 *
 * Most per-query state should simply be allocated in the query region.
 * This is destroyed at the end of the query.
 *
 * The module environment contains services and information and caches
 * shared by the modules and the rest of the system.  It also contains
 * function pointers for module-specific tasks (like sending queries).
 *
 * *** Example module calls for a normal query
 *
 * In this example, the query does not need recursion, all the other data
 * can be found in the cache.  This makes the example shorter.
 *
 * At the start of the program the iterator module is initialised.
 * The iterator module sets up its global state, such as donotquery lists
 * and private address trees.
 *
 * A query comes in, and a mesh entry is created for it.  The mesh
 * starts the resolution process.  The validator module is the first
 * in the list of modules, and it is started on this new query.  The
 * operate() function is called.  The validator decides it needs not do
 * anything yet until there is a result and returns wait_module, that
 * causes the next module in the list to be started.
 *
 * The next module is the iterator.  It is started on the passed query and
 * decides to perform a lookup.  For this simple example, the delegation
 * point information is available, and all the iterator wants to do is
 * send a UDP query.  The iterator uses env.send_query() to send the
 * query.  Then the iterator suspends (returns from the operate call).
 *
 * When the UDP reply comes back (and on errors and timeouts), the
 * operate function is called for the query, on the iterator module,
 * with the event that there is a reply.  The iterator decides that this
 * is enough, the work is done.  It returns the value finished from the
 * operate call, which causes the previous module to be started.
 *
 * The previous module, the validator module, is started with the event
 * that the iterator module is done.  The validator decides to validate
 * the query.  Once it is done (which could take recursive lookups, but
 * in this example no recursive lookups are needed), it returns from the
 * operate function with finished.
 *
 * There is no previous module from the validator module, and the mesh
 * takes this to mean that the query is finally done.  The mesh invokes
 * callbacks and sends packets to queriers.
 *
 * If other modules had been waiting (recursively) on the answer to this
 * query, then the mesh will tell them about it.  It calls the inform_super
 * routine on all the waiting modules, and once that is done it calls all of
 * them with the operate() call.  During inform_super the query that is done
 * still exists and information can be copied from it (but the module should
 * not really re-entry codepoints and services).  During the operate call
 * the modules can use stored state to continue operation with the results.
 * (network buffers are used to contain the answer packet during the
 * inform_super phase, but after that the network buffers will be cleared
 * of their contents so that other tasks can be performed).
 *
 * *** Example module calls for recursion
 *
 * A module is called in operate, and it decides that it wants to perform
 * recursion.  That is, it wants the full state-machine-list to operate on
 * a different query.  It calls env.attach_sub() to create a new query state.
 * The routine returns the newly created state, and potentially the module
 * can edit the module-states for the newly created query (i.e. pass along
 * some information, like delegation points).  The module then suspends,
 * returns from the operate routine.
 *
 * The mesh meanwhile will have the newly created query (or queries) on
 * a waiting list, and will call operate() on this query (or queries).
 * It starts again at the start of the module list for them.  The query
 * (or queries) continue to operate their state machines, until they are
 * done.  When they are done the mesh calls inform_super on the module that
 * wanted the recursion.  After that the mesh calls operate() on the module
 * that wanted to do the recursion, and during this phase the module could,
 * for example, decide to create more recursions.
 *
 * If the module decides it no longer wants the recursive information
 * it can call detach_subs.  Those queries will still run to completion,
 * potentially filling the cache with information.  Inform_super is not
 * called any more.
 *
 * The iterator module will fetch items from the cache, so a recursion
 * attempt may complete very quickly if the item is in cache.  The calling
 * module has to wait for completion or eventual timeout.  A recursive query
 * that times out returns a servfail rcode (servfail is also returned for
 * other errors during the lookup).
 *
 * Results are passed in the qstate, the rcode member is used to pass
 * errors without requiring memory allocation, so that the code can continue
 * in out-of-memory conditions.  If the rcode member is 0 (NOERROR) then
 * the dns_msg entry contains a filled out message.  This message may
 * also contain an rcode that is nonzero, but in this case additional
 * information (query, additional) can be passed along.
 *
 * The rcode and dns_msg are used to pass the result from the the rightmost
 * module towards the leftmost modules and then towards the user.
 *
 * If you want to avoid recursion-cycles where queries need other queries
 * that need the first one, use detect_cycle() to see if that will happen.
 *
 */

#ifndef UTIL_MODULE_H
#define UTIL_MODULE_H
#include "util/storage/lruhash.h"
#include "util/data/msgreply.h"
#include "util/data/msgparse.h"
struct sldns_buffer;
struct alloc_cache;
struct rrset_cache;
struct key_cache;
struct config_file;
struct slabhash;
struct query_info;
struct edns_data;
struct regional;
struct worker;
struct comm_base;
struct auth_zones;
struct outside_network;
struct module_qstate;
struct ub_randstate;
struct mesh_area;
struct mesh_state;
struct val_anchors;
struct val_neg_cache;
struct iter_forwards;
struct iter_hints;
struct respip_set;
struct respip_client_info;
struct respip_addr_info;

/** Maximum number of modules in operation */
#define MAX_MODULE 16

/** Maximum number of known edns options */
#define MAX_KNOWN_EDNS_OPTS 256

enum inplace_cb_list_type {
	/* Inplace callbacks for when a resolved reply is ready to be sent to the
	 * front.*/
	inplace_cb_reply = 0,
	/* Inplace callbacks for when a reply is given from the cache. */
	inplace_cb_reply_cache,
	/* Inplace callbacks for when a reply is given with local data
	 * (or Chaos reply). */
	inplace_cb_reply_local,
	/* Inplace callbacks for when the reply is servfail. */
	inplace_cb_reply_servfail,
	/* Inplace callbacks for when a query is ready to be sent to the back.*/
	inplace_cb_query,
	/* Inplace callback for when a reply is received from the back. */
	inplace_cb_query_response,
	/* Inplace callback for when EDNS is parsed on a reply received from the
	 * back. */
	inplace_cb_edns_back_parsed,
	/* Total number of types. Used for array initialization.
	 * Should always be last. */
	inplace_cb_types_total
};


/** Known edns option. Can be populated during modules' init. */
struct edns_known_option {
	/** type of this edns option */
	uint16_t opt_code;
	/** whether the option needs to bypass the cache stage */
	int bypass_cache_stage;
	/** whether the option needs mesh aggregation */
	int no_aggregation;
};

/**
 * Inplace callback list of registered routines to be called.
 */
struct inplace_cb {
	/** next in list */
	struct inplace_cb* next;
	/** Inplace callback routine */
	void* cb;
	void* cb_arg;
	/** module id */
	int id;
};

/**
 * Inplace callback function called before replying.
 * Called as func(qinfo, qstate, rep, rcode, edns, opt_list_out, repinfo,
 *                region, id, python_callback)
 * Where:
 *	qinfo: the query info.
 *	qstate: the module state. NULL when calling before the query reaches the
 *		mesh states.
 *	rep: reply_info. Could be NULL.
 *	rcode: the return code.
 *	edns: the edns_data of the reply. When qstate is NULL, it is also used as
 *		the edns input.
 *	opt_list_out: the edns options list for the reply.
 *	repinfo: reply information for a communication point. NULL when calling
 *		during the mesh states; the same could be found from
 *		qstate->mesh_info->reply_list.
 *	region: region to store data.
 *	id: module id.
 *	python_callback: only used for registering a python callback function.
 */
typedef int inplace_cb_reply_func_type(struct query_info* qinfo,
	struct module_qstate* qstate, struct reply_info* rep, int rcode,
	struct edns_data* edns, struct edns_option** opt_list_out,
	struct comm_reply* repinfo, struct regional* region, int id,
	void* callback);

/**
 * Inplace callback function called before sending the query to a nameserver.
 * Called as func(qinfo, flags, qstate, addr, addrlen, zone, zonelen, region,
 *                id, python_callback)
 * Where:
 *	qinfo: query info.
 *	flags: flags of the query.
 *	qstate: query state.
 *	addr: to which server to send the query.
 *	addrlen: length of addr.
 *	zone: name of the zone of the delegation point. wireformat dname.
 *		This is the delegation point name for which the server is deemed
 *		authoritative.
 *	zonelen: length of zone.
 *	region: region to store data.
 *	id: module id.
 *	python_callback: only used for registering a python callback function.
 */
typedef int inplace_cb_query_func_type(struct query_info* qinfo, uint16_t flags,
	struct module_qstate* qstate, struct sockaddr_storage* addr,
	socklen_t addrlen, uint8_t* zone, size_t zonelen, struct regional* region,
	int id, void* callback);

/**
 * Inplace callback function called after parsing edns on query reply.
 * Called as func(qstate, id, cb_args)
 * Where:
 *	qstate: the query state.
 *	id: module id.
 *	cb_args: argument passed when registering callback.
 */
typedef int inplace_cb_edns_back_parsed_func_type(struct module_qstate* qstate, 
	int id, void* cb_args);

/**
 * Inplace callback function called after parsing query response.
 * Called as func(qstate, response, id, cb_args)
 * Where:
 *	qstate: the query state.
 *	response: query response.
 *	id: module id.
 *	cb_args: argument passed when registering callback.
 */
typedef int inplace_cb_query_response_func_type(struct module_qstate* qstate,
	struct dns_msg* response, int id, void* cb_args);

/**
 * Module environment.
 * Services and data provided to the module.
 */
struct module_env {
	/* --- data --- */
	/** config file with config options */
	struct config_file* cfg;
	/** shared message cache */
	struct slabhash* msg_cache;
	/** shared rrset cache */
	struct rrset_cache* rrset_cache;
	/** shared infrastructure cache (edns, lameness) */
	struct infra_cache* infra_cache;
	/** shared key cache */
	struct key_cache* key_cache;

	/* --- services --- */
	/** 
	 * Send serviced DNS query to server. UDP/TCP and EDNS is handled.
	 * operate() should return with wait_reply. Later on a callback 
	 * will cause operate() to be called with event timeout or reply.
	 * The time until a timeout is calculated from roundtrip timing,
	 * several UDP retries are attempted.
	 * @param qinfo: query info.
	 * @param flags: host order flags word, with opcode and CD bit.
	 * @param dnssec: if set, EDNS record will have bits set.
	 *	If EDNS_DO bit is set, DO bit is set in EDNS records.
	 *	If BIT_CD is set, CD bit is set in queries with EDNS records.
	 * @param want_dnssec: if set, the validator wants DNSSEC.  Without
	 * 	EDNS, the answer is likely to be useless for this domain.
	 * @param nocaps: do not use caps_for_id, use the qname as given.
	 *	(ignored if caps_for_id is disabled).
	 * @param addr: where to.
	 * @param addrlen: length of addr.
	 * @param zone: delegation point name.
	 * @param zonelen: length of zone name.
	 * @param ssl_upstream: use SSL for upstream queries.
	 * @param tls_auth_name: if ssl_upstream, use this name with TLS
	 * 	authentication.
	 * @param q: wich query state to reactivate upon return.
	 * @return: false on failure (memory or socket related). no query was
	 *	sent. Or returns an outbound entry with qsent and qstate set.
	 *	This outbound_entry will be used on later module invocations
	 *	that involve this query (timeout, error or reply).
	 */
	struct outbound_entry* (*send_query)(struct query_info* qinfo,
		uint16_t flags, int dnssec, int want_dnssec, int nocaps,
		struct sockaddr_storage* addr, socklen_t addrlen,
		uint8_t* zone, size_t zonelen, int ssl_upstream,
		char* tls_auth_name, struct module_qstate* q);

	/**
	 * Detach-subqueries.
	 * Remove all sub-query references from this query state.
	 * Keeps super-references of those sub-queries correct.
	 * Updates stat items in mesh_area structure.
	 * @param qstate: used to find mesh state.
	 */
	void (*detach_subs)(struct module_qstate* qstate);

	/**
	 * Attach subquery.
	 * Creates it if it does not exist already.
	 * Keeps sub and super references correct.
	 * Updates stat items in mesh_area structure.
	 * Pass if it is priming query or not.
	 * return:
	 * o if error (malloc) happened.
	 * o need to initialise the new state (module init; it is a new state).
	 *   so that the next run of the query with this module is successful.
	 * o no init needed, attachment successful.
	 * 
	 * @param qstate: the state to find mesh state, and that wants to 
	 * 	receive the results from the new subquery.
	 * @param qinfo: what to query for (copied).
	 * @param qflags: what flags to use (RD, CD flag or not).
	 * @param prime: if it is a (stub) priming query.
	 * @param valrec: validation lookup recursion, does not need validation
	 * @param newq: If the new subquery needs initialisation, it is 
	 * 	returned, otherwise NULL is returned.
	 * @return: false on error, true if success (and init may be needed).
	 */ 
	int (*attach_sub)(struct module_qstate* qstate, 
		struct query_info* qinfo, uint16_t qflags, int prime, 
		int valrec, struct module_qstate** newq);

	/**
	 * Add detached query.
	 * Creates it if it does not exist already.
	 * Does not make super/sub references.
	 * Performs a cycle detection - for double check - and fails if there is
	 * 	one.
	 * Updates stat items in mesh_area structure.
	 * Pass if it is priming query or not.
	 * return:
	 * 	o if error (malloc) happened.
	 * 	o need to initialise the new state (module init; it is a new state).
	 * 	  so that the next run of the query with this module is successful.
	 * 	o no init needed, attachment successful.
	 * 	o added subquery, created if it did not exist already.
	 *
	 * @param qstate: the state to find mesh state, and that wants to receive
	 * 	the results from the new subquery.
	 * @param qinfo: what to query for (copied).
	 * @param qflags: what flags to use (RD / CD flag or not).
	 * @param prime: if it is a (stub) priming query.
	 * @param valrec: if it is a validation recursion query (lookup of key, DS).
	 * @param newq: If the new subquery needs initialisation, it is returned,
	 * 	otherwise NULL is returned.
	 * @param sub: The added mesh state, created if it did not exist already.
	 * @return: false on error, true if success (and init may be needed).
	 */
	int (*add_sub)(struct module_qstate* qstate, 
		struct query_info* qinfo, uint16_t qflags, int prime, 
		int valrec, struct module_qstate** newq,
		struct mesh_state** sub);

	/**
	 * Kill newly attached sub. If attach_sub returns newq for 
	 * initialisation, but that fails, then this routine will cleanup and
	 * delete the freshly created sub.
	 * @param newq: the new subquery that is no longer needed.
	 * 	It is removed.
	 */
	void (*kill_sub)(struct module_qstate* newq);

	/**
	 * Detect if adding a dependency for qstate on name,type,class will
	 * create a dependency cycle.
	 * @param qstate: given mesh querystate.
	 * @param qinfo: query info for dependency. 
	 * @param flags: query flags of dependency, RD/CD flags.
	 * @param prime: if dependency is a priming query or not.
	 * @param valrec: validation lookup recursion, does not need validation
	 * @return true if the name,type,class exists and the given 
	 * 	qstate mesh exists as a dependency of that name. Thus 
	 * 	if qstate becomes dependent on name,type,class then a 
	 * 	cycle is created.
	 */
	int (*detect_cycle)(struct module_qstate* qstate, 
		struct query_info* qinfo, uint16_t flags, int prime,
		int valrec);

	/** region for temporary usage. May be cleared after operate() call. */
	struct regional* scratch;
	/** buffer for temporary usage. May be cleared after operate() call. */
	struct sldns_buffer* scratch_buffer;
	/** internal data for daemon - worker thread. */
	struct worker* worker;
	/** the worker event base */
	struct comm_base* worker_base;
	/** the outside network */
	struct outside_network* outnet;
	/** mesh area with query state dependencies */
	struct mesh_area* mesh;
	/** allocation service */
	struct alloc_cache* alloc;
	/** random table to generate random numbers */
	struct ub_randstate* rnd;
	/** time in seconds, converted to integer */
	time_t* now;
	/** time in microseconds. Relatively recent. */
	struct timeval* now_tv;
	/** is validation required for messages, controls client-facing
	 * validation status (AD bits) and servfails */
	int need_to_validate;
	/** trusted key storage; these are the configured keys, if not NULL,
	 * otherwise configured by validator. These are the trust anchors,
	 * and are not primed and ready for validation, but on the bright
	 * side, they are read only memory, thus no locks and fast. */
	struct val_anchors* anchors;
	/** negative cache, configured by the validator. if not NULL,
	 * contains NSEC record lookup trees. */
	struct val_neg_cache* neg_cache;
	/** the 5011-probe timer (if any) */
	struct comm_timer* probe_timer;
	/** auth zones */
	struct auth_zones* auth_zones;
	/** Mapping of forwarding zones to targets.
	 * iterator forwarder information. per-thread, created by worker */
	struct iter_forwards* fwds;
	/** 
	 * iterator forwarder information. per-thread, created by worker.
	 * The hints -- these aren't stored in the cache because they don't 
	 * expire. The hints are always used to "prime" the cache. Note 
	 * that both root hints and stub zone "hints" are stored in this 
	 * data structure. 
	 */
	struct iter_hints* hints;
	/** module specific data. indexed by module id. */
	void* modinfo[MAX_MODULE];

	/* Shared linked list of inplace callback functions */
	struct inplace_cb* inplace_cb_lists[inplace_cb_types_total];

	/**
	 * Shared array of known edns options (size MAX_KNOWN_EDNS_OPTS).
	 * Filled by edns literate modules during init.
	 */
	struct edns_known_option* edns_known_options;
	/* Number of known edns options */
	size_t edns_known_options_num;

	/* Make every mesh state unique, do not aggregate mesh states. */
	int unique_mesh;
};

/**
 * External visible states of the module state machine 
 * Modules may also have an internal state.
 * Modules are supposed to run to completion or until blocked.
 */
enum module_ext_state {
	/** initial state - new query */
	module_state_initial = 0,
	/** waiting for reply to outgoing network query */
	module_wait_reply,
	/** module is waiting for another module */
	module_wait_module,
	/** module is waiting for another module; that other is restarted */
	module_restart_next,
	/** module is waiting for sub-query */
	module_wait_subquery,
	/** module could not finish the query */
	module_error,
	/** module is finished with query */
	module_finished
};

/**
 * Events that happen to modules, that start or wakeup modules.
 */
enum module_ev {
	/** new query */
	module_event_new = 0,
	/** query passed by other module */
	module_event_pass,
	/** reply inbound from server */
	module_event_reply,
	/** no reply, timeout or other error */
	module_event_noreply,
	/** reply is there, but capitalisation check failed */
	module_event_capsfail,
	/** next module is done, and its reply is awaiting you */
	module_event_moddone,
	/** error */
	module_event_error
};

/** 
 * Linked list of sockaddrs 
 * May be allocated such that only 'len' bytes of addr exist for the structure.
 */
struct sock_list {
	/** next in list */
	struct sock_list* next;
	/** length of addr */
	socklen_t len;
	/** sockaddr */
	struct sockaddr_storage addr;
};

struct respip_action_info;

/**
 * Module state, per query.
 */
struct module_qstate {
	/** which query is being answered: name, type, class */
	struct query_info qinfo;
	/** flags uint16 from query */
	uint16_t query_flags;
	/** if this is a (stub or root) priming query (with hints) */
	int is_priming;
	/** if this is a validation recursion query that does not get
	 * validation itself */
	int is_valrec;

	/** comm_reply contains server replies */
	struct comm_reply* reply;
	/** the reply message, with message for client and calling module */
	struct dns_msg* return_msg;
	/** the rcode, in case of error, instead of a reply message */
	int return_rcode;
	/** origin of the reply (can be NULL from cache, list for cnames) */
	struct sock_list* reply_origin;
	/** IP blacklist for queries */
	struct sock_list* blacklist;
	/** region for this query. Cleared when query process finishes. */
	struct regional* region;
	/** failure reason information if val-log-level is high */
	struct config_strlist* errinf;

	/** which module is executing */
	int curmod;
	/** module states */
	enum module_ext_state ext_state[MAX_MODULE];
	/** module specific data for query. indexed by module id. */
	void* minfo[MAX_MODULE];
	/** environment for this query */
	struct module_env* env;
	/** mesh related information for this query */
	struct mesh_state* mesh_info;
	/** how many seconds before expiry is this prefetched (0 if not) */
	time_t prefetch_leeway;

	/** incoming edns options from the front end */
	struct edns_option* edns_opts_front_in;
	/** outgoing edns options to the back end */
	struct edns_option* edns_opts_back_out;
	/** incoming edns options from the back end */
	struct edns_option* edns_opts_back_in;
	/** outgoing edns options to the front end */
	struct edns_option* edns_opts_front_out;
	/** whether modules should answer from the cache */
	int no_cache_lookup;
	/** whether modules should store answer in the cache */
	int no_cache_store;
	/** whether to refetch a fresh answer on finishing this state*/
	int need_refetch;
	/** whether the query (or a subquery) was ratelimited */
	int was_ratelimited;

	/**
	 * Attributes of clients that share the qstate that may affect IP-based
	 * actions.
	 */
	struct respip_client_info* client_info;

	/** Extended result of response-ip action processing, mainly
	 *  for logging purposes. */
	struct respip_action_info* respip_action_info;

	/** whether the reply should be dropped */
	int is_drop;
};

/** 
 * Module functionality block
 */
struct module_func_block {
	/** text string name of module */
	const char* name;

	/** 
	 * init the module. Called once for the global state.
	 * This is the place to apply settings from the config file.
	 * @param env: module environment.
	 * @param id: module id number.
	 * return: 0 on error
	 */
	int (*init)(struct module_env* env, int id);

	/**
	 * de-init, delete, the module. Called once for the global state.
	 * @param env: module environment.
	 * @param id: module id number.
	 */
	void (*deinit)(struct module_env* env, int id);

	/**
	 * accept a new query, or work further on existing query.
	 * Changes the qstate->ext_state to be correct on exit.
	 * @param ev: event that causes the module state machine to 
	 *	(re-)activate.
	 * @param qstate: the query state. 
	 *	Note that this method is not allowed to change the
	 *	query state 'identity', that is query info, qflags,
	 *	and priming status.
	 *	Attach a subquery to get results to a different query.
	 * @param id: module id number that operate() is called on. 
	 * @param outbound: if not NULL this event is due to the reply/timeout
	 *	or error on this outbound query.
	 * @return: if at exit the ext_state is:
	 *	o wait_module: next module is started. (with pass event).
	 *	o error or finished: previous module is resumed.
	 *	o otherwise it waits until that event happens (assumes
	 *	  the service routine to make subrequest or send message
	 *	  have been called.
	 */
	void (*operate)(struct module_qstate* qstate, enum module_ev event, 
		int id, struct outbound_entry* outbound);

	/**
	 * inform super querystate about the results from this subquerystate.
	 * Is called when the querystate is finished.  The method invoked is
	 * the one from the current module active in the super querystate.
	 * @param qstate: the query state that is finished.
	 *	Examine return_rcode and return_reply in the qstate.
	 * @param id: module id for this module.
	 *	This coincides with the current module for the super qstate.
	 * @param super: the super querystate that needs to be informed.
	 */
	void (*inform_super)(struct module_qstate* qstate, int id,
		struct module_qstate* super);

	/**
	 * clear module specific data
	 */
	void (*clear)(struct module_qstate* qstate, int id);

	/**
	 * How much memory is the module specific data using. 
	 * @param env: module environment.
	 * @param id: the module id.
	 * @return the number of bytes that are alloced.
	 */
	size_t (*get_mem)(struct module_env* env, int id);
};

/** 
 * Debug utility: module external qstate to string 
 * @param s: the state value.
 * @return descriptive string.
 */
const char* strextstate(enum module_ext_state s);

/** 
 * Debug utility: module event to string 
 * @param e: the module event value.
 * @return descriptive string.
 */
const char* strmodulevent(enum module_ev e);

/**
 * Initialize the edns known options by allocating the required space.
 * @param env: the module environment.
 * @return false on failure (no memory).
 */
int edns_known_options_init(struct module_env* env);

/**
 * Free the allocated space for the known edns options.
 * @param env: the module environment.
 */
void edns_known_options_delete(struct module_env* env);

/**
 * Register a known edns option. Overwrite the flags if it is already
 * registered. Used before creating workers to register known edns options.
 * @param opt_code: the edns option code.
 * @param bypass_cache_stage: whether the option interacts with the cache.
 * @param no_aggregation: whether the option implies more specific
 *	aggregation.
 * @param env: the module environment.
 * @return true on success, false on failure (registering more options than
 *	allowed or trying to register after the environment is copied to the
 *	threads.)
 */
int edns_register_option(uint16_t opt_code, int bypass_cache_stage,
	int no_aggregation, struct module_env* env);

/**
 * Register an inplace callback function.
 * @param cb: pointer to the callback function.
 * @param type: inplace callback type.
 * @param cbarg: argument for the callback function, or NULL.
 * @param env: the module environment.
 * @param id: module id.
 * @return true on success, false on failure (out of memory or trying to
 *	register after the environment is copied to the threads.)
 */
int
inplace_cb_register(void* cb, enum inplace_cb_list_type type, void* cbarg,
	struct module_env* env, int id);

/**
 * Delete callback for specified type and module id.
 * @param env: the module environment.
 * @param type: inplace callback type.
 * @param id: module id.
 */
void
inplace_cb_delete(struct module_env* env, enum inplace_cb_list_type type,
	int id);

/**
 * Delete all the inplace callback linked lists.
 * @param env: the module environment.
 */
void inplace_cb_lists_delete(struct module_env* env);

/**
 * Check if an edns option is known.
 * @param opt_code: the edns option code.
 * @param env: the module environment.
 * @return pointer to registered option if the edns option is known,
 *	NULL otherwise.
 */
struct edns_known_option* edns_option_is_known(uint16_t opt_code,
	struct module_env* env);

/**
 * Check if an edns option needs to bypass the reply from cache stage.
 * @param list: the edns options.
 * @param env: the module environment.
 * @return true if an edns option needs to bypass the cache stage,
 *	false otherwise.
 */
int edns_bypass_cache_stage(struct edns_option* list,
	struct module_env* env);

/**
 * Check if an unique mesh state is required. Might be triggered by EDNS option
 * or set for the complete env.
 * @param list: the edns options.
 * @param env: the module environment.
 * @return true if an edns option needs a unique mesh state,
 *	false otherwise.
 */
int unique_mesh_state(struct edns_option* list, struct module_env* env);

/**
 * Log the known edns options.
 * @param level: the desired verbosity level.
 * @param env: the module environment.
 */
void log_edns_known_options(enum verbosity_value level,
	struct module_env* env);

/**
 * Copy state that may have happened in the subquery and is always relevant to
 * the super.
 * @param qstate: query state that finished.
 * @param id: module id.
 * @param super: the qstate to inform.
 */
void copy_state_to_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);

#endif /* UTIL_MODULE_H */
