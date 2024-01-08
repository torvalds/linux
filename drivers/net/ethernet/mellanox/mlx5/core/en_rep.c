/*
 * Copyright (c) 2016, Mellanox Technologies. All rights reserved.
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

#include <linux/debugfs.h>
#include <linux/mlx5/fs.h>
#include <net/switchdev.h>
#include <net/pkt_cls.h>
#include <net/act_api.h>
#include <net/devlink.h>
#include <net/ipv6_stubs.h>

#include "eswitch.h"
#include "en.h"
#include "en_rep.h"
#include "en/params.h"
#include "en/txrx.h"
#include "en_tc.h"
#include "en/rep/tc.h"
#include "en/rep/neigh.h"
#include "en/rep/bridge.h"
#include "en/devlink.h"
#include "fs_core.h"
#include "lib/mlx5.h"
#include "lib/devcom.h"
#include "lib/vxlan.h"
#define CREATE_TRACE_POINTS
#include "diag/en_rep_tracepoint.h"
#include "diag/reporter_vnic.h"
#include "en_accel/ipsec.h"
#include "en/tc/int_port.h"
#include "en/ptp.h"
#include "en/fs_ethtool.h"

#define MLX5E_REP_PARAMS_DEF_LOG_SQ_SIZE \
	max(0x7, MLX5E_PARAMS_MINIMUM_LOG_SQ_SIZE)
#define MLX5E_REP_PARAMS_DEF_NUM_CHANNELS 1

static const char mlx5e_rep_driver_name[] = "mlx5e_rep";

static void mlx5e_rep_get_drvinfo(struct net_device *dev,
				  struct ethtool_drvinfo *drvinfo)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5_core_dev *mdev = priv->mdev;
	int count;

	strscpy(drvinfo->driver, mlx5e_rep_driver_name,
		sizeof(drvinfo->driver));
	count = snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
			 "%d.%d.%04d (%.16s)", fw_rev_maj(mdev),
			 fw_rev_min(mdev), fw_rev_sub(mdev), mdev->board_id);
	if (count == sizeof(drvinfo->fw_version))
		snprintf(drvinfo->fw_version, sizeof(drvinfo->fw_version),
			 "%d.%d.%04d", fw_rev_maj(mdev),
			 fw_rev_min(mdev), fw_rev_sub(mdev));
}

static const struct counter_desc sw_rep_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_sw_stats, tx_bytes) },
};

static const struct counter_desc vport_rep_stats_desc[] = {
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats, vport_rx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats, vport_rx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats, vport_tx_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats, vport_tx_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats,
			     rx_vport_rdma_unicast_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats, rx_vport_rdma_unicast_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats,
			     tx_vport_rdma_unicast_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats, tx_vport_rdma_unicast_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats,
			     rx_vport_rdma_multicast_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats,
			     rx_vport_rdma_multicast_bytes) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats,
			     tx_vport_rdma_multicast_packets) },
	{ MLX5E_DECLARE_STAT(struct mlx5e_rep_stats,
			     tx_vport_rdma_multicast_bytes) },
};

#define NUM_VPORT_REP_SW_COUNTERS ARRAY_SIZE(sw_rep_stats_desc)
#define NUM_VPORT_REP_HW_COUNTERS ARRAY_SIZE(vport_rep_stats_desc)

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(sw_rep)
{
	return NUM_VPORT_REP_SW_COUNTERS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(sw_rep)
{
	int i;

	for (i = 0; i < NUM_VPORT_REP_SW_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN,
		       sw_rep_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(sw_rep)
{
	int i;

	for (i = 0; i < NUM_VPORT_REP_SW_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(&priv->stats.sw,
						   sw_rep_stats_desc, i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(sw_rep)
{
	struct mlx5e_sw_stats *s = &priv->stats.sw;
	struct rtnl_link_stats64 stats64 = {};

	memset(s, 0, sizeof(*s));
	mlx5e_fold_sw_stats64(priv, &stats64);

	s->rx_packets = stats64.rx_packets;
	s->rx_bytes   = stats64.rx_bytes;
	s->tx_packets = stats64.tx_packets;
	s->tx_bytes   = stats64.tx_bytes;
	s->tx_queue_dropped = stats64.tx_dropped;
}

static MLX5E_DECLARE_STATS_GRP_OP_NUM_STATS(vport_rep)
{
	return NUM_VPORT_REP_HW_COUNTERS;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STRS(vport_rep)
{
	int i;

	for (i = 0; i < NUM_VPORT_REP_HW_COUNTERS; i++)
		strcpy(data + (idx++) * ETH_GSTRING_LEN, vport_rep_stats_desc[i].format);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_FILL_STATS(vport_rep)
{
	int i;

	for (i = 0; i < NUM_VPORT_REP_HW_COUNTERS; i++)
		data[idx++] = MLX5E_READ_CTR64_CPU(&priv->stats.rep_stats,
						   vport_rep_stats_desc, i);
	return idx;
}

static MLX5E_DECLARE_STATS_GRP_OP_UPDATE_STATS(vport_rep)
{
	struct mlx5e_rep_stats *rep_stats = &priv->stats.rep_stats;
	int outlen = MLX5_ST_SZ_BYTES(query_vport_counter_out);
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	u32 *out;
	int err;

	out = kvzalloc(outlen, GFP_KERNEL);
	if (!out)
		return;

	err = mlx5_core_query_vport_counter(esw->dev, 1, rep->vport - 1, 0, out);
	if (err) {
		netdev_warn(priv->netdev, "vport %d error %d reading stats\n",
			    rep->vport, err);
		goto out;
	}

	#define MLX5_GET_CTR(p, x) \
		MLX5_GET64(query_vport_counter_out, p, x)
	/* flip tx/rx as we are reporting the counters for the switch vport */
	rep_stats->vport_rx_packets =
		MLX5_GET_CTR(out, transmitted_ib_unicast.packets) +
		MLX5_GET_CTR(out, transmitted_eth_unicast.packets) +
		MLX5_GET_CTR(out, transmitted_ib_multicast.packets) +
		MLX5_GET_CTR(out, transmitted_eth_multicast.packets) +
		MLX5_GET_CTR(out, transmitted_eth_broadcast.packets);

	rep_stats->vport_tx_packets =
		MLX5_GET_CTR(out, received_ib_unicast.packets) +
		MLX5_GET_CTR(out, received_eth_unicast.packets) +
		MLX5_GET_CTR(out, received_ib_multicast.packets) +
		MLX5_GET_CTR(out, received_eth_multicast.packets) +
		MLX5_GET_CTR(out, received_eth_broadcast.packets);

	rep_stats->vport_rx_bytes =
		MLX5_GET_CTR(out, transmitted_ib_unicast.octets) +
		MLX5_GET_CTR(out, transmitted_eth_unicast.octets) +
		MLX5_GET_CTR(out, transmitted_ib_multicast.octets) +
		MLX5_GET_CTR(out, transmitted_eth_broadcast.octets);

	rep_stats->vport_tx_bytes =
		MLX5_GET_CTR(out, received_ib_unicast.octets) +
		MLX5_GET_CTR(out, received_eth_unicast.octets) +
		MLX5_GET_CTR(out, received_ib_multicast.octets) +
		MLX5_GET_CTR(out, received_eth_multicast.octets) +
		MLX5_GET_CTR(out, received_eth_broadcast.octets);

	rep_stats->rx_vport_rdma_unicast_packets =
		MLX5_GET_CTR(out, transmitted_ib_unicast.packets);
	rep_stats->tx_vport_rdma_unicast_packets =
		MLX5_GET_CTR(out, received_ib_unicast.packets);
	rep_stats->rx_vport_rdma_unicast_bytes =
		MLX5_GET_CTR(out, transmitted_ib_unicast.octets);
	rep_stats->tx_vport_rdma_unicast_bytes =
		MLX5_GET_CTR(out, received_ib_unicast.octets);
	rep_stats->rx_vport_rdma_multicast_packets =
		MLX5_GET_CTR(out, transmitted_ib_multicast.packets);
	rep_stats->tx_vport_rdma_multicast_packets =
		MLX5_GET_CTR(out, received_ib_multicast.packets);
	rep_stats->rx_vport_rdma_multicast_bytes =
		MLX5_GET_CTR(out, transmitted_ib_multicast.octets);
	rep_stats->tx_vport_rdma_multicast_bytes =
		MLX5_GET_CTR(out, received_ib_multicast.octets);

