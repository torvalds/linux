/*
 * services/mesh.h - deal with mesh of query states and handle events for that.
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
 * This file contains functions to assist in dealing with a mesh of
 * query states. This mesh is supposed to be thread-specific.
 * It consists of query states (per qname, qtype, qclass) and connections
 * between query states and the super and subquery states, and replies to
 * send back to clients.
 */

#ifndef SERVICES_MESH_H
#define SERVICES_MESH_H

#include "util/rbtree.h"
#include "util/netevent.h"
#include "util/data/msgparse.h"
#include "util/module.h"
#include "services/modstack.h"
struct sldns_buffer;
struct mesh_state;
struct mesh_reply;
struct mesh_cb;
struct query_info;
struct reply_info;
struct outbound_entry;
struct timehist;
struct respip_client_info;

/**
 * Maximum number of mesh state activations. Any more is likely an
 * infinite loop in the module. It is then terminated.
 */
#define MESH_MAX_ACTIVATION 3000

/**
 * Max number of references-to-references-to-references.. search size.
 * Any more is treated like 'too large', and the creation of a new
 * dependency is failed (so that no loops can be created).
 */
#define MESH_MAX_SUBSUB 1024

/** 
 * Mesh of query states
 */
struct mesh_area {
	/** active module stack */
	struct module_stack mods;
	/** environment for new states */
	struct module_env* env;

	/** set of runnable queries (mesh_state.run_node) */
	rbtree_type run;
	/** rbtree of all current queries (mesh_state.node)*/
	rbtree_type all;

	/** count of the total number of mesh_reply entries */
	size_t num_reply_addrs;
	/** count of the number of mesh_states that have mesh_replies 
	 * Because a state can send results to multiple reply addresses,
	 * this number must be equal or lower than num_reply_addrs. */
	size_t num_reply_states;
	/** number of mesh_states that have no mesh_replies, and also
	 * an empty set of super-states, thus are 'toplevel' or detached
	 * internal opportunistic queries */
	size_t num_detached_states;
	/** number of reply states in the forever list */
	size_t num_forever_states;

	/** max total number of reply states to have */
	size_t max_reply_states;
	/** max forever number of reply states to have */
	size_t max_forever_states;

	/** stats, cumulative number of reply states jostled out */
	size_t stats_jostled;
	/** stats, cumulative number of incoming client msgs dropped */
	size_t stats_dropped;
	/** number of replies sent */
	size_t replies_sent;
	/** sum of waiting times for the replies */
	struct timeval replies_sum_wait;
	/** histogram of time values */
	struct timehist* histogram;
	/** (extended stats) secure replies */
	size_t ans_secure;
	/** (extended stats) bogus replies */
	size_t ans_bogus;
	/** (extended stats) rcodes in replies */
	size_t ans_rcode[16];
	/** (extended stats) rcode nodata in replies */
	size_t ans_nodata;

	/** backup of query if other operations recurse and need the
	 * network buffers */
	struct sldns_buffer* qbuf_bak;

	/** double linked list of the run-to-completion query states.
	 * These are query states with a reply */
	struct mesh_state* forever_first;
	/** last entry in run forever list */
	struct mesh_state* forever_last;

	/** double linked list of the query states that can be jostled out
	 * by new queries if too old.  These are query states with a reply */
	struct mesh_state* jostle_first;
	/** last entry in jostle list - this is the entry that is newest */
	struct mesh_state* jostle_last;
	/** timeout for jostling. if age is lower, it does not get jostled. */
	struct timeval jostle_max;
};

/**
 * A mesh query state
 * Unique per qname, qtype, qclass (from the qstate).
 * And RD / CD flag; in case a client turns it off.
 * And priming queries are different from ordinary queries (because of hints).
 *
 * The entire structure is allocated in a region, this region is the qstate
 * region. All parts (rbtree nodes etc) are also allocated in the region.
 */
