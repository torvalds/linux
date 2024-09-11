/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2024 NVIDIA Corporation & Affiliates */

#ifndef MLX5HWS_H_
#define MLX5HWS_H_

struct mlx5hws_context;
struct mlx5hws_table;
struct mlx5hws_matcher;
struct mlx5hws_rule;

enum mlx5hws_table_type {
	MLX5HWS_TABLE_TYPE_FDB,
	MLX5HWS_TABLE_TYPE_MAX,
};

enum mlx5hws_matcher_resource_mode {
	/* Allocate resources based on number of rules with minimal failure probability */
	MLX5HWS_MATCHER_RESOURCE_MODE_RULE,
	/* Allocate fixed size hash table based on given column and rows */
	MLX5HWS_MATCHER_RESOURCE_MODE_HTABLE,
};

enum mlx5hws_action_type {
	MLX5HWS_ACTION_TYP_LAST,
	MLX5HWS_ACTION_TYP_REFORMAT_TNL_L2_TO_L2,
	MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L2,
	MLX5HWS_ACTION_TYP_REFORMAT_TNL_L3_TO_L2,
	MLX5HWS_ACTION_TYP_REFORMAT_L2_TO_TNL_L3,
	MLX5HWS_ACTION_TYP_DROP,
	MLX5HWS_ACTION_TYP_MISS,
	MLX5HWS_ACTION_TYP_TBL,
	MLX5HWS_ACTION_TYP_CTR,
	MLX5HWS_ACTION_TYP_TAG,
	MLX5HWS_ACTION_TYP_MODIFY_HDR,
	MLX5HWS_ACTION_TYP_VPORT,
	MLX5HWS_ACTION_TYP_POP_VLAN,
	MLX5HWS_ACTION_TYP_PUSH_VLAN,
	MLX5HWS_ACTION_TYP_ASO_METER,
	MLX5HWS_ACTION_TYP_INSERT_HEADER,
	MLX5HWS_ACTION_TYP_REMOVE_HEADER,
	MLX5HWS_ACTION_TYP_RANGE,
	MLX5HWS_ACTION_TYP_SAMPLER,
	MLX5HWS_ACTION_TYP_DEST_ARRAY,
	MLX5HWS_ACTION_TYP_MAX,
};

enum mlx5hws_action_flags {
	MLX5HWS_ACTION_FLAG_HWS_FDB = 1 << 0,
	/* Shared action can be used over a few threads, since the
	 * data is written only once at the creation of the action.
	 */
	MLX5HWS_ACTION_FLAG_SHARED = 1 << 1,
};

enum mlx5hws_action_aso_meter_color {
	MLX5HWS_ACTION_ASO_METER_COLOR_RED = 0x0,
	MLX5HWS_ACTION_ASO_METER_COLOR_YELLOW = 0x1,
	MLX5HWS_ACTION_ASO_METER_COLOR_GREEN = 0x2,
	MLX5HWS_ACTION_ASO_METER_COLOR_UNDEFINED = 0x3,
};

enum mlx5hws_send_queue_actions {
	/* Start executing all pending queued rules */
	MLX5HWS_SEND_QUEUE_ACTION_DRAIN_ASYNC = 1 << 0,
	/* Start executing all pending queued rules wait till completion */
	MLX5HWS_SEND_QUEUE_ACTION_DRAIN_SYNC = 1 << 1,
};

struct mlx5hws_context_attr {
	u16 queues;
	u16 queue_size;
	bool bwc; /* add support for backward compatible API*/
};

struct mlx5hws_table_attr {
	enum mlx5hws_table_type type;
	u32 level;
};

enum mlx5hws_matcher_flow_src {
	MLX5HWS_MATCHER_FLOW_SRC_ANY = 0x0,
	MLX5HWS_MATCHER_FLOW_SRC_WIRE = 0x1,
	MLX5HWS_MATCHER_FLOW_SRC_VPORT = 0x2,
};