out:
	kvfree(out);
}

static void mlx5e_rep_get_strings(struct net_device *dev,
				  u32 stringset, uint8_t *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	switch (stringset) {
	case ETH_SS_STATS:
		mlx5e_stats_fill_strings(priv, data);
		break;
	}
}

static void mlx5e_rep_get_ethtool_stats(struct net_device *dev,
					struct ethtool_stats *stats, u64 *data)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_ethtool_stats(priv, stats, data);
}

static int mlx5e_rep_get_sset_count(struct net_device *dev, int sset)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	switch (sset) {
	case ETH_SS_STATS:
		return mlx5e_stats_total_num(priv);
	default:
		return -EOPNOTSUPP;
	}
}

static void
mlx5e_rep_get_ringparam(struct net_device *dev,
			struct ethtool_ringparam *param,
			struct kernel_ethtool_ringparam *kernel_param,
			struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_ringparam(priv, param, kernel_param);
}

static int
mlx5e_rep_set_ringparam(struct net_device *dev,
			struct ethtool_ringparam *param,
			struct kernel_ethtool_ringparam *kernel_param,
			struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_set_ringparam(priv, param);
}

static void mlx5e_rep_get_channels(struct net_device *dev,
				   struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_ethtool_get_channels(priv, ch);
}

static int mlx5e_rep_set_channels(struct net_device *dev,
				  struct ethtool_channels *ch)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	return mlx5e_ethtool_set_channels(priv, ch);
}

static int mlx5e_rep_get_coalesce(struct net_device *netdev,
				  struct ethtool_coalesce *coal,
				  struct kernel_ethtool_coalesce *kernel_coal,
				  struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_get_coalesce(priv, coal, kernel_coal);
}

static int mlx5e_rep_set_coalesce(struct net_device *netdev,
				  struct ethtool_coalesce *coal,
				  struct kernel_ethtool_coalesce *kernel_coal,
				  struct netlink_ext_ack *extack)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_set_coalesce(priv, coal, kernel_coal, extack);
}

static u32 mlx5e_rep_get_rxfh_key_size(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_get_rxfh_key_size(priv);
}

static u32 mlx5e_rep_get_rxfh_indir_size(struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	return mlx5e_ethtool_get_rxfh_indir_size(priv);
}

static const struct ethtool_ops mlx5e_rep_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS |
				     ETHTOOL_COALESCE_MAX_FRAMES |
				     ETHTOOL_COALESCE_USE_ADAPTIVE,
	.get_drvinfo	   = mlx5e_rep_get_drvinfo,
	.get_link	   = ethtool_op_get_link,
	.get_strings       = mlx5e_rep_get_strings,
	.get_sset_count    = mlx5e_rep_get_sset_count,
	.get_ethtool_stats = mlx5e_rep_get_ethtool_stats,
	.get_ringparam     = mlx5e_rep_get_ringparam,
	.set_ringparam     = mlx5e_rep_set_ringparam,
	.get_channels      = mlx5e_rep_get_channels,
	.set_channels      = mlx5e_rep_set_channels,
	.get_coalesce      = mlx5e_rep_get_coalesce,
	.set_coalesce      = mlx5e_rep_set_coalesce,
	.get_rxfh_key_size   = mlx5e_rep_get_rxfh_key_size,
	.get_rxfh_indir_size = mlx5e_rep_get_rxfh_indir_size,
};

static void mlx5e_sqs2vport_stop(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_sq *rep_sq, *tmp;
	struct mlx5e_rep_sq_peer *sq_peer;
	struct mlx5e_rep_priv *rpriv;
	unsigned long i;

	if (esw->mode != MLX5_ESWITCH_OFFLOADS)
		return;

	rpriv = mlx5e_rep_to_rep_priv(rep);
	list_for_each_entry_safe(rep_sq, tmp, &rpriv->vport_sqs_list, list) {
		mlx5_eswitch_del_send_to_vport_rule(rep_sq->send_to_vport_rule);
		xa_for_each(&rep_sq->sq_peer, i, sq_peer) {
			if (sq_peer->rule)
				mlx5_eswitch_del_send_to_vport_rule(sq_peer->rule);

			xa_erase(&rep_sq->sq_peer, i);
			kfree(sq_peer);
		}

		xa_destroy(&rep_sq->sq_peer);
		list_del(&rep_sq->list);
		kfree(rep_sq);
	}
}

static int mlx5e_sqs2vport_add_peers_rules(struct mlx5_eswitch *esw, struct mlx5_eswitch_rep *rep,
					   struct mlx5e_rep_sq *rep_sq, int i)
{
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_devcom_comp_dev *tmp;
	struct mlx5_eswitch *peer_esw;

	mlx5_devcom_for_each_peer_entry(esw->devcom, peer_esw, tmp) {
		u16 peer_rule_idx = MLX5_CAP_GEN(peer_esw->dev, vhca_id);
		struct mlx5e_rep_sq_peer *sq_peer;
		int err;

		sq_peer = kzalloc(sizeof(*sq_peer), GFP_KERNEL);
		if (!sq_peer)
			return -ENOMEM;

		flow_rule = mlx5_eswitch_add_send_to_vport_rule(peer_esw, esw,
								rep, rep_sq->sqn);
		if (IS_ERR(flow_rule)) {
			kfree(sq_peer);
			return PTR_ERR(flow_rule);
		}

		sq_peer->rule = flow_rule;
		sq_peer->peer = peer_esw;
		err = xa_insert(&rep_sq->sq_peer, peer_rule_idx, sq_peer, GFP_KERNEL);
		if (err) {
			kfree(sq_peer);
			mlx5_eswitch_del_send_to_vport_rule(flow_rule);
			return err;
		}
	}

	return 0;
}

