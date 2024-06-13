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

#include <linux/hash.h>
#include "ipoib.h"

#define MLX5I_MAX_LOG_PKEY_SUP 7

struct qpn_to_netdev {
	struct net_device *netdev;
	struct hlist_node hlist;
	u32 underlay_qpn;
};

struct mlx5i_pkey_qpn_ht {
	struct hlist_head buckets[1 << MLX5I_MAX_LOG_PKEY_SUP];
	spinlock_t ht_lock; /* Synchronise with NAPI */
};

int mlx5i_pkey_qpn_ht_init(struct net_device *netdev)
{
	struct mlx5i_priv *ipriv = netdev_priv(netdev);
	struct mlx5i_pkey_qpn_ht *qpn_htbl;

	qpn_htbl = kzalloc(sizeof(*qpn_htbl), GFP_KERNEL);
	if (!qpn_htbl)
		return -ENOMEM;

	ipriv->qpn_htbl = qpn_htbl;
	spin_lock_init(&qpn_htbl->ht_lock);

	return 0;
}

void mlx5i_pkey_qpn_ht_cleanup(struct net_device *netdev)
{
	struct mlx5i_priv *ipriv = netdev_priv(netdev);

	kfree(ipriv->qpn_htbl);
}

static struct qpn_to_netdev *mlx5i_find_qpn_to_netdev_node(struct hlist_head *buckets,
							   u32 qpn)
{
	struct hlist_head *h = &buckets[hash_32(qpn, MLX5I_MAX_LOG_PKEY_SUP)];
	struct qpn_to_netdev *node;

	hlist_for_each_entry(node, h, hlist) {
		if (node->underlay_qpn == qpn)
			return node;
	}

	return NULL;
}

int mlx5i_pkey_add_qpn(struct net_device *netdev, u32 qpn)
{
	struct mlx5i_priv *ipriv = netdev_priv(netdev);
	struct mlx5i_pkey_qpn_ht *ht = ipriv->qpn_htbl;
	u8 key = hash_32(qpn, MLX5I_MAX_LOG_PKEY_SUP);
	struct qpn_to_netdev *new_node;

	new_node = kzalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node)
		return -ENOMEM;

	new_node->netdev = netdev;
	new_node->underlay_qpn = qpn;
	spin_lock_bh(&ht->ht_lock);
	hlist_add_head(&new_node->hlist, &ht->buckets[key]);
	spin_unlock_bh(&ht->ht_lock);

	return 0;
}

int mlx5i_pkey_del_qpn(struct net_device *netdev, u32 qpn)
{
	struct mlx5e_priv *epriv = mlx5i_epriv(netdev);
	struct mlx5i_priv *ipriv = epriv->ppriv;
	struct mlx5i_pkey_qpn_ht *ht = ipriv->qpn_htbl;
	struct qpn_to_netdev *node;

	node = mlx5i_find_qpn_to_netdev_node(ht->buckets, qpn);
	if (!node) {
		mlx5_core_warn(epriv->mdev, "QPN to netdev delete from HT failed\n");
		return -EINVAL;
	}

	spin_lock_bh(&ht->ht_lock);
	hlist_del_init(&node->hlist);
	spin_unlock_bh(&ht->ht_lock);
	kfree(node);

	return 0;
}

struct net_device *mlx5i_pkey_get_netdev(struct net_device *netdev, u32 qpn)
{
	struct mlx5i_priv *ipriv = netdev_priv(netdev);
	struct qpn_to_netdev *node;

	node = mlx5i_find_qpn_to_netdev_node(ipriv->qpn_htbl->buckets, qpn);
	if (!node)
		return NULL;

	return node->netdev;
}

static int mlx5i_pkey_open(struct net_device *netdev);
static int mlx5i_pkey_close(struct net_device *netdev);
static int mlx5i_pkey_dev_init(struct net_device *dev);
static void mlx5i_pkey_dev_cleanup(struct net_device *netdev);
static int mlx5i_pkey_change_mtu(struct net_device *netdev, int new_mtu);
static int mlx5i_pkey_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

