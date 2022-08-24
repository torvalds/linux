/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2019, Mellanox Technologies */

#ifndef _MLX5DR_H_
#define _MLX5DR_H_

struct mlx5dr_domain;
struct mlx5dr_table;
struct mlx5dr_matcher;
struct mlx5dr_rule;
struct mlx5dr_action;

enum mlx5dr_domain_type {
	MLX5DR_DOMAIN_TYPE_NIC_RX,
	MLX5DR_DOMAIN_TYPE_NIC_TX,
	MLX5DR_DOMAIN_TYPE_FDB,
};

enum mlx5dr_domain_sync_flags {
	MLX5DR_DOMAIN_SYNC_FLAGS_SW = 1 << 0,
	MLX5DR_DOMAIN_SYNC_FLAGS_HW = 1 << 1,
};

enum mlx5dr_action_reformat_type {
	DR_ACTION_REFORMAT_TYP_TNL_L2_TO_L2,
	DR_ACTION_REFORMAT_TYP_L2_TO_TNL_L2,
	DR_ACTION_REFORMAT_TYP_TNL_L3_TO_L2,
	DR_ACTION_REFORMAT_TYP_L2_TO_TNL_L3,
	DR_ACTION_REFORMAT_TYP_INSERT_HDR,
	DR_ACTION_REFORMAT_TYP_REMOVE_HDR,
};

struct mlx5dr_match_parameters {
	size_t match_sz;
	u64 *match_buf; /* Device spec format */
};

struct mlx5dr_action_dest {
	struct mlx5dr_action *dest;
	struct mlx5dr_action *reformat;
};

struct mlx5dr_domain *
mlx5dr_domain_create(struct mlx5_core_dev *mdev, enum mlx5dr_domain_type type);

int mlx5dr_domain_destroy(struct mlx5dr_domain *domain);

int mlx5dr_domain_sync(struct mlx5dr_domain *domain, u32 flags);

void mlx5dr_domain_set_peer(struct mlx5dr_domain *dmn,
			    struct mlx5dr_domain *peer_dmn);

struct mlx5dr_table *
mlx5dr_table_create(struct mlx5dr_domain *domain, u32 level, u32 flags,
		    u16 uid);

struct mlx5dr_table *
mlx5dr_table_get_from_fs_ft(struct mlx5_flow_table *ft);

int mlx5dr_table_destroy(struct mlx5dr_table *table);

u32 mlx5dr_table_get_id(struct mlx5dr_table *table);

struct mlx5dr_matcher *
mlx5dr_matcher_create(struct mlx5dr_table *table,
		      u32 priority,
		      u8 match_criteria_enable,
		      struct mlx5dr_match_parameters *mask);

int mlx5dr_matcher_destroy(struct mlx5dr_matcher *matcher);

struct mlx5dr_rule *
mlx5dr_rule_create(struct mlx5dr_matcher *matcher,
		   struct mlx5dr_match_parameters *value,
		   size_t num_actions,
		   struct mlx5dr_action *actions[],
		   u32 flow_source);

int mlx5dr_rule_destroy(struct mlx5dr_rule *rule);

int mlx5dr_table_set_miss_action(struct mlx5dr_table *tbl,
				 struct mlx5dr_action *action);

struct mlx5dr_action *
mlx5dr_action_create_dest_table_num(struct mlx5dr_domain *dmn, u32 table_num);

struct mlx5dr_action *
mlx5dr_action_create_dest_table(struct mlx5dr_table *table);

struct mlx5dr_action *
mlx5dr_action_create_dest_flow_fw_table(struct mlx5dr_domain *domain,
					struct mlx5_flow_table *ft);

struct mlx5dr_action *
mlx5dr_action_create_dest_vport(struct mlx5dr_domain *domain,
				u16 vport, u8 vhca_id_valid,
				u16 vhca_id);

struct mlx5dr_action *
mlx5dr_action_create_mult_dest_tbl(struct mlx5dr_domain *dmn,
				   struct mlx5dr_action_dest *dests,
				   u32 num_of_dests,
				   bool ignore_flow_level,
				   u32 flow_source);

struct mlx5dr_action *mlx5dr_action_create_drop(void);

struct mlx5dr_action *mlx5dr_action_create_tag(u32 tag_value);

struct mlx5dr_action *
mlx5dr_action_create_flow_sampler(struct mlx5dr_domain *dmn, u32 sampler_id);

struct mlx5dr_action *
mlx5dr_action_create_flow_counter(u32 counter_id);

struct mlx5dr_action *
mlx5dr_action_create_packet_reformat(struct mlx5dr_domain *dmn,
				     enum mlx5dr_action_reformat_type reformat_type,
				     u8 reformat_param_0,
				     u8 reformat_param_1,
				     size_t data_sz,
				     void *data);

struct mlx5dr_action *
mlx5dr_action_create_modify_header(struct mlx5dr_domain *domain,
				   u32 flags,
				   size_t actions_sz,
				   __be64 actions[]);

struct mlx5dr_action *mlx5dr_action_create_pop_vlan(void);

struct mlx5dr_action *
mlx5dr_action_create_push_vlan(struct mlx5dr_domain *domain, __be32 vlan_hdr);

struct mlx5dr_action *
mlx5dr_action_create_aso(struct mlx5dr_domain *dmn,
			 u32 obj_id,
			 u8 return_reg_id,
			 u8 aso_type,
			 u8 init_color,
			 u8 meter_id);

int mlx5dr_action_destroy(struct mlx5dr_action *action);

static inline bool
mlx5dr_is_supported(struct mlx5_core_dev *dev)
{
	return MLX5_CAP_GEN(dev, roce) &&
	       (MLX5_CAP_ESW_FLOWTABLE_FDB(dev, sw_owner) ||
		(MLX5_CAP_ESW_FLOWTABLE_FDB(dev, sw_owner_v2) &&
		 (MLX5_CAP_GEN(dev, steering_format_version) <=
		  MLX5_STEERING_FORMAT_CONNECTX_7)));
}

/* buddy functions & structure */

struct mlx5dr_icm_mr;

struct mlx5dr_icm_buddy_mem {
	unsigned long		**bitmap;
	unsigned int		*num_free;
	u32			max_order;
	struct list_head	list_node;
	struct mlx5dr_icm_mr	*icm_mr;
	struct mlx5dr_icm_pool	*pool;

	/* This is the list of used chunks. HW may be accessing this memory */
	struct list_head	used_list;
	u64			used_memory;

	/* Hardware may be accessing this memory but at some future,
	 * undetermined time, it might cease to do so.
	 * sync_ste command sets them free.
	 */
	struct list_head	hot_list;

	/* Memory optimisation */
	struct mlx5dr_ste	*ste_arr;
	struct list_head	*miss_list;
	u8			*hw_ste_arr;
};

int mlx5dr_buddy_init(struct mlx5dr_icm_buddy_mem *buddy,
		      unsigned int max_order);
void mlx5dr_buddy_cleanup(struct mlx5dr_icm_buddy_mem *buddy);
int mlx5dr_buddy_alloc_mem(struct mlx5dr_icm_buddy_mem *buddy,
			   unsigned int order,
			   unsigned int *segment);
void mlx5dr_buddy_free_mem(struct mlx5dr_icm_buddy_mem *buddy,
			   unsigned int seg, unsigned int order);

#endif /* _MLX5DR_H_ */