static int mlx5e_sqs2vport_start(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep,
				 u32 *sqns_array, int sqns_num)
{
	struct mlx5_flow_handle *flow_rule;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_rep_sq *rep_sq;
	bool devcom_locked = false;
	int err;
	int i;

	if (esw->mode != MLX5_ESWITCH_OFFLOADS)
		return 0;

	rpriv = mlx5e_rep_to_rep_priv(rep);

	if (mlx5_devcom_comp_is_ready(esw->devcom) &&
	    mlx5_devcom_for_each_peer_begin(esw->devcom))
		devcom_locked = true;

	for (i = 0; i < sqns_num; i++) {
		rep_sq = kzalloc(sizeof(*rep_sq), GFP_KERNEL);
		if (!rep_sq) {
			err = -ENOMEM;
			goto out_err;
		}

		/* Add re-inject rule to the PF/representor sqs */
		flow_rule = mlx5_eswitch_add_send_to_vport_rule(esw, esw, rep,
								sqns_array[i]);
		if (IS_ERR(flow_rule)) {
			err = PTR_ERR(flow_rule);
			kfree(rep_sq);
			goto out_err;
		}
		rep_sq->send_to_vport_rule = flow_rule;
		rep_sq->sqn = sqns_array[i];

		xa_init(&rep_sq->sq_peer);
		if (devcom_locked) {
			err = mlx5e_sqs2vport_add_peers_rules(esw, rep, rep_sq, i);
			if (err) {
				mlx5_eswitch_del_send_to_vport_rule(rep_sq->send_to_vport_rule);
				xa_destroy(&rep_sq->sq_peer);
				kfree(rep_sq);
				goto out_err;
			}
		}

		list_add(&rep_sq->list, &rpriv->vport_sqs_list);
	}

	if (devcom_locked)
		mlx5_devcom_for_each_peer_end(esw->devcom);

	return 0;

out_err:
	mlx5e_sqs2vport_stop(esw, rep);

	if (devcom_locked)
		mlx5_devcom_for_each_peer_end(esw->devcom);

	return err;
}

static int
mlx5e_add_sqs_fwd_rules(struct mlx5e_priv *priv)
{
	int sqs_per_channel = mlx5e_get_dcb_num_tc(&priv->channels.params);
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	bool is_uplink_rep = mlx5e_is_uplink_rep(priv);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	int n, tc, nch, num_sqs = 0;
	struct mlx5e_channel *c;
	int err = -ENOMEM;
	bool ptp_sq;
	u32 *sqs;

	ptp_sq = !!(priv->channels.ptp &&
		    MLX5E_GET_PFLAG(&priv->channels.params, MLX5E_PFLAG_TX_PORT_TS));
	nch = priv->channels.num + ptp_sq;
	/* +2 for xdpsqs, they don't exist on the ptp channel but will not be
	 * counted for by num_sqs.
	 */
	if (is_uplink_rep)
		sqs_per_channel += 2;

	sqs = kvcalloc(nch * sqs_per_channel, sizeof(*sqs), GFP_KERNEL);
	if (!sqs)
		goto out;

	for (n = 0; n < priv->channels.num; n++) {
		c = priv->channels.c[n];
		for (tc = 0; tc < c->num_tc; tc++)
			sqs[num_sqs++] = c->sq[tc].sqn;

		if (is_uplink_rep) {
			if (c->xdp)
				sqs[num_sqs++] = c->rq_xdpsq.sqn;

			sqs[num_sqs++] = c->xdpsq.sqn;
		}
	}
	if (ptp_sq) {
		struct mlx5e_ptp *ptp_ch = priv->channels.ptp;

		for (tc = 0; tc < ptp_ch->num_tc; tc++)
			sqs[num_sqs++] = ptp_ch->ptpsq[tc].txqsq.sqn;
	}

	err = mlx5e_sqs2vport_start(esw, rep, sqs, num_sqs);
	kvfree(sqs);

out:
	if (err)
		netdev_warn(priv->netdev, "Failed to add SQs FWD rules %d\n", err);
	return err;
}

static void
mlx5e_remove_sqs_fwd_rules(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;

	mlx5e_sqs2vport_stop(esw, rep);
}

static int
mlx5e_rep_add_meta_tunnel_rule(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_group *g;

	g = esw->fdb_table.offloads.send_to_vport_meta_grp;
	if (!g)
		return 0;

	flow_rule = mlx5_eswitch_add_send_to_vport_meta_rule(esw, rep->vport);
	if (IS_ERR(flow_rule))
		return PTR_ERR(flow_rule);

	rpriv->send_to_vport_meta_rule = flow_rule;

	return 0;
}

static void
mlx5e_rep_del_meta_tunnel_rule(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	if (rpriv->send_to_vport_meta_rule)
		mlx5_eswitch_del_send_to_vport_meta_rule(rpriv->send_to_vport_meta_rule);
}

void mlx5e_rep_activate_channels(struct mlx5e_priv *priv)
{
	mlx5e_add_sqs_fwd_rules(priv);
	mlx5e_rep_add_meta_tunnel_rule(priv);
}

void mlx5e_rep_deactivate_channels(struct mlx5e_priv *priv)
{
	mlx5e_rep_del_meta_tunnel_rule(priv);
	mlx5e_remove_sqs_fwd_rules(priv);
}

static int mlx5e_rep_open(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	int err;

	mutex_lock(&priv->state_lock);
	err = mlx5e_open_locked(dev);
	if (err)
		goto unlock;

	if (!mlx5_modify_vport_admin_state(priv->mdev,
					   MLX5_VPORT_STATE_OP_MOD_ESW_VPORT,
					   rep->vport, 1,
					   MLX5_VPORT_ADMIN_STATE_UP))
		netif_carrier_on(dev);

unlock:
	mutex_unlock(&priv->state_lock);
	return err;
}

static int mlx5e_rep_close(struct net_device *dev)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	int ret;

	mutex_lock(&priv->state_lock);
	mlx5_modify_vport_admin_state(priv->mdev,
				      MLX5_VPORT_STATE_OP_MOD_ESW_VPORT,
				      rep->vport, 1,
				      MLX5_VPORT_ADMIN_STATE_DOWN);
	ret = mlx5e_close_locked(dev);
	mutex_unlock(&priv->state_lock);
	return ret;
}

bool mlx5e_is_uplink_rep(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep;

	if (!MLX5_ESWITCH_MANAGER(priv->mdev))
		return false;

	if (!rpriv) /* non vport rep mlx5e instances don't use this field */
		return false;

	rep = rpriv->rep;
	return (rep->vport == MLX5_VPORT_UPLINK);
}

bool mlx5e_rep_has_offload_stats(const struct net_device *dev, int attr_id)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
			return true;
	}

	return false;
}

static int
mlx5e_get_sw_stats64(const struct net_device *dev,
		     struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	mlx5e_fold_sw_stats64(priv, stats);
	return 0;
}

int mlx5e_rep_get_offload_stats(int attr_id, const struct net_device *dev,
				void *sp)
{
	switch (attr_id) {
	case IFLA_OFFLOAD_XSTATS_CPU_HIT:
		return mlx5e_get_sw_stats64(dev, sp);
	}

	return -EINVAL;
}