enum mlx5hws_matcher_insert_mode {
	MLX5HWS_MATCHER_INSERT_BY_HASH = 0x0,
	MLX5HWS_MATCHER_INSERT_BY_INDEX = 0x1,
};

enum mlx5hws_matcher_distribute_mode {
	MLX5HWS_MATCHER_DISTRIBUTE_BY_HASH = 0x0,
	MLX5HWS_MATCHER_DISTRIBUTE_BY_LINEAR = 0x1,
};

struct mlx5hws_matcher_attr {
	/* Processing priority inside table */
	u32 priority;
	/* Provide all rules with unique rule_idx in num_log range to reduce locking */
	bool optimize_using_rule_idx;
	/* Resource mode and corresponding size */
	enum mlx5hws_matcher_resource_mode mode;
	/* Optimize insertion in case packet origin is the same for all rules */
	enum mlx5hws_matcher_flow_src optimize_flow_src;
	/* Define the insertion and distribution modes for this matcher */
	enum mlx5hws_matcher_insert_mode insert_mode;
	enum mlx5hws_matcher_distribute_mode distribute_mode;
	/* Define whether the created matcher supports resizing into a bigger matcher */
	bool resizable;
	union {
		struct {
			u8 sz_row_log;
			u8 sz_col_log;
		} table;

		struct {
			u8 num_log;
		} rule;
	};
	/* Optional AT attach configuration - Max number of additional AT */
	u8 max_num_of_at_attach;
};

struct mlx5hws_rule_attr {
	void *user_data;
	/* Valid if matcher optimize_using_rule_idx is set or
	 * if matcher is configured to insert rules by index.
	 */
	u32 rule_idx;
	u32 flow_source;
	u16 queue_id;
	u32 burst:1;
};

/* In actions that take offset, the offset is unique, pointing to a single
 * resource and the user should not reuse the same index because data changing
 * is not atomic.
 */
struct mlx5hws_rule_action {
	struct mlx5hws_action *action;
	union {
		struct {
			u32 value;
		} tag;

		struct {
			u32 offset;
		} counter;

		struct {
			u32 offset;
			u8 *data;
		} modify_header;

		struct {
			u32 offset;
			u8 hdr_idx;
			u8 *data;
		} reformat;

		struct {
			__be32 vlan_hdr;
		} push_vlan;

		struct {
			u32 offset;
			enum mlx5hws_action_aso_meter_color init_color;
		} aso_meter;
	};
};

struct mlx5hws_action_reformat_header {
	size_t sz;
	void *data;
};

struct mlx5hws_action_insert_header {
	struct mlx5hws_action_reformat_header hdr;
	/* PRM start anchor to which header will be inserted */
	u8 anchor;
	/* Header insertion offset in bytes, from the start
	 * anchor to the location where new header will be inserted.
	 */
	u8 offset;
	/* Indicates this header insertion adds encapsulation header to the packet,
	 * requiring device to update offloaded fields (for example IPv4 total length).
	 */
	bool encap;
};

struct mlx5hws_action_remove_header_attr {
	/* PRM start anchor from which header will be removed */
	u8 anchor;
	/* Header remove offset in bytes, from the start
	 * anchor to the location where remove header starts.
	 */
	u8 offset;
	/* Indicates the removed header size in bytes */
	size_t size;
};

struct mlx5hws_action_mh_pattern {
	/* Byte size of modify actions provided by "data" */
	size_t sz;
	/* PRM format modify actions pattern */
	__be64 *data;
};

struct mlx5hws_action_dest_attr {
	/* Required destination action to forward the packet */
	struct mlx5hws_action *dest;
	/* Optional reformat action */
	struct mlx5hws_action *reformat;
};

/* Check whether HWS is supported
 *
 * @param[in] mdev
 *	The device to check.
 * @return true if supported, false otherwise.
 */
