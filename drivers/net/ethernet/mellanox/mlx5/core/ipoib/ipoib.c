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
static int mlx5i_change_mtu(struct net_device *netdev, int new_mtu);

static const struct net_device_ops mlx5i_netdev_ops = {
	.ndo_open                = mlx5i_open,
	.ndo_stop                = mlx5i_close,
	.ndo_get_stats64         = mlx5i_get_stats,
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
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_STRIDING_RQ, false);
	mlx5e_set_rq_type(mdev, params);
	mlx5e_init_rq_type_params(mdev, params);

	/* RQ size in ipoib by default is 512 */
	params->log_rq_mtu_frames = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_RQ_SIZE :
		MLX5I_PARAMS_DEFAULT_LOG_RQ_SIZE;

	params->lro_en = false;
	params->hard_mtu = MLX5_IB_GRH_BYTES + MLX5_IPOIB_HARD_LEN;
	params->tunneled_offload_en = false;
}

/* Called directly after IPoIB netdevice was created to initialize SW structs */
int mlx5i_init(struct mlx5_core_dev *mdev,
	       struct net_device *netdev,
	       const struct mlx5e_profile *profile,
	       void *ppriv)
{
	struct mlx5e_priv *priv  = mlx5i_epriv(netdev);
	int err;

	err = mlx5e_netdev_init(netdev, priv, mdev, profile, ppriv);
	if (err)
		return err;

	mlx5e_set_netdev_mtu_boundaries(priv);
	netdev->mtu = netdev->max_mtu;

	mlx5e_build_nic_params(priv, NULL, &priv->rss_params, &priv->channels.params,
			       netdev->mtu);
	mlx5i_build_nic_params(mdev, &priv->channels.params);

	mlx5e_timestamp_init(priv);

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

	return 0;
}

/* Called directly before IPoIB netdevice is destroyed to cleanup SW structs */
void mlx5i_cleanup(struct mlx5e_priv *priv)
{
	mlx5e_netdev_cleanup(priv->netdev, priv);
}

static void mlx5i_grp_sw_update_stats(struct mlx5e_priv *priv)
{
	struct mlx5e_sw_stats s = { 0 };
	int i, j;

	for (i = 0; i < priv->max_nch; i++) {
		struct mlx5e_channel_stats *channel_stats;
		struct mlx5e_rq_stats *rq_stats;

		channel_stats = &priv->channel_stats[i];
		rq_stats = &channel_stats->rq;

		s.rx_packets += rq_stats->packets;
		s.rx_bytes   += rq_stats->bytes;

		for (j = 0; j < priv->max_opened_tc; j++) {
			struct mlx5e_sq_stats *sq_stats = &channel_stats->sq[j];

			s.tx_packets           += sq_stats->packets;
			s.tx_bytes             += sq_stats->bytes;
			s.tx_queue_dropped     += sq_stats->dropped;
		}
	}

	memcpy(&priv->stats.sw, &s, sizeof(s));
}

void mlx5i_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv     *priv   = mlx5i_epriv(dev);
	struct mlx5e_sw_stats *sstats = &priv->stats.sw;

	mlx5i_grp_sw_update_stats(priv);

	stats->rx_packets = sstats->rx_packets;
	stats->rx_bytes   = sstats->rx_bytes;
	stats->tx_packets = sstats->tx_packets;
	stats->tx_bytes   = sstats->tx_bytes;
	stats->tx_dropped = sstats->tx_queue_dropped;
}

