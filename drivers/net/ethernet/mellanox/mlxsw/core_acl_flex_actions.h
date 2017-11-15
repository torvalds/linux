/*
 * drivers/net/ethernet/mellanox/mlxsw/core_acl_flex_actions.h
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the names of the copyright holders nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MLXSW_CORE_ACL_FLEX_ACTIONS_H
#define _MLXSW_CORE_ACL_FLEX_ACTIONS_H

#include <linux/types.h>

struct mlxsw_afa;
struct mlxsw_afa_block;

struct mlxsw_afa_ops {
	int (*kvdl_set_add)(void *priv, u32 *p_kvdl_index,
			    char *enc_actions, bool is_first);
	void (*kvdl_set_del)(void *priv, u32 kvdl_index, bool is_first);
	int (*kvdl_fwd_entry_add)(void *priv, u32 *p_kvdl_index, u8 local_port);
	void (*kvdl_fwd_entry_del)(void *priv, u32 kvdl_index);
};

struct mlxsw_afa *mlxsw_afa_create(unsigned int max_acts_per_set,
				   const struct mlxsw_afa_ops *ops,
				   void *ops_priv);
void mlxsw_afa_destroy(struct mlxsw_afa *mlxsw_afa);
struct mlxsw_afa_block *mlxsw_afa_block_create(struct mlxsw_afa *mlxsw_afa);
void mlxsw_afa_block_destroy(struct mlxsw_afa_block *block);
int mlxsw_afa_block_commit(struct mlxsw_afa_block *block);
char *mlxsw_afa_block_first_set(struct mlxsw_afa_block *block);
u32 mlxsw_afa_block_first_set_kvdl_index(struct mlxsw_afa_block *block);
int mlxsw_afa_block_continue(struct mlxsw_afa_block *block);
int mlxsw_afa_block_jump(struct mlxsw_afa_block *block, u16 group_id);
int mlxsw_afa_block_append_drop(struct mlxsw_afa_block *block);
int mlxsw_afa_block_append_trap(struct mlxsw_afa_block *block, u16 trap_id);
int mlxsw_afa_block_append_trap_and_forward(struct mlxsw_afa_block *block,
					    u16 trap_id);
int mlxsw_afa_block_append_fwd(struct mlxsw_afa_block *block,
			       u8 local_port, bool in_port);
int mlxsw_afa_block_append_vlan_modify(struct mlxsw_afa_block *block,
				       u16 vid, u8 pcp, u8 et);
int mlxsw_afa_block_append_counter(struct mlxsw_afa_block *block,
				   u32 counter_index);
int mlxsw_afa_block_append_fid_set(struct mlxsw_afa_block *block, u16 fid);
int mlxsw_afa_block_append_mcrouter(struct mlxsw_afa_block *block,
				    u16 expected_irif, u16 min_mtu,
				    bool rmid_valid, u32 kvdl_index);

#endif
