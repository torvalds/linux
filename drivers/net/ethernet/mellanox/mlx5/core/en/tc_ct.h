/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2018 Mellanox Technologies. */

#ifndef __MLX5_EN_TC_CT_H__
#define __MLX5_EN_TC_CT_H__

#include <net/pkt_cls.h>
#include <linux/mlx5/fs.h>
#include <net/tc_act/tc_ct.h>

#include "en.h"

struct mlx5_flow_attr;
struct mlx5e_tc_mod_hdr_acts;
struct mlx5_rep_uplink_priv;
struct mlx5e_tc_flow;
struct mlx5e_priv;

struct mlx5_fs_chains;
struct mlx5_tc_ct_priv;
struct mlx5_ct_flow;

struct nf_flowtable;

struct mlx5_ct_attr {
	u16 zone;
	u16 ct_action;
	struct nf_flowtable *nf_ft;
	u32 ct_labels_id;
	u32 act_miss_mapping;
	u64 act_miss_cookie;
	struct mlx5_ct_ft *ft;
};

#define zone_to_reg_ct {\
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_2,\
	.moffset = 0,\
	.mlen = 16,\
	.soffset = MLX5_BYTE_OFF(fte_match_param,\
				 misc_parameters_2.metadata_reg_c_2),\
}

#define ctstate_to_reg_ct {\
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_2,\
	.moffset = 16,\
	.mlen = 16,\
	.soffset = MLX5_BYTE_OFF(fte_match_param,\
				 misc_parameters_2.metadata_reg_c_2),\
}

#define mark_to_reg_ct {\
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_3,\
	.moffset = 0,\
	.mlen = 32,\
	.soffset = MLX5_BYTE_OFF(fte_match_param,\
				 misc_parameters_2.metadata_reg_c_3),\
}

#define labels_to_reg_ct {\
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_4,\
	.moffset = 0,\
	.mlen = 32,\
	.soffset = MLX5_BYTE_OFF(fte_match_param,\
				 misc_parameters_2.metadata_reg_c_4),\
}

/* 8 LSB of metadata C5 are reserved for packet color */
#define fteid_to_reg_ct {\
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_5,\
	.moffset = 8,\
	.mlen = 24,\
	.soffset = MLX5_BYTE_OFF(fte_match_param,\
				 misc_parameters_2.metadata_reg_c_5),\
}

#define zone_restore_to_reg_ct {\
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_C_1,\
	.moffset = 0,\
	.mlen = ESW_ZONE_ID_BITS,\
	.soffset = MLX5_BYTE_OFF(fte_match_param,\
				 misc_parameters_2.metadata_reg_c_1),\
}

#define nic_zone_restore_to_reg_ct {\
	.mfield = MLX5_ACTION_IN_FIELD_METADATA_REG_B,\
	.moffset = 16,\
	.mlen = ESW_ZONE_ID_BITS,\
}

#define MLX5_CT_ZONE_BITS MLX5_REG_MAPPING_MBITS(ZONE_TO_REG)
#define MLX5_CT_ZONE_MASK MLX5_REG_MAPPING_MASK(ZONE_TO_REG)

#if IS_ENABLED(CONFIG_MLX5_TC_CT)

struct mlx5_tc_ct_priv *
mlx5_tc_ct_init(struct mlx5e_priv *priv, struct mlx5_fs_chains *chains,
		struct mod_hdr_tbl *mod_hdr,
		enum mlx5_flow_namespace_type ns_type,
		struct mlx5e_post_act *post_act);
void
mlx5_tc_ct_clean(struct mlx5_tc_ct_priv *ct_priv);

void
mlx5_tc_ct_match_del(struct mlx5_tc_ct_priv *priv, struct mlx5_ct_attr *ct_attr);

int
mlx5_tc_ct_match_add(struct mlx5_tc_ct_priv *priv,
		     struct mlx5_flow_spec *spec,
		     struct flow_cls_offload *f,
		     struct mlx5_ct_attr *ct_attr,
		     struct netlink_ext_ack *extack);
int mlx5_tc_ct_add_no_trk_match(struct mlx5_flow_spec *spec);
int
mlx5_tc_ct_parse_action(struct mlx5_tc_ct_priv *priv,
			struct mlx5_flow_attr *attr,
			const struct flow_action_entry *act,
			struct netlink_ext_ack *extack);

int
mlx5_tc_ct_flow_offload(struct mlx5_tc_ct_priv *priv, struct mlx5_flow_attr *attr);

void
mlx5_tc_ct_delete_flow(struct mlx5_tc_ct_priv *priv,
		       struct mlx5_flow_attr *attr);

bool
mlx5e_tc_ct_restore_flow(struct mlx5_tc_ct_priv *ct_priv,
			 struct sk_buff *skb, u8 zone_restore_id);

#else /* CONFIG_MLX5_TC_CT */

static inline struct mlx5_tc_ct_priv *
mlx5_tc_ct_init(struct mlx5e_priv *priv, struct mlx5_fs_chains *chains,
		struct mod_hdr_tbl *mod_hdr,
		enum mlx5_flow_namespace_type ns_type,
		struct mlx5e_post_act *post_act)
{
	return NULL;
}

static inline void
mlx5_tc_ct_clean(struct mlx5_tc_ct_priv *ct_priv)
{
}

static inline void
mlx5_tc_ct_match_del(struct mlx5_tc_ct_priv *priv, struct mlx5_ct_attr *ct_attr) {}

static inline int
mlx5_tc_ct_match_add(struct mlx5_tc_ct_priv *priv,
		     struct mlx5_flow_spec *spec,
		     struct flow_cls_offload *f,
		     struct mlx5_ct_attr *ct_attr,
		     struct netlink_ext_ack *extack)
{
	struct flow_rule *rule = flow_cls_offload_flow_rule(f);

	if (!flow_rule_match_key(rule, FLOW_DISSECTOR_KEY_CT))
		return 0;

	NL_SET_ERR_MSG_MOD(extack, "mlx5 tc ct offload isn't enabled.");
	return -EOPNOTSUPP;
}

static inline int
mlx5_tc_ct_add_no_trk_match(struct mlx5_flow_spec *spec)
{
	return 0;
}

static inline int
mlx5_tc_ct_parse_action(struct mlx5_tc_ct_priv *priv,
			struct mlx5_flow_attr *attr,
			const struct flow_action_entry *act,
			struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack, "mlx5 tc ct offload isn't enabled.");
	return -EOPNOTSUPP;
}

static inline int
mlx5_tc_ct_flow_offload(struct mlx5_tc_ct_priv *priv,
			struct mlx5_flow_attr *attr)
{
	return -EOPNOTSUPP;
}

static inline void
mlx5_tc_ct_delete_flow(struct mlx5_tc_ct_priv *priv,
		       struct mlx5_flow_attr *attr)
{
}

static inline bool
mlx5e_tc_ct_restore_flow(struct mlx5_tc_ct_priv *ct_priv,
			 struct sk_buff *skb, u8 zone_restore_id)
{
	if (!zone_restore_id)
		return true;

	return false;
}

#endif /* !IS_ENABLED(CONFIG_MLX5_TC_CT) */
#endif /* __MLX5_EN_TC_CT_H__ */
