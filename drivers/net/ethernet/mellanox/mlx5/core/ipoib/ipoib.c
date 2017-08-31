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

#include <rdma/ib_verbs.h>
#include <linux/mlx5/fs.h>
#include "en.h"
#include "ipoib.h"

#define IB_DEFAULT_Q_KEY   0xb1b
#define MLX5I_PARAMS_DEFAULT_LOG_RQ_SIZE 9

static int mlx5i_open(struct net_device *netdev);
static int mlx5i_close(struct net_device *netdev);
static int  mlx5i_dev_init(struct net_device *dev);
static void mlx5i_dev_cleanup(struct net_device *dev);
static int mlx5i_change_mtu(struct net_device *netdev, int new_mtu);
static int mlx5i_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);

static const struct net_device_ops mlx5i_netdev_ops = {
	.ndo_open                = mlx5i_open,
	.ndo_stop                = mlx5i_close,
	.ndo_init                = mlx5i_dev_init,
	.ndo_uninit              = mlx5i_dev_cleanup,
	.ndo_change_mtu          = mlx5i_change_mtu,
	.ndo_do_ioctl            = mlx5i_ioctl,
};

/* IPoIB mlx5 netdev profile */
static void mlx5i_build_nic_params(struct mlx5_core_dev *mdev,
				   struct mlx5e_params *params)
{
	/* Override RQ params as IPoIB supports only LINKED LIST RQ for now */
	mlx5e_set_rq_type_params(mdev, params, MLX5_WQ_TYPE_LINKED_LIST);

	/* RQ size in ipoib by default is 512 */
	params->log_rq_size = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE :
		MLX5I_PARAMS_DEFAULT_LOG_RQ_SIZE;

	params->lro_en = false;
}

/* Called directly after IPoIB netdevice was created to initialize SW structs */
static void mlx5i_init(struct mlx5_core_dev *mdev,
		       struct net_device *netdev,
		       const struct mlx5e_profile *profile,
		       void *ppriv)
{
	struct mlx5e_priv *priv  = mlx5i_epriv(netdev);

	/* priv init */
	priv->mdev        = mdev;
	priv->netdev      = netdev;
	priv->profile     = profile;
	priv->ppriv       = ppriv;
	priv->hard_mtu = MLX5_IB_GRH_BYTES + MLX5_IPOIB_HARD_LEN;
	mutex_init(&priv->state_lock);

	mlx5e_build_nic_params(mdev, &priv->channels.params, profile->max_nch(mdev));
	mlx5i_build_nic_params(mdev, &priv->channels.params);

	/* netdev init */
	netdev->hw_features    |= NETIF_F_SG;
	netdev->hw_features    |= NETIF_F_IP_CSUM;
	netdev->hw_features    |= NETIF_F_IPV6_CSUM;
	netdev->hw_features    |= NETIF_F_GRO;
	netdev->hw_features    |= NETIF_F_TSO;
	netdev->hw_features    |= NETIF_F_TSO6;
	netdev->hw_features    |= NETIF_F_RXCSUM;
	netdev->hw_features    |= NETIF_F_RXHASH;

	netdev->netdev_ops = &mlx5i_netdev_ops;
	netdev->ethtool_ops = &mlx5i_ethtool_ops;
}

/* Called directly before IPoIB netdevice is destroyed to cleanup SW structs */
static void mlx5i_cleanup(struct mlx5e_priv *priv)
{
	/* Do nothing .. */
}

#define MLX5_QP_ENHANCED_ULP_STATELESS_MODE 2