static const struct net_device_ops mlx5i_pkey_netdev_ops = {
	.ndo_open                = mlx5i_pkey_open,
	.ndo_stop                = mlx5i_pkey_close,
	.ndo_init                = mlx5i_pkey_dev_init,
	.ndo_get_stats64         = mlx5i_get_stats,
	.ndo_uninit              = mlx5i_pkey_dev_cleanup,
	.ndo_change_mtu          = mlx5i_pkey_change_mtu,
	.ndo_eth_ioctl            = mlx5i_pkey_ioctl,
};

/* Child NDOs */
static int mlx5i_pkey_dev_init(struct net_device *dev)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);
	struct mlx5i_priv *ipriv, *parent_ipriv;
	struct net_device *parent_dev;

	ipriv = priv->ppriv;

	/* Link to parent */
	parent_dev = mlx5i_parent_get(dev);
	if (!parent_dev) {
		mlx5_core_warn(priv->mdev, "failed to get parent device\n");
		return -EINVAL;
	}

	if (dev->num_rx_queues < parent_dev->real_num_rx_queues) {
		mlx5_core_warn(priv->mdev,
			       "failed to create child device with rx queues [%d] less than parent's [%d]\n",
			       dev->num_rx_queues,
			       parent_dev->real_num_rx_queues);
		mlx5i_parent_put(dev);
		return -EINVAL;
	}

	/* Get QPN to netdevice hash table from parent */
	parent_ipriv = netdev_priv(parent_dev);
	ipriv->qpn_htbl = parent_ipriv->qpn_htbl;

	return mlx5i_dev_init(dev);
}

static int mlx5i_pkey_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	return mlx5i_ioctl(dev, ifr, cmd);
}

static void mlx5i_pkey_dev_cleanup(struct net_device *netdev)
{
	mlx5i_parent_put(netdev);
	return mlx5i_dev_cleanup(netdev);
}

static int mlx5i_pkey_open(struct net_device *netdev)
{
	struct mlx5e_priv *epriv = mlx5i_epriv(netdev);
	struct mlx5i_priv *ipriv = epriv->ppriv;
	struct mlx5_core_dev *mdev = epriv->mdev;
	int err;

	mutex_lock(&epriv->state_lock);

	set_bit(MLX5E_STATE_OPENED, &epriv->state);

	err = mlx5i_init_underlay_qp(epriv);
	if (err) {
		mlx5_core_warn(mdev, "prepare child underlay qp state failed, %d\n", err);
		goto err_release_lock;
	}

	err = mlx5_fs_add_rx_underlay_qpn(mdev, ipriv->qpn);
	if (err) {
		mlx5_core_warn(mdev, "attach child underlay qp to ft failed, %d\n", err);
		goto err_unint_underlay_qp;
	}

	err = mlx5i_create_tis(mdev, ipriv->qpn, &epriv->tisn[0][0]);
	if (err) {
		mlx5_core_warn(mdev, "create child tis failed, %d\n", err);
		goto err_remove_rx_uderlay_qp;
	}

	err = mlx5e_open_channels(epriv, &epriv->channels);
	if (err) {
		mlx5_core_warn(mdev, "opening child channels failed, %d\n", err);
		goto err_clear_state_opened_flag;
	}
	epriv->profile->update_rx(epriv);
	mlx5e_activate_priv_channels(epriv);
	mutex_unlock(&epriv->state_lock);

	return 0;

err_clear_state_opened_flag:
	mlx5e_destroy_tis(mdev, epriv->tisn[0][0]);
err_remove_rx_uderlay_qp:
	mlx5_fs_remove_rx_underlay_qpn(mdev, ipriv->qpn);
err_unint_underlay_qp:
	mlx5i_uninit_underlay_qp(epriv);
err_release_lock:
	clear_bit(MLX5E_STATE_OPENED, &epriv->state);
	mutex_unlock(&epriv->state_lock);
	return err;
}