static void
mlx5e_rep_get_stats(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct mlx5e_priv *priv = netdev_priv(dev);

	/* update HW stats in background for next time */
	mlx5e_queue_update_stats(priv);
	mlx5e_stats_copy_rep_stats(stats, &priv->stats.rep_stats);
}

static int mlx5e_rep_change_mtu(struct net_device *netdev, int new_mtu)
{
	return mlx5e_change_mtu(netdev, new_mtu, NULL);
}

static int mlx5e_rep_change_carrier(struct net_device *dev, bool new_carrier)
{
	struct mlx5e_priv *priv = netdev_priv(dev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	int err;

	if (new_carrier) {
		err = mlx5_modify_vport_admin_state(priv->mdev, MLX5_VPORT_STATE_OP_MOD_ESW_VPORT,
						    rep->vport, 1, MLX5_VPORT_ADMIN_STATE_UP);
		if (err)
			return err;
		netif_carrier_on(dev);
	} else {
		err = mlx5_modify_vport_admin_state(priv->mdev, MLX5_VPORT_STATE_OP_MOD_ESW_VPORT,
						    rep->vport, 1, MLX5_VPORT_ADMIN_STATE_DOWN);
		if (err)
			return err;
		netif_carrier_off(dev);
	}
	return 0;
}

static const struct net_device_ops mlx5e_netdev_ops_rep = {
	.ndo_open                = mlx5e_rep_open,
	.ndo_stop                = mlx5e_rep_close,
	.ndo_start_xmit          = mlx5e_xmit,
	.ndo_setup_tc            = mlx5e_rep_setup_tc,
	.ndo_get_stats64         = mlx5e_rep_get_stats,
	.ndo_has_offload_stats	 = mlx5e_rep_has_offload_stats,
	.ndo_get_offload_stats	 = mlx5e_rep_get_offload_stats,
	.ndo_change_mtu          = mlx5e_rep_change_mtu,
	.ndo_change_carrier      = mlx5e_rep_change_carrier,
};

bool mlx5e_eswitch_uplink_rep(const struct net_device *netdev)
{
	return netdev->netdev_ops == &mlx5e_netdev_ops &&
	       mlx5e_is_uplink_rep(netdev_priv(netdev));
}

bool mlx5e_eswitch_vf_rep(const struct net_device *netdev)
{
	return netdev->netdev_ops == &mlx5e_netdev_ops_rep;
}

/* One indirect TIR set for outer. Inner not supported in reps. */
#define REP_NUM_INDIR_TIRS MLX5E_NUM_INDIR_TIRS

static int mlx5e_rep_max_nch_limit(struct mlx5_core_dev *mdev)
{
	int max_tir_num = 1 << MLX5_CAP_GEN(mdev, log_max_tir);
	int num_vports = mlx5_eswitch_get_total_vports(mdev);

	return (max_tir_num - mlx5e_get_pf_num_tirs(mdev)
		- (num_vports * REP_NUM_INDIR_TIRS)) / num_vports;
}

static void mlx5e_build_rep_params(struct net_device *netdev)
{
	const bool take_rtnl = netdev->reg_state == NETREG_REGISTERED;
	struct mlx5e_priv *priv = netdev_priv(netdev);
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5_core_dev *mdev = priv->mdev;
	struct mlx5e_params *params;

	u8 cq_period_mode = MLX5_CAP_GEN(mdev, cq_period_start_from_cqe) ?
					 MLX5_CQ_PERIOD_MODE_START_FROM_CQE :
					 MLX5_CQ_PERIOD_MODE_START_FROM_EQE;

	params = &priv->channels.params;

	params->num_channels = MLX5E_REP_PARAMS_DEF_NUM_CHANNELS;
	params->hard_mtu    = MLX5E_ETH_HARD_MTU;
	params->sw_mtu      = netdev->mtu;

	/* SQ */
	if (rep->vport == MLX5_VPORT_UPLINK)
		params->log_sq_size = MLX5E_PARAMS_DEFAULT_LOG_SQ_SIZE;
	else
		params->log_sq_size = MLX5E_REP_PARAMS_DEF_LOG_SQ_SIZE;

	/* RQ */
	mlx5e_build_rq_params(mdev, params);

	/* If netdev is already registered (e.g. move from nic profile to uplink,
	 * RTNL lock must be held before triggering netdev notifiers.
	 */
	if (take_rtnl)
		rtnl_lock();
	/* update XDP supported features */
	mlx5e_set_xdp_feature(netdev);
	if (take_rtnl)
		rtnl_unlock();

	/* CQ moderation params */
	params->rx_dim_enabled = MLX5_CAP_GEN(mdev, cq_moderation);
	mlx5e_set_rx_cq_mode_params(params, cq_period_mode);

	params->mqprio.num_tc       = 1;
	if (rep->vport != MLX5_VPORT_UPLINK)
		params->vlan_strip_disable = true;

	mlx5_query_min_inline(mdev, &params->tx_min_inline_mode);
}

static void mlx5e_build_rep_netdev(struct net_device *netdev,
				   struct mlx5_core_dev *mdev)
{
	SET_NETDEV_DEV(netdev, mdev->device);
	netdev->netdev_ops = &mlx5e_netdev_ops_rep;
	eth_hw_addr_random(netdev);
	netdev->ethtool_ops = &mlx5e_rep_ethtool_ops;

	netdev->watchdog_timeo    = 15 * HZ;

#if IS_ENABLED(CONFIG_MLX5_CLS_ACT)
	netdev->hw_features    |= NETIF_F_HW_TC;
#endif
	netdev->hw_features    |= NETIF_F_SG;
	netdev->hw_features    |= NETIF_F_IP_CSUM;
	netdev->hw_features    |= NETIF_F_IPV6_CSUM;
	netdev->hw_features    |= NETIF_F_GRO;
	netdev->hw_features    |= NETIF_F_TSO;
	netdev->hw_features    |= NETIF_F_TSO6;
	netdev->hw_features    |= NETIF_F_RXCSUM;

	netdev->features |= netdev->hw_features;
	netdev->features |= NETIF_F_NETNS_LOCAL;
}

static int mlx5e_init_rep(struct mlx5_core_dev *mdev,
			  struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	priv->fs =
		mlx5e_fs_init(priv->profile, mdev,
			      !test_bit(MLX5E_STATE_DESTROYING, &priv->state),
			      priv->dfs_root);
	if (!priv->fs) {
		netdev_err(priv->netdev, "FS allocation failed\n");
		return -ENOMEM;
	}

	mlx5e_build_rep_params(netdev);
	mlx5e_timestamp_init(priv);

	return 0;
}

static int mlx5e_init_ul_rep(struct mlx5_core_dev *mdev,
			     struct net_device *netdev)
{
	struct mlx5e_priv *priv = netdev_priv(netdev);

	priv->dfs_root = debugfs_create_dir("nic",
					    mlx5_debugfs_get_dev_root(mdev));

	priv->fs = mlx5e_fs_init(priv->profile, mdev,
				 !test_bit(MLX5E_STATE_DESTROYING, &priv->state),
				 priv->dfs_root);
	if (!priv->fs) {
		netdev_err(priv->netdev, "FS allocation failed\n");
		debugfs_remove_recursive(priv->dfs_root);
		return -ENOMEM;
	}

	mlx5e_vxlan_set_netdev_info(priv);
	mlx5e_build_rep_params(netdev);
	mlx5e_timestamp_init(priv);
	return 0;
}

static void mlx5e_cleanup_rep(struct mlx5e_priv *priv)
{
	mlx5e_fs_cleanup(priv->fs);
	debugfs_remove_recursive(priv->dfs_root);
	priv->fs = NULL;
}

static int mlx5e_create_rep_ttc_table(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct ttc_params ttc_params = {};
	int err;

	mlx5e_fs_set_ns(priv->fs,
			mlx5_get_flow_namespace(priv->mdev,
						MLX5_FLOW_NAMESPACE_KERNEL), false);

	/* The inner_ttc in the ttc params is intentionally not set */
	mlx5e_set_ttc_params(priv->fs, priv->rx_res, &ttc_params, false);

	if (rep->vport != MLX5_VPORT_UPLINK)
		/* To give uplik rep TTC a lower level for chaining from root ft */
		ttc_params.ft_attr.level = MLX5E_TTC_FT_LEVEL + 1;

	mlx5e_fs_set_ttc(priv->fs, mlx5_create_ttc_table(priv->mdev, &ttc_params), false);
	if (IS_ERR(mlx5e_fs_get_ttc(priv->fs, false))) {
		err = PTR_ERR(mlx5e_fs_get_ttc(priv->fs, false));
		netdev_err(priv->netdev, "Failed to create rep ttc table, err=%d\n",
			   err);
		return err;
	}
	return 0;
}

static int mlx5e_create_rep_root_ft(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns;
	int err = 0;

	if (rep->vport != MLX5_VPORT_UPLINK) {
		/* non uplik reps will skip any bypass tables and go directly to
		 * their own ttc
		 */
		rpriv->root_ft = mlx5_get_ttc_flow_table(mlx5e_fs_get_ttc(priv->fs, false));
		return 0;
	}

	/* uplink root ft will be used to auto chain, to ethtool or ttc tables */
	ns = mlx5_get_flow_namespace(priv->mdev, MLX5_FLOW_NAMESPACE_OFFLOADS);
	if (!ns) {
		netdev_err(priv->netdev, "Failed to get reps offloads namespace\n");
		return -EOPNOTSUPP;
	}

	ft_attr.max_fte = 0; /* Empty table, miss rule will always point to next table */
	ft_attr.prio = 1;
	ft_attr.level = 1;

	rpriv->root_ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(rpriv->root_ft)) {
		err = PTR_ERR(rpriv->root_ft);
		rpriv->root_ft = NULL;
	}

	return err;
}