struct mesh_state {
	/** node in mesh_area all tree, key is this struct. Must be first. */
	rbnode_type node;
	/** node in mesh_area runnable tree, key is this struct */
	rbnode_type run_node;
	/** the query state. Note that the qinfo and query_flags 
	 * may not change. */
	struct module_qstate s;
	/** the list of replies to clients for the results */
	struct mesh_reply* reply_list;
	/** the list of callbacks for the results */
	struct mesh_cb* cb_list;
	/** set of superstates (that want this state's result) 
	 * contains struct mesh_state_ref* */
	rbtree_type super_set;
	/** set of substates (that this state needs to continue)
	 * contains struct mesh_state_ref* */
	rbtree_type sub_set;
	/** number of activations for the mesh state */
	size_t num_activated;

	/** previous in linked list for reply states */
	struct mesh_state* prev;
	/** next in linked list for reply states */
	struct mesh_state* next;
	/** if this state is in the forever list, jostle list, or neither */
	enum mesh_list_select { mesh_no_list, mesh_forever_list, 
		mesh_jostle_list } list_select;
	/** pointer to this state for uniqueness or NULL */
	struct mesh_state* unique;

	/** true if replies have been sent out (at end for alignment) */
	uint8_t replies_sent;
};

/**
 * Rbtree reference to a mesh_state.
 * Used in super_set and sub_set. 
 */
struct mesh_state_ref {
	/** node in rbtree for set, key is this structure */
	rbnode_type node;
	/** the mesh state */
	struct mesh_state* s;
};

/**
 * Reply to a client
 */
struct mesh_reply {
	/** next in reply list */
	struct mesh_reply* next;
	/** the query reply destination, packet buffer and where to send. */
	struct comm_reply query_reply;
	/** edns data from query */
	struct edns_data edns;
	/** the time when request was entered */
	struct timeval start_time;
	/** id of query, in network byteorder. */
	uint16_t qid;
	/** flags of query, for reply flags */
	uint16_t qflags;
	/** qname from this query. len same as mesh qinfo. */
	uint8_t* qname;
	/** same as that in query_info. */
	struct local_rrset* local_alias;
};

/** 
 * Mesh result callback func.
 * called as func(cb_arg, rcode, buffer_with_reply, security, why_bogus,
 *		was_ratelimited);
 */
typedef void (*mesh_cb_func_type)(void* cb_arg, int rcode, struct sldns_buffer*,
	enum sec_status, char* why_bogus, int was_ratelimited);

/**
 * Callback to result routine
 */
struct mesh_cb {
	/** next in list */
	struct mesh_cb* next;
	/** edns data from query */
	struct edns_data edns;
	/** id of query, in network byteorder. */
	uint16_t qid;
	/** flags of query, for reply flags */
	uint16_t qflags;
	/** buffer for reply */
	struct sldns_buffer* buf;
	/** callback routine for results. if rcode != 0 buf has message.
	 * called as cb(cb_arg, rcode, buf, sec_state, why_bogus, was_ratelimited);
	 */
	mesh_cb_func_type cb;
	/** user arg for callback */
	void* cb_arg;
};

/* ------------------- Functions for worker -------------------- */

/**
 * Allocate mesh, to empty.
 * @param stack: module stack to activate, copied (as readonly reference).
 * @param env: environment for new queries.
 * @return mesh: the new mesh or NULL on error.
 */
struct mesh_area* mesh_create(struct module_stack* stack, 
	struct module_env* env);

/**
 * Delete mesh, and all query states and replies in it.
 * @param mesh: the mesh to delete.
 */
void mesh_delete(struct mesh_area* mesh);

/**
 * New query incoming from clients. Create new query state if needed, and
 * add mesh_reply to it. Returns error to client on malloc failures.
 * Will run the mesh area queries to process if a new query state is created.
 *
 * @param mesh: the mesh.
 * @param qinfo: query from client.
 * @param cinfo: additional information associated with the query client.
 * 	'cinfo' itself is ephemeral but data pointed to by its members
 *      can be assumed to be valid and unchanged until the query processing is
 *      completed.
 * @param qflags: flags from client query.
 * @param edns: edns data from client query.
 * @param rep: where to reply to.
 * @param qid: query id to reply with.
 */
void mesh_new_client(struct mesh_area* mesh, struct query_info* qinfo,
	struct respip_client_info* cinfo, uint16_t qflags,
	struct edns_data* edns, struct comm_reply* rep, uint16_t qid);