static int mlx5i_pkey_close(struct net_device *netdev)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);
	struct mlx5i_priv *ipriv = priv->ppriv;
	struct mlx5_core_dev *mdev = priv->mdev;

	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	clear_bit(MLX5E_STATE_OPENED, &priv->state);

	netif_carrier_off(priv->netdev);
	mlx5_fs_remove_rx_underlay_qpn(mdev, ipriv->qpn);
	mlx5i_uninit_underlay_qp(priv);
	mlx5e_deactivate_priv_channels(priv);
	mlx5e_close_channels(&priv->channels);
	mlx5e_destroy_tis(mdev, priv->tisn[0][0]);
unlock:
	mutex_unlock(&priv->state_lock);
	return 0;
}

static int mlx5i_pkey_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	mutex_lock(&priv->state_lock);
	netdev->mtu = new_mtu;
	mutex_unlock(&priv->state_lock);

	return 0;
}

/* Called directly after IPoIB netdevice was created to initialize SW structs */
static int mlx5i_pkey_init(struct mlx5_core_dev *mdev,
			   struct net_device *netdev)
{
	struct mlx5e_priv *priv  = mlx5i_epriv(netdev);
	int err;

	err = mlx5i_init(mdev, netdev);
	if (err)
		return err;

	/* Override parent ndo */
	netdev->netdev_ops = &mlx5i_pkey_netdev_ops;

	/* Set child limited ethtool support */
	netdev->ethtool_ops = &mlx5i_pkey_ethtool_ops;

	/* Use dummy rqs */
	priv->channels.params.log_rq_mtu_frames = MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE;

	return 0;
}

/* Called directly before IPoIB netdevice is destroyed to cleanup SW structs */
static void mlx5i_pkey_cleanup(struct mlx5e_priv *priv)
{
	mlx5i_cleanup(priv);
}

static int mlx5i_pkey_init_tx(struct mlx5e_priv *priv)
{
	int err;

	err = mlx5i_create_underlay_qp(priv);
	if (err)
		mlx5_core_warn(priv->mdev, "create child underlay QP failed, %d\n", err);

	return err;
}

static void mlx5i_pkey_cleanup_tx(struct mlx5e_priv *priv)
{
	struct mlx5i_priv *ipriv = priv->ppriv;

	mlx5i_destroy_underlay_qp(priv->mdev, ipriv->qpn);
}

static int mlx5i_pkey_init_rx(struct mlx5e_priv *priv)
{
	/* Since the rx resources are shared between child and parent, the
	 * parent interface is taking care of rx resource allocation and init
	 */
	return 0;
}

static void mlx5i_pkey_cleanup_rx(struct mlx5e_priv *priv)
{
	/* Since the rx resources are shared between child and parent, the
	 * parent interface is taking care of rx resource free and de-init
	 */
}

static const struct mlx5e_profile mlx5i_pkey_nic_profile = {
	.init		   = mlx5i_pkey_init,
	.cleanup	   = mlx5i_pkey_cleanup,
	.init_tx	   = mlx5i_pkey_init_tx,
	.cleanup_tx	   = mlx5i_pkey_cleanup_tx,
	.init_rx	   = mlx5i_pkey_init_rx,
	.cleanup_rx	   = mlx5i_pkey_cleanup_rx,
	.enable		   = NULL,
	.disable	   = NULL,
	.update_rx	   = mlx5i_update_nic_rx,
	.update_stats	   = NULL,
	.rx_handlers       = &mlx5i_rx_handlers,
	.max_tc		   = MLX5I_MAX_NUM_TC,
};

const struct mlx5e_profile *mlx5i_pkey_get_profile(void)
{
	return &mlx5i_pkey_nic_profile;
}
