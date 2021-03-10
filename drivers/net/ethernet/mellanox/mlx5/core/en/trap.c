// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies */

#include <net/page_pool.h>
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

	ch_stats->poll++;

	work_done = mlx5e_poll_rx_cq(&rq->cq, budget);
	busy |= work_done == budget;
	busy |= rq->post_wqes(rq);

	if (busy)
		return budget;

	if (unlikely(!napi_complete_done(napi, work_done)))
		return work_done;

	mlx5e_cq_arm(&rq->cq);
	return work_done;
}

static int mlx5e_alloc_trap_rq(struct mlx5e_priv *priv, struct mlx5e_rq_param *rqp,
			       struct mlx5e_rq_stats *stats, struct mlx5e_params *params,
			       struct mlx5e_ch_stats *ch_stats,
			       struct mlx5e_rq *rq)
{
	void *rqc_wq = MLX5_ADDR_OF(rqc, rqp->rqc, wq);
	struct mlx5_core_dev *mdev = priv->mdev;
	struct page_pool_params pp_params = {};
	int node = dev_to_node(mdev->device);
	u32 pool_size;
	int wq_sz;
	int err;
	int i;

	rqp->wq.db_numa_node = node;

	rq->wq_type  = params->rq_wq_type;
	rq->pdev     = mdev->device;
	rq->netdev   = priv->netdev;
	rq->mdev     = mdev;
	rq->priv     = priv;
	rq->stats    = stats;
	rq->clock    = &mdev->clock;
	rq->tstamp   = &priv->tstamp;
	rq->hw_mtu   = MLX5E_SW2HW_MTU(params, params->sw_mtu);

	xdp_rxq_info_unused(&rq->xdp_rxq);

	rq->buff.map_dir = DMA_FROM_DEVICE;
	rq->buff.headroom = mlx5e_get_rq_headroom(mdev, params, NULL);
	pool_size = 1 << params->log_rq_mtu_frames;

	err = mlx5_wq_cyc_create(mdev, &rqp->wq, rqc_wq, &rq->wqe.wq, &rq->wq_ctrl);
	if (err)
		return err;

	rq->wqe.wq.db = &rq->wqe.wq.db[MLX5_RCV_DBR];

	wq_sz = mlx5_wq_cyc_get_size(&rq->wqe.wq);

	rq->wqe.info = rqp->frags_info;
	rq->buff.frame0_sz = rq->wqe.info.arr[0].frag_stride;
	rq->wqe.frags =	kvzalloc_node(array_size(sizeof(*rq->wqe.frags),
						 (wq_sz << rq->wqe.info.log_num_frags)),
				      GFP_KERNEL, node);
	if (!rq->wqe.frags) {
		err = -ENOMEM;
		goto err_wq_cyc_destroy;
	}

	err = mlx5e_init_di_list(rq, wq_sz, node);
	if (err)
		goto err_free_frags;

	rq->mkey_be = cpu_to_be32(mdev->mlx5e_res.hw_objs.mkey.key);

	mlx5e_rq_set_trap_handlers(rq, params);

	/* Create a page_pool and register it with rxq */
	pp_params.order     = 0;
	pp_params.flags     = 0; /* No-internal DMA mapping in page_pool */
	pp_params.pool_size = pool_size;
	pp_params.nid       = node;
	pp_params.dev       = mdev->device;
	pp_params.dma_dir   = rq->buff.map_dir;