static inline bool mlx5hws_is_supported(struct mlx5_core_dev *mdev)
{
	u8 ignore_flow_level_rtc_valid;
	u8 wqe_based_flow_table_update;

	wqe_based_flow_table_update =
		MLX5_CAP_GEN(mdev, wqe_based_flow_table_update_cap);
	ignore_flow_level_rtc_valid =
		MLX5_CAP_FLOWTABLE(mdev,
				   flow_table_properties_nic_receive.ignore_flow_level_rtc_valid);

	return wqe_based_flow_table_update && ignore_flow_level_rtc_valid;
}

/* Open a context used for direct rule insertion using hardware steering.
 * Each context can contain multiple tables of different types.
 *
 * @param[in] mdev
 *	The device to be used for HWS.
 * @param[in] attr
 *	Attributes used for context open.
 * @return pointer to mlx5hws_context on success NULL otherwise.
 */
struct mlx5hws_context *
mlx5hws_context_open(struct mlx5_core_dev *mdev,
		     struct mlx5hws_context_attr *attr);

/* Close a context used for direct hardware steering.
 *
 * @param[in] ctx
 *	mlx5hws context to close.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_context_close(struct mlx5hws_context *ctx);

/* Set a peer context, each context can have multiple contexts as peers.
 *
 * @param[in] ctx
 *	The context in which the peer_ctx will be peered to it.
 * @param[in] peer_ctx
 *	The peer context.
 * @param[in] peer_vhca_id
 *	The peer context vhca id.
 */
void mlx5hws_context_set_peer(struct mlx5hws_context *ctx,
			      struct mlx5hws_context *peer_ctx,
			      u16 peer_vhca_id);

/* Create a new direct rule table. Each table can contain multiple matchers.
 *
 * @param[in] ctx
 *	The context in which the new table will be opened.
 * @param[in] attr
 *	Attributes used for table creation.
 * @return pointer to mlx5hws_table on success NULL otherwise.
 */
struct mlx5hws_table *
mlx5hws_table_create(struct mlx5hws_context *ctx,
		     struct mlx5hws_table_attr *attr);

/* Destroy direct rule table.
 *
 * @param[in] tbl
 *	Table to destroy.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_table_destroy(struct mlx5hws_table *tbl);

/* Get ID of the flow table.
 *
 * @param[in] tbl
 *	Table to get ID of.
 * @return ID of the table.
 */
u32 mlx5hws_table_get_id(struct mlx5hws_table *tbl);

/* Set default miss table for mlx5hws_table by using another mlx5hws_table
 * Traffic which all table matchers miss will be forwarded to miss table.
 *
 * @param[in] tbl
 *	Source table
 * @param[in] miss_tbl
 *	Target (miss) table, or NULL to remove current miss table
 * @return zero on success non zero otherwise.
 */
int mlx5hws_table_set_default_miss(struct mlx5hws_table *tbl,
				   struct mlx5hws_table *miss_tbl);

/* Create new match template based on items mask, the match template
 * will be used for matcher creation.
 *
 * @param[in] ctx
 *	The context in which the new template will be created.
 * @param[in] match_param
 *	Describe the mask based on PRM match parameters
 * @param[in] match_param_sz
 *	Size of match param buffer
 * @param[in] match_criteria_enable
 *	Bitmap for each sub-set in match_criteria buffer
 * @return pointer to mlx5hws_match_template on success NULL otherwise
 */
struct mlx5hws_match_template *
mlx5hws_match_template_create(struct mlx5hws_context *ctx,
			      u32 *match_param,
			      u32 match_param_sz,
			      u8 match_criteria_enable);

/* Destroy match template.
 *
 * @param[in] mt
 *	Match template to destroy.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_match_template_destroy(struct mlx5hws_match_template *mt);

/* Create new action template based on action_type array, the action template
 * will be used for matcher creation.
 *
 * @param[in] action_type
 *	An array of actions based on the order of actions which will be provided
 *	with rule_actions to mlx5hws_rule_create. The last action is marked
 *	using MLX5HWS_ACTION_TYP_LAST.
 * @return pointer to mlx5hws_action_template on success NULL otherwise
 */
struct mlx5hws_action_template *
mlx5hws_action_template_create(enum mlx5hws_action_type action_type[]);

