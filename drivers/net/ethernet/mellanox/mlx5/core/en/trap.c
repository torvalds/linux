// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies */

#include "en/txrx.h"
#include "en/params.h"
#include "en/trap.h"

static int mlx5e_trap_napi_poll(struct napi_struct *napi, int budget)
{
	struct mlx5e_trap *trap_ctx = container_of(napi, struct mlx5e_trap, napi);
	struct mlx5e_ch_stats *ch_stats = trap_ctx->stats;
	struct mlx5e_rq *rq = &trap_ctx->rq;
	bool busy = false;
	int work_done = 0;

	rcu_read_lock();

	ch_stats->poll++;

	work_done = mlx5e_poll_rx_cq(&rq->cq, budget);
	busy |= work_done == budget;
	busy |= rq->post_wqes(rq);

	if (busy) {
		work_done = budget;
		goto out;
	}

	if (unlikely(!napi_complete_done(napi, work_done)))
		goto out;

	mlx5e_cq_arm(&rq->cq);

out:
	rcu_read_unlock();
	return work_done;
}

static void mlx5e_init_trap_rq(struct mlx5e_trap *t, struct mlx5e_params *params,
			       struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = t->mdev;
	struct mlx5e_priv *priv = t->priv;

	rq->wq_type      = params->rq_wq_type;
	rq->pdev         = t->pdev;
	rq->netdev       = priv->netdev;
	rq->priv         = priv;
	rq->clock        = mdev->clock;
	rq->tstamp       = &priv->tstamp;
	rq->mdev         = mdev;
	rq->hw_mtu       = MLX5E_SW2HW_MTU(params, params->sw_mtu);
	rq->stats        = &priv->trap_stats.rq;
	rq->ptp_cyc2time = mlx5_rq_ts_translator(mdev);
	xdp_rxq_info_unused(&rq->xdp_rxq);
	mlx5e_rq_set_trap_handlers(rq, params);
}

static int mlx5e_open_trap_rq(struct mlx5e_priv *priv, struct mlx5e_trap *t)
{
	struct mlx5e_rq_param *rq_param = &t->rq_param;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_create_cq_param ccp = {};
	struct dim_cq_moder trap_moder = {};
	struct mlx5e_rq *rq = &t->rq;
	u16 q_counter;
	int node;
	int err;

	node = dev_to_node(mdev->device);
	q_counter = priv->q_counter[0];

	ccp.netdev   = priv->netdev;
	ccp.wq       = priv->wq;
	ccp.node     = node;
	ccp.ch_stats = t->stats;
	ccp.napi     = &t->napi;
	ccp.ix       = 0;
	ccp.uar      = mdev->priv.bfreg.up;
	err = mlx5e_open_cq(priv->mdev, trap_moder, &rq_param->cqp, &ccp, &rq->cq);
	if (err)
		return err;

	mlx5e_init_trap_rq(t, &t->params, rq);
	err = mlx5e_open_rq(&t->params, rq_param, NULL, node, q_counter, rq);
	if (err)
		goto err_destroy_cq;

	return 0;

err_destroy_cq:
	mlx5e_close_cq(&rq->cq);

	return err;
}

static void mlx5e_close_trap_rq(struct mlx5e_rq *rq)
{
	mlx5e_close_rq(rq);
	mlx5e_close_cq(&rq->cq);
}

static int mlx5e_create_trap_direct_rq_tir(struct mlx5_core_dev *mdev, struct mlx5e_tir *tir,
					   u32 rqn)
{
	struct mlx5e_tir_builder *builder;
	int err;

	builder = mlx5e_tir_builder_alloc(false);
	if (!builder)
		return -ENOMEM;

	mlx5e_tir_builder_build_inline(builder, mdev->mlx5e_res.hw_objs.td.tdn, rqn);
	err = mlx5e_tir_init(tir, builder, mdev, true);

	mlx5e_tir_builder_free(builder);

	return err;
}

static void mlx5e_build_trap_params(struct mlx5_core_dev *mdev,
				    int max_mtu, struct mlx5e_trap *t)
{
	struct mlx5e_params *params = &t->params;

	params->rq_wq_type = MLX5_WQ_TYPE_CYCLIC;
	mlx5e_init_rq_type_params(mdev, params);
	params->sw_mtu = max_mtu;
	mlx5e_build_rq_param(mdev, params, NULL, &t->rq_param);
}

static struct mlx5e_trap *mlx5e_open_trap(struct mlx5e_priv *priv)
{
	int cpu = mlx5_comp_vector_get_cpu(priv->mdev, 0);
	struct net_device *netdev = priv->netdev;
	struct mlx5e_trap *t;
	int err;

	t = kvzalloc_node(sizeof(*t), GFP_KERNEL, cpu_to_node(cpu));
	if (!t)
		return ERR_PTR(-ENOMEM);

	mlx5e_build_trap_params(priv->mdev, netdev->max_mtu, t);

	t->priv     = priv;
	t->mdev     = priv->mdev;
	t->tstamp   = &priv->tstamp;
	t->pdev     = mlx5_core_dma_dev(priv->mdev);
	t->netdev   = priv->netdev;
	t->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.hw_objs.mkey);
	t->stats    = &priv->trap_stats.ch;

	netif_napi_add_locked(netdev, &t->napi, mlx5e_trap_napi_poll);

	err = mlx5e_open_trap_rq(priv, t);
	if (unlikely(err))
		goto err_napi_del;

	err = mlx5e_create_trap_direct_rq_tir(t->mdev, &t->tir, t->rq.rqn);
	if (err)
		goto err_close_trap_rq;

	return t;