static void mlx5e_destroy_rep_root_ft(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;

	if (rep->vport != MLX5_VPORT_UPLINK)
		return;
	mlx5_destroy_flow_table(rpriv->root_ft);
}

static int mlx5e_create_rep_vport_rx_rule(struct mlx5e_priv *priv)
{
	struct mlx5_eswitch *esw = priv->mdev->priv.eswitch;
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;
	struct mlx5_flow_handle *flow_rule;
	struct mlx5_flow_destination dest;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = rpriv->root_ft;

	flow_rule = mlx5_eswitch_create_vport_rx_rule(esw, rep->vport, &dest);
	if (IS_ERR(flow_rule))
		return PTR_ERR(flow_rule);
	rpriv->vport_rx_rule = flow_rule;
	return 0;
}

static void rep_vport_rx_rule_destroy(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	if (!rpriv->vport_rx_rule)
		return;

	mlx5_del_flow_rules(rpriv->vport_rx_rule);
	rpriv->vport_rx_rule = NULL;
}

int mlx5e_rep_bond_update(struct mlx5e_priv *priv, bool cleanup)
{
	rep_vport_rx_rule_destroy(priv);

	return cleanup ? 0 : mlx5e_create_rep_vport_rx_rule(priv);
}

static int mlx5e_init_rep_rx(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;
	int err;

	mlx5e_fs_init_l2_addr(priv->fs, priv->netdev);

	err = mlx5e_open_drop_rq(priv, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "open drop rq failed, %d\n", err);
		goto err_free_fs;
	}

	priv->rx_res = mlx5e_rx_res_create(priv->mdev, 0, priv->max_nch, priv->drop_rq.rqn,
					   &priv->channels.params.packet_merge,
					   priv->channels.params.num_channels);
	if (IS_ERR(priv->rx_res)) {
		err = PTR_ERR(priv->rx_res);
		mlx5_core_err(mdev, "Create rx resources failed, err=%d\n", err);
		goto err_close_drop_rq;
	}

	err = mlx5e_create_rep_ttc_table(priv);
	if (err)
		goto err_destroy_rx_res;

	err = mlx5e_create_rep_root_ft(priv);
	if (err)
		goto err_destroy_ttc_table;

	err = mlx5e_create_rep_vport_rx_rule(priv);
	if (err)
		goto err_destroy_root_ft;

	mlx5e_ethtool_init_steering(priv->fs);

	return 0;

err_destroy_root_ft:
	mlx5e_destroy_rep_root_ft(priv);
err_destroy_ttc_table:
	mlx5_destroy_ttc_table(mlx5e_fs_get_ttc(priv->fs, false));
err_destroy_rx_res:
	mlx5e_rx_res_destroy(priv->rx_res);
	priv->rx_res = ERR_PTR(-EINVAL);
err_close_drop_rq:
	mlx5e_close_drop_rq(&priv->drop_rq);
err_free_fs:
	mlx5e_fs_cleanup(priv->fs);
	priv->fs = NULL;
	return err;
}

static void mlx5e_cleanup_rep_rx(struct mlx5e_priv *priv)
{
	mlx5e_ethtool_cleanup_steering(priv->fs);
	rep_vport_rx_rule_destroy(priv);
	mlx5e_destroy_rep_root_ft(priv);
	mlx5_destroy_ttc_table(mlx5e_fs_get_ttc(priv->fs, false));
	mlx5e_rx_res_destroy(priv->rx_res);
	priv->rx_res = ERR_PTR(-EINVAL);
	mlx5e_close_drop_rq(&priv->drop_rq);
}

static void mlx5e_rep_mpesw_work(struct work_struct *work)
{
	struct mlx5_rep_uplink_priv *uplink_priv =
		container_of(work, struct mlx5_rep_uplink_priv,
			     mpesw_work);
	struct mlx5e_rep_priv *rpriv =
		container_of(uplink_priv, struct mlx5e_rep_priv,
			     uplink_priv);
	struct mlx5e_priv *priv = netdev_priv(rpriv->netdev);

	rep_vport_rx_rule_destroy(priv);
	mlx5e_create_rep_vport_rx_rule(priv);
}

static int mlx5e_init_ul_rep_rx(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	int err;

	mlx5e_create_q_counters(priv);
	err = mlx5e_init_rep_rx(priv);
	if (err)
		goto out;

	mlx5e_tc_int_port_init_rep_rx(priv);

	INIT_WORK(&rpriv->uplink_priv.mpesw_work, mlx5e_rep_mpesw_work);

out:
	return err;
}