static int mlx5i_create_underlay_qp(struct mlx5_core_dev *mdev, struct mlx5_core_qp *qp)
{
	struct mlx5_qp_context *context = NULL;
	u32 *in = NULL;
	void *addr_path;
	int ret = 0;
	int inlen;
	void *qpc;

	inlen = MLX5_ST_SZ_BYTES(create_qp_in);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_UD);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, ulp_stateless_offload_mode,
		 MLX5_QP_ENHANCED_ULP_STATELESS_MODE);

	addr_path = MLX5_ADDR_OF(qpc, qpc, primary_address_path);
	MLX5_SET(ads, addr_path, port, 1);
	MLX5_SET(ads, addr_path, grh, 1);

	ret = mlx5_core_create_qp(mdev, qp, in, inlen);
	if (ret) {
		mlx5_core_err(mdev, "Failed creating IPoIB QP err : %d\n", ret);
		goto out;
	}

	/* QP states */
	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context) {
		ret = -ENOMEM;
		goto out;
	}

	context->flags = cpu_to_be32(MLX5_QP_PM_MIGRATED << 11);
	context->pri_path.port = 1;
	context->qkey = cpu_to_be32(IB_DEFAULT_Q_KEY);

	ret = mlx5_core_qp_modify(mdev, MLX5_CMD_OP_RST2INIT_QP, 0, context, qp);
	if (ret) {
		mlx5_core_err(mdev, "Failed to modify qp RST2INIT, err: %d\n", ret);
		goto out;
	}
	memset(context, 0, sizeof(*context));

	ret = mlx5_core_qp_modify(mdev, MLX5_CMD_OP_INIT2RTR_QP, 0, context, qp);
	if (ret) {
		mlx5_core_err(mdev, "Failed to modify qp INIT2RTR, err: %d\n", ret);
		goto out;
	}

	ret = mlx5_core_qp_modify(mdev, MLX5_CMD_OP_RTR2RTS_QP, 0, context, qp);
	if (ret) {
		mlx5_core_err(mdev, "Failed to modify qp RTR2RTS, err: %d\n", ret);
		goto out;
	}

out:
	kfree(context);
	kvfree(in);
	return ret;
}

static void mlx5i_destroy_underlay_qp(struct mlx5_core_dev *mdev, struct mlx5_core_qp *qp)
{
	mlx5_core_destroy_qp(mdev, qp);
}

static int mlx5i_init_tx(struct mlx5e_priv *priv)
{
	struct mlx5i_priv *ipriv = priv->ppriv;
	int err;

	err = mlx5i_create_underlay_qp(priv->mdev, &ipriv->qp);
	if (err) {
		mlx5_core_warn(priv->mdev, "create underlay QP failed, %d\n", err);
		return err;
	}

	err = mlx5e_create_tis(priv->mdev, 0 /* tc */, ipriv->qp.qpn, &priv->tisn[0]);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tis failed, %d\n", err);
		return err;
	}

	return 0;
}

static void mlx5i_cleanup_tx(struct mlx5e_priv *priv)
{
	struct mlx5i_priv *ipriv = priv->ppriv;

	mlx5e_destroy_tis(priv->mdev, priv->tisn[0]);
	mlx5i_destroy_underlay_qp(priv->mdev, &ipriv->qp);
}

static int mlx5i_create_flow_steering(struct mlx5e_priv *priv)
{
	int err;

	priv->fs.ns = mlx5_get_flow_namespace(priv->mdev,
					       MLX5_FLOW_NAMESPACE_KERNEL);

	if (!priv->fs.ns)
		return -EINVAL;

	err = mlx5e_arfs_create_tables(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create arfs tables, err=%d\n",
			   err);
		priv->netdev->hw_features &= ~NETIF_F_NTUPLE;
	}

	err = mlx5e_create_ttc_table(priv);
	if (err) {
		netdev_err(priv->netdev, "Failed to create ttc table, err=%d\n",
			   err);
		goto err_destroy_arfs_tables;
	}

	return 0;

err_destroy_arfs_tables:
	mlx5e_arfs_destroy_tables(priv);

	return err;
}

static void mlx5i_destroy_flow_steering(struct mlx5e_priv *priv)
{
	mlx5e_destroy_ttc_table(priv);
	mlx5e_arfs_destroy_tables(priv);
}

