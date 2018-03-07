/*
 * drivers/net/ethernet/mellanox/mlxsw/spectrum_acl_flex_actions.c
 * Copyright (c) 2017 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2017 Jiri Pirko <jiri@mellanox.com>
 * Copyright (c) 2017 Yotam Gigi <yotamg@mellanox.com>
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

#include "spectrum_acl_flex_actions.h"
#include "core_acl_flex_actions.h"

#define MLXSW_SP_KVDL_ACT_EXT_SIZE 1

static int mlxsw_sp_act_kvdl_set_add(void *priv, u32 *p_kvdl_index,
				     char *enc_actions, bool is_first)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	char pefa_pl[MLXSW_REG_PEFA_LEN];
	u32 kvdl_index;
	int err;

	/* The first action set of a TCAM entry is stored directly in TCAM,
	 * not KVD linear area.
	 */
	if (is_first)
		return 0;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ACT_EXT_SIZE,
				  &kvdl_index);
	if (err)
		return err;
	mlxsw_reg_pefa_pack(pefa_pl, kvdl_index, enc_actions);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pefa), pefa_pl);
	if (err)
		goto err_pefa_write;
	*p_kvdl_index = kvdl_index;
	return 0;

err_pefa_write:
	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
	return err;
}

static void mlxsw_sp_act_kvdl_set_del(void *priv, u32 kvdl_index,
				      bool is_first)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	if (is_first)
		return;
	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
}

static int mlxsw_sp_act_kvdl_fwd_entry_add(void *priv, u32 *p_kvdl_index,
					   u8 local_port)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	char ppbs_pl[MLXSW_REG_PPBS_LEN];
	u32 kvdl_index;
	int err;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, 1, &kvdl_index);
	if (err)
		return err;
	mlxsw_reg_ppbs_pack(ppbs_pl, kvdl_index, local_port);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppbs), ppbs_pl);
	if (err)
		goto err_ppbs_write;
	*p_kvdl_index = kvdl_index;
	return 0;

err_ppbs_write:
	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
	return err;
}

static void mlxsw_sp_act_kvdl_fwd_entry_del(void *priv, u32 kvdl_index)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_kvdl_free(mlxsw_sp, kvdl_index);
}

static const struct mlxsw_afa_ops mlxsw_sp_act_afa_ops = {
	.kvdl_set_add		= mlxsw_sp_act_kvdl_set_add,
	.kvdl_set_del		= mlxsw_sp_act_kvdl_set_del,
	.kvdl_fwd_entry_add	= mlxsw_sp_act_kvdl_fwd_entry_add,
	.kvdl_fwd_entry_del	= mlxsw_sp_act_kvdl_fwd_entry_del,
};

int mlxsw_sp_afa_init(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp->afa = mlxsw_afa_create(MLXSW_CORE_RES_GET(mlxsw_sp->core,
							    ACL_ACTIONS_PER_SET),
					 &mlxsw_sp_act_afa_ops, mlxsw_sp);
	return PTR_ERR_OR_ZERO(mlxsw_sp->afa);
}

void mlxsw_sp_afa_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_afa_destroy(mlxsw_sp->afa);
}
