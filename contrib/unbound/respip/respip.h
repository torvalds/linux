/*
 * respip/respip.h - IP-based response modification module
 */

/**
 * \file
 *
 * This file contains a module that selectively modifies query responses
 * based on their AAAA/A IP addresses.
 */

#ifndef RESPIP_RESPIP_H
#define RESPIP_RESPIP_H

#include "util/module.h"
#include "services/localzone.h"

/**
 * Set of response IP addresses with associated actions and tags. 
 * Forward declaration only here.  Actual definition is hidden within the
 * module.
 */
struct respip_set;

/**
 * Forward declaration for the structure that represents a node in the
 * respip_set address tree
 */
struct resp_addr;

/**
 * Forward declaration for the structure that represents a tree of view data.
 */
struct views;

struct respip_addr_info;

/**
 * Client-specific attributes that can affect IP-based actions.
 * This is essentially a subset of acl_addr (except for respip_set) but
 * defined as a separate structure to avoid dependency on the daemon-specific
 * structure.
 * respip_set is supposed to refer to the response-ip set for the global view.
 */
struct respip_client_info {
	uint8_t* taglist;
	size_t taglen;
	uint8_t* tag_actions;
	size_t tag_actions_size;
	struct config_strlist** tag_datas;
	size_t tag_datas_size;
	struct view* view;
	struct respip_set* respip_set;
};

/**
 * Data items representing the result of response-ip processing.
 * Note: this structure currently only define a few members, but exists
 * as a separate struct mainly for the convenience of custom extensions.
 */
struct respip_action_info {
	enum respip_action action;
	struct respip_addr_info* addrinfo; /* set only for inform variants */
};

/**
  * Forward declaration for the structure that represents a node in the
  * respip_set address tree
  */
struct resp_addr;

/**
 * Create response IP set.
 * @return new struct or NULL on error.
 */
struct respip_set* respip_set_create(void);

/**
 * Delete response IP set.
 * @param set: to delete.
 */
void respip_set_delete(struct respip_set* set);

/**
 * Apply response-ip config settings to the global (default) view.
 * It assumes exclusive access to set (no internal locks).
 * @param set: processed global respip config data
 * @param cfg: config data.
 * @return 1 on success, 0 on error.
 */
int respip_global_apply_cfg(struct respip_set* set, struct config_file* cfg);

/**
 * Apply response-ip config settings in named views.
 * @param vs: view structures with processed config data
 * @param cfg: config data.
 * @param have_view_respip_cfg: set to true if any named view has respip
 * 	configuration; otherwise set to false
 * @return 1 on success, 0 on error.
 */
int respip_views_apply_cfg(struct views* vs, struct config_file* cfg,
	int* have_view_respip_cfg);

/**
 * Merge two replies to build a complete CNAME chain.
 * It appends the content of 'tgt_rep' to 'base_rep', assuming (but not
 * checking) the former ends with a CNAME and the latter resolves its target.
 * A merged new reply will be built using 'region' and *new_repp will point
 * to the new one on success.
 * If the target reply would also be subject to a response-ip action for
 * 'cinfo', this function uses 'base_rep' as the merged reply, ignoring
 * 'tgt_rep'.  This is for avoiding cases like a CNAME loop or failure of
 * applying an action to an address.
 * RRSIGs in 'tgt_rep' will be excluded in the merged reply, as the resulting
 * reply is assumed to be faked due to a response-ip action and can't be
 * considered secure in terms of DNSSEC.
 * The caller must ensure that neither 'base_rep' nor 'tgt_rep' can be modified
 * until this function returns. 
 * @param base_rep: the reply info containing an incomplete CNAME.
 * @param qinfo: query info corresponding to 'base_rep'.
 * @param tgt_rep: the reply info that completes the CNAME chain.
 * @param cinfo: client info corresponding to 'base_rep'.
 * @param must_validate: whether 'tgt_rep' must be DNSSEC-validated.
 * @param new_repp: pointer placeholder for the merged reply.  will be intact
 *   on error.
 * @param region: allocator to build *new_repp.
 * @return 1 on success, 0 on error.
 */