	/* page_pool can be used even when there is no rq->xdp_prog,
	 * given page_pool does not handle DMA mapping there is no
	 * required state to clear. And page_pool gracefully handle
	 * elevated refcnt.
	 */
	rq->page_pool = page_pool_create(&pp_params);
	if (IS_ERR(rq->page_pool)) {
		err = PTR_ERR(rq->page_pool);
		rq->page_pool = NULL;
		goto err_free_di_list;
	}
	for (i = 0; i < wq_sz; i++) {
		struct mlx5e_rx_wqe_cyc *wqe =
			mlx5_wq_cyc_get_wqe(&rq->wqe.wq, i);
		int f;

		for (f = 0; f < rq->wqe.info.num_frags; f++) {
			u32 frag_size = rq->wqe.info.arr[f].frag_size |
				MLX5_HW_START_PADDING;

			wqe->data[f].byte_count = cpu_to_be32(frag_size);
			wqe->data[f].lkey = rq->mkey_be;
		}
		/* check if num_frags is not a pow of two */
		if (rq->wqe.info.num_frags < (1 << rq->wqe.info.log_num_frags)) {
			wqe->data[f].byte_count = 0;
			wqe->data[f].lkey = cpu_to_be32(MLX5_INVALID_LKEY);
			wqe->data[f].addr = 0;
		}
	}
	return 0;

err_free_di_list:
	mlx5e_free_di_list(rq);
err_free_frags:
	kvfree(rq->wqe.frags);
err_wq_cyc_destroy:
	mlx5_wq_destroy(&rq->wq_ctrl);

	return err;
}

static void mlx5e_free_trap_rq(struct mlx5e_rq *rq)
{
	page_pool_destroy(rq->page_pool);
	mlx5e_free_di_list(rq);
	kvfree(rq->wqe.frags);
	mlx5_wq_destroy(&rq->wq_ctrl);
}

static int mlx5e_open_trap_rq(struct mlx5e_priv *priv, struct napi_struct *napi,
			      struct mlx5e_rq_stats *stats, struct mlx5e_params *params,
			      struct mlx5e_rq_param *rq_param,
			      struct mlx5e_ch_stats *ch_stats,
			      struct mlx5e_rq *rq)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_create_cq_param ccp = {};
	struct dim_cq_moder trap_moder = {};
	struct mlx5e_cq *cq = &rq->cq;
	int err;

	ccp.node     = dev_to_node(mdev->device);
	ccp.ch_stats = ch_stats;
	ccp.napi     = napi;
	ccp.ix       = 0;
	err = mlx5e_open_cq(priv, trap_moder, &rq_param->cqp, &ccp, cq);
	if (err)
		return err;

	err = mlx5e_alloc_trap_rq(priv, rq_param, stats, params, ch_stats, rq);
	if (err)
		goto err_destroy_cq;

	err = mlx5e_create_rq(rq, rq_param);
	if (err)
		goto err_free_rq;

	err = mlx5e_modify_rq_state(rq, MLX5_RQC_STATE_RST, MLX5_RQC_STATE_RDY);
	if (err)
		goto err_destroy_rq;

	return 0;

err_destroy_rq:
	mlx5e_destroy_rq(rq);
	mlx5e_free_rx_descs(rq);
err_free_rq:
	mlx5e_free_trap_rq(rq);
err_destroy_cq:
	mlx5e_close_cq(cq);

	return err;
}

static void mlx5e_close_trap_rq(struct mlx5e_rq *rq)
{
	mlx5e_destroy_rq(rq);
	mlx5e_free_rx_descs(rq);
	mlx5e_free_trap_rq(rq);
	mlx5e_close_cq(&rq->cq);
}

static int mlx5e_create_trap_direct_rq_tir(struct mlx5_core_dev *mdev, struct mlx5e_tir *tir,
					   u32 rqn)
{
	void *tirc;
	int inlen;
	u32 *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_tir_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	tirc = MLX5_ADDR_OF(create_tir_in, in, ctx);
	MLX5_SET(tirc, tirc, transport_domain, mdev->mlx5e_res.hw_objs.td.tdn);
	MLX5_SET(tirc, tirc, rx_hash_fn, MLX5_RX_HASH_FN_NONE);
	MLX5_SET(tirc, tirc, disp_type, MLX5_TIRC_DISP_TYPE_DIRECT);
	MLX5_SET(tirc, tirc, inline_rqn, rqn);
	err = mlx5e_create_tir(mdev, tir, in);
	kvfree(in);

	return err;
}