static int mlx5i_init_rx(struct mlx5e_priv *priv)
{
	struct mlx5i_priv *ipriv  = priv->ppriv;
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

	err = mlx5_fs_add_rx_underlay_qpn(priv->mdev, ipriv->qp.qpn);
	if (err)
		goto err_destroy_direct_tirs;

	err = mlx5i_create_flow_steering(priv);
	if (err)
		goto err_remove_rx_underlay_qpn;

	return 0;

err_remove_rx_underlay_qpn:
	mlx5_fs_remove_rx_underlay_qpn(priv->mdev, ipriv->qp.qpn);
err_destroy_direct_tirs:
	mlx5e_destroy_direct_tirs(priv);
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
	struct mlx5i_priv *ipriv  = priv->ppriv;

	mlx5_fs_remove_rx_underlay_qpn(priv->mdev, ipriv->qp.qpn);
	mlx5i_destroy_flow_steering(priv);
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
	.update_carrier    = NULL, /* no HW update in IB link */
	.rx_handlers.handle_rx_cqe       = mlx5i_handle_rx_cqe,
	.rx_handlers.handle_rx_cqe_mpwqe = NULL, /* Not supported */
	.max_tc		   = MLX5I_MAX_NUM_TC,
};

/* mlx5i netdev NDos */

static int mlx5i_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);
	struct mlx5e_channels new_channels = {};
	int curr_mtu;
	int err = 0;

	mutex_lock(&priv->state_lock);

	curr_mtu    = netdev->mtu;
	netdev->mtu = new_mtu;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto out;

	new_channels.params = priv->channels.params;
	err = mlx5e_open_channels(priv, &new_channels);
	if (err) {
		netdev->mtu = curr_mtu;
		goto out;
	}

	mlx5e_switch_priv_channels(priv, &new_channels, NULL);

out:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5i_dev_init(struct net_device *dev)
{
	struct mlx5e_priv    *priv   = mlx5i_epriv(dev);
	struct mlx5i_priv    *ipriv  = priv->ppriv;

	/* Set dev address using underlay QP */
	dev->dev_addr[1] = (ipriv->qp.qpn >> 16) & 0xff;
	dev->dev_addr[2] = (ipriv->qp.qpn >>  8) & 0xff;
	dev->dev_addr[3] = (ipriv->qp.qpn) & 0xff;

	return 0;
}

static int mlx5i_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mlx5e_priv *priv = mlx5i_epriv(dev);

	switch (cmd) {
	case SIOCSHWTSTAMP:
		return mlx5e_hwstamp_set(priv, ifr);
	case SIOCGHWTSTAMP:
		return mlx5e_hwstamp_get(priv, ifr);
	default:
		return -EOPNOTSUPP;
	}
}

static void mlx5i_dev_cleanup(struct net_device *dev)
{
	struct mlx5e_priv    *priv   = mlx5i_epriv(dev);
	struct mlx5_core_dev *mdev   = priv->mdev;
	struct mlx5i_priv    *ipriv  = priv->ppriv;
	struct mlx5_qp_context context;

	/* detach qp from flow-steering by reset it */
	mlx5_core_qp_modify(mdev, MLX5_CMD_OP_2RST_QP, 0, &context, &ipriv->qp);
}

static int mlx5i_open(struct net_device *netdev)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);
	int err;

	mutex_lock(&priv->state_lock);

	set_bit(MLX5E_STATE_OPENED, &priv->state);

	err = mlx5e_open_channels(priv, &priv->channels);
	if (err)
		goto err_clear_state_opened_flag;

	mlx5e_refresh_tirs(priv, false);
	mlx5e_activate_priv_channels(priv);
	mlx5e_timestamp_init(priv);

	mutex_unlock(&priv->state_lock);
	return 0;

