// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2023, 2024 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <linux/ethtool.h>

#include "rockchip_canfd.h"

enum rkcanfd_stats_type {
	RKCANFD_STATS_TYPE_RX_FIFO_EMPTY_ERRORS,
	RKCANFD_STATS_TYPE_TX_EXTENDED_AS_STANDARD_ERRORS,
};

static const char rkcanfd_stats_strings[][ETH_GSTRING_LEN] = {
	[RKCANFD_STATS_TYPE_RX_FIFO_EMPTY_ERRORS] = "rx_fifo_empty_errors",
	[RKCANFD_STATS_TYPE_TX_EXTENDED_AS_STANDARD_ERRORS] = "tx_extended_as_standard_errors",
};

static void
rkcanfd_ethtool_get_strings(struct net_device *ndev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, rkcanfd_stats_strings,
		       sizeof(rkcanfd_stats_strings));
	}
}

static int rkcanfd_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(rkcanfd_stats_strings);
	default:
		return -EOPNOTSUPP;
	}
}

static void
rkcanfd_ethtool_get_ethtool_stats(struct net_device *ndev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct rkcanfd_priv *priv = netdev_priv(ndev);
	struct rkcanfd_stats *rkcanfd_stats;
	unsigned int start;

	rkcanfd_stats = &priv->stats;

	do {
		start = u64_stats_fetch_begin(&rkcanfd_stats->syncp);

		data[RKCANFD_STATS_TYPE_RX_FIFO_EMPTY_ERRORS] =
			u64_stats_read(&rkcanfd_stats->rx_fifo_empty_errors);
		data[RKCANFD_STATS_TYPE_TX_EXTENDED_AS_STANDARD_ERRORS] =
			u64_stats_read(&rkcanfd_stats->tx_extended_as_standard_errors);
	} while (u64_stats_fetch_retry(&rkcanfd_stats->syncp, start));
}

static const struct ethtool_ops rkcanfd_ethtool_ops = {
	.get_ts_info = can_ethtool_op_get_ts_info_hwts,
	.get_strings = rkcanfd_ethtool_get_strings,
	.get_sset_count = rkcanfd_ethtool_get_sset_count,
	.get_ethtool_stats = rkcanfd_ethtool_get_ethtool_stats,
};

void rkcanfd_ethtool_init(struct rkcanfd_priv *priv)
{
	priv->ndev->ethtool_ops = &rkcanfd_ethtool_ops;

	u64_stats_init(&priv->stats.syncp);
}
