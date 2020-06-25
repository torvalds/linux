// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "setup.h"
#include "en/params.h"

/* It matches XDP_UMEM_MIN_CHUNK_SIZE, but as this constant is private and may
 * change unexpectedly, and mlx5e has a minimum valid stride size for striding
 * RQ, keep this check in the driver.
 */
#define MLX5E_MIN_XSK_CHUNK_SIZE 2048

bool mlx5e_validate_xsk_param(struct mlx5e_params *params,
			      struct mlx5e_xsk_param *xsk,
			      struct mlx5_core_dev *mdev)
{
	/* AF_XDP doesn't support frames larger than PAGE_SIZE. */
	if (xsk->chunk_size > PAGE_SIZE ||
			xsk->chunk_size < MLX5E_MIN_XSK_CHUNK_SIZE)
		return false;

	/* Current MTU and XSK headroom don't allow packets to fit the frames. */
	if (mlx5e_rx_get_min_frag_sz(params, xsk) > xsk->chunk_size)
		return false;

	/* frag_sz is different for regular and XSK RQs, so ensure that linear
	 * SKB mode is possible.
	 */
	switch (params->rq_wq_type) {
	case MLX5_WQ_TYPE_LINKED_LIST_STRIDING_RQ:
		return mlx5e_rx_mpwqe_is_linear_skb(mdev, params, xsk);
	default: /* MLX5_WQ_TYPE_CYCLIC */
		return mlx5e_rx_is_linear_skb(params, xsk);
	}
}

static void mlx5e_build_xskicosq_param(struct mlx5e_priv *priv,
				       u8 log_wq_size,
				       struct mlx5e_sq_param *param)
{
	void *sqc = param->sqc;
	void *wq = MLX5_ADDR_OF(sqc, sqc, wq);

	mlx5e_build_sq_param_common(priv, param);

	MLX5_SET(wq, wq, log_wq_sz, log_wq_size);
}

static void mlx5e_build_xsk_cparam(struct mlx5e_priv *priv,
				   struct mlx5e_params *params,
				   struct mlx5e_xsk_param *xsk,
				   struct mlx5e_channel_param *cparam)
{
	const u8 xskicosq_size = MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE;

	mlx5e_build_rq_param(priv, params, xsk, &cparam->rq);
	mlx5e_build_xdpsq_param(priv, params, &cparam->xdp_sq);
	mlx5e_build_xskicosq_param(priv, xskicosq_size, &cparam->icosq);
	mlx5e_build_rx_cq_param(priv, params, xsk, &cparam->rx_cq);
	mlx5e_build_tx_cq_param(priv, params, &cparam->tx_cq);
	mlx5e_build_ico_cq_param(priv, xskicosq_size, &cparam->icosq_cq);
}

int mlx5e_open_xsk(struct mlx5e_priv *priv, struct mlx5e_params *params,
		   struct mlx5e_xsk_param *xsk, struct xdp_umem *umem,
		   struct mlx5e_channel *c)
{
	struct mlx5e_channel_param *cparam;
	struct dim_cq_moder icocq_moder = {};
	int err;

	if (!mlx5e_validate_xsk_param(params, xsk, priv->mdev))
		return -EINVAL;

	cparam = kvzalloc(sizeof(*cparam), GFP_KERNEL);
	if (!cparam)
		return -ENOMEM;

	mlx5e_build_xsk_cparam(priv, params, xsk, cparam);

	err = mlx5e_open_cq(c, params->rx_cq_moderation, &cparam->rx_cq, &c->xskrq.cq);
	if (unlikely(err))
		goto err_free_cparam;

	err = mlx5e_open_rq(c, params, &cparam->rq, xsk, umem, &c->xskrq);
	if (unlikely(err))
		goto err_close_rx_cq;

	err = mlx5e_open_cq(c, params->tx_cq_moderation, &cparam->tx_cq, &c->xsksq.cq);
	if (unlikely(err))
		goto err_close_rq;

	/* Create a separate SQ, so that when the UMEM is disabled, we could
	 * close this SQ safely and stop receiving CQEs. In other case, e.g., if
	 * the XDPSQ was used instead, we might run into trouble when the UMEM
	 * is disabled and then reenabled, but the SQ continues receiving CQEs
	 * from the old UMEM.
	 */
	err = mlx5e_open_xdpsq(c, params, &cparam->xdp_sq, umem, &c->xsksq, true);
	if (unlikely(err))
		goto err_close_tx_cq;

	err = mlx5e_open_cq(c, icocq_moder, &cparam->icosq_cq, &c->xskicosq.cq);
	if (unlikely(err))
		goto err_close_sq;