/* Destroy action template.
 *
 * @param[in] at
 *	Action template to destroy.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_action_template_destroy(struct mlx5hws_action_template *at);

/* Create a new direct rule matcher. Each matcher can contain multiple rules.
 * Matchers on the table will be processed by priority. Matching fields and
 * mask are described by the match template. In some cases multiple match
 * templates can be used on the same matcher.
 *
 * @param[in] table
 *	The table in which the new matcher will be opened.
 * @param[in] mt
 *	Array of match templates to be used on matcher.
 * @param[in] num_of_mt
 *	Number of match templates in mt array.
 * @param[in] at
 *	Array of action templates to be used on matcher.
 * @param[in] num_of_at
 *	Number of action templates in mt array.
 * @param[in] attr
 *	Attributes used for matcher creation.
 * @return pointer to mlx5hws_matcher on success NULL otherwise.
 */
struct mlx5hws_matcher *
mlx5hws_matcher_create(struct mlx5hws_table *table,
		       struct mlx5hws_match_template *mt[],
		       u8 num_of_mt,
		       struct mlx5hws_action_template *at[],
		       u8 num_of_at,
		       struct mlx5hws_matcher_attr *attr);

/* Destroy direct rule matcher.
 *
 * @param[in] matcher
 *	Matcher to destroy.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_matcher_destroy(struct mlx5hws_matcher *matcher);

/* Attach new action template to direct rule matcher.
 *
 * @param[in] matcher
 *	Matcher to attach at to.
 * @param[in] at
 *	Action template to be attached to the matcher.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_matcher_attach_at(struct mlx5hws_matcher *matcher,
			      struct mlx5hws_action_template *at);

/* Link two matchers and enable moving rules from src matcher to dst matcher.
 * Both matchers must be in the same table type, must be created with 'resizable'
 * property, and should have the same characteristics (e.g. same mt, same at).
 *
 * It is the user's responsibility to make sure that the dst matcher
 * was allocated with the appropriate size.
 *
 * Once the function is completed, the user is:
 *  - allowed to move rules from src into dst matcher
 *  - no longer allowed to insert rules to the src matcher
 *
 * The user is always allowed to insert rules to the dst matcher and
 * to delete rules from any matcher.
 *
 * @param[in] src_matcher
 *	source matcher for moving rules from
 * @param[in] dst_matcher
 *	destination matcher for moving rules to
 * @return zero on successful move, non zero otherwise.
 */
int mlx5hws_matcher_resize_set_target(struct mlx5hws_matcher *src_matcher,
				      struct mlx5hws_matcher *dst_matcher);

/* Enqueue moving rule operation: moving rule from src matcher to a dst matcher
 *
 * @param[in] src_matcher
 *	matcher that the rule belongs to
 * @param[in] rule
 *	the rule to move
 * @param[in] attr
 *	rule attributes
 * @return zero on success, non zero otherwise.
 */
int mlx5hws_matcher_resize_rule_move(struct mlx5hws_matcher *src_matcher,
				     struct mlx5hws_rule *rule,
				     struct mlx5hws_rule_attr *attr);

/* Enqueue create rule operation.
 *
 * @param[in] matcher
 *	The matcher in which the new rule will be created.
 * @param[in] mt_idx
 *	Match template index to create the match with.
 * @param[in] match_param
 *	The match parameter PRM buffer used for the value matching.
 * @param[in] rule_actions
 *	Rule action to be executed on match.
 * @param[in] at_idx
 *	Action template index to apply the actions with.
 * @param[in] num_of_actions
 *	Number of rule actions.
 * @param[in] attr
 *	Rule creation attributes.
 * @param[in, out] rule_handle
 *	A valid rule handle. The handle doesn't require any initialization.
 * @return zero on successful enqueue non zero otherwise.
 */
int mlx5hws_rule_create(struct mlx5hws_matcher *matcher,
			u8 mt_idx,
			u32 *match_param,
			u8 at_idx,
			struct mlx5hws_rule_action rule_actions[],
			struct mlx5hws_rule_attr *attr,
			struct mlx5hws_rule *rule_handle);