static void mlx5e_cleanup_ul_rep_rx(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	cancel_work_sync(&rpriv->uplink_priv.mpesw_work);
	mlx5e_tc_int_port_cleanup_rep_rx(priv);
	mlx5e_cleanup_rep_rx(priv);
	mlx5e_destroy_q_counters(priv);
}

static int mlx5e_init_uplink_rep_tx(struct mlx5e_rep_priv *rpriv)
{
	struct mlx5_rep_uplink_priv *uplink_priv;
	struct net_device *netdev;
	struct mlx5e_priv *priv;
	int err;

	netdev = rpriv->netdev;
	priv = netdev_priv(netdev);
	uplink_priv = &rpriv->uplink_priv;

	err = mlx5e_rep_tc_init(rpriv);
	if (err)
		return err;

	mlx5_init_port_tun_entropy(&uplink_priv->tun_entropy, priv->mdev);

	mlx5e_rep_bond_init(rpriv);
	err = mlx5e_rep_tc_netdevice_event_register(rpriv);
	if (err) {
		mlx5_core_err(priv->mdev, "Failed to register netdev notifier, err: %d\n",
			      err);
		goto err_event_reg;
	}

	return 0;

err_event_reg:
	mlx5e_rep_bond_cleanup(rpriv);
	mlx5e_rep_tc_cleanup(rpriv);
	return err;
}

static void mlx5e_cleanup_uplink_rep_tx(struct mlx5e_rep_priv *rpriv)
{
	mlx5e_rep_tc_netdevice_event_unregister(rpriv);
	mlx5e_rep_bond_cleanup(rpriv);
	mlx5e_rep_tc_cleanup(rpriv);
}

static int mlx5e_init_rep_tx(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	int err;

	err = mlx5e_create_tises(priv);
	if (err) {
		mlx5_core_warn(priv->mdev, "create tises failed, %d\n", err);
		return err;
	}

	err = mlx5e_rep_neigh_init(rpriv);
	if (err)
		goto err_neigh_init;

	if (rpriv->rep->vport == MLX5_VPORT_UPLINK) {
		err = mlx5e_init_uplink_rep_tx(rpriv);
		if (err)
			goto err_init_tx;
	}

	err = mlx5e_tc_ht_init(&rpriv->tc_ht);
	if (err)
		goto err_ht_init;

	return 0;

err_ht_init:
	if (rpriv->rep->vport == MLX5_VPORT_UPLINK)
		mlx5e_cleanup_uplink_rep_tx(rpriv);
err_init_tx:
	mlx5e_rep_neigh_cleanup(rpriv);
err_neigh_init:
	mlx5e_destroy_tises(priv);
	return err;
}

static void mlx5e_cleanup_rep_tx(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	mlx5e_tc_ht_cleanup(&rpriv->tc_ht);

	if (rpriv->rep->vport == MLX5_VPORT_UPLINK)
		mlx5e_cleanup_uplink_rep_tx(rpriv);

	mlx5e_rep_neigh_cleanup(rpriv);
	mlx5e_destroy_tises(priv);
}

static void mlx5e_rep_enable(struct mlx5e_priv *priv)
{
	mlx5e_set_netdev_mtu_boundaries(priv);
}

static void mlx5e_rep_disable(struct mlx5e_priv *priv)
{
}

static int mlx5e_update_rep_rx(struct mlx5e_priv *priv)
{
	return 0;
}

static int mlx5e_rep_event_mpesw(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct mlx5_eswitch_rep *rep = rpriv->rep;

	if (rep->vport != MLX5_VPORT_UPLINK)
		return NOTIFY_DONE;

	queue_work(priv->wq, &rpriv->uplink_priv.mpesw_work);

	return NOTIFY_OK;
}

static int uplink_rep_async_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5e_priv *priv = container_of(nb, struct mlx5e_priv, events_nb);

	if (event == MLX5_EVENT_TYPE_PORT_CHANGE) {
		struct mlx5_eqe *eqe = data;

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

	if (event == MLX5_DEV_EVENT_PORT_AFFINITY)
		return mlx5e_rep_tc_event_port_affinity(priv);
	else if (event == MLX5_DEV_EVENT_MULTIPORT_ESW)
		return mlx5e_rep_event_mpesw(priv);

	return NOTIFY_DONE;
}

static void mlx5e_uplink_rep_enable(struct mlx5e_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct mlx5_core_dev *mdev = priv->mdev;
	u16 max_mtu;

	mlx5e_ipsec_init(priv);

	netdev->min_mtu = ETH_MIN_MTU;
	mlx5_query_port_max_mtu(priv->mdev, &max_mtu, 1);
	netdev->max_mtu = MLX5E_HW2SW_MTU(&priv->channels.params, max_mtu);
	mlx5e_set_dev_port_mtu(priv);

	mlx5e_rep_tc_enable(priv);

	if (MLX5_CAP_GEN(mdev, uplink_follow))
		mlx5_modify_vport_admin_state(mdev, MLX5_VPORT_STATE_OP_MOD_UPLINK,
					      0, 0, MLX5_VPORT_ADMIN_STATE_AUTO);
	mlx5_lag_add_netdev(mdev, netdev);
	priv->events_nb.notifier_call = uplink_rep_async_event;
	mlx5_notifier_register(mdev, &priv->events_nb);
	mlx5e_dcbnl_initialize(priv);
	mlx5e_dcbnl_init_app(priv);
	mlx5e_rep_bridge_init(priv);

	netdev->wanted_features |= NETIF_F_HW_TC;

	rtnl_lock();
	if (netif_running(netdev))
		mlx5e_open(netdev);
	udp_tunnel_nic_reset_ntf(priv->netdev);
	netif_device_attach(netdev);
	rtnl_unlock();
}

static void mlx5e_uplink_rep_disable(struct mlx5e_priv *priv)
{
	struct mlx5_core_dev *mdev = priv->mdev;

	rtnl_lock();
	if (netif_running(priv->netdev))
		mlx5e_close(priv->netdev);
	netif_device_detach(priv->netdev);
	rtnl_unlock();

	mlx5e_rep_bridge_cleanup(priv);
	mlx5e_dcbnl_delete_app(priv);
	mlx5_notifier_unregister(mdev, &priv->events_nb);
	mlx5e_rep_tc_disable(priv);
	mlx5_lag_remove_netdev(mdev, priv->netdev);
	mlx5_vxlan_reset_to_default(mdev->vxlan);

	mlx5e_ipsec_cleanup(priv);
}

static MLX5E_DEFINE_STATS_GRP(sw_rep, 0);
static MLX5E_DEFINE_STATS_GRP(vport_rep, MLX5E_NDO_UPDATE_STATS);

/* The stats groups order is opposite to the update_stats() order calls */
static mlx5e_stats_grp_t mlx5e_rep_stats_grps[] = {
	&MLX5E_STATS_GRP(sw_rep),
	&MLX5E_STATS_GRP(vport_rep),
};

static unsigned int mlx5e_rep_stats_grps_num(struct mlx5e_priv *priv)
{
	return ARRAY_SIZE(mlx5e_rep_stats_grps);
}

