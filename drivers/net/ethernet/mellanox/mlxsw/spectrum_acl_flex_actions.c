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
					   u16 local_port)
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
mlxsw_sp_act_mirror_add(void *priv, u16 local_in_port,
			const struct net_device *out_dev,
			bool ingress, int *p_span_id)
{
	struct mlxsw_sp_span_agent_parms agent_parms = {};
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp = priv;
	int err;

	agent_parms.to_dev = out_dev;
	err = mlxsw_sp_span_agent_get(mlxsw_sp, p_span_id, &agent_parms);
	if (err)
		return err;

	mlxsw_sp_port = mlxsw_sp->ports[local_in_port];
	err = mlxsw_sp_span_analyzed_port_get(mlxsw_sp_port, ingress);
	if (err)
		goto err_analyzed_port_get;

	return 0;

err_analyzed_port_get:
	mlxsw_sp_span_agent_put(mlxsw_sp, *p_span_id);
	return err;
}

static void
mlxsw_sp_act_mirror_del(void *priv, u16 local_in_port, int span_id, bool ingress)
{
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_port = mlxsw_sp->ports[local_in_port];
	mlxsw_sp_span_analyzed_port_put(mlxsw_sp_port, ingress);
	mlxsw_sp_span_agent_put(mlxsw_sp, span_id);
}

static int mlxsw_sp_act_policer_add(void *priv, u64 rate_bytes_ps, u32 burst,
				    u16 *p_policer_index,
				    struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_policer_params params;
	struct mlxsw_sp *mlxsw_sp = priv;

	params.rate = rate_bytes_ps;
	params.burst = burst;
	params.bytes = true;
	return mlxsw_sp_policer_add(mlxsw_sp,
				    MLXSW_SP_POLICER_TYPE_SINGLE_RATE,
				    &params, extack, p_policer_index);
}

static void mlxsw_sp_act_policer_del(void *priv, u16 policer_index)
{
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_policer_del(mlxsw_sp, MLXSW_SP_POLICER_TYPE_SINGLE_RATE,
			     policer_index);
}

static int mlxsw_sp1_act_sampler_add(void *priv, u16 local_port,
				     struct psample_group *psample_group,
				     u32 rate, u32 trunc_size, bool truncate,
				     bool ingress, int *p_span_id,
				     struct netlink_ext_ack *extack)
{
	NL_SET_ERR_MSG_MOD(extack, "Sampling action is not supported on Spectrum-1");
	return -EOPNOTSUPP;
}

static void mlxsw_sp1_act_sampler_del(void *priv, u16 local_port, int span_id,
				      bool ingress)
{
	WARN_ON_ONCE(1);
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
	.policer_add		= mlxsw_sp_act_policer_add,
	.policer_del		= mlxsw_sp_act_policer_del,
	.sampler_add		= mlxsw_sp1_act_sampler_add,
	.sampler_del		= mlxsw_sp1_act_sampler_del,
};

static int mlxsw_sp2_act_sampler_add(void *priv, u16 local_port,
				     struct psample_group *psample_group,
				     u32 rate, u32 trunc_size, bool truncate,
				     bool ingress, int *p_span_id,
				     struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_span_agent_parms agent_parms = {
		.session_id = MLXSW_SP_SPAN_SESSION_ID_SAMPLING,
	};
	struct mlxsw_sp_sample_trigger trigger = {
		.type = MLXSW_SP_SAMPLE_TRIGGER_TYPE_POLICY_ENGINE,
	};
	struct mlxsw_sp_sample_params params;
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp = priv;
	int err;

	params.psample_group = psample_group;
	params.trunc_size = trunc_size;
	params.rate = rate;
	params.truncate = truncate;
	err = mlxsw_sp_sample_trigger_params_set(mlxsw_sp, &trigger, &params,
						 extack);
	if (err)
		return err;

	err = mlxsw_sp_span_agent_get(mlxsw_sp, p_span_id, &agent_parms);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to get SPAN agent");
		goto err_span_agent_get;
	}

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	err = mlxsw_sp_span_analyzed_port_get(mlxsw_sp_port, ingress);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to get analyzed port");
		goto err_analyzed_port_get;
	}

	return 0;

err_analyzed_port_get:
	mlxsw_sp_span_agent_put(mlxsw_sp, *p_span_id);
err_span_agent_get:
	mlxsw_sp_sample_trigger_params_unset(mlxsw_sp, &trigger);
	return err;
}

static void mlxsw_sp2_act_sampler_del(void *priv, u16 local_port, int span_id,
				      bool ingress)
{
	struct mlxsw_sp_sample_trigger trigger = {
		.type = MLXSW_SP_SAMPLE_TRIGGER_TYPE_POLICY_ENGINE,
	};
	struct mlxsw_sp_port *mlxsw_sp_port;
	struct mlxsw_sp *mlxsw_sp = priv;

	mlxsw_sp_port = mlxsw_sp->ports[local_port];
	mlxsw_sp_span_analyzed_port_put(mlxsw_sp_port, ingress);
	mlxsw_sp_span_agent_put(mlxsw_sp, span_id);
	mlxsw_sp_sample_trigger_params_unset(mlxsw_sp, &trigger);
}

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
	.policer_add		= mlxsw_sp_act_policer_add,
	.policer_del		= mlxsw_sp_act_policer_del,
	.sampler_add		= mlxsw_sp2_act_sampler_add,
	.sampler_del		= mlxsw_sp2_act_sampler_del,
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