int mlx5i_init_underlay_qp(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5i_priv *ipriv = priv->ppriv;
	int ret;

	{
		u32 in[MLX5_ST_SZ_DW(rst2init_qp_in)] = {};
		u32 *qpc;

		qpc = MLX5_ADDR_OF(rst2init_qp_in, in, qpc);

		MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
		MLX5_SET(qpc, qpc, primary_address_path.pkey_index,
			 ipriv->pkey_index);
		MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, 1);
		MLX5_SET(qpc, qpc, q_key, IB_DEFAULT_Q_KEY);

		MLX5_SET(rst2init_qp_in, in, opcode, MLX5_CMD_OP_RST2INIT_QP);
		MLX5_SET(rst2init_qp_in, in, qpn, ipriv->qpn);
		ret = mlx5_cmd_exec_in(mdev, rst2init_qp, in);
		if (ret)
			goto err_qp_modify_to_err;
	}
	{
		u32 in[MLX5_ST_SZ_DW(init2rtr_qp_in)] = {};

		MLX5_SET(init2rtr_qp_in, in, opcode, MLX5_CMD_OP_INIT2RTR_QP);
		MLX5_SET(init2rtr_qp_in, in, qpn, ipriv->qpn);
		ret = mlx5_cmd_exec_in(mdev, init2rtr_qp, in);
		if (ret)
			goto err_qp_modify_to_err;
	}
	{
		u32 in[MLX5_ST_SZ_DW(rtr2rts_qp_in)] = {};

		MLX5_SET(rtr2rts_qp_in, in, opcode, MLX5_CMD_OP_RTR2RTS_QP);
		MLX5_SET(rtr2rts_qp_in, in, qpn, ipriv->qpn);
		ret = mlx5_cmd_exec_in(mdev, rtr2rts_qp, in);
		if (ret)
			goto err_qp_modify_to_err;
	}
	return 0;

err_qp_modify_to_err:
	{
		u32 in[MLX5_ST_SZ_DW(qp_2err_in)] = {};

		MLX5_SET(qp_2err_in, in, opcode, MLX5_CMD_OP_2ERR_QP);
		MLX5_SET(qp_2err_in, in, qpn, ipriv->qpn);
		mlx5_cmd_exec_in(mdev, qp_2err, in);
	}
	return ret;
}

void mlx5i_uninit_underlay_qp(struct mlx5e_priv *priv)
{
	struct mlx5i_priv *ipriv = priv->ppriv;
	struct mlx5_core_dev *mdev = priv->mdev;
	u32 in[MLX5_ST_SZ_DW(qp_2rst_in)] = {};

	MLX5_SET(qp_2rst_in, in, opcode, MLX5_CMD_OP_2RST_QP);
	MLX5_SET(qp_2rst_in, in, qpn, ipriv->qpn);
	mlx5_cmd_exec_in(mdev, qp_2rst, in);
}

#define MLX5_QP_ENHANCED_ULP_STATELESS_MODE 2

int mlx5i_create_underlay_qp(struct mlx5e_priv *priv)
{
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	u32 in[MLX5_ST_SZ_DW(create_qp_in)] = {};
	struct mlx5i_priv *ipriv = priv->ppriv;
	void *addr_path;
	int ret = 0;
	void *qpc;

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, st, MLX5_QP_ST_UD);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	MLX5_SET(qpc, qpc, ulp_stateless_offload_mode,
		 MLX5_QP_ENHANCED_ULP_STATELESS_MODE);

	addr_path = MLX5_ADDR_OF(qpc, qpc, primary_address_path);
	MLX5_SET(ads, addr_path, vhca_port_num, 1);
	MLX5_SET(ads, addr_path, grh, 1);

	MLX5_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);
	ret = mlx5_cmd_exec_inout(priv->mdev, create_qp, in, out);
	if (ret)
		return ret;

	ipriv->qpn = MLX5_GET(create_qp_out, out, qpn);

	return 0;
}

void mlx5i_destroy_underlay_qp(struct mlx5_core_dev *mdev, u32 qpn)
{
	u32 in[MLX5_ST_SZ_DW(destroy_qp_in)] = {};

	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, qpn);
	mlx5_cmd_exec_in(mdev, destroy_qp, in);
}

int mlx5i_update_nic_rx(struct mlx5e_priv *priv)
{
	return mlx5e_refresh_tirs(priv, true, true);
}

int mlx5i_create_tis(struct mlx5_core_dev *mdev, u32 underlay_qpn, u32 *tisn)
{
	u32 in[MLX5_ST_SZ_DW(create_tis_in)] = {};
	void *tisc;

	tisc = MLX5_ADDR_OF(create_tis_in, in, ctx);

	MLX5_SET(tisc, tisc, underlay_qpn, underlay_qpn);

	return mlx5e_create_tis(mdev, in, tisn);
}

