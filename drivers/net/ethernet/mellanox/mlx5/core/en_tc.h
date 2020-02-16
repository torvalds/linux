/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __MLX5_EN_TC_H__
#define __MLX5_EN_TC_H__

#include <net/pkt_cls.h>

#define MLX5E_TC_FLOW_ID_MASK 0x0000ffff

#ifdef CONFIG_MLX5_ESWITCH

enum {
	MLX5E_TC_FLAG_INGRESS_BIT,
	MLX5E_TC_FLAG_EGRESS_BIT,
	MLX5E_TC_FLAG_NIC_OFFLOAD_BIT,
	MLX5E_TC_FLAG_ESW_OFFLOAD_BIT,
	MLX5E_TC_FLAG_FT_OFFLOAD_BIT,
	MLX5E_TC_FLAG_LAST_EXPORTED_BIT = MLX5E_TC_FLAG_FT_OFFLOAD_BIT,
};

#define MLX5_TC_FLAG(flag) BIT(MLX5E_TC_FLAG_##flag##_BIT)

int mlx5e_tc_nic_init(struct mlx5e_priv *priv);
void mlx5e_tc_nic_cleanup(struct mlx5e_priv *priv);

int mlx5e_tc_esw_init(struct rhashtable *tc_ht);
void mlx5e_tc_esw_cleanup(struct rhashtable *tc_ht);

int mlx5e_configure_flower(struct net_device *dev, struct mlx5e_priv *priv,
			   struct flow_cls_offload *f, unsigned long flags);
int mlx5e_delete_flower(struct net_device *dev, struct mlx5e_priv *priv,
			struct flow_cls_offload *f, unsigned long flags);

int mlx5e_stats_flower(struct net_device *dev, struct mlx5e_priv *priv,
		       struct flow_cls_offload *f, unsigned long flags);

int mlx5e_tc_configure_matchall(struct mlx5e_priv *priv,
				struct tc_cls_matchall_offload *f);
int mlx5e_tc_delete_matchall(struct mlx5e_priv *priv,
			     struct tc_cls_matchall_offload *f);
void mlx5e_tc_stats_matchall(struct mlx5e_priv *priv,
			     struct tc_cls_matchall_offload *ma);

struct mlx5e_encap_entry;
void mlx5e_tc_encap_flows_add(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list);
void mlx5e_tc_encap_flows_del(struct mlx5e_priv *priv,
			      struct mlx5e_encap_entry *e,
			      struct list_head *flow_list);
bool mlx5e_encap_take(struct mlx5e_encap_entry *e);
void mlx5e_encap_put(struct mlx5e_priv *priv, struct mlx5e_encap_entry *e);

void mlx5e_take_all_encap_flows(struct mlx5e_encap_entry *e, struct list_head *flow_list);
void mlx5e_put_encap_flow_list(struct mlx5e_priv *priv, struct list_head *flow_list);

struct mlx5e_neigh_hash_entry;
void mlx5e_tc_update_neigh_used_value(struct mlx5e_neigh_hash_entry *nhe);

int mlx5e_tc_num_filters(struct mlx5e_priv *priv, unsigned long flags);

void mlx5e_tc_reoffload_flows_work(struct work_struct *work);

enum mlx5e_tc_attr_to_reg {
	CHAIN_TO_REG,
	TUNNEL_TO_REG,
};

struct mlx5e_tc_attr_to_reg_mapping {
	int mfield; /* rewrite field */
	int moffset; /* offset of mfield */
	int mlen; /* bytes to rewrite/match */

	int soffset; /* offset of spec for match */
};

extern struct mlx5e_tc_attr_to_reg_mapping mlx5e_tc_attr_to_reg_mappings[];

bool mlx5e_is_valid_eswitch_fwd_dev(struct mlx5e_priv *priv,
				    struct net_device *out_dev);

struct mlx5e_tc_update_priv {
	struct net_device *tun_dev;
};

bool mlx5e_tc_rep_update_skb(struct mlx5_cqe64 *cqe, struct sk_buff *skb,
			     struct mlx5e_tc_update_priv *tc_priv);

void mlx5_tc_rep_post_napi_receive(struct mlx5e_tc_update_priv *tc_priv);

struct mlx5e_tc_mod_hdr_acts {
	int num_actions;
	int max_actions;
	void *actions;
};

int mlx5e_tc_match_to_reg_set(struct mlx5_core_dev *mdev,
			      struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts,
			      enum mlx5e_tc_attr_to_reg type,
			      u32 data);

void mlx5e_tc_match_to_reg_match(struct mlx5_flow_spec *spec,
				 enum mlx5e_tc_attr_to_reg type,
				 u32 data,
				 u32 mask);

int alloc_mod_hdr_actions(struct mlx5_core_dev *mdev,
			  int namespace,
			  struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts);
void dealloc_mod_hdr_actions(struct mlx5e_tc_mod_hdr_acts *mod_hdr_acts);

#else /* CONFIG_MLX5_ESWITCH */
static inline int  mlx5e_tc_nic_init(struct mlx5e_priv *priv) { return 0; }
static inline void mlx5e_tc_nic_cleanup(struct mlx5e_priv *priv) {}
static inline int  mlx5e_tc_num_filters(struct mlx5e_priv *priv,
					unsigned long flags)
{
	return 0;
}
#endif

#endif /* __MLX5_EN_TC_H__ */