/* The stats groups order is opposite to the update_stats() order calls */
static mlx5e_stats_grp_t mlx5e_ul_rep_stats_grps[] = {
	&MLX5E_STATS_GRP(sw),
	&MLX5E_STATS_GRP(qcnt),
	&MLX5E_STATS_GRP(vnic_env),
	&MLX5E_STATS_GRP(vport),
	&MLX5E_STATS_GRP(802_3),
	&MLX5E_STATS_GRP(2863),
	&MLX5E_STATS_GRP(2819),
	&MLX5E_STATS_GRP(phy),
	&MLX5E_STATS_GRP(eth_ext),
	&MLX5E_STATS_GRP(pcie),
	&MLX5E_STATS_GRP(per_prio),
	&MLX5E_STATS_GRP(pme),
	&MLX5E_STATS_GRP(channels),
	&MLX5E_STATS_GRP(per_port_buff_congest),
#ifdef CONFIG_MLX5_EN_IPSEC
	&MLX5E_STATS_GRP(ipsec_hw),
	&MLX5E_STATS_GRP(ipsec_sw),
#endif
	&MLX5E_STATS_GRP(ptp),
};

static unsigned int mlx5e_ul_rep_stats_grps_num(struct mlx5e_priv *priv)
{
	return ARRAY_SIZE(mlx5e_ul_rep_stats_grps);
}

static int
mlx5e_rep_vnic_reporter_diagnose(struct devlink_health_reporter *reporter,
				 struct devlink_fmsg *fmsg,
				 struct netlink_ext_ack *extack)
{
	struct mlx5e_rep_priv *rpriv = devlink_health_reporter_priv(reporter);
	struct mlx5_eswitch_rep *rep = rpriv->rep;

	mlx5_reporter_vnic_diagnose_counters(rep->esw->dev, fmsg, rep->vport,
					     true);
	return 0;
}

static const struct devlink_health_reporter_ops mlx5_rep_vnic_reporter_ops = {
	.name = "vnic",
	.diagnose = mlx5e_rep_vnic_reporter_diagnose,
};

static void mlx5e_rep_vnic_reporter_create(struct mlx5e_priv *priv,
					   struct devlink_port *dl_port)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;
	struct devlink_health_reporter *reporter;

	reporter = devl_port_health_reporter_create(dl_port,
						    &mlx5_rep_vnic_reporter_ops,
						    0, rpriv);
	if (IS_ERR(reporter)) {
		mlx5_core_err(priv->mdev,
			      "Failed to create representor vnic reporter, err = %ld\n",
			      PTR_ERR(reporter));
		return;
	}

	rpriv->rep_vnic_reporter = reporter;
}

static void mlx5e_rep_vnic_reporter_destroy(struct mlx5e_priv *priv)
{
	struct mlx5e_rep_priv *rpriv = priv->ppriv;

	if (!IS_ERR_OR_NULL(rpriv->rep_vnic_reporter))
		devl_health_reporter_destroy(rpriv->rep_vnic_reporter);
}

static const struct mlx5e_profile mlx5e_rep_profile = {
	.init			= mlx5e_init_rep,
	.cleanup		= mlx5e_cleanup_rep,
	.init_rx		= mlx5e_init_rep_rx,
	.cleanup_rx		= mlx5e_cleanup_rep_rx,
	.init_tx		= mlx5e_init_rep_tx,
	.cleanup_tx		= mlx5e_cleanup_rep_tx,
	.enable		        = mlx5e_rep_enable,
	.disable	        = mlx5e_rep_disable,
	.update_rx		= mlx5e_update_rep_rx,
	.update_stats           = mlx5e_stats_update_ndo_stats,
	.rx_handlers            = &mlx5e_rx_handlers_rep,
	.max_tc			= 1,
	.stats_grps		= mlx5e_rep_stats_grps,
	.stats_grps_num		= mlx5e_rep_stats_grps_num,
	.max_nch_limit		= mlx5e_rep_max_nch_limit,
};

static const struct mlx5e_profile mlx5e_uplink_rep_profile = {
	.init			= mlx5e_init_ul_rep,
	.cleanup		= mlx5e_cleanup_rep,
	.init_rx		= mlx5e_init_ul_rep_rx,
	.cleanup_rx		= mlx5e_cleanup_ul_rep_rx,
	.init_tx		= mlx5e_init_rep_tx,
	.cleanup_tx		= mlx5e_cleanup_rep_tx,
	.enable		        = mlx5e_uplink_rep_enable,
	.disable	        = mlx5e_uplink_rep_disable,
	.update_rx		= mlx5e_update_rep_rx,
	.update_stats           = mlx5e_stats_update_ndo_stats,
	.update_carrier	        = mlx5e_update_carrier,
	.rx_handlers            = &mlx5e_rx_handlers_rep,
	.max_tc			= MLX5E_MAX_NUM_TC,
	.stats_grps		= mlx5e_ul_rep_stats_grps,
	.stats_grps_num		= mlx5e_ul_rep_stats_grps_num,
};

/* e-Switch vport representors */
static int
mlx5e_vport_uplink_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_priv *priv = netdev_priv(mlx5_uplink_netdev_get(dev));
	struct mlx5e_rep_priv *rpriv = mlx5e_rep_to_rep_priv(rep);

	rpriv->netdev = priv->netdev;
	return mlx5e_netdev_change_profile(priv, &mlx5e_uplink_rep_profile,
					   rpriv);
}

static void
mlx5e_vport_uplink_rep_unload(struct mlx5e_rep_priv *rpriv)
{
	struct net_device *netdev = rpriv->netdev;
	struct mlx5e_priv *priv;

	priv = netdev_priv(netdev);

	mlx5e_netdev_attach_nic_profile(priv);
}

static int
mlx5e_vport_vf_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *rpriv = mlx5e_rep_to_rep_priv(rep);
	const struct mlx5e_profile *profile;
	struct devlink_port *dl_port;
	struct net_device *netdev;
	struct mlx5e_priv *priv;
	int err;

	profile = &mlx5e_rep_profile;
	netdev = mlx5e_create_netdev(dev, profile);
	if (!netdev) {
		mlx5_core_warn(dev,
			       "Failed to create representor netdev for vport %d\n",
			       rep->vport);
		return -EINVAL;
	}

	mlx5e_build_rep_netdev(netdev, dev);
	rpriv->netdev = netdev;

	priv = netdev_priv(netdev);
	priv->profile = profile;
	priv->ppriv = rpriv;
	err = profile->init(dev, netdev);
	if (err) {
		netdev_warn(netdev, "rep profile init failed, %d\n", err);
		goto err_destroy_netdev;
	}

	err = mlx5e_attach_netdev(netdev_priv(netdev));
	if (err) {
		netdev_warn(netdev,
			    "Failed to attach representor netdev for vport %d\n",
			    rep->vport);
		goto err_cleanup_profile;
	}

	dl_port = mlx5_esw_offloads_devlink_port(dev->priv.eswitch,
						 rpriv->rep->vport);
	if (!IS_ERR(dl_port)) {
		SET_NETDEV_DEVLINK_PORT(netdev, dl_port);
		mlx5e_rep_vnic_reporter_create(priv, dl_port);
	}

	err = register_netdev(netdev);
	if (err) {
		netdev_warn(netdev,
			    "Failed to register representor netdev for vport %d\n",
			    rep->vport);
		goto err_detach_netdev;
	}

	return 0;

