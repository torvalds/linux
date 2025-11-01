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
};

struct mlx5hws_table_attr {
	enum mlx5hws_table_type type;
	u32 level;
	u16 uid;
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

enum mlx5hws_matcher_size_type {
	MLX5HWS_MATCHER_SIZE_TYPE_RX,
	MLX5HWS_MATCHER_SIZE_TYPE_TX,
	MLX5HWS_MATCHER_SIZE_TYPE_MAX,
};

union mlx5hws_matcher_size {
	struct {
		u8 sz_row_log;
		u8 sz_col_log;
	} table;

	struct {
		u8 num_log;
	} rule;
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
	union mlx5hws_matcher_size size[MLX5HWS_MATCHER_SIZE_TYPE_MAX];
	/* Optional AT attach configuration - Max number of additional AT */
	u8 max_num_of_at_attach;
	/* Optional end FT (miss FT ID) for match RTC (for isolated matcher) */
	u32 isolated_matcher_end_ft_id;
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
	bool is_wire_ft;
};

/**
 * mlx5hws_is_supported - Check whether HWS is supported
 *
 * @mdev: The device to check.
 *
 * Return: true if supported, false otherwise.
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

/**
 * mlx5hws_context_open - Open a context used for direct rule insertion
 * using hardware steering.
 *
 * @mdev: The device to be used for HWS.
 * @attr: Attributes used for context open.
 *
 * Return: pointer to mlx5hws_context on success NULL otherwise.
 */
struct mlx5hws_context *
mlx5hws_context_open(struct mlx5_core_dev *mdev,
		     struct mlx5hws_context_attr *attr);

/**
 * mlx5hws_context_close - Close a context used for direct hardware steering.
 *
 * @ctx: mlx5hws context to close.
 *
 * Return: zero on success non zero otherwise.
 */
int mlx5hws_context_close(struct mlx5hws_context *ctx);

/**
 * mlx5hws_context_set_peer - Set a peer context.
 * Each context can have multiple contexts as peers.
 *
 * @ctx: The context in which the peer_ctx will be peered to it.
 * @peer_ctx: The peer context.
 * @peer_vhca_id: The peer context vhca id.
 */
void mlx5hws_context_set_peer(struct mlx5hws_context *ctx,
			      struct mlx5hws_context *peer_ctx,
			      u16 peer_vhca_id);

/**
 * mlx5hws_table_create - Create a new direct rule table.
 * Each table can contain multiple matchers.
 *
 * @ctx: The context in which the new table will be opened.
 * @attr: Attributes used for table creation.
 *
 * Return: pointer to mlx5hws_table on success NULL otherwise.
 */
struct mlx5hws_table *
mlx5hws_table_create(struct mlx5hws_context *ctx,
		     struct mlx5hws_table_attr *attr);

/**
 * mlx5hws_table_destroy - Destroy direct rule table.
 *
 * @tbl: Table to destroy.
 *
 * Return: zero on success non zero otherwise.
 */
int mlx5hws_table_destroy(struct mlx5hws_table *tbl);

/**
 * mlx5hws_table_get_id() - Get ID of the flow table.
 *
 * @tbl:Table to get ID of.
 *
 * Return: ID of the table.
 */
u32 mlx5hws_table_get_id(struct mlx5hws_table *tbl);

/**
 * mlx5hws_table_set_default_miss - Set default miss table for mlx5hws_table
 * by using another mlx5hws_table.
 * Traffic which all table matchers miss will be forwarded to miss table.
 *
 * @tbl: Source table
 * @miss_tbl: Target (miss) table, or NULL to remove current miss table
 *
 * Return: zero on success non zero otherwise.
 */
int mlx5hws_table_set_default_miss(struct mlx5hws_table *tbl,
				   struct mlx5hws_table *miss_tbl);

/**
 * mlx5hws_match_template_create - Create a new match template based on items mask.
 * The match template will be used for matcher creation.
 *
 * @ctx: The context in which the new template will be created.
 * @match_param: Describe the mask based on PRM match parameters.
 * @match_param_sz: Size of match param buffer.
 * @match_criteria_enable: Bitmap for each sub-set in match_criteria buffer.
 *
 * Return: Pointer to mlx5hws_match_template on success, NULL otherwise.
 */
struct mlx5hws_match_template *
mlx5hws_match_template_create(struct mlx5hws_context *ctx,
			      u32 *match_param,
			      u32 match_param_sz,
			      u8 match_criteria_enable);

/**
 * mlx5hws_match_template_destroy - Destroy a match template.
 *
 * @mt: Match template to destroy.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int mlx5hws_match_template_destroy(struct mlx5hws_match_template *mt);

/**
 * mlx5hws_action_template_create - Create a new action template based on an action_type array.
 *
 * @action_type: An array of actions based on the order of actions which will be provided
 *               with rule_actions to mlx5hws_rule_create. The last action is marked
 *               using MLX5HWS_ACTION_TYP_LAST.
 *
 * Return: Pointer to mlx5hws_action_template on success, NULL otherwise.
 */
struct mlx5hws_action_template *
mlx5hws_action_template_create(enum mlx5hws_action_type action_type[]);

/**
 * mlx5hws_action_template_destroy - Destroy action template.
 *
 * @at: Action template to destroy.
 *
 * Return: zero on success non zero otherwise.
 */
int mlx5hws_action_template_destroy(struct mlx5hws_action_template *at);

/**
 * mlx5hws_matcher_create - Create a new direct rule matcher.
 *
 * Each matcher can contain multiple rules. Matchers on the table will be
 * processed by priority. Matching fields and mask are described by the
 * match template. In some cases, multiple match templates can be used on
 * the same matcher.
 *
 * @table: The table in which the new matcher will be opened.
 * @mt: Array of match templates to be used on matcher.
 * @num_of_mt: Number of match templates in mt array.
 * @at: Array of action templates to be used on matcher.
 * @num_of_at: Number of action templates in at array.
 * @attr: Attributes used for matcher creation.
 *
 * Return: Pointer to mlx5hws_matcher on success, NULL otherwise.
 *
 */
struct mlx5hws_matcher *
mlx5hws_matcher_create(struct mlx5hws_table *table,
		       struct mlx5hws_match_template *mt[],
		       u8 num_of_mt,
		       struct mlx5hws_action_template *at[],
		       u8 num_of_at,
		       struct mlx5hws_matcher_attr *attr);

/**
 * mlx5hws_matcher_destroy - Destroy a direct rule matcher.
 *
 * @matcher: Matcher to destroy.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int mlx5hws_matcher_destroy(struct mlx5hws_matcher *matcher);

/**
 * mlx5hws_matcher_attach_at - Attach a new action template to a direct rule matcher.
 *
 * @matcher: Matcher to attach the action template to.
 * @at: Action template to be attached to the matcher.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int mlx5hws_matcher_attach_at(struct mlx5hws_matcher *matcher,
			      struct mlx5hws_action_template *at);

/**
 * mlx5hws_matcher_resize_set_target - Link two matchers and enable moving rules.
 *
 * Both matchers must be in the same table type, must be created with the
 * 'resizable' property, and should have the same characteristics (e.g., same
 * match templates and action templates). It is the user's responsibility to
 * ensure that the destination matcher is allocated with the appropriate size.
 *
 * Once the function is completed, the user is:
 * - Allowed to move rules from the source into the destination matcher.
 * - No longer allowed to insert rules into the source matcher.
 *
 * The user is always allowed to insert rules into the destination matcher and
 * to delete rules from any matcher.
 *
 * @src_matcher: Source matcher for moving rules from.
 * @dst_matcher: Destination matcher for moving rules to.
 *
 * Return: Zero on successful move, non-zero otherwise.
 */
int mlx5hws_matcher_resize_set_target(struct mlx5hws_matcher *src_matcher,
				      struct mlx5hws_matcher *dst_matcher);

/**
 * mlx5hws_matcher_resize_rule_move - Enqueue moving rule operation.
 *
 * This function enqueues the operation of moving a rule from the source
 * matcher to the destination matcher.
 *
 * @src_matcher: Matcher that the rule belongs to.
 * @rule: The rule to move.
 * @attr: Rule attributes.
 *
 * Return: Zero on success, non-zero otherwise.
 */
int mlx5hws_matcher_resize_rule_move(struct mlx5hws_matcher *src_matcher,
				     struct mlx5hws_rule *rule,
				     struct mlx5hws_rule_attr *attr);

/**
 * mlx5hws_rule_create - Enqueue create rule operation.
 *
 * @matcher: The matcher in which the new rule will be created.
 * @mt_idx: Match template index to create the match with.
 * @match_param: The match parameter PRM buffer used for value matching.
 * @at_idx: Action template index to apply the actions with.
 * @rule_actions: Rule actions to be executed on match.
 * @attr: Rule creation attributes.
 * @rule_handle: A valid rule handle. The handle doesn't require any initialization.
 *
 * Return: Zero on successful enqueue, non-zero otherwise.
 */
int mlx5hws_rule_create(struct mlx5hws_matcher *matcher,
			u8 mt_idx,
			u32 *match_param,
			u8 at_idx,
			struct mlx5hws_rule_action rule_actions[],
			struct mlx5hws_rule_attr *attr,
			struct mlx5hws_rule *rule_handle);

/**
 * mlx5hws_rule_destroy - Enqueue destroy rule operation.
 *
 * @rule: The rule destruction to enqueue.
 * @attr: Rule destruction attributes.
 *
 * Return: Zero on successful enqueue, non-zero otherwise.
 */
int mlx5hws_rule_destroy(struct mlx5hws_rule *rule,
			 struct mlx5hws_rule_attr *attr);

/**
 * mlx5hws_rule_action_update - Enqueue update actions on an existing rule.
 *
 * @rule: A valid rule handle to update.
 * @at_idx: Action template index to update the actions with.
 * @rule_actions: Rule actions to be executed on match.
 * @attr: Rule update attributes.
 *
 * Return: Zero on successful enqueue, non-zero otherwise.
 */
int mlx5hws_rule_action_update(struct mlx5hws_rule *rule,
			       u8 at_idx,
			       struct mlx5hws_rule_action rule_actions[],
			       struct mlx5hws_rule_attr *attr);

/**
 * mlx5hws_action_get_type - Get action type.
 *
 * @action: The action to get the type of.
 *
 * Return: action type.
 */
enum mlx5hws_action_type
mlx5hws_action_get_type(struct mlx5hws_action *action);

/**
 * mlx5hws_action_get_dev - Get mlx5 core device.
 *
 * @action: The action to get the device from.
 *
 * Return: mlx5 core device.
 */
struct mlx5_core_dev *mlx5hws_action_get_dev(struct mlx5hws_action *action);

/**
 * mlx5hws_action_create_dest_drop - Create a direct rule drop action.
 *
 * @ctx: The context in which the new action will be created.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: Pointer to mlx5hws_action on success, NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_drop(struct mlx5hws_context *ctx,
				u32 flags);

/**
 * mlx5hws_action_create_default_miss - Create a direct rule default miss action.
 * Defaults are RX: Drop, TX: Wire.
 *
 * @ctx: The context in which the new action will be created.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: Pointer to mlx5hws_action on success, NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_default_miss(struct mlx5hws_context *ctx,
				   u32 flags);

/**
 * mlx5hws_action_create_dest_table - Create direct rule goto table action.
 *
 * @ctx: The context in which the new action will be created.
 * @tbl: Destination table.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_table(struct mlx5hws_context *ctx,
				 struct mlx5hws_table *tbl,
				 u32 flags);

/**
 * mlx5hws_action_create_dest_table_num - Create direct rule goto table number action.
 *
 * @ctx: The context in which the new action will be created.
 * @tbl_num: Destination table number.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_table_num(struct mlx5hws_context *ctx,
				     u32 tbl_num, u32 flags);

/**
 * mlx5hws_action_create_dest_match_range - Create direct rule range match action.
 *
 * @ctx: The context in which the new action will be created.
 * @field: Field to comapare the value.
 * @hit_ft: Flow table to go to on hit.
 * @miss_ft: Flow table to go to on miss.
 * @min: Minimal value of the field to be considered as hit.
 * @max: Maximal value of the field to be considered as hit.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_match_range(struct mlx5hws_context *ctx,
				       u32 field,
				       struct mlx5_flow_table *hit_ft,
				       struct mlx5_flow_table *miss_ft,
				       u32 min, u32 max, u32 flags);

/**
 * mlx5hws_action_create_flow_sampler - Create direct rule flow sampler action.
 *
 * @ctx: The context in which the new action will be created.
 * @sampler_id: Flow sampler object ID.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_flow_sampler(struct mlx5hws_context *ctx,
				   u32 sampler_id, u32 flags);

/**
 * mlx5hws_action_create_dest_vport - Create direct rule goto vport action.
 *
 * @ctx: The context in which the new action will be created.
 * @vport_num: Destination vport number.
 * @vhca_id_valid: Tells if the vhca_id parameter is valid.
 * @vhca_id: VHCA ID of the destination vport.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_vport(struct mlx5hws_context *ctx,
				 u16 vport_num,
				 bool vhca_id_valid,
				 u16 vhca_id,
				 u32 flags);

/**
 * mlx5hws_action_create_tag - Create direct rule TAG action.
 *
 * @ctx: The context in which the new action will be created.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_tag(struct mlx5hws_context *ctx, u32 flags);

/**
 * mlx5hws_action_create_counter - Create direct rule counter action.
 *
 * @ctx: The context in which the new action will be created.
 * @obj_id: Direct rule counter object ID.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_counter(struct mlx5hws_context *ctx,
			      u32 obj_id,
			      u32 flags);

/**
 * mlx5hws_action_create_reformat - Create direct rule reformat action.
 *
 * @ctx: The context in which the new action will be created.
 * @reformat_type: Type of reformat prefixed with MLX5HWS_ACTION_TYP_REFORMAT.
 * @num_of_hdrs: Number of provided headers in "hdrs" array.
 * @hdrs: Headers array containing header information.
 * @log_bulk_size: Number of unique values used with this reformat.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_reformat(struct mlx5hws_context *ctx,
			       enum mlx5hws_action_type reformat_type,
			       u8 num_of_hdrs,
			       struct mlx5hws_action_reformat_header *hdrs,
			       u32 log_bulk_size,
			       u32 flags);

/**
 * mlx5hws_action_create_modify_header - Create direct rule modify header action.
 *
 * @ctx: The context in which the new action will be created.
 * @num_of_patterns: Number of provided patterns in "patterns" array.
 * @patterns: Patterns array containing pattern information.
 * @log_bulk_size: Number of unique values used with this pattern.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_modify_header(struct mlx5hws_context *ctx,
				    u8 num_of_patterns,
				    struct mlx5hws_action_mh_pattern *patterns,
				    u32 log_bulk_size,
				    u32 flags);

/**
 * mlx5hws_action_create_aso_meter - Create direct rule ASO flow meter action.
 *
 * @ctx: The context in which the new action will be created.
 * @obj_id: ASO object ID.
 * @return_reg_c: Copy the ASO object value into this reg_c,
 *		  after a packet hits a rule with this ASO object.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_aso_meter(struct mlx5hws_context *ctx,
				u32 obj_id,
				u8 return_reg_c,
				u32 flags);

/**
 * mlx5hws_action_create_pop_vlan - Create direct rule pop vlan action.
 *
 * @ctx: The context in which the new action will be created.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_pop_vlan(struct mlx5hws_context *ctx, u32 flags);

/**
 * mlx5hws_action_create_push_vlan - Create direct rule push vlan action.
 *
 * @ctx: The context in which the new action will be created.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_push_vlan(struct mlx5hws_context *ctx, u32 flags);

/**
 * mlx5hws_action_create_dest_array - Create a dest array action, this action can
 * duplicate packets and forward to multiple destinations in the destination list.
 *
 * @ctx: The context in which the new action will be created.
 * @num_dest: The number of dests attributes.
 * @dests: The destination array. Each contains a destination action and can
 *	   have additional actions.
 * @flags: Action creation flags (enum mlx5hws_action_flags).
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_dest_array(struct mlx5hws_context *ctx, size_t num_dest,
				 struct mlx5hws_action_dest_attr *dests,
				 u32 flags);

/**
 * mlx5hws_action_create_insert_header - Create insert header action.
 *
 * @ctx: The context in which the new action will be created.
 * @num_of_hdrs: Number of provided headers in "hdrs" array.
 * @hdrs: Headers array containing header information.
 * @log_bulk_size: Number of unique values used with this insert header.
 * @flags: Action creation flags. (enum mlx5hws_action_flags)
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_insert_header(struct mlx5hws_context *ctx,
				    u8 num_of_hdrs,
				    struct mlx5hws_action_insert_header *hdrs,
				    u32 log_bulk_size,
				    u32 flags);

/**
 * mlx5hws_action_create_remove_header - Create remove header action.
 *
 * @ctx: The context in which the new action will be created.
 * @attr: attributes that specifie the remove header type, PRM start anchor and
 *	  the PRM end anchor or the PRM start anchor and remove size in bytes.
 * @flags: Action creation flags. (enum mlx5hws_action_flags)
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_remove_header(struct mlx5hws_context *ctx,
				    struct mlx5hws_action_remove_header_attr *attr,
				    u32 flags);

/**
 * mlx5hws_action_create_last - Create direct rule LAST action.
 *
 * @ctx: The context in which the new action will be created.
 * @flags: Action creation flags. (enum mlx5hws_action_flags)
 *
 * Return: pointer to mlx5hws_action on success NULL otherwise.
 */
struct mlx5hws_action *
mlx5hws_action_create_last(struct mlx5hws_context *ctx, u32 flags);

/**
 * mlx5hws_action_destroy - Destroy direct rule action.
 *
 * @action: The action to destroy.
 *
 * Return: zero on success non zero otherwise.
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

/**
 * mlx5hws_send_queue_poll - Poll queue for rule creation and deletions completions.
 *
 * @ctx: The context to which the queue belong to.
 * @queue_id: The id of the queue to poll.
 * @res: Completion array.
 * @res_nb: Maximum number of results to return.
 *
 * Return: negative number on failure, the number of completions otherwise.
 */
int mlx5hws_send_queue_poll(struct mlx5hws_context *ctx,
			    u16 queue_id,
			    struct mlx5hws_flow_op_result res[],
			    u32 res_nb);

/**
 * mlx5hws_send_queue_action - Perform an action on the queue
 *
 * @ctx: The context to which the queue belong to.
 * @queue_id: The id of the queue to perform the action on.
 * @actions: Actions to perform on the queue (enum mlx5hws_send_queue_actions)
 *
 * Return: zero on success non zero otherwise.
 */
int mlx5hws_send_queue_action(struct mlx5hws_context *ctx,
			      u16 queue_id,
			      u32 actions);

/**
 * mlx5hws_debug_dump - Dump HWS info
 *
 * @ctx: The context which to dump the info from.
 *
 * Return: zero on success non zero otherwise.
 */
int mlx5hws_debug_dump(struct mlx5hws_context *ctx);

struct mlx5hws_bwc_matcher;
struct mlx5hws_bwc_rule;

struct mlx5hws_match_parameters {
	size_t match_sz;
	u32 *match_buf; /* Device spec format */
};

/**
 * mlx5hws_bwc_matcher_create - Create a new BWC direct rule matcher.
 *
 * This function does the following:
 *   - creates match template based on flow items
 *   - creates an empty action template
 *   - creates a usual mlx5hws_matcher with these mt and at, setting
 *     its size to minimal
 * Notes:
 *   - table->ctx must have BWC support
 *   - complex rules are not supported
 *
 * @table: The table in which the new matcher will be opened
 * @priority: Priority for this BWC matcher
 * @match_criteria_enable: Bitmask that defines matching criteria
 * @mask: Match parameters
 *
 * Return: pointer to mlx5hws_bwc_matcher on success or NULL otherwise.
 */
struct mlx5hws_bwc_matcher *
mlx5hws_bwc_matcher_create(struct mlx5hws_table *table,
			   u32 priority,
			   u8 match_criteria_enable,
			   struct mlx5hws_match_parameters *mask);

/**
 * mlx5hws_bwc_matcher_destroy - Destroy BWC direct rule matcher.
 *
 * @bwc_matcher: Matcher to destroy
 *
 * Return: zero on success, non zero otherwise
 */
int mlx5hws_bwc_matcher_destroy(struct mlx5hws_bwc_matcher *bwc_matcher);

/**
 * mlx5hws_bwc_rule_create - Create a new BWC rule.
 *
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
 * @bwc_matcher: The BWC matcher in which the new rule will be created.
 * @params: Match perameters
 * @flow_source: Flow source for this rule
 * @rule_actions: Rule action to be executed on match
 *
 * Return: valid BWC rule handle on success, NULL otherwise
 */
struct mlx5hws_bwc_rule *
mlx5hws_bwc_rule_create(struct mlx5hws_bwc_matcher *bwc_matcher,
			struct mlx5hws_match_parameters *params,
			u32 flow_source,
			struct mlx5hws_rule_action rule_actions[]);

/**
 * mlx5hws_bwc_rule_destroy - Destroy BWC direct rule.
 *
 * @bwc_rule: Rule to destroy.
 *
 * Return: zero on success, non zero otherwise.
 */
int mlx5hws_bwc_rule_destroy(struct mlx5hws_bwc_rule *bwc_rule);

/**
 * mlx5hws_bwc_rule_action_update - Update actions on an existing BWC rule.
 *
 * @bwc_rule: Rule to update
 * @rule_actions: Rule action to update with
 *
 * Return: zero on successful update, non zero otherwise.
 */
int mlx5hws_bwc_rule_action_update(struct mlx5hws_bwc_rule *bwc_rule,
				   struct mlx5hws_rule_action rule_actions[]);

#endif