int respip_merge_cname(struct reply_info* base_rep,
	const struct query_info* qinfo, const struct reply_info* tgt_rep,
	const struct respip_client_info* cinfo, int must_validate,
	struct reply_info** new_repp, struct regional* region);

/**
 * See if any IP-based action should apply to any IP address of AAAA/A answer
 * record in the reply.  If so, apply the action.  In some cases it rewrites
 * the reply rrsets, in which case *new_repp will point to the updated reply
 * info.  Depending on the action, some of the rrsets in 'rep' will be
 * shallow-copied into '*new_repp'; the caller must ensure that the rrsets
 * in 'rep' are valid throughout the lifetime of *new_repp, and it must
 * provide appropriate mutex if the rrsets can be shared by multiple threads.
 * @param qinfo: query info corresponding to the reply.
 * @param cinfo: client-specific info to identify the best matching action.
 *   can be NULL.
 * @param rep: original reply info.  must not be NULL.
 * @param new_repp: can be set to the rewritten reply info (intact on failure).
 * @param actinfo: result of response-ip processing
 * @param alias_rrset: must not be NULL.
 * @param search_only: if true, only check if an action would apply.  actionp
 *   will be set (or intact) accordingly but the modified reply won't be built.
 * @param region: allocator to build *new_repp.
 * @return 1 on success, 0 on error.
 */
int respip_rewrite_reply(const struct query_info* qinfo,
	const struct respip_client_info* cinfo,
	const struct reply_info *rep, struct reply_info** new_repp,
	struct respip_action_info* actinfo,
	struct ub_packed_rrset_key** alias_rrset,
	int search_only, struct regional* region);

/**
 * Get the response-ip function block.
 * @return: function block with function pointers to response-ip methods.
 */
struct module_func_block* respip_get_funcblock(void);

/** response-ip init */
int respip_init(struct module_env* env, int id);

/** response-ip deinit */
void respip_deinit(struct module_env* env, int id);

/** response-ip operate on a query */
void respip_operate(struct module_qstate* qstate, enum module_ev event, int id,
	struct outbound_entry* outbound);

/** inform response-ip super */
void respip_inform_super(struct module_qstate* qstate, int id,
	struct module_qstate* super);

/** response-ip cleanup query state */
void respip_clear(struct module_qstate* qstate, int id);

/**
 * returns address of the IP address tree of the specified respip set;
 * returns NULL for NULL input; exists for test purposes only
 */
struct rbtree_type* respip_set_get_tree(struct respip_set* set);

/**
 * returns respip action for the specified node in the respip address
 * returns respip_none for NULL input; exists for test purposes only
 */
enum respip_action resp_addr_get_action(const struct resp_addr* addr);

/**
 * returns rrset portion of the specified node in the respip address
 * tree; returns NULL for NULL input; exists for test purposes only
 */
struct ub_packed_rrset_key* resp_addr_get_rrset(struct resp_addr* addr);

/** response-ip alloc size routine */
size_t respip_get_mem(struct module_env* env, int id);

/**
 * respip set emptiness test
 * @param set respip set to test
 * @return 0 if the specified set exists (non-NULL) and is non-empty;
 *	otherwise returns 1
 */
int respip_set_is_empty(const struct respip_set* set);

/**
 * print log information for a query subject to an inform or inform-deny
 * response-ip action.
 * @param respip_addr: response-ip information that causes the action
 * @param qname: query name in the context, will be ignored if local_alias is
 *   non-NULL.
 * @param qtype: query type, in host byte order.
 * @param qclass: query class, in host byte order.
 * @param local_alias: set to a local alias if the query matches an alias in
 *  a local zone.  In this case its owner name will be considered the actual
 *  query name.
 * @param repinfo: reply info containing the client's source address and port.
 */
void respip_inform_print(struct respip_addr_info* respip_addr, uint8_t* qname,
	uint16_t qtype, uint16_t qclass, struct local_rrset* local_alias,
	struct comm_reply* repinfo);

#endif	/* RESPIP_RESPIP_H */