err_detach_netdev:
	mlx5e_rep_vnic_reporter_destroy(priv);
	mlx5e_detach_netdev(netdev_priv(netdev));
err_cleanup_profile:
	priv->profile->cleanup(priv);

err_destroy_netdev:
	mlx5e_destroy_netdev(netdev_priv(netdev));
	return err;
}

static int
mlx5e_vport_rep_load(struct mlx5_core_dev *dev, struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *rpriv;
	int err;

	rpriv = kvzalloc(sizeof(*rpriv), GFP_KERNEL);
	if (!rpriv)
		return -ENOMEM;

	/* rpriv->rep to be looked up when profile->init() is called */
	rpriv->rep = rep;
	rep->rep_data[REP_ETH].priv = rpriv;
	INIT_LIST_HEAD(&rpriv->vport_sqs_list);

	if (rep->vport == MLX5_VPORT_UPLINK)
		err = mlx5e_vport_uplink_rep_load(dev, rep);
	else
		err = mlx5e_vport_vf_rep_load(dev, rep);

	if (err)
		kvfree(rpriv);

	return err;
}

static void
mlx5e_vport_rep_unload(struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *rpriv = mlx5e_rep_to_rep_priv(rep);
	struct net_device *netdev = rpriv->netdev;
	struct mlx5e_priv *priv = netdev_priv(netdev);
	void *ppriv = priv->ppriv;

	if (rep->vport == MLX5_VPORT_UPLINK) {
		mlx5e_vport_uplink_rep_unload(rpriv);
		goto free_ppriv;
	}

	unregister_netdev(netdev);
	mlx5e_rep_vnic_reporter_destroy(priv);
	mlx5e_detach_netdev(priv);
	priv->profile->cleanup(priv);
	mlx5e_destroy_netdev(priv);
free_ppriv:
	kvfree(ppriv); /* mlx5e_rep_priv */
}

static void *mlx5e_vport_rep_get_proto_dev(struct mlx5_eswitch_rep *rep)
{
	struct mlx5e_rep_priv *rpriv;

	rpriv = mlx5e_rep_to_rep_priv(rep);

	return rpriv->netdev;
}

static void mlx5e_vport_rep_event_unpair(struct mlx5_eswitch_rep *rep,
					 struct mlx5_eswitch *peer_esw)
{
	u16 i = MLX5_CAP_GEN(peer_esw->dev, vhca_id);
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_rep_sq *rep_sq;

	WARN_ON_ONCE(!peer_esw);
	rpriv = mlx5e_rep_to_rep_priv(rep);
	list_for_each_entry(rep_sq, &rpriv->vport_sqs_list, list) {
		struct mlx5e_rep_sq_peer *sq_peer = xa_load(&rep_sq->sq_peer, i);

		if (!sq_peer || sq_peer->peer != peer_esw)
			continue;

		mlx5_eswitch_del_send_to_vport_rule(sq_peer->rule);
		xa_erase(&rep_sq->sq_peer, i);
		kfree(sq_peer);
	}
}

static int mlx5e_vport_rep_event_pair(struct mlx5_eswitch *esw,
				      struct mlx5_eswitch_rep *rep,
				      struct mlx5_eswitch *peer_esw)
{
	u16 i = MLX5_CAP_GEN(peer_esw->dev, vhca_id);
	struct mlx5_flow_handle *flow_rule;
	struct mlx5e_rep_sq_peer *sq_peer;
	struct mlx5e_rep_priv *rpriv;
	struct mlx5e_rep_sq *rep_sq;
	int err;

	rpriv = mlx5e_rep_to_rep_priv(rep);
	list_for_each_entry(rep_sq, &rpriv->vport_sqs_list, list) {
		sq_peer = xa_load(&rep_sq->sq_peer, i);

		if (sq_peer && sq_peer->peer)
			continue;

		flow_rule = mlx5_eswitch_add_send_to_vport_rule(peer_esw, esw, rep,
								rep_sq->sqn);
		if (IS_ERR(flow_rule)) {
			err = PTR_ERR(flow_rule);
			goto err_out;
		}

		if (sq_peer) {
			sq_peer->rule = flow_rule;
			sq_peer->peer = peer_esw;
			continue;
		}
		sq_peer = kzalloc(sizeof(*sq_peer), GFP_KERNEL);
		if (!sq_peer) {
			err = -ENOMEM;
			goto err_sq_alloc;
		}
		err = xa_insert(&rep_sq->sq_peer, i, sq_peer, GFP_KERNEL);
		if (err)
			goto err_xa;
		sq_peer->rule = flow_rule;
		sq_peer->peer = peer_esw;
	}

	return 0;
err_xa:
	kfree(sq_peer);
err_sq_alloc:
	mlx5_eswitch_del_send_to_vport_rule(flow_rule);
err_out:
	mlx5e_vport_rep_event_unpair(rep, peer_esw);
	return err;
}

static int mlx5e_vport_rep_event(struct mlx5_eswitch *esw,
				 struct mlx5_eswitch_rep *rep,
				 enum mlx5_switchdev_event event,
				 void *data)
{
	int err = 0;

	if (event == MLX5_SWITCHDEV_EVENT_PAIR)
		err = mlx5e_vport_rep_event_pair(esw, rep, data);
	else if (event == MLX5_SWITCHDEV_EVENT_UNPAIR)
		mlx5e_vport_rep_event_unpair(rep, data);

	return err;
}

static const struct mlx5_eswitch_rep_ops rep_ops = {
	.load = mlx5e_vport_rep_load,
	.unload = mlx5e_vport_rep_unload,
	.get_proto_dev = mlx5e_vport_rep_get_proto_dev,
	.event = mlx5e_vport_rep_event,
};

static int mlx5e_rep_probe(struct auxiliary_device *adev,
			   const struct auxiliary_device_id *id)
{
	struct mlx5_adev *edev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = edev->mdev;
	struct mlx5_eswitch *esw;

	esw = mdev->priv.eswitch;
	mlx5_eswitch_register_vport_reps(esw, &rep_ops, REP_ETH);
	return 0;
}

static void mlx5e_rep_remove(struct auxiliary_device *adev)
{
	struct mlx5_adev *vdev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = vdev->mdev;
	struct mlx5_eswitch *esw;

	esw = mdev->priv.eswitch;
	mlx5_eswitch_unregister_vport_reps(esw, REP_ETH);
}

static const struct auxiliary_device_id mlx5e_rep_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".eth-rep", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlx5e_rep_id_table);

static struct auxiliary_driver mlx5e_rep_driver = {
	.name = "eth-rep",
	.probe = mlx5e_rep_probe,
	.remove = mlx5e_rep_remove,
	.id_table = mlx5e_rep_id_table,
};

int mlx5e_rep_init(void)
{
	return auxiliary_driver_register(&mlx5e_rep_driver);
}

void mlx5e_rep_cleanup(void)
{
	auxiliary_driver_unregister(&mlx5e_rep_driver);
}