/* Enqueue destroy rule operation.
 *
 * @param[in] rule
 *	The rule destruction to enqueue.
 * @param[in] attr
 *	Rule destruction attributes.
 * @return zero on successful enqueue non zero otherwise.
 */
int mlx5hws_rule_destroy(struct mlx5hws_rule *rule,
			 struct mlx5hws_rule_attr *attr);

/* Enqueue update actions on an existing rule.
 *
 * @param[in, out] rule_handle
 *	A valid rule handle to update.
 * @param[in] at_idx
 *	Action template index to update the actions with.
 *  @param[in] rule_actions
 *	Rule action to be executed on match.
 * @param[in] attr
 *	Rule update attributes.
 * @return zero on successful enqueue non zero otherwise.
 */
int mlx5hws_rule_action_update(struct mlx5hws_rule *rule,
			       u8 at_idx,
			       struct mlx5hws_rule_action rule_actions[],
			       struct mlx5hws_rule_attr *attr);

/* Get action type.
 *
 * @param[in] action
 *	The action to get the type of.
 * @return action type.
 */
enum mlx5hws_action_type
mlx5hws_action_get_type(struct mlx5hws_action *action);

/* Create direct rule drop action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_drop(struct mlx5hws_context *ctx,
				u32 flags);

/* Create direct rule default miss action.
 * Defaults are RX: Drop TX: Wire.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_default_miss(struct mlx5hws_context *ctx,
				   u32 flags);

/* Create direct rule goto table action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] tbl
 *	Destination table.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_table(struct mlx5hws_context *ctx,
				 struct mlx5hws_table *tbl,
				 u32 flags);

/* Create direct rule goto table number action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] tbl_num
 *	Destination table number.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_table_num(struct mlx5hws_context *ctx,
				     u32 table_num, u32 flags);

/* Create direct rule range match action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] field
 *	Field to comapare the value.
 * @param[in] hit_ft
 *	Flow table to go to on hit.
 * @param[in] miss_ft
 *	Flow table to go to on miss.
 * @param[in] min
 *	Minimal value of the field to be considered as hit.
 * @param[in] max
 *	Maximal value of the field to be considered as hit.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_match_range(struct mlx5hws_context *ctx,
				       u32 field,
				       struct mlx5_flow_table *hit_ft,
				       struct mlx5_flow_table *miss_ft,
				       u32 min, u32 max, u32 flags);

/* Create direct rule flow sampler action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] sampler_id
 *	Flow sampler object ID.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_flow_sampler(struct mlx5hws_context *ctx,
				   u32 sampler_id, u32 flags);

/* Create direct rule goto vport action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] vport_num
 *	Destination vport number.
 * @param[in] vhca_id_valid
 *	Tells if the vhca_id parameter is valid.
 * @param[in] vhca_id
 *	VHCA ID of the destination vport.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_vport(struct mlx5hws_context *ctx,
				 u16 vport_num,
				 bool vhca_id_valid,
				 u16 vhca_id,
				 u32 flags);

/* Create direct rule TAG action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_tag(struct mlx5hws_context *ctx,
			  u32 flags);

/* Create direct rule counter action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] obj_id
 *	Direct rule counter object ID.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_counter(struct mlx5hws_context *ctx,
			      u32 obj_id,
			      u32 flags);

/* Create direct rule reformat action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] reformat_type
 *	Type of reformat prefixed with MLX5HWS_ACTION_TYP_REFORMAT.
 * @param[in] num_of_hdrs
 *	Number of provided headers in "hdrs" array.
 * @param[in] hdrs
 *	Headers array containing header information.
 * @param[in] log_bulk_size
 *	Number of unique values used with this reformat.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_reformat(struct mlx5hws_context *ctx,
			       enum mlx5hws_action_type reformat_type,
			       u8 num_of_hdrs,
			       struct mlx5hws_action_reformat_header *hdrs,
			       u32 log_bulk_size,
			       u32 flags);

/* Create direct rule modify header action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] num_of_patterns
 *	Number of provided patterns in "patterns" array.
 * @param[in] patterns
 *	Patterns array containing pattern information.
 * @param[in] log_bulk_size
 *	Number of unique values used with this pattern.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_modify_header(struct mlx5hws_context *ctx,
				    u8 num_of_patterns,
				    struct mlx5hws_action_mh_pattern *patterns,
				    u32 log_bulk_size,
				    u32 flags);

/* Create direct rule ASO flow meter action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] obj_id
 *	ASO object ID.
 * @param[in] return_reg_c
 *	Copy the ASO object value into this reg_c, after a packet hits a rule with this ASO object.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_aso_meter(struct mlx5hws_context *ctx,
				u32 obj_id,
				u8 return_reg_c,
				u32 flags);

/* Create direct rule pop vlan action.
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_pop_vlan(struct mlx5hws_context *ctx, u32 flags);

/* Create direct rule push vlan action.
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_push_vlan(struct mlx5hws_context *ctx, u32 flags);

/* Create a dest array action, this action can duplicate packets and forward to
 * multiple destinations in the destination list.
 * @param[in] ctx
 *     The context in which the new action will be created.
 * @param[in] num_dest
 *     The number of dests attributes.
 * @param[in] dests
 *     The destination array. Each contains a destination action and can have
 *     additional actions.
 * @param[in] ignore_flow_level
 *     Boolean that says whether to turn on 'ignore_flow_level' for this dest.
 * @param[in] flow_source
 *     Source port of the traffic for this actions.
 * @param[in] flags
 *     Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_array(struct mlx5hws_context *ctx,
				 size_t num_dest,
				 struct mlx5hws_action_dest_attr *dests,
				 bool ignore_flow_level,
				 u32 flow_source,
				 u32 flags);

/* Create insert header action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] num_of_hdrs
 *	Number of provided headers in "hdrs" array.
 * @param[in] hdrs
 *	Headers array containing header information.
 * @param[in] log_bulk_size
 *	Number of unique values used with this insert header.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_insert_header(struct mlx5hws_context *ctx,
				    u8 num_of_hdrs,
				    struct mlx5hws_action_insert_header *hdrs,
				    u32 log_bulk_size,
				    u32 flags);

/* Create remove header action.
 *
 * @param[in] ctx
 *	The context in which the new action will be created.
 * @param[in] attr
 *	attributes: specifies the remove header type, PRM start anchor and
 *	the PRM end anchor or the PRM start anchor and remove size in bytes.
 * @param[in] flags
 *	Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_remove_header(struct mlx5hws_context *ctx,
				    struct mlx5hws_action_remove_header_attr *attr,
				    u32 flags);

/* Create direct rule LAST action.
 *
 * @param[in] ctx
 *     The context in which the new action will be created.
 * @param[in] flags
 *     Action creation flags. (enum mlx5hws_action_flags)
 * @return pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_last(struct mlx5hws_context *ctx, u32 flags);

/* Destroy direct rule action.
 *
 * @param[in] action
 *	The action to destroy.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_action_destroy(struct mlx5hws_action *action);

enum mlx5hws_flow_op_status {
	MLX5HWS_FLOW_OP_SUCCESS,
	MLX5HWS_FLOW_OP_ERROR,
};

struct mlx5hws_flow_op_result {
	enum mlx5hws_flow_op_status status;
	void *user_data;
};

/* Poll queue for rule creation and deletions completions.
 *
 * @param[in] ctx
 *	The context to which the queue belong to.
 * @param[in] queue_id
 *	The id of the queue to poll.
 * @param[in, out] res
 *	Completion array.
 * @param[in] res_nb
 *	Maximum number of results to return.
 * @return negative number on failure, the number of completions otherwise.
 */