/**
 * New query with callback. Create new query state if needed, and
 * add mesh_cb to it. 
 * Will run the mesh area queries to process if a new query state is created.
 *
 * @param mesh: the mesh.
 * @param qinfo: query from client.
 * @param qflags: flags from client query.
 * @param edns: edns data from client query.
 * @param buf: buffer for reply contents.
 * @param qid: query id to reply with.
 * @param cb: callback function.
 * @param cb_arg: callback user arg.
 * @return 0 on error.
 */
int mesh_new_callback(struct mesh_area* mesh, struct query_info* qinfo,
	uint16_t qflags, struct edns_data* edns, struct sldns_buffer* buf, 
	uint16_t qid, mesh_cb_func_type cb, void* cb_arg);

/**
 * New prefetch message. Create new query state if needed.
 * Will run the mesh area queries to process if a new query state is created.
 *
 * @param mesh: the mesh.
 * @param qinfo: query from client.
 * @param qflags: flags from client query.
 * @param leeway: TTL leeway what to expire earlier for this update.
 */
void mesh_new_prefetch(struct mesh_area* mesh, struct query_info* qinfo,
	uint16_t qflags, time_t leeway);

/**
 * Handle new event from the wire. A serviced query has returned.
 * The query state will be made runnable, and the mesh_area will process
 * query states until processing is complete.
 *
 * @param mesh: the query mesh.
 * @param e: outbound entry, with query state to run and reply pointer.
 * @param reply: the comm point reply info.
 * @param what: NETEVENT_* error code (if not 0, what is wrong, TIMEOUT).
 */
void mesh_report_reply(struct mesh_area* mesh, struct outbound_entry* e,
	struct comm_reply* reply, int what);

/* ------------------- Functions for module environment --------------- */

/**
 * Detach-subqueries.
 * Remove all sub-query references from this query state.
 * Keeps super-references of those sub-queries correct.
 * Updates stat items in mesh_area structure.
 * @param qstate: used to find mesh state.
 */
void mesh_detach_subs(struct module_qstate* qstate);

/**
 * Attach subquery.
 * Creates it if it does not exist already.
 * Keeps sub and super references correct.
 * Performs a cycle detection - for double check - and fails if there is one.
 * Also fails if the sub-sub-references become too large.
 * Updates stat items in mesh_area structure.
 * Pass if it is priming query or not.
 * return:
 * 	o if error (malloc) happened.
 * 	o need to initialise the new state (module init; it is a new state).
 * 	  so that the next run of the query with this module is successful.
 * 	o no init needed, attachment successful.
 *
 * @param qstate: the state to find mesh state, and that wants to receive
 * 	the results from the new subquery.
 * @param qinfo: what to query for (copied).
 * @param qflags: what flags to use (RD / CD flag or not).
 * @param prime: if it is a (stub) priming query.
 * @param valrec: if it is a validation recursion query (lookup of key, DS).
 * @param newq: If the new subquery needs initialisation, it is returned,
 * 	otherwise NULL is returned.
 * @return: false on error, true if success (and init may be needed).
 */
int mesh_attach_sub(struct module_qstate* qstate, struct query_info* qinfo,
	uint16_t qflags, int prime, int valrec, struct module_qstate** newq);

/**
 * Add detached query.
 * Creates it if it does not exist already.
 * Does not make super/sub references.
 * Performs a cycle detection - for double check - and fails if there is one.
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
int mesh_add_sub(struct module_qstate* qstate, struct query_info* qinfo,
        uint16_t qflags, int prime, int valrec, struct module_qstate** newq,
	struct mesh_state** sub);

/**
 * Query state is done, send messages to reply entries.
 * Encode messages using reply entry values and the querystate (with original
 * qinfo), using given reply_info.
 * Pass errcode != 0 if an error reply is needed.
 * If no reply entries, nothing is done.
 * Must be called before a module can module_finished or return module_error.
 * The module must handle the super query states itself as well.
 *
 * @param mstate: mesh state that is done. return_rcode and return_msg
 * 	are used for replies.
 * 	return_rcode: if not 0 (NOERROR) an error is sent back (and 
 * 		return_msg is ignored).
 * 	return_msg: reply to encode and send back to clients.
 */
