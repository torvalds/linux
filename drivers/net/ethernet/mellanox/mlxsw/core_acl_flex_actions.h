/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_CORE_ACL_FLEX_ACTIONS_H
#define _MLXSW_CORE_ACL_FLEX_ACTIONS_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <net/flow_offload.h>

struct mlxsw_afa;
struct mlxsw_afa_block;

struct mlxsw_afa_ops {
	int (*kvdl_set_add)(void *priv, u32 *p_kvdl_index,
			    char *enc_actions, bool is_first);
	void (*kvdl_set_del)(void *priv, u32 kvdl_index, bool is_first);
	int (*kvdl_set_activity_get)(void *priv, u32 kvdl_index,
				     bool *activity);
	int (*kvdl_fwd_entry_add)(void *priv, u32 *p_kvdl_index, u8 local_port);
	void (*kvdl_fwd_entry_del)(void *priv, u32 kvdl_index);
	int (*counter_index_get)(void *priv, unsigned int *p_counter_index);
	void (*counter_index_put)(void *priv, unsigned int counter_index);
	int (*mirror_add)(void *priv, u8 local_in_port,
			  const struct net_device *out_dev,
			  bool ingress, int *p_span_id);
	void (*mirror_del)(void *priv, u8 local_in_port, int span_id,
			   bool ingress);
	bool dummy_first_set;
};

struct mlxsw_afa *mlxsw_afa_create(unsigned int max_acts_per_set,
				   const struct mlxsw_afa_ops *ops,
				   void *ops_priv);
void mlxsw_afa_destroy(struct mlxsw_afa *mlxsw_afa);
struct mlxsw_afa_block *mlxsw_afa_block_create(struct mlxsw_afa *mlxsw_afa);
void mlxsw_afa_block_destroy(struct mlxsw_afa_block *block);
int mlxsw_afa_block_commit(struct mlxsw_afa_block *block);
char *mlxsw_afa_block_first_set(struct mlxsw_afa_block *block);
char *mlxsw_afa_block_cur_set(struct mlxsw_afa_block *block);
u32 mlxsw_afa_block_first_kvdl_index(struct mlxsw_afa_block *block);
int mlxsw_afa_block_activity_get(struct mlxsw_afa_block *block, bool *activity);
int mlxsw_afa_block_continue(struct mlxsw_afa_block *block);
int mlxsw_afa_block_jump(struct mlxsw_afa_block *block, u16 group_id);
int mlxsw_afa_block_terminate(struct mlxsw_afa_block *block);
const struct flow_action_cookie *
mlxsw_afa_cookie_lookup(struct mlxsw_afa *mlxsw_afa, u32 cookie_index);
int mlxsw_afa_block_append_drop(struct mlxsw_afa_block *block, bool ingress,
				const struct flow_action_cookie *fa_cookie,
				struct netlink_ext_ack *extack);
int mlxsw_afa_block_append_trap(struct mlxsw_afa_block *block, u16 trap_id);
int mlxsw_afa_block_append_trap_and_forward(struct mlxsw_afa_block *block,
					    u16 trap_id);
int mlxsw_afa_block_append_mirror(struct mlxsw_afa_block *block,
				  u8 local_in_port,
				  const struct net_device *out_dev,
				  bool ingress,
				  struct netlink_ext_ack *extack);
int mlxsw_afa_block_append_fwd(struct mlxsw_afa_block *block,
			       u8 local_port, bool in_port,
			       struct netlink_ext_ack *extack);
int mlxsw_afa_block_append_vlan_modify(struct mlxsw_afa_block *block,
				       u16 vid, u8 pcp, u8 et,
				       struct netlink_ext_ack *extack);
int mlxsw_afa_block_append_allocated_counter(struct mlxsw_afa_block *block,
					     u32 counter_index);
int mlxsw_afa_block_append_counter(struct mlxsw_afa_block *block,
				   u32 *p_counter_index,
				   struct netlink_ext_ack *extack);
int mlxsw_afa_block_append_fid_set(struct mlxsw_afa_block *block, u16 fid,
				   struct netlink_ext_ack *extack);
int mlxsw_afa_block_append_mcrouter(struct mlxsw_afa_block *block,
				    u16 expected_irif, u16 min_mtu,
				    bool rmid_valid, u32 kvdl_index);

#endif
