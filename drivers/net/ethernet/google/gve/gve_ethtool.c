// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2019 Google, Inc.
 */

#include <linux/rtnetlink.h>
#include "gve.h"

static void gve_get_drvinfo(struct net_device *netdev,
			    struct ethtool_drvinfo *info)
{
	struct gve_priv *priv = netdev_priv(netdev);

	strlcpy(info->driver, "gve", sizeof(info->driver));
	strlcpy(info->version, gve_version_str, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(priv->pdev), sizeof(info->bus_info));
}

static void gve_set_msglevel(struct net_device *netdev, u32 value)
{
	struct gve_priv *priv = netdev_priv(netdev);

	priv->msg_enable = value;
}

static u32 gve_get_msglevel(struct net_device *netdev)
{
	struct gve_priv *priv = netdev_priv(netdev);

	return priv->msg_enable;
}

static const char gve_gstrings_main_stats[][ETH_GSTRING_LEN] = {
	"rx_packets", "tx_packets", "rx_bytes", "tx_bytes",
	"rx_dropped", "tx_dropped", "tx_timeouts",
};

#define GVE_MAIN_STATS_LEN  ARRAY_SIZE(gve_gstrings_main_stats)
#define NUM_GVE_TX_CNTS	5
#define NUM_GVE_RX_CNTS	2

static void gve_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct gve_priv *priv = netdev_priv(netdev);
	char *s = (char *)data;
	int i;

	if (stringset != ETH_SS_STATS)
		return;

	memcpy(s, *gve_gstrings_main_stats,
	       sizeof(gve_gstrings_main_stats));
	s += sizeof(gve_gstrings_main_stats);
	for (i = 0; i < priv->rx_cfg.num_queues; i++) {
		snprintf(s, ETH_GSTRING_LEN, "rx_desc_cnt[%u]", i);
		s += ETH_GSTRING_LEN;
		snprintf(s, ETH_GSTRING_LEN, "rx_desc_fill_cnt[%u]", i);
		s += ETH_GSTRING_LEN;
	}
	for (i = 0; i < priv->tx_cfg.num_queues; i++) {
		snprintf(s, ETH_GSTRING_LEN, "tx_req[%u]", i);
		s += ETH_GSTRING_LEN;
		snprintf(s, ETH_GSTRING_LEN, "tx_done[%u]", i);
		s += ETH_GSTRING_LEN;
		snprintf(s, ETH_GSTRING_LEN, "tx_wake[%u]", i);
		s += ETH_GSTRING_LEN;
		snprintf(s, ETH_GSTRING_LEN, "tx_stop[%u]", i);
		s += ETH_GSTRING_LEN;
		snprintf(s, ETH_GSTRING_LEN, "tx_event_counter[%u]", i);
		s += ETH_GSTRING_LEN;
	}
}

static int gve_get_sset_count(struct net_device *netdev, int sset)
{
	struct gve_priv *priv = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		return GVE_MAIN_STATS_LEN +
		       (priv->rx_cfg.num_queues * NUM_GVE_RX_CNTS) +
		       (priv->tx_cfg.num_queues * NUM_GVE_TX_CNTS);
	default:
		return -EOPNOTSUPP;
	}
}