void mesh_query_done(struct mesh_state* mstate);

/**
 * Call inform_super for the super query states that are interested in the 
 * results from this query state. These can then be changed for error 
 * or results.
 * Called when a module is module_finished or returns module_error.
 * The super query states become runnable with event module_event_pass,
 * it calls the current module for the super with the inform_super event.
 *
 * @param mesh: mesh area to add newly runnable modules to.
 * @param mstate: the state that has results, used to find mesh state.
 */
void mesh_walk_supers(struct mesh_area* mesh, struct mesh_state* mstate);

/**
 * Delete mesh state, cleanup and also rbtrees and so on.
 * Will detach from all super/subnodes.
 * @param qstate: to remove.
 */
void mesh_state_delete(struct module_qstate* qstate);

/* ------------------- Functions for mesh -------------------- */

/**
 * Create and initialize a new mesh state and its query state
 * Does not put the mesh state into rbtrees and so on.
 * @param env: module environment to set.
 * @param qinfo: query info that the mesh is for.
 * @param cinfo: control info for the query client (can be NULL).
 * @param qflags: flags for query (RD / CD flag).
 * @param prime: if true, it is a priming query, set is_priming on mesh state.
 * @param valrec: if true, it is a validation recursion query, and sets
 * 	is_valrec on the mesh state.
 * @return: new mesh state or NULL on allocation error.
 */
struct mesh_state* mesh_state_create(struct module_env* env,
	struct query_info* qinfo, struct respip_client_info* cinfo,
	uint16_t qflags, int prime, int valrec);

/**
 * Check if the mesh state is unique.
 * A unique mesh state uses it's unique member to point to itself, else NULL.
 * @param mstate: mesh state to check.
 * @return true if the mesh state is unique, false otherwise.
 */
int mesh_state_is_unique(struct mesh_state* mstate);

/**
 * Make a mesh state unique.
 * A unique mesh state uses it's unique member to point to itself.
 * @param mstate: mesh state to check.
 */
void mesh_state_make_unique(struct mesh_state* mstate);

/**
 * Cleanup a mesh state and its query state. Does not do rbtree or 
 * reference cleanup.
 * @param mstate: mesh state to cleanup. Its pointer may no longer be used
 * 	afterwards. Cleanup rbtrees before calling this function.
 */
void mesh_state_cleanup(struct mesh_state* mstate);

/**
 * Delete all mesh states from the mesh.
 * @param mesh: the mesh area to clear
 */
void mesh_delete_all(struct mesh_area* mesh);

/**
 * Find a mesh state in the mesh area. Pass relevant flags.
 *
 * @param mesh: the mesh area to look in.
 * @param cinfo: if non-NULL client specific info that may affect IP-based
 * 	actions that apply to the query result.
 * @param qinfo: what query
 * @param qflags: if RD / CD bit is set or not.
 * @param prime: if it is a priming query.
 * @param valrec: if it is a validation-recursion query.
 * @return: mesh state or NULL if not found.
 */
struct mesh_state* mesh_area_find(struct mesh_area* mesh,
	struct respip_client_info* cinfo, struct query_info* qinfo,
	uint16_t qflags, int prime, int valrec);

/**
 * Setup attachment super/sub relation between super and sub mesh state.
 * The relation must not be present when calling the function.
 * Does not update stat items in mesh_area.
 * @param super: super state.
 * @param sub: sub state.
 * @return: 0 on alloc error.
 */
int mesh_state_attachment(struct mesh_state* super, struct mesh_state* sub);

/**
 * Create new reply structure and attach it to a mesh state.
 * Does not update stat items in mesh area.
 * @param s: the mesh state.
 * @param edns: edns data for reply (bufsize).
 * @param rep: comm point reply info.
 * @param qid: ID of reply.
 * @param qflags: original query flags.
 * @param qinfo: original query info.
 * @return: 0 on alloc error.
 */
int mesh_state_add_reply(struct mesh_state* s, struct edns_data* edns,
	struct comm_reply* rep, uint16_t qid, uint16_t qflags,
	const struct query_info* qinfo);

