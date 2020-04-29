// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include "spectrum_acl_flex_actions.h"
#include "core_acl_flex_actions.h"
#include "spectrum_span.h"

static int mlxsw_sp_act_kvdl_set_add(void *priv, u32 *p_kvdl_index,
				     char *enc_actions, bool is_first, bool ca)
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

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET,
				  1, &kvdl_index);
	if (err)
		return err;
	mlxsw_reg_pefa_pack(pefa_pl, kvdl_index, ca, enc_actions);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(pefa), pefa_pl);
	if (err)
		goto err_pefa_write;
	*p_kvdl_index = kvdl_index;
	return 0;

err_pefa_write:
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET,
			   1, kvdl_index);
	return err;
}

static int mlxsw_sp1_act_kvdl_set_add(void *priv, u32 *p_kvdl_index,
				      char *enc_actions, bool is_first)
{
	return mlxsw_sp_act_kvdl_set_add(priv, p_kvdl_index, enc_actions,
					 is_first, false);
}

static int mlxsw_sp2_act_kvdl_set_add(void *priv, u32 *p_kvdl_index,
				      char *enc_actions, bool is_first)
{
	return mlxsw_sp_act_kvdl_set_add(priv, p_kvdl_index, enc_actions,
					 is_first, true);
}

static void mlxsw_sp_act_kvdl_set_del(void *priv, u32 kvdl_index,
				      bool is_first)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	if (is_first)
		return;
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ACTSET,
			   1, kvdl_index);
}

static int mlxsw_sp1_act_kvdl_set_activity_get(void *priv, u32 kvdl_index,
					       bool *activity)
{
	return -EOPNOTSUPP;
}

static int mlxsw_sp2_act_kvdl_set_activity_get(void *priv, u32 kvdl_index,
					       bool *activity)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	char pefa_pl[MLXSW_REG_PEFA_LEN];
	int err;

	mlxsw_reg_pefa_pack(pefa_pl, kvdl_index, true, NULL);
	err = mlxsw_reg_query(mlxsw_sp->core, MLXSW_REG(pefa), pefa_pl);
	if (err)
		return err;
	mlxsw_reg_pefa_unpack(pefa_pl, activity);
	return 0;
}

static int mlxsw_sp_act_kvdl_fwd_entry_add(void *priv, u32 *p_kvdl_index,
					   u8 local_port)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	char ppbs_pl[MLXSW_REG_PPBS_LEN];
	u32 kvdl_index;
	int err;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_PBS,
				  1, &kvdl_index);
	if (err)
		return err;
	mlxsw_reg_ppbs_pack(ppbs_pl, kvdl_index, local_port);
	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(ppbs), ppbs_pl);
	if (err)
		goto err_ppbs_write;
	*p_kvdl_index = kvdl_index;
	return 0;

err_ppbs_write:
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_PBS,
			   1, kvdl_index);
	return err;
}

static void mlxsw_sp_act_kvdl_fwd_entry_del(void *priv, u32 kvdl_index)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_PBS,
			   1, kvdl_index);
}

static int
mlxsw_sp_act_counter_index_get(void *priv, unsigned int *p_counter_index)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	return mlxsw_sp_flow_counter_alloc(mlxsw_sp, p_counter_index);
}

static void
mlxsw_sp_act_counter_index_put(void *priv, unsigned int counter_index)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_flow_counter_free(mlxsw_sp, counter_index);
}

static int
mlxsw_sp_act_mirror_add(void *priv, u8 local_in_port,
			const struct net_device *out_dev,
			bool ingress, int *p_span_id)
{
	struct mlxsw_sp_port *in_port;
	struct mlxsw_sp *mlxsw_sp = priv;
	enum mlxsw_sp_span_type type;

	type = ingress ? MLXSW_SP_SPAN_INGRESS : MLXSW_SP_SPAN_EGRESS;
	in_port = mlxsw_sp->ports[local_in_port];

	return mlxsw_sp_span_mirror_add(in_port, out_dev, type,
					false, p_span_id);
}

static void
mlxsw_sp_act_mirror_del(void *priv, u8 local_in_port, int span_id, bool ingress)
{
	struct mlxsw_sp *mlxsw_sp = priv;
	struct mlxsw_sp_port *in_port;
	enum mlxsw_sp_span_type type;

	type = ingress ? MLXSW_SP_SPAN_INGRESS : MLXSW_SP_SPAN_EGRESS;
	in_port = mlxsw_sp->ports[local_in_port];

	mlxsw_sp_span_mirror_del(in_port, span_id, type, false);
}

const struct mlxsw_afa_ops mlxsw_sp1_act_afa_ops = {
	.kvdl_set_add		= mlxsw_sp1_act_kvdl_set_add,
	.kvdl_set_del		= mlxsw_sp_act_kvdl_set_del,
	.kvdl_set_activity_get	= mlxsw_sp1_act_kvdl_set_activity_get,
	.kvdl_fwd_entry_add	= mlxsw_sp_act_kvdl_fwd_entry_add,
	.kvdl_fwd_entry_del	= mlxsw_sp_act_kvdl_fwd_entry_del,
	.counter_index_get	= mlxsw_sp_act_counter_index_get,
	.counter_index_put	= mlxsw_sp_act_counter_index_put,
	.mirror_add		= mlxsw_sp_act_mirror_add,
	.mirror_del		= mlxsw_sp_act_mirror_del,
};

const struct mlxsw_afa_ops mlxsw_sp2_act_afa_ops = {
	.kvdl_set_add		= mlxsw_sp2_act_kvdl_set_add,
	.kvdl_set_del		= mlxsw_sp_act_kvdl_set_del,
	.kvdl_set_activity_get	= mlxsw_sp2_act_kvdl_set_activity_get,
	.kvdl_fwd_entry_add	= mlxsw_sp_act_kvdl_fwd_entry_add,
	.kvdl_fwd_entry_del	= mlxsw_sp_act_kvdl_fwd_entry_del,
	.counter_index_get	= mlxsw_sp_act_counter_index_get,
	.counter_index_put	= mlxsw_sp_act_counter_index_put,
	.mirror_add		= mlxsw_sp_act_mirror_add,
	.mirror_del		= mlxsw_sp_act_mirror_del,
	.dummy_first_set	= true,
};

int mlxsw_sp_afa_init(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_sp->afa = mlxsw_afa_create(MLXSW_CORE_RES_GET(mlxsw_sp->core,
							    ACL_ACTIONS_PER_SET),
					 mlxsw_sp->afa_ops, mlxsw_sp);
	return PTR_ERR_OR_ZERO(mlxsw_sp->afa);
}

void mlxsw_sp_afa_fini(struct mlxsw_sp *mlxsw_sp)
{
	mlxsw_afa_destroy(mlxsw_sp->afa);
}
