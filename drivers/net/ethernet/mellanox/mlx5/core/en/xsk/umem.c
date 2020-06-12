// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include <net/xdp_sock_drv.h>
#include "umem.h"
#include "setup.h"
#include "en/params.h"

static int mlx5e_xsk_map_umem(struct mlx5e_priv *priv,
			      struct xdp_umem *umem)
{
	struct device *dev = priv->mdev->device;

	return xsk_buff_dma_map(umem, dev, 0);
}

static void mlx5e_xsk_unmap_umem(struct mlx5e_priv *priv,
				 struct xdp_umem *umem)
{
	return xsk_buff_dma_unmap(umem, 0);
}

static int mlx5e_xsk_get_umems(struct mlx5e_xsk *xsk)
{
	if (!xsk->umems) {
		xsk->umems = kcalloc(MLX5E_MAX_NUM_CHANNELS,
				     sizeof(*xsk->umems), GFP_KERNEL);
		if (unlikely(!xsk->umems))
			return -ENOMEM;
	}

	xsk->refcnt++;
	xsk->ever_used = true;

	return 0;
}

static void mlx5e_xsk_put_umems(struct mlx5e_xsk *xsk)
{
	if (!--xsk->refcnt) {
		kfree(xsk->umems);
		xsk->umems = NULL;
	}
}

static int mlx5e_xsk_add_umem(struct mlx5e_xsk *xsk, struct xdp_umem *umem, u16 ix)
{
	int err;

	err = mlx5e_xsk_get_umems(xsk);
	if (unlikely(err))
		return err;

	xsk->umems[ix] = umem;
	return 0;
}

static void mlx5e_xsk_remove_umem(struct mlx5e_xsk *xsk, u16 ix)
{
	xsk->umems[ix] = NULL;

	mlx5e_xsk_put_umems(xsk);
}

static bool mlx5e_xsk_is_umem_sane(struct xdp_umem *umem)
{
	return xsk_umem_get_headroom(umem) <= 0xffff &&
		xsk_umem_get_chunk_size(umem) <= 0xffff;
}

void mlx5e_build_xsk_param(struct xdp_umem *umem, struct mlx5e_xsk_param *xsk)
{
	xsk->headroom = xsk_umem_get_headroom(umem);
	xsk->chunk_size = xsk_umem_get_chunk_size(umem);
}

static int mlx5e_xsk_enable_locked(struct mlx5e_priv *priv,
				   struct xdp_umem *umem, u16 ix)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5e_xsk_param xsk;
	struct mlx5e_channel *c;
	int err;

	if (unlikely(mlx5e_xsk_get_umem(&priv->channels.params, &priv->xsk, ix)))
		return -EBUSY;

	if (unlikely(!mlx5e_xsk_is_umem_sane(umem)))
		return -EINVAL;

	err = mlx5e_xsk_map_umem(priv, umem);
	if (unlikely(err))
		return err;

	err = mlx5e_xsk_add_umem(&priv->xsk, umem, ix);
	if (unlikely(err))
		goto err_unmap_umem;

	mlx5e_build_xsk_param(umem, &xsk);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		/* XSK objects will be created on open. */
		goto validate_closed;
	}

	if (!params->xdp_prog) {
		/* XSK objects will be created when an XDP program is set,
		 * and the channels are reopened.
		 */
		goto validate_closed;
	}

	c = priv->channels.c[ix];

	err = mlx5e_open_xsk(priv, params, &xsk, umem, c);
	if (unlikely(err))
		goto err_remove_umem;

	mlx5e_activate_xsk(c);

	/* Don't wait for WQEs, because the newer xdpsock sample doesn't provide
	 * any Fill Ring entries at the setup stage.
	 */

	err = mlx5e_xsk_redirect_rqt_to_channel(priv, priv->channels.c[ix]);
	if (unlikely(err))
		goto err_deactivate;

	return 0;

err_deactivate:
	mlx5e_deactivate_xsk(c);
	mlx5e_close_xsk(c);

err_remove_umem:
	mlx5e_xsk_remove_umem(&priv->xsk, ix);

err_unmap_umem:
	mlx5e_xsk_unmap_umem(priv, umem);

	return err;

validate_closed:
	/* Check the configuration in advance, rather than fail at a later stage
	 * (in mlx5e_xdp_set or on open) and end up with no channels.
	 */
	if (!mlx5e_validate_xsk_param(params, &xsk, priv->mdev)) {
		err = -EINVAL;
		goto err_remove_umem;
	}

	return 0;
}

static int mlx5e_xsk_disable_locked(struct mlx5e_priv *priv, u16 ix)
{
	struct xdp_umem *umem = mlx5e_xsk_get_umem(&priv->channels.params,
						   &priv->xsk, ix);
	struct mlx5e_channel *c;

	if (unlikely(!umem))
		return -EINVAL;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto remove_umem;

	/* XSK RQ and SQ are only created if XDP program is set. */
	if (!priv->channels.params.xdp_prog)
		goto remove_umem;

	c = priv->channels.c[ix];
	mlx5e_xsk_redirect_rqt_to_drop(priv, ix);
	mlx5e_deactivate_xsk(c);
	mlx5e_close_xsk(c);

remove_umem:
	mlx5e_xsk_remove_umem(&priv->xsk, ix);
	mlx5e_xsk_unmap_umem(priv, umem);

	return 0;
}

static int mlx5e_xsk_enable_umem(struct mlx5e_priv *priv, struct xdp_umem *umem,
				 u16 ix)
{
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_xsk_enable_locked(priv, umem, ix);
	mutex_unlock(&priv->state_lock);

	return err;
}

static int mlx5e_xsk_disable_umem(struct mlx5e_priv *priv, u16 ix)
{
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_xsk_disable_locked(priv, ix);
	mutex_unlock(&priv->state_lock);

	return err;
}

int mlx5e_xsk_setup_umem(struct net_device *dev, struct xdp_umem *umem, u16 qid)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_params *params = &priv->channels.params;
	u16 ix;

	if (unlikely(!mlx5e_qid_get_ch_if_in_group(params, qid, MLX5E_RQ_GROUP_XSK, &ix)))
		return -EINVAL;

	return umem ? mlx5e_xsk_enable_umem(priv, umem, ix) :
		      mlx5e_xsk_disable_umem(priv, ix);
}

u16 mlx5e_xsk_first_unused_channel(struct mlx5e_params *params, struct mlx5e_xsk *xsk)
{
	u16 res = xsk->refcnt ? params->num_channels : 0;

	while (res) {
		if (mlx5e_xsk_get_umem(params, xsk, res - 1))
			break;
		--res;
	}

	return res;
}