err_close_trap_rq:
	mlx5e_close_trap_rq(&t->rq);
err_napi_del:
	netif_napi_del_locked(&t->napi);
	kvfree(t);
	return ERR_PTR(err);
}

void mlx5e_close_trap(struct mlx5e_trap *trap)
{
	mlx5e_tir_destroy(&trap->tir);
	mlx5e_close_trap_rq(&trap->rq);
	netif_napi_del_locked(&trap->napi);
	kvfree(trap);
}

static void mlx5e_activate_trap(struct mlx5e_trap *trap)
{
	napi_enable_locked(&trap->napi);
	mlx5e_activate_rq(&trap->rq);
	mlx5e_trigger_napi_sched(&trap->napi);
}

void mlx5e_deactivate_trap(struct mlx5e_priv *priv)
{
	struct mlx5e_trap *trap = priv->en_trap;

	mlx5e_deactivate_rq(&trap->rq);
	napi_disable_locked(&trap->napi);
}

static struct mlx5e_trap *mlx5e_add_trap_queue(struct mlx5e_priv *priv)
{
	struct mlx5e_trap *trap;

	trap = mlx5e_open_trap(priv);
	if (IS_ERR(trap))
		goto out;

	mlx5e_activate_trap(trap);
out:
	return trap;
}

static void mlx5e_del_trap_queue(struct mlx5e_priv *priv)
{
	mlx5e_deactivate_trap(priv);
	mlx5e_close_trap(priv->en_trap);
	priv->en_trap = NULL;
}

static int mlx5e_trap_get_tirn(struct mlx5e_trap *en_trap)
{
	return en_trap->tir.tirn;
}

static int mlx5e_handle_action_trap(struct mlx5e_priv *priv, int trap_id)
{
	bool open_queue = !priv->en_trap;
	struct mlx5e_trap *trap;
	int err;

	if (open_queue) {
		trap = mlx5e_add_trap_queue(priv);
		if (IS_ERR(trap))
			return PTR_ERR(trap);
		priv->en_trap = trap;
	}

	switch (trap_id) {
	case DEVLINK_TRAP_GENERIC_ID_INGRESS_VLAN_FILTER:
		err = mlx5e_add_vlan_trap(priv->fs, trap_id, mlx5e_trap_get_tirn(priv->en_trap));
		if (err)
			goto err_out;
		break;
	case DEVLINK_TRAP_GENERIC_ID_DMAC_FILTER:
		err = mlx5e_add_mac_trap(priv->fs, trap_id, mlx5e_trap_get_tirn(priv->en_trap));
		if (err)
			goto err_out;
		break;
	default:
		netdev_warn(priv->netdev, "%s: Unknown trap id %d\n", __func__, trap_id);
		err = -EINVAL;
		goto err_out;
	}
	return 0;

err_out:
	if (open_queue)
		mlx5e_del_trap_queue(priv);
	return err;
}

static int mlx5e_handle_action_drop(struct mlx5e_priv *priv, int trap_id)
{
	switch (trap_id) {
	case DEVLINK_TRAP_GENERIC_ID_INGRESS_VLAN_FILTER:
		mlx5e_remove_vlan_trap(priv->fs);
		break;
	case DEVLINK_TRAP_GENERIC_ID_DMAC_FILTER:
		mlx5e_remove_mac_trap(priv->fs);
		break;
	default:
		netdev_warn(priv->netdev, "%s: Unknown trap id %d\n", __func__, trap_id);
		return -EINVAL;
	}
	if (priv->en_trap && !mlx5_devlink_trap_get_num_active(priv->mdev))
		mlx5e_del_trap_queue(priv);

	return 0;
}

int mlx5e_handle_trap_event(struct mlx5e_priv *priv, struct mlx5_trap_ctx *trap_ctx)
{
	int err = 0;

	/* Traps are unarmed when interface is down, no need to update
	 * them. The configuration is saved in the core driver,
	 * queried and applied upon interface up operation in
	 * mlx5e_open_locked().
	 */
	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		return 0;

	netdev_lock(priv->netdev);
	switch (trap_ctx->action) {
	case DEVLINK_TRAP_ACTION_TRAP:
		err = mlx5e_handle_action_trap(priv, trap_ctx->id);
		break;
	case DEVLINK_TRAP_ACTION_DROP:
		err = mlx5e_handle_action_drop(priv, trap_ctx->id);
		break;
	default:
		netdev_warn(priv->netdev, "%s: Unsupported action %d\n", __func__,
			    trap_ctx->action);
		err = -EINVAL;
	}
	netdev_unlock(priv->netdev);
	return err;
}

static int mlx5e_apply_trap(struct mlx5e_priv *priv, int trap_id, bool enable)
{
	enum devlink_trap_action action;
	int err;

	err = mlx5_devlink_traps_get_action(priv->mdev, trap_id, &action);
	if (err)
		return err;
	if (action == DEVLINK_TRAP_ACTION_TRAP)
		err = enable ? mlx5e_handle_action_trap(priv, trap_id) :
			       mlx5e_handle_action_drop(priv, trap_id);
	return err;
}

static const int mlx5e_traps_arr[] = {
	DEVLINK_TRAP_GENERIC_ID_INGRESS_VLAN_FILTER,
	DEVLINK_TRAP_GENERIC_ID_DMAC_FILTER,
};

int mlx5e_apply_traps(struct mlx5e_priv *priv, bool enable)
{
	int err;
	int i;

	for (i = 0; i < ARRAY_SIZE(mlx5e_traps_arr); i++) {
		err = mlx5e_apply_trap(priv, mlx5e_traps_arr[i], enable);
		if (err)
			return err;
	}
	return 0;
}