static void
gve_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats *stats, u64 *data)
{
	struct gve_priv *priv = netdev_priv(netdev);
	u64 rx_pkts, rx_bytes, tx_pkts, tx_bytes;
	unsigned int start;
	int ring;
	int i;

	ASSERT_RTNL();

	for (rx_pkts = 0, rx_bytes = 0, ring = 0;
	     ring < priv->rx_cfg.num_queues; ring++) {
		if (priv->rx) {
			do {
				u64_stats_fetch_begin(&priv->rx[ring].statss);
				rx_pkts += priv->rx[ring].rpackets;
				rx_bytes += priv->rx[ring].rbytes;
			} while (u64_stats_fetch_retry(&priv->rx[ring].statss,
						       start));
		}
	}
	for (tx_pkts = 0, tx_bytes = 0, ring = 0;
	     ring < priv->tx_cfg.num_queues; ring++) {
		if (priv->tx) {
			do {
				u64_stats_fetch_begin(&priv->tx[ring].statss);
				tx_pkts += priv->tx[ring].pkt_done;
				tx_bytes += priv->tx[ring].bytes_done;
			} while (u64_stats_fetch_retry(&priv->tx[ring].statss,
						       start));
		}
	}

	i = 0;
	data[i++] = rx_pkts;
	data[i++] = tx_pkts;
	data[i++] = rx_bytes;
	data[i++] = tx_bytes;
	/* Skip rx_dropped and tx_dropped */
	i += 2;
	data[i++] = priv->tx_timeo_cnt;
	i = GVE_MAIN_STATS_LEN;

	/* walk RX rings */
	if (priv->rx) {
		for (ring = 0; ring < priv->rx_cfg.num_queues; ring++) {
			struct gve_rx_ring *rx = &priv->rx[ring];

			data[i++] = rx->desc.cnt;
			data[i++] = rx->desc.fill_cnt;
		}
	} else {
		i += priv->rx_cfg.num_queues * NUM_GVE_RX_CNTS;
	}
	/* walk TX rings */
	if (priv->tx) {
		for (ring = 0; ring < priv->tx_cfg.num_queues; ring++) {
			struct gve_tx_ring *tx = &priv->tx[ring];

			data[i++] = tx->req;
			data[i++] = tx->done;
			data[i++] = tx->wake_queue;
			data[i++] = tx->stop_queue;
			data[i++] = be32_to_cpu(gve_tx_load_event_counter(priv,
									  tx));
		}
	} else {
		i += priv->tx_cfg.num_queues * NUM_GVE_TX_CNTS;
	}
}

static void gve_get_channels(struct net_device *netdev,
			     struct ethtool_channels *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);

	cmd->max_rx = priv->rx_cfg.max_queues;
	cmd->max_tx = priv->tx_cfg.max_queues;
	cmd->max_other = 0;
	cmd->max_combined = 0;
	cmd->rx_count = priv->rx_cfg.num_queues;
	cmd->tx_count = priv->tx_cfg.num_queues;
	cmd->other_count = 0;
	cmd->combined_count = 0;
}

static int gve_set_channels(struct net_device *netdev,
			    struct ethtool_channels *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);
	struct gve_queue_config new_tx_cfg = priv->tx_cfg;
	struct gve_queue_config new_rx_cfg = priv->rx_cfg;
	struct ethtool_channels old_settings;
	int new_tx = cmd->tx_count;
	int new_rx = cmd->rx_count;

	gve_get_channels(netdev, &old_settings);

	/* Changing combined is not allowed allowed */
	if (cmd->combined_count != old_settings.combined_count)
		return -EINVAL;

	if (!new_rx || !new_tx)
		return -EINVAL;

	if (!netif_carrier_ok(netdev)) {
		priv->tx_cfg.num_queues = new_tx;
		priv->rx_cfg.num_queues = new_rx;
		return 0;
	}

	new_tx_cfg.num_queues = new_tx;
	new_rx_cfg.num_queues = new_rx;

	return gve_adjust_queues(priv, new_rx_cfg, new_tx_cfg);
}

static void gve_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);

	cmd->rx_max_pending = priv->rx_desc_cnt;
	cmd->tx_max_pending = priv->tx_desc_cnt;
	cmd->rx_pending = priv->rx_desc_cnt;
	cmd->tx_pending = priv->tx_desc_cnt;
}

static int gve_user_reset(struct net_device *netdev, u32 *flags)
{
	struct gve_priv *priv = netdev_priv(netdev);

	if (*flags == ETH_RESET_ALL) {
		*flags = 0;
		return gve_reset(priv, true);
	}

	return -EOPNOTSUPP;
}

const struct ethtool_ops gve_ethtool_ops = {
	.get_drvinfo = gve_get_drvinfo,
	.get_strings = gve_get_strings,
	.get_sset_count = gve_get_sset_count,
	.get_ethtool_stats = gve_get_ethtool_stats,
	.set_msglevel = gve_set_msglevel,
	.get_msglevel = gve_get_msglevel,
	.set_channels = gve_set_channels,
	.get_channels = gve_get_channels,
	.get_link = ethtool_op_get_link,
	.get_ringparam = gve_get_ringparam,
	.reset = gve_user_reset,
};