static int mlx5i_init_tx(struct mlx5e_priv *priv)
{
	struct mlx5i_priv *ipriv = priv->ppriv;
	int err;

	err = mlx5i_create_underlay_qp(priv);
	if (err) {
		mlx5_core_warn(priv->mdev, "create underlay QP failed, %d\n", err);
		return err;
	}

	err = mlx5i_create_tis(priv->mdev, ipriv->qpn, &priv->tisn[0][0]);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tis failed, %d\n", err);
		goto err_destroy_underlay_qp;
	}

	return 0;

err_destroy_underlay_qp:
	mlx5i_destroy_underlay_qp(priv->mdev, ipriv->qpn);
	return err;
}

static void mlx5i_cleanup_tx(struct mlx5e_priv *priv)
{
	struct mlx5i_priv *ipriv = priv->ppriv;

	mlx5e_destroy_tis(priv->mdev, priv->tisn[0][0]);
	mlx5i_destroy_underlay_qp(priv->mdev, ipriv->qpn);
}

static int mlx5i_create_flow_steering(struct mlx5e_priv *priv)
{
	struct ttc_params ttc_params = {};
	int tt, err;

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

	mlx5e_set_ttc_basic_params(priv, &ttc_params);
	mlx5e_set_inner_ttc_ft_params(&ttc_params);
	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		ttc_params.indir_tirn[tt] = priv->inner_indir_tir[tt].tirn;

	err = mlx5e_create_inner_ttc_table(priv, &ttc_params, &priv->fs.inner_ttc);
	if (err) {
		netdev_err(priv->netdev, "Failed to create inner ttc table, err=%d\n",
			   err);
		goto err_destroy_arfs_tables;
	}

	mlx5e_set_ttc_ft_params(&ttc_params);
	for (tt = 0; tt < MLX5E_NUM_INDIR_TIRS; tt++)
		ttc_params.indir_tirn[tt] = priv->indir_tir[tt].tirn;

	err = mlx5e_create_ttc_table(priv, &ttc_params, &priv->fs.ttc);
	if (err) {
		netdev_err(priv->netdev, "Failed to create ttc table, err=%d\n",
			   err);
		goto err_destroy_inner_ttc_table;
	}

	return 0;

err_destroy_inner_ttc_table:
	mlx5e_destroy_inner_ttc_table(priv, &priv->fs.inner_ttc);
err_destroy_arfs_tables:
	mlx5e_arfs_destroy_tables(priv);

	return err;
}

static void mlx5i_destroy_flow_steering(struct mlx5e_priv *priv)
{
	mlx5e_destroy_ttc_table(priv, &priv->fs.ttc);
	mlx5e_destroy_inner_ttc_table(priv, &priv->fs.inner_ttc);
	mlx5e_arfs_destroy_tables(priv);
}

static int mlx5i_init_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	mlx5e_create_q_counters(priv);

	err = mlx5e_open_drop_rq(priv, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_destroy_q_counters;
	}

	err = mlx5e_create_indirect_rqt(priv);
	if (err)
		goto err_close_drop_rq;

	err = mlx5e_create_direct_rqts(priv, priv->direct_tir);
	if (err)
		goto err_destroy_indirect_rqts;

	err = mlx5e_create_indirect_tirs(priv, true);
	if (err)
		goto err_destroy_direct_rqts;

	err = mlx5e_create_direct_tirs(priv, priv->direct_tir);
	if (err)
		goto err_destroy_indirect_tirs;

	err = mlx5i_create_flow_steering(priv);
	if (err)
		goto err_destroy_direct_tirs;

	return 0;

err_destroy_direct_tirs:
	mlx5e_destroy_direct_tirs(priv, priv->direct_tir);
err_destroy_indirect_tirs:
	mlx5e_destroy_indirect_tirs(priv);
err_destroy_direct_rqts:
	mlx5e_destroy_direct_rqts(priv, priv->direct_tir);