err_clear_state_opened_flag:
	clear_bit(MLX5E_STATE_OPENED, &priv->state);
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5i_close(struct net_device *netdev)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);

	/* May already be CLOSED in case a previous configuration operation
	 * (e.g RX/TX queue size change) that involves close&open failed.
	 */
	mutex_lock(&priv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state))
		goto unlock;

	clear_bit(MLX5E_STATE_OPENED, &priv->state);

	mlx5e_timestamp_cleanup(priv);
	netif_carrier_off(priv->netdev);
	mlx5e_deactivate_priv_channels(priv);
	mlx5e_close_channels(&priv->channels);
unlock:
	mutex_unlock(&priv->state_lock);
	return 0;
}

/* IPoIB RDMA netdev callbacks */
static int mlx5i_attach_mcast(struct net_device *netdev, struct ib_device *hca,
			      union ib_gid *gid, u16 lid, int set_qkey,
			      u32 qkey)
{
	struct mlx5e_priv    *epriv = mlx5i_epriv(netdev);
	struct mlx5_core_dev *mdev  = epriv->mdev;
	struct mlx5i_priv    *ipriv = epriv->ppriv;
	int err;

	mlx5_core_dbg(mdev, "attaching QPN 0x%x, MGID %pI6\n", ipriv->qp.qpn, gid->raw);
	err = mlx5_core_attach_mcg(mdev, gid, ipriv->qp.qpn);
	if (err)
		mlx5_core_warn(mdev, "failed attaching QPN 0x%x, MGID %pI6\n",
			       ipriv->qp.qpn, gid->raw);

	if (set_qkey) {
		mlx5_core_dbg(mdev, "%s setting qkey 0x%x\n",
			      netdev->name, qkey);
		ipriv->qkey = qkey;
	}

	return err;
}

static int mlx5i_detach_mcast(struct net_device *netdev, struct ib_device *hca,
			      union ib_gid *gid, u16 lid)
{
	struct mlx5e_priv    *epriv = mlx5i_epriv(netdev);
	struct mlx5_core_dev *mdev  = epriv->mdev;
	struct mlx5i_priv    *ipriv = epriv->ppriv;
	int err;

	mlx5_core_dbg(mdev, "detaching QPN 0x%x, MGID %pI6\n", ipriv->qp.qpn, gid->raw);

	err = mlx5_core_detach_mcg(mdev, gid, ipriv->qp.qpn);
	if (err)
		mlx5_core_dbg(mdev, "failed dettaching QPN 0x%x, MGID %pI6\n",
			      ipriv->qp.qpn, gid->raw);

	return err;
}

static int mlx5i_xmit(struct net_device *dev, struct sk_buff *skb,
		      struct ib_ah *address, u32 dqpn)
{
	struct mlx5e_priv *epriv = mlx5i_epriv(dev);
	struct mlx5e_txqsq *sq   = epriv->txq2sq[skb_get_queue_mapping(skb)];
	struct mlx5_ib_ah *mah   = to_mah(address);
	struct mlx5i_priv *ipriv = epriv->ppriv;

	return mlx5i_sq_xmit(sq, skb, &mah->av, dqpn, ipriv->qkey);
}

static int mlx5i_check_required_hca_cap(struct mlx5_core_dev *mdev)
{
	if (MLX5_CAP_GEN(mdev, port_type) != MLX5_CAP_PORT_TYPE_IB)
		return -EOPNOTSUPP;

	if (!MLX5_CAP_GEN(mdev, ipoib_enhanced_offloads)) {
		mlx5_core_warn(mdev, "IPoIB enhanced offloads are not supported\n");
		return -EOPNOTSUPP;
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
	struct rdma_netdev *rn;
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

	/* set rdma_netdev func pointers */
	rn = &ipriv->rn;
	rn->hca  = ibdev;
	rn->send = mlx5i_xmit;
	rn->attach_mcast = mlx5i_attach_mcast;
	rn->detach_mcast = mlx5i_detach_mcast;

	return netdev;

err_free_netdev:
	free_netdev(netdev);
free_mdev_resources:
	mlx5e_destroy_mdev_resources(mdev);

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