	/* Create a dedicated SQ for posting NOPs whenever we need an IRQ to be
	 * triggered and NAPI to be called on the correct CPU.
	 */
	err = mlx5e_open_icosq(c, params, &cparam->icosq, &c->xskicosq);
	if (unlikely(err))
		goto err_close_icocq;

	kvfree(cparam);

	spin_lock_init(&c->xskicosq_lock);

	set_bit(MLX5E_CHANNEL_STATE_XSK, c->state);

	return 0;

err_close_icocq:
	mlx5e_close_cq(&c->xskicosq.cq);

err_close_sq:
	mlx5e_close_xdpsq(&c->xsksq);

err_close_tx_cq:
	mlx5e_close_cq(&c->xsksq.cq);

err_close_rq:
	mlx5e_close_rq(&c->xskrq);

err_close_rx_cq:
	mlx5e_close_cq(&c->xskrq.cq);

err_free_cparam:
	kvfree(cparam);

	return err;
}

void mlx5e_close_xsk(struct mlx5e_channel *c)
{
	clear_bit(MLX5E_CHANNEL_STATE_XSK, c->state);
	napi_synchronize(&c->napi);
	synchronize_rcu(); /* Sync with the XSK wakeup. */

	mlx5e_close_rq(&c->xskrq);
	mlx5e_close_cq(&c->xskrq.cq);
	mlx5e_close_icosq(&c->xskicosq);
	mlx5e_close_cq(&c->xskicosq.cq);
	mlx5e_close_xdpsq(&c->xsksq);
	mlx5e_close_cq(&c->xsksq.cq);

	memset(&c->xskrq, 0, sizeof(c->xskrq));
	memset(&c->xsksq, 0, sizeof(c->xsksq));
	memset(&c->xskicosq, 0, sizeof(c->xskicosq));
}

void mlx5e_activate_xsk(struct mlx5e_channel *c)
{
	mlx5e_activate_icosq(&c->xskicosq);
	set_bit(MLX5E_RQ_STATE_ENABLED, &c->xskrq.state);
	/* TX queue is created active. */

	spin_lock(&c->xskicosq_lock);
	mlx5e_trigger_irq(&c->xskicosq);
	spin_unlock(&c->xskicosq_lock);
}

void mlx5e_deactivate_xsk(struct mlx5e_channel *c)
{
	mlx5e_deactivate_rq(&c->xskrq);
	/* TX queue is disabled on close. */
	mlx5e_deactivate_icosq(&c->xskicosq);
}

static int mlx5e_redirect_xsk_rqt(struct mlx5e_priv *priv, u16 ix, u32 rqn)
{
	struct mlx5e_redirect_rqt_param direct_rrp = {
		.is_rss = false,
		{
			.rqn = rqn,
		},
	};

	u32 rqtn = priv->xsk_tir[ix].rqt.rqtn;

	return mlx5e_redirect_rqt(priv, rqtn, 1, direct_rrp);
}

int mlx5e_xsk_redirect_rqt_to_channel(struct mlx5e_priv *priv, struct mlx5e_channel *c)
{
	return mlx5e_redirect_xsk_rqt(priv, c->ix, c->xskrq.rqn);
}

int mlx5e_xsk_redirect_rqt_to_drop(struct mlx5e_priv *priv, u16 ix)
{
	return mlx5e_redirect_xsk_rqt(priv, ix, priv->drop_rq.rqn);
}

int mlx5e_xsk_redirect_rqts_to_channels(struct mlx5e_priv *priv, struct mlx5e_channels *chs)
{
	int err, i;

	if (!priv->xsk.refcnt)
		return 0;

	for (i = 0; i < chs->num; i++) {
		struct mlx5e_channel *c = chs->c[i];

		if (!test_bit(MLX5E_CHANNEL_STATE_XSK, c->state))
			continue;

		err = mlx5e_xsk_redirect_rqt_to_channel(priv, c);
		if (unlikely(err))
			goto err_stop;
	}

	return 0;

err_stop:
	for (i--; i >= 0; i--) {
		if (!test_bit(MLX5E_CHANNEL_STATE_XSK, chs->c[i]->state))
			continue;

		mlx5e_xsk_redirect_rqt_to_drop(priv, i);
	}

	return err;
}

void mlx5e_xsk_redirect_rqts_to_drop(struct mlx5e_priv *priv, struct mlx5e_channels *chs)
{
	int i;

	if (!priv->xsk.refcnt)
		return;

	for (i = 0; i < chs->num; i++) {
		if (!test_bit(MLX5E_CHANNEL_STATE_XSK, chs->c[i]->state))
			continue;

		mlx5e_xsk_redirect_rqt_to_drop(priv, i);
	}
}