err_destroy_indirect_rqts:
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
err_close_drop_rq:
	mlx5e_close_drop_rq(&priv->drop_rq);
err_destroy_q_counters:
	mlx5e_destroy_q_counters(priv);
	return err;
}

static void mlx5i_cleanup_rx(struct mlx5e_priv *priv)
{
	mlx5i_destroy_flow_steering(priv);
	mlx5e_destroy_direct_tirs(priv, priv->direct_tir);
	mlx5e_destroy_indirect_tirs(priv);
	mlx5e_destroy_direct_rqts(priv, priv->direct_tir);
	mlx5e_destroy_rqt(priv, &priv->indir_rqt);
	mlx5e_close_drop_rq(&priv->drop_rq);
	mlx5e_destroy_q_counters(priv);
}

/* The stats groups order is opposite to the update_stats() order calls */
static mlx5e_stats_grp_t mlx5i_stats_grps[] = {
	&MLX5E_STATS_GRP(sw),
	&MLX5E_STATS_GRP(qcnt),
	&MLX5E_STATS_GRP(vnic_env),
	&MLX5E_STATS_GRP(vport),
	&MLX5E_STATS_GRP(802_3),
	&MLX5E_STATS_GRP(2863),
	&MLX5E_STATS_GRP(2819),
	&MLX5E_STATS_GRP(phy),
	&MLX5E_STATS_GRP(pcie),
	&MLX5E_STATS_GRP(per_prio),
	&MLX5E_STATS_GRP(pme),
	&MLX5E_STATS_GRP(channels),
	&MLX5E_STATS_GRP(per_port_buff_congest),
};

static unsigned int mlx5i_stats_grps_num(struct mlx5e_priv *priv)
{
	return ARRAY_SIZE(mlx5i_stats_grps);
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
	.update_rx	   = mlx5i_update_nic_rx,
	.update_stats	   = NULL, /* mlx5i_update_stats */
	.update_carrier    = NULL, /* no HW update in IB link */
	.rx_handlers.handle_rx_cqe       = mlx5i_handle_rx_cqe,
	.rx_handlers.handle_rx_cqe_mpwqe = NULL, /* Not supported */
	.max_tc		   = MLX5I_MAX_NUM_TC,
	.rq_groups	   = MLX5E_NUM_RQ_GROUPS(REGULAR),
	.stats_grps        = mlx5i_stats_grps,
	.stats_grps_num    = mlx5i_stats_grps_num,
};

/* mlx5i netdev NDos */

static int mlx5i_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);
	struct mlx5e_channels new_channels = {};
	struct mlx5e_params *params;
	int err = 0;

	mutex_lock(&priv->state_lock);

	params = &priv->channels.params;

	if (!test_bit(MLX5E_STATE_OPENED, &priv->state)) {
		params->sw_mtu = new_mtu;
		netdev->mtu = params->sw_mtu;
		goto out;
	}

	new_channels.params = *params;
	new_channels.params.sw_mtu = new_mtu;

	err = mlx5e_safe_switch_channels(priv, &new_channels, NULL, NULL);
	if (err)
		goto out;

	netdev->mtu = new_channels.params.sw_mtu;

out:
	mutex_unlock(&priv->state_lock);
	return err;
}

int mlx5i_dev_init(struct net_device *dev)
{
	struct mlx5e_priv    *priv   = mlx5i_epriv(dev);
	struct mlx5i_priv    *ipriv  = priv->ppriv;

	/* Set dev address using underlay QP */
	dev->dev_addr[1] = (ipriv->qpn >> 16) & 0xff;
	dev->dev_addr[2] = (ipriv->qpn >>  8) & 0xff;
	dev->dev_addr[3] = (ipriv->qpn) & 0xff;

	/* Add QPN to net-device mapping to HT */
	mlx5i_pkey_add_qpn(dev, ipriv->qpn);

	return 0;
}

int mlx5i_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
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

