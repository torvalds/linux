/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#include <linux/mlx5/fs.h>
#include "en.h"
#include "ipoib.h"

/* IPoIB mlx5 netdev profile */

/* Called directly after IPoIB netdevice was created to initialize SW structs */
static void mlx5i_init(struct mlx5_core_dev *mdev,
		       struct net_device *netdev,
		       const struct mlx5e_profile *profile,
		       void *ppriv)
{
	struct mlx5e_priv *priv  = mlx5i_epriv(netdev);

	priv->mdev        = mdev;
	priv->netdev      = netdev;
	priv->profile     = profile;
	priv->ppriv       = ppriv;

	mlx5e_build_nic_params(mdev, &priv->channels.params, profile->max_nch(mdev));

	mutex_init(&priv->state_lock);
	/* TODO : init netdev features here */
}

/* Called directly before IPoIB netdevice is destroyed to cleanup SW structs */
static void mlx5i_cleanup(struct mlx5e_priv *priv)
{
	/* Do nothing .. */
}

static int mlx5i_init_tx(struct mlx5e_priv *priv)
{
	/* TODO: Create IPoIB underlay QP */
	/* TODO: create IPoIB TX HW TIS */
	return 0;
}

static void mlx5i_cleanup_tx(struct mlx5e_priv *priv)
{
}

static int mlx5i_init_rx(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5e_create_indirect_rqt(priv);
	if (err)
		return err;

	err = mlx5e_create_direct_rqts(priv);
	if (err)
		goto err_destroy_indirect_rqts;

	err = mlx5e_create_indirect_tirs(priv);
	if (err)
		goto err_destroy_direct_rqts;

	err = mlx5e_create_direct_tirs(priv);
	if (err)
		goto err_destroy_indirect_tirs;

	return 0;

err_destroy_indirect_tirs:
	mlx5e_destroy_indirect_tirs(priv);
err_destroy_direct_rqts:
	mlx5e_destroy_direct_rqts(priv);
err_destroy_indirect_rqts:
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
	return err;
}

static void mlx5i_cleanup_rx(struct mlx5e_priv *priv)
{
	mlx5e_destroy_direct_tirs(priv);
	mlx5e_destroy_indirect_tirs(priv);
	mlx5e_destroy_direct_rqts(priv);
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
}

static const struct mlx5e_profile mlx5i_nic_profile = {
	.init		   = mlx5i_init,
	.cleanup	   = mlx5i_cleanup,
	.init_tx	   = mlx5i_init_tx,
	.cleanup_tx	   = mlx5i_cleanup_tx,
	.init_rx	   = mlx5i_init_rx,
	.cleanup_rx	   = mlx5i_cleanup_rx,
	.enable		   = NULL, /* mlx5i_enable */
	.disable	   = NULL, /* mlx5i_disable */
	.update_stats	   = NULL, /* mlx5i_update_stats */
	.max_nch	   = mlx5e_get_max_num_channels,
	.max_tc		   = MLX5I_MAX_NUM_TC,
};

/* IPoIB RDMA netdev callbacks */

static int mlx5i_check_required_hca_cap(struct mlx5_core_dev *mdev)
{
	if (MLX5_CAP_GEN(mdev, port_type) != MLX5_CAP_PORT_TYPE_IB)
		return -EOPNOTSUPP;

	if (!MLX5_CAP_GEN(mdev, ipoib_enhanced_offloads)) {
		mlx5_core_warn(mdev, "IPoIB enhanced offloads are not supported\n");
		return -ENOTSUPP;
	}

	return 0;
}

struct net_device *mlx5_rdma_netdev_alloc(struct mlx5_core_dev *mdev,
					  struct ib_device *ibdev,
					  const char *name,
					  void (*setup)(struct net_device *))
{
	const struct mlx5e_profile *profile = &mlx5i_nic_profile;
	int nch = profile->max_nch(mdev);
	struct net_device *netdev;
	struct mlx5i_priv *ipriv;
	struct mlx5e_priv *epriv;
	int err;

	if (mlx5i_check_required_hca_cap(mdev)) {
		mlx5_core_warn(mdev, "Accelerated mode is not supported\n");
		return ERR_PTR(-EOPNOTSUPP);
	}

	/* This function should only be called once per mdev */
	err = mlx5e_create_mdev_resources(mdev);
	if (err)
		return NULL;

	netdev = alloc_netdev_mqs(sizeof(struct mlx5i_priv) + sizeof(struct mlx5e_priv),
				  name, NET_NAME_UNKNOWN,
				  setup,
				  nch * MLX5E_MAX_NUM_TC,
				  nch);
	if (!netdev) {
		mlx5_core_warn(mdev, "alloc_netdev_mqs failed\n");
		goto free_mdev_resources;
	}

	ipriv = netdev_priv(netdev);
	epriv = mlx5i_epriv(netdev);

	epriv->wq = create_singlethread_workqueue("mlx5i");
	if (!epriv->wq)
		goto err_free_netdev;

	profile->init(mdev, netdev, profile, ipriv);

	mlx5e_attach_netdev(epriv);
	netif_carrier_off(netdev);

	/* TODO: set rdma_netdev func pointers
	 * rn = &ipriv->rn;
	 * rn->hca  = ibdev;
	 * rn->send = mlx5i_xmit;
	 * rn->attach_mcast = mlx5i_attach_mcast;
	 * rn->detach_mcast = mlx5i_detach_mcast;
	 */
	return netdev;

free_mdev_resources:
	mlx5e_destroy_mdev_resources(mdev);
err_free_netdev:
	free_netdev(netdev);
	return NULL;
}
EXPORT_SYMBOL(mlx5_rdma_netdev_alloc);

void mlx5_rdma_netdev_free(struct net_device *netdev)
{
	struct mlx5e_priv          *priv    = mlx5i_epriv(netdev);
	const struct mlx5e_profile *profile = priv->profile;

	mlx5e_detach_netdev(priv);
	profile->cleanup(priv);
	destroy_workqueue(priv->wq);
	free_netdev(netdev);

	mlx5e_destroy_mdev_resources(priv->mdev);
}
EXPORT_SYMBOL(mlx5_rdma_netdev_free);

