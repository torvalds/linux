// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

#include <linux/kernel.h>
#include "en/params.h"
#include "en/health.h"
#include "lib/eq.h"
#include "en/dcbnl.h"
#include "en_accel/ipsec.h"
#include "en_accel/en_accel.h"
#include "en/trap.h"
#include "en/monitor_stats.h"
#include "en/hv_vhca_stats.h"
#include "en_rep.h"
#include "en.h"

static int mgmt_pf_async_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5e_priv *priv = container_of(nb, struct mlx5e_priv, events_nb);
	struct mlx5_eqe   *eqe = data;

	if (event != MLX5_EVENT_TYPE_PORT_CHANGE)
		return NOTIFY_DONE;

	switch (eqe->sub_type) {
	case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
	case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
		queue_work(priv->wq, &priv->update_carrier_work);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static void mlx5e_mgmt_pf_enable_async_events(struct mlx5e_priv *priv)
{
	priv->events_nb.notifier_call = mgmt_pf_async_event;
	mlx5_notifier_register(priv->mdev, &priv->events_nb);
}

static void mlx5e_disable_mgmt_pf_async_events(struct mlx5e_priv *priv)
{
	mlx5_notifier_unregister(priv->mdev, &priv->events_nb);
}

static void mlx5e_modify_mgmt_pf_admin_state(struct mlx5_core_dev *mdev,
					     enum mlx5_port_status state)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	int vport_admin_state;

	mlx5_set_port_admin_status(mdev, state);

	if (state == MLX5_PORT_UP)
		vport_admin_state = MLX5_VPORT_ADMIN_STATE_AUTO;
	else
		vport_admin_state = MLX5_VPORT_ADMIN_STATE_DOWN;

	mlx5_eswitch_set_vport_state(esw, MLX5_VPORT_UPLINK, vport_admin_state);
}

static void mlx5e_build_mgmt_pf_nic_params(struct mlx5e_priv *priv, u16 mtu)
{
	struct mlx5e_params *params = &priv->channels.params;
	struct mlx5_core_dev *mdev = priv->mdev;
	u8 rx_cq_period_mode;

	params->sw_mtu = mtu;
	params->hard_mtu = MLX5E_ETH_HARD_MTU;
	params->num_channels = 1;

	/* SQ */
	params->log_sq_size = is_kdump_kernel() ?
		MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE :
		MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;
	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_SKB_TX_MPWQE, mlx5e_tx_mpwqe_supported(mdev));

	MLX5E_SET_PFLAG(params, MLX5E_PFLAG_RX_NO_CSUM_COMPLETE, false);

	/* RQ */
	mlx5e_build_rq_params(mdev, params);

	/* CQ moderation params */
	rx_cq_period_mode = MLX5_CAP_GEN(mdev, cq_period_start_from_cqe) ?
			MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
			MLX5_CQ_PERIOD_MODE_START_FROM_EQE;
	params->rx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	params->tx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	mlx5e_set_rx_cq_mode_params(params, rx_cq_period_mode);
	mlx5e_set_tx_cq_mode_params(params, MLX5_CQ_PERIOD_MODE_START_FROM_EQE);

	/* TX inline */
	mlx5_query_min_inline(mdev, &params->tx_min_inline_mode);
}

static int mlx5e_mgmt_pf_init(struct mlx5_core_dev *mdev,
			      struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_flow_steering *fs;
	int err;

	mlx5e_build_mgmt_pf_nic_params(priv, netdev->mtu);

	mlx5e_timestamp_init(priv);

	fs = mlx5e_fs_init(priv->profile, mdev,
			   !test_bit(MLX5E_STATE_DESTROYING, &priv->state),
			   priv->dfs_root);
	if (!fs) {
		err = -ENOMEM;
		mlx5_core_err(mdev, "FS initialization failed, %d\n", err);
		return err;
	}
	priv->fs = fs;

	mlx5e_health_create_reporters(priv);

	return 0;
}

static void mlx5e_mgmt_pf_cleanup(struct mlx5e_priv *priv)
{
	mlx5e_health_destroy_reporters(priv);
	mlx5e_fs_cleanup(priv->fs);
	priv->fs = NULL;
}

static int mlx5e_mgmt_pf_init_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	priv->rx_res = mlx5e_rx_res_create(mdev, 0, priv->max_nch, priv->drop_rq.rqn,
					   &priv->channels.params.packet_merge,
					   priv->channels.params.num_channels);
	if (!priv->rx_res)
		return -ENOMEM;

	mlx5e_create_q_counters(priv);

	err = mlx5e_open_drop_rq(priv, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_destroy_q_counters;
	}

	err = mlx5e_create_flow_steering(priv->fs, priv->rx_res, priv->profile,
					 priv->netdev);
	if (err) {
		mlx5_core_warn(mdev, "create flow steering failed, %d\n", err);
		goto err_destroy_rx_res;
	}

	return 0;