void mlx5i_dev_cleanup(struct net_device *dev)
{
	struct mlx5e_priv    *priv   = mlx5i_epriv(dev);
	struct mlx5i_priv    *ipriv = priv->ppriv;

	mlx5i_uninit_underlay_qp(priv);

	/* Delete QPN to net-device mapping from HT */
	mlx5i_pkey_del_qpn(dev, ipriv->qpn);
}

static int mlx5i_open(struct net_device *netdev)
{
	struct mlx5e_priv *epriv = mlx5i_epriv(netdev);
	struct mlx5i_priv *ipriv = epriv->ppriv;
	struct mlx5_core_dev *mdev = epriv->mdev;
	int err;

	mutex_lock(&epriv->state_lock);

	set_bit(MLX5E_STATE_OPENED, &epriv->state);

	err = mlx5i_init_underlay_qp(epriv);
	if (err) {
		mlx5_core_warn(mdev, "prepare underlay qp state failed, %d\n", err);
		goto err_clear_state_opened_flag;
	}

	err = mlx5_fs_add_rx_underlay_qpn(mdev, ipriv->qpn);
	if (err) {
		mlx5_core_warn(mdev, "attach underlay qp to ft failed, %d\n", err);
		goto err_reset_qp;
	}

	err = mlx5e_open_channels(epriv, &epriv->channels);
	if (err)
		goto err_remove_fs_underlay_qp;

	epriv->profile->update_rx(epriv);
	mlx5e_activate_priv_channels(epriv);

	mutex_unlock(&epriv->state_lock);
	return 0;

err_remove_fs_underlay_qp:
	mlx5_fs_remove_rx_underlay_qpn(mdev, ipriv->qpn);
err_reset_qp:
	mlx5i_uninit_underlay_qp(epriv);
err_clear_state_opened_flag:
	clear_bit(MLX5E_STATE_OPENED, &epriv->state);
	mutex_unlock(&epriv->state_lock);
	return err;
}

static int mlx5i_close(struct net_device *netdev)
{
	struct mlx5e_priv *epriv = mlx5i_epriv(netdev);
	struct mlx5i_priv *ipriv = epriv->ppriv;
	struct mlx5_core_dev *mdev = epriv->mdev;

	/* May already be CLOSED in case a previous configuration operation
	 * (e.g RX/TX queue size change) that involves close&open failed.
	 */
	mutex_lock(&epriv->state_lock);

	if (!test_bit(MLX5E_STATE_OPENED, &epriv->state))
		goto unlock;

	clear_bit(MLX5E_STATE_OPENED, &epriv->state);

	netif_carrier_off(epriv->netdev);
	mlx5_fs_remove_rx_underlay_qpn(mdev, ipriv->qpn);
	mlx5e_deactivate_priv_channels(epriv);
	mlx5e_close_channels(&epriv->channels);
	mlx5i_uninit_underlay_qp(epriv);
unlock:
	mutex_unlock(&epriv->state_lock);
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

	mlx5_core_dbg(mdev, "attaching QPN 0x%x, MGID %pI6\n", ipriv->qpn,
		      gid->raw);
	err = mlx5_core_attach_mcg(mdev, gid, ipriv->qpn);
	if (err)
		mlx5_core_warn(mdev, "failed attaching QPN 0x%x, MGID %pI6\n",
			       ipriv->qpn, gid->raw);

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

	mlx5_core_dbg(mdev, "detaching QPN 0x%x, MGID %pI6\n", ipriv->qpn,
		      gid->raw);

	err = mlx5_core_detach_mcg(mdev, gid, ipriv->qpn);
	if (err)
		mlx5_core_dbg(mdev, "failed detaching QPN 0x%x, MGID %pI6\n",
			      ipriv->qpn, gid->raw);

	return err;
}

static int mlx5i_xmit(struct net_device *dev, struct sk_buff *skb,
		      struct ib_ah *address, u32 dqpn)
{
	struct mlx5e_priv *epriv = mlx5i_epriv(dev);
	struct mlx5e_txqsq *sq   = epriv->txq2sq[skb_get_queue_mapping(skb)];
	struct mlx5_ib_ah *mah   = to_mah(address);
	struct mlx5i_priv *ipriv = epriv->ppriv;