int mlx5hws_send_queue_poll(struct mlx5hws_context *ctx,
			    u16 queue_id,
			    struct mlx5hws_flow_op_result res[],
			    u32 res_nb);

/* Perform an action on the queue
 *
 * @param[in] ctx
 *	The context to which the queue belong to.
 * @param[in] queue_id
 *	The id of the queue to perform the action on.
 * @param[in] actions
 *	Actions to perform on the queue. (enum mlx5hws_send_queue_actions)
 * @return zero on success non zero otherwise.
 */
int mlx5hws_send_queue_action(struct mlx5hws_context *ctx,
			      u16 queue_id,
			      u32 actions);

/* Dump HWS info
 *
 * @param[in] ctx
 *	The context which to dump the info from.
 * @return zero on success non zero otherwise.
 */
int mlx5hws_debug_dump(struct mlx5hws_context *ctx);

struct mlx5hws_bwc_matcher;
struct mlx5hws_bwc_rule;

struct mlx5hws_match_parameters {
	size_t match_sz;
	u32 *match_buf; /* Device spec format */
};

/* Create a new BWC direct rule matcher.
 * This function does the following:
 *   - creates match template based on flow items
 *   - creates an empty action template
 *   - creates a usual mlx5hws_matcher with these mt and at, setting
 *     its size to minimal
 * Notes:
 *   - table->ctx must have BWC support
 *   - complex rules are not supported
 *
 * @param[in] table
 *	The table in which the new matcher will be opened
 * @param[in] priority
 *	Priority for this BWC matcher
 * @param[in] match_criteria_enable
 *	Bitmask that defines matching criteria
 * @param[in] mask
 *	Match parameters
 * @return pointer to mlx5hws_bwc_matcher on success or NULL otherwise.
 */