err_destroy_rx_res:
	mlx5e_rx_res_destroy(priv->rx_res);
	priv->rx_res = NULL;
	mlx5e_close_drop_rq(&priv->drop_rq);
err_destroy_q_counters:
	mlx5e_destroy_q_counters(priv);
	return err;
}

static void mlx5e_mgmt_pf_cleanup_rx(struct mlx5e_priv *priv)
{
	mlx5e_destroy_flow_steering(priv->fs, !!(priv->netdev->hw_features & NETIF_F_NTUPLE),
				    priv->profile);
	mlx5e_rx_res_destroy(priv->rx_res);
	priv->rx_res = NULL;
	mlx5e_close_drop_rq(&priv->drop_rq);
	mlx5e_destroy_q_counters(priv);
}

static int mlx5e_mgmt_pf_init_tx(struct mlx5e_priv *priv)
{
	return 0;
}

static void mlx5e_mgmt_pf_cleanup_tx(struct mlx5e_priv *priv)
{
}

static void mlx5e_mgmt_pf_enable(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;

	mlx5e_fs_init_l2_addr(priv->fs, netdev);

	/* Marking the link as currently not needed by the Driver */
	if (!netif_running(netdev))
		mlx5e_modify_mgmt_pf_admin_state(mdev, MLX5_PORT_DOWN);

	mlx5e_set_netdev_mtu_boundaries(priv);
	mlx5e_set_dev_port_mtu(priv);

	mlx5e_mgmt_pf_enable_async_events(priv);
	if (mlx5e_monitor_counter_supported(priv))
		mlx5e_monitor_counter_init(priv);

	mlx5e_hv_vhca_stats_create(priv);
	if (netdev->reg_state != NETREG_REGISTERED)
		return;
	mlx5e_dcbnl_init_app(priv);

	mlx5e_nic_set_rx_mode(priv);

	rtnl_lock();
	if (netif_running(netdev))
		mlx5e_open(netdev);
	udp_tunnel_nic_reset_ntf(priv->netdev);
	netif_device_attach(netdev);
	rtnl_unlock();
}

static void mlx5e_mgmt_pf_disable(struct mlx5e_priv *priv)
{
	if (priv->netdev->reg_state == NETREG_REGISTERED)
		mlx5e_dcbnl_delete_app(priv);

	rtnl_lock();
	if (netif_running(priv->netdev))
		mlx5e_close(priv->netdev);
	netif_device_detach(priv->netdev);
	rtnl_unlock();

	mlx5e_nic_set_rx_mode(priv);

	mlx5e_hv_vhca_stats_destroy(priv);
	if (mlx5e_monitor_counter_supported(priv))
		mlx5e_monitor_counter_cleanup(priv);

	mlx5e_disable_mgmt_pf_async_events(priv);
	mlx5e_ipsec_cleanup(priv);
}

static int mlx5e_mgmt_pf_update_rx(struct mlx5e_priv *priv)
{
	return mlx5e_refresh_tirs(priv, false, false);
}

static int mlx5e_mgmt_pf_max_nch_limit(struct mlx5_core_dev *mdev)
{
	return 1;
}

const struct mlx5e_profile mlx5e_mgmt_pf_nic_profile = {
	.init		   = mlx5e_mgmt_pf_init,
	.cleanup	   = mlx5e_mgmt_pf_cleanup,
	.init_rx	   = mlx5e_mgmt_pf_init_rx,
	.cleanup_rx	   = mlx5e_mgmt_pf_cleanup_rx,
	.init_tx	   = mlx5e_mgmt_pf_init_tx,
	.cleanup_tx	   = mlx5e_mgmt_pf_cleanup_tx,
	.enable		   = mlx5e_mgmt_pf_enable,
	.disable	   = mlx5e_mgmt_pf_disable,
	.update_rx	   = mlx5e_mgmt_pf_update_rx,
	.update_stats	   = mlx5e_stats_update_ndo_stats,
	.update_carrier	   = mlx5e_update_carrier,
	.rx_handlers       = &mlx5e_rx_handlers_nic,
	.max_tc		   = 1,
	.max_nch_limit	   = mlx5e_mgmt_pf_max_nch_limit,
	.stats_grps	   = mlx5e_nic_stats_grps,
	.stats_grps_num	   = mlx5e_nic_stats_grps_num
};