	mlx5i_sq_xmit(sq, skb, &mah->av, dqpn, ipriv->qkey, netdev_xmit_more());

	return NETDEV_TX_OK;
}

static void mlx5i_set_pkey_index(struct net_device *netdev, int id)
{
	struct mlx5i_priv *ipriv = netdev_priv(netdev);

	ipriv->pkey_index = (u16)id;
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

static void mlx5_rdma_netdev_free(struct net_device *netdev)
{
	struct mlx5e_priv *priv = mlx5i_epriv(netdev);
	struct mlx5i_priv *ipriv = priv->ppriv;
	const struct mlx5e_profile *profile = priv->profile;

	mlx5e_detach_netdev(priv);
	profile->cleanup(priv);

	if (!ipriv->sub_interface) {
		mlx5i_pkey_qpn_ht_cleanup(netdev);
		mlx5e_destroy_mdev_resources(priv->mdev);
	}
}

static bool mlx5_is_sub_interface(struct mlx5_core_dev *mdev)
{
	return mdev->mlx5e_res.pdn != 0;
}

static const struct mlx5e_profile *mlx5_get_profile(struct mlx5_core_dev *mdev)
{
	if (mlx5_is_sub_interface(mdev))
		return mlx5i_pkey_get_profile();
	return &mlx5i_nic_profile;
}

static int mlx5_rdma_setup_rn(struct ib_device *ibdev, u8 port_num,
			      struct net_device *netdev, void *param)
{
	struct mlx5_core_dev *mdev = (struct mlx5_core_dev *)param;
	const struct mlx5e_profile *prof = mlx5_get_profile(mdev);
	struct mlx5i_priv *ipriv;
	struct mlx5e_priv *epriv;
	struct rdma_netdev *rn;
	int err;

	ipriv = netdev_priv(netdev);
	epriv = mlx5i_epriv(netdev);

	ipriv->sub_interface = mlx5_is_sub_interface(mdev);
	if (!ipriv->sub_interface) {
		err = mlx5i_pkey_qpn_ht_init(netdev);
		if (err) {
			mlx5_core_warn(mdev, "allocate qpn_to_netdev ht failed\n");
			return err;
		}

		/* This should only be called once per mdev */
		err = mlx5e_create_mdev_resources(mdev);
		if (err)
			goto destroy_ht;
	}

	prof->init(mdev, netdev, prof, ipriv);

	err = mlx5e_attach_netdev(epriv);
	if (err)
		goto detach;
	netif_carrier_off(netdev);

	/* set rdma_netdev func pointers */
	rn = &ipriv->rn;
	rn->hca  = ibdev;
	rn->send = mlx5i_xmit;
	rn->attach_mcast = mlx5i_attach_mcast;
	rn->detach_mcast = mlx5i_detach_mcast;
	rn->set_id = mlx5i_set_pkey_index;

	netdev->priv_destructor = mlx5_rdma_netdev_free;
	netdev->needs_free_netdev = 1;

	return 0;

detach:
	prof->cleanup(epriv);
	if (ipriv->sub_interface)
		return err;
	mlx5e_destroy_mdev_resources(mdev);
destroy_ht:
	mlx5i_pkey_qpn_ht_cleanup(netdev);
	return err;
}

int mlx5_rdma_rn_get_params(struct mlx5_core_dev *mdev,
			    struct ib_device *device,
			    struct rdma_netdev_alloc_params *params)
{
	int nch;
	int rc;

	rc = mlx5i_check_required_hca_cap(mdev);
	if (rc)
		return rc;

	nch = mlx5e_get_max_num_channels(mdev);

	*params = (struct rdma_netdev_alloc_params){
		.sizeof_priv = sizeof(struct mlx5i_priv) +
			       sizeof(struct mlx5e_priv),
		.txqs = nch * MLX5E_MAX_NUM_TC,
		.rxqs = nch,
		.param = mdev,
		.initialize_rdma_netdev = mlx5_rdma_setup_rn,
	};

	return 0;
}
EXPORT_SYMBOL(mlx5_rdma_rn_get_params);