/**
 * Create new callback structure and attach it to a mesh state.
 * Does not update stat items in mesh area.
 * @param s: the mesh state.
 * @param edns: edns data for reply (bufsize).
 * @param buf: buffer for reply
 * @param cb: callback to call with results.
 * @param cb_arg: callback user arg.
 * @param qid: ID of reply.
 * @param qflags: original query flags.
 * @return: 0 on alloc error.
 */
int mesh_state_add_cb(struct mesh_state* s, struct edns_data* edns,
        struct sldns_buffer* buf, mesh_cb_func_type cb, void* cb_arg,
	uint16_t qid, uint16_t qflags);

/**
 * Run the mesh. Run all runnable mesh states. Which can create new
 * runnable mesh states. Until completion. Automatically called by
 * mesh_report_reply and mesh_new_client as needed.
 * @param mesh: mesh area.
 * @param mstate: first mesh state to run.
 * @param ev: event the mstate. Others get event_pass.
 * @param e: if a reply, its outbound entry.
 */
void mesh_run(struct mesh_area* mesh, struct mesh_state* mstate, 
	enum module_ev ev, struct outbound_entry* e);

/**
 * Print some stats about the mesh to the log.
 * @param mesh: the mesh to print it for.
 * @param str: descriptive string to go with it.
 */
void mesh_stats(struct mesh_area* mesh, const char* str);

/**
 * Clear the stats that the mesh keeps (number of queries serviced)
 * @param mesh: the mesh
 */
void mesh_stats_clear(struct mesh_area* mesh);

/**
 * Print all the states in the mesh to the log.
 * @param mesh: the mesh to print all states of.
 */
void mesh_log_list(struct mesh_area* mesh);

/**
 * Calculate memory size in use by mesh and all queries inside it.
 * @param mesh: the mesh to examine.
 * @return size in bytes.
 */
size_t mesh_get_mem(struct mesh_area* mesh);

/**
 * Find cycle; see if the given mesh is in the targets sub, or sub-sub, ...
 * trees.
 * If the sub-sub structure is too large, it returns 'a cycle'=2.
 * @param qstate: given mesh querystate.
 * @param qinfo: query info for dependency.
 * @param flags: query flags of dependency.
 * @param prime: if dependency is a priming query or not.
 * @param valrec: if it is a validation recursion query (lookup of key, DS).
 * @return true if the name,type,class exists and the given qstate mesh exists
 * 	as a dependency of that name. Thus if qstate becomes dependent on
 * 	name,type,class then a cycle is created, this is return value 1.
 * 	Too large to search is value 2 (also true).
 */
int mesh_detect_cycle(struct module_qstate* qstate, struct query_info* qinfo,
	uint16_t flags, int prime, int valrec);

/** compare two mesh_states */
int mesh_state_compare(const void* ap, const void* bp);

/** compare two mesh references */
int mesh_state_ref_compare(const void* ap, const void* bp);

/**
 * Make space for another recursion state for a reply in the mesh
 * @param mesh: mesh area
 * @param qbuf: query buffer to save if recursion is invoked to make space.
 *    This buffer is necessary, because the following sequence in calls
 *    can result in an overwrite of the incoming query:
 *    delete_other_mesh_query - iter_clean - serviced_delete - waiting
 *    udp query is sent - on error callback - callback sends SERVFAIL reply
 *    over the same network channel, and shared UDP buffer is overwritten.
 *    You can pass NULL if there is no buffer that must be backed up.
 * @return false if no space is available.
 */
int mesh_make_new_space(struct mesh_area* mesh, struct sldns_buffer* qbuf);

/**
 * Insert mesh state into a double linked list.  Inserted at end.
 * @param m: mesh state.
 * @param fp: pointer to the first-elem-pointer of the list.
 * @param lp: pointer to the last-elem-pointer of the list.
 */
void mesh_list_insert(struct mesh_state* m, struct mesh_state** fp,
	struct mesh_state** lp);

/**
 * Remove mesh state from a double linked list.  Remove from any position.
 * @param m: mesh state.
 * @param fp: pointer to the first-elem-pointer of the list.
 * @param lp: pointer to the last-elem-pointer of the list.
 */
void mesh_list_remove(struct mesh_state* m, struct mesh_state** fp,
	struct mesh_state** lp);

#endif /* SERVICES_MESH_H */