struct mlx5hws_bwc_matcher *
mlx5hws_bwc_matcher_create(struct mlx5hws_table *table,
			   u32 priority,
			   u8 match_criteria_enable,
			   struct mlx5hws_match_parameters *mask);

/* Destroy BWC direct rule matcher.
 *
 * @param[in] bwc_matcher
 *	Matcher to destroy
 * @return zero on success, non zero otherwise
 */
int mlx5hws_bwc_matcher_destroy(struct mlx5hws_bwc_matcher *bwc_matcher);

/* Create a new BWC rule.
 * Unlike the usual rule creation function, this one is blocking: when the
 * function returns, the rule is written to its place (no need to poll).
 * This function does the following:
 *   - finds matching action template based on the provided rule_actions, or
 *     creates new action template if matching action template doesn't exist
 *   - updates corresponding BWC matcher stats
 *   - if needed, the function performs rehash:
 *       - creates a new matcher based on mt, at, new_sz
 *       - moves all the existing matcher rules to the new matcher
 *       - removes the old matcher
 *   - inserts new rule
 *   - polls till completion is received
 * Notes:
 *   - matcher->tbl->ctx must have BWC support
 *   - separate BWC ctx queues are used
 *
 * @param[in] bwc_matcher
 *	The BWC matcher in which the new rule will be created.
 * @param[in] params
 *	Match perameters
 * @param[in] flow_source
 *	Flow source for this rule
 * @param[in] rule_actions
 *	Rule action to be executed on match
 * @return valid BWC rule handle on success, NULL otherwise
 */
struct mlx5hws_bwc_rule *
mlx5hws_bwc_rule_create(struct mlx5hws_bwc_matcher *bwc_matcher,
			struct mlx5hws_match_parameters *params,
			u32 flow_source,
			struct mlx5hws_rule_action rule_actions[]);

/* Destroy BWC direct rule.
 *
 * @param[in] bwc_rule
 *	Rule to destroy
 * @return zero on success, non zero otherwise
 */
int mlx5hws_bwc_rule_destroy(struct mlx5hws_bwc_rule *bwc_rule);

/* Update actions on an existing BWC rule.
 *
 * @param[in] bwc_rule
 *	Rule to update
 * @param[in] rule_actions
 *	Rule action to update with
 * @return zero on successful update, non zero otherwise.
 */
int mlx5hws_bwc_rule_action_update(struct mlx5hws_bwc_rule *bwc_rule,
				   struct mlx5hws_rule_action rule_actions[]);

#endif