static void mlx5e_destroy_trap_direct_rq_tir(struct mlx5_core_dev *mdev, struct mlx5e_tir *tir)
{
	mlx5e_destroy_tir(mdev, tir);
}

static void mlx5e_activate_trap_rq(struct mlx5e_rq *rq)
{
	set_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
}

static void mlx5e_deactivate_trap_rq(struct mlx5e_rq *rq)
{
	clear_bit(MLX5E_RQ_STATE_ENABLED, &rq->state);
}

static void mlx5e_build_trap_params(struct mlx5_core_dev *mdev,
				    int max_mtu, u16 q_counter,
				    struct mlx5e_trap *t)
{
	struct mlx5e_params *params = &t->params;

	params->rq_wq_type = MLX5_WQ_TYPE_CYCLIC;
	mlx5e_init_rq_type_params(mdev, params);
	params->sw_mtu = max_mtu;
	mlx5e_build_rq_param(mdev, params, NULL, q_counter, &t->rq_param);
}

static struct mlx5e_trap *mlx5e_open_trap(struct mlx5e_priv *priv)
{
	int cpu = cpumask_first(mlx5_comp_irq_get_affinity_mask(priv->mdev, 0));
	struct net_device *netdev = priv->netdev;
	struct mlx5e_trap *t;
	int err;

	t = kvzalloc_node(sizeof(*t), GFP_KERNEL, cpu_to_node(cpu));
	if (!t)
		return ERR_PTR(-ENOMEM);

	mlx5e_build_trap_params(priv->mdev, netdev->max_mtu, priv->q_counter, t);

	t->priv     = priv;
	t->mdev     = priv->mdev;
	t->tstamp   = &priv->tstamp;
	t->pdev     = mlx5_core_dma_dev(priv->mdev);
	t->netdev   = priv->netdev;
	t->mkey_be  = cpu_to_be32(priv->mdev->mlx5e_res.hw_objs.mkey.key);
	t->stats    = &priv->trap_stats.ch;

	netif_napi_add(netdev, &t->napi, mlx5e_trap_napi_poll, 64);

	err = mlx5e_open_trap_rq(priv, &t->napi,
				 &priv->trap_stats.rq,
				 &t->params, &t->rq_param,
				 &priv->trap_stats.ch,
				 &t->rq);
	if (unlikely(err))
		goto err_napi_del;

	err = mlx5e_create_trap_direct_rq_tir(t->mdev, &t->tir, t->rq.rqn);
	if (err)
		goto err_close_trap_rq;

	return t;

err_close_trap_rq:
	mlx5e_close_trap_rq(&t->rq);
err_napi_del:
	netif_napi_del(&t->napi);
	kvfree(t);
	return ERR_PTR(err);
}

void mlx5e_close_trap(struct mlx5e_trap *trap)
{
	mlx5e_destroy_trap_direct_rq_tir(trap->mdev, &trap->tir);
	mlx5e_close_trap_rq(&trap->rq);
	netif_napi_del(&trap->napi);
	kvfree(trap);
}

static void mlx5e_activate_trap(struct mlx5e_trap *trap)
{
	napi_enable(&trap->napi);
	mlx5e_activate_trap_rq(&trap->rq);
	napi_schedule(&trap->napi);
}

void mlx5e_deactivate_trap(struct mlx5e_priv *priv)
{
	struct mlx5e_trap *trap = priv->en_trap;

	mlx5e_deactivate_trap_rq(&trap->rq);
	napi_disable(&trap->napi);
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
		err = mlx5e_add_vlan_trap(priv, trap_id, mlx5e_trap_get_tirn(priv->en_trap));
		if (err)
			goto err_out;
		break;
	case DEVLINK_TRAP_GENERIC_ID_DMAC_FILTER:
		err = mlx5e_add_mac_trap(priv, trap_id, mlx5e_trap_get_tirn(priv->en_trap));
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
		mlx5e_remove_vlan_trap(priv);
		break;
	case DEVLINK_TRAP_GENERIC_ID_DMAC_FILTER:
		mlx5e_remove_mac_trap(priv);
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
