// SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause

/* Ethtool support for Mellanox Gigabit Ethernet driver
 *
 * Copyright (C) 2020-2021 NVIDIA CORPORATION & AFFILIATES
 */

#include <linux/phy.h>

#include "mlxbf_gige.h"
#include "mlxbf_gige_regs.h"

/* Start of struct ethtool_ops functions */
static int mlxbf_gige_get_regs_len(struct net_device *netdev)
{
	return MLXBF_GIGE_MMIO_REG_SZ;
}

static void mlxbf_gige_get_regs(struct net_device *netdev,
				struct ethtool_regs *regs, void *p)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);

	regs->version = MLXBF_GIGE_REGS_VERSION;

	/* Read entire MMIO register space and store results
	 * into the provided buffer. By design, a read to an
	 * offset without an existing register will be
	 * acknowledged and return zero.
	 */
	memcpy_fromio(p, priv->base, MLXBF_GIGE_MMIO_REG_SZ);
}

static void
mlxbf_gige_get_ringparam(struct net_device *netdev,
			 struct ethtool_ringparam *ering,
			 struct kernel_ethtool_ringparam *kernel_ering,
			 struct netlink_ext_ack *extack)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);

	ering->rx_max_pending = MLXBF_GIGE_MAX_RXQ_SZ;
	ering->tx_max_pending = MLXBF_GIGE_MAX_TXQ_SZ;
	ering->rx_pending = priv->rx_q_entries;
	ering->tx_pending = priv->tx_q_entries;
}

static const struct {
	const char string[ETH_GSTRING_LEN];
} mlxbf_gige_ethtool_stats_keys[] = {
	{ "hw_access_errors" },
	{ "tx_invalid_checksums" },
	{ "tx_small_frames" },
	{ "tx_index_errors" },
	{ "sw_config_errors" },
	{ "sw_access_errors" },
	{ "rx_truncate_errors" },
	{ "rx_mac_errors" },
	{ "rx_din_dropped_pkts" },
	{ "tx_fifo_full" },
	{ "rx_filter_passed_pkts" },
	{ "rx_filter_discard_pkts" },
};

static int mlxbf_gige_get_sset_count(struct net_device *netdev, int stringset)
{
	if (stringset != ETH_SS_STATS)
		return -EOPNOTSUPP;
	return ARRAY_SIZE(mlxbf_gige_ethtool_stats_keys);
}

static void mlxbf_gige_get_strings(struct net_device *netdev, u32 stringset,
				   u8 *buf)
{
	if (stringset != ETH_SS_STATS)
		return;
	memcpy(buf, &mlxbf_gige_ethtool_stats_keys,
	       sizeof(mlxbf_gige_ethtool_stats_keys));
}

static void mlxbf_gige_get_ethtool_stats(struct net_device *netdev,
					 struct ethtool_stats *estats,
					 u64 *data)
{
	struct mlxbf_gige *priv = netdev_priv(netdev);

	/* Fill data array with interface statistics
	 *
	 * NOTE: the data writes must be in
	 *       sync with the strings shown in
	 *       the mlxbf_gige_ethtool_stats_keys[] array
	 *
	 * NOTE2: certain statistics below are zeroed upon
	 *        port disable, so the calculation below
	 *        must include the "cached" value of the stat
	 *        plus the value read directly from hardware.
	 *        Cached statistics are currently:
	 *          rx_din_dropped_pkts
	 *          rx_filter_passed_pkts
	 *          rx_filter_discard_pkts
	 */
	*data++ = priv->stats.hw_access_errors;
	*data++ = priv->stats.tx_invalid_checksums;
	*data++ = priv->stats.tx_small_frames;
	*data++ = priv->stats.tx_index_errors;
	*data++ = priv->stats.sw_config_errors;
	*data++ = priv->stats.sw_access_errors;
	*data++ = priv->stats.rx_truncate_errors;
	*data++ = priv->stats.rx_mac_errors;
	*data++ = (priv->stats.rx_din_dropped_pkts +
		   readq(priv->base + MLXBF_GIGE_RX_DIN_DROP_COUNTER));
	*data++ = priv->stats.tx_fifo_full;
	*data++ = (priv->stats.rx_filter_passed_pkts +
		   readq(priv->base + MLXBF_GIGE_RX_PASS_COUNTER_ALL));
	*data++ = (priv->stats.rx_filter_discard_pkts +
		   readq(priv->base + MLXBF_GIGE_RX_DISC_COUNTER_ALL));
}

static void mlxbf_gige_get_pauseparam(struct net_device *netdev,
				      struct ethtool_pauseparam *pause)
{
	pause->autoneg = AUTONEG_DISABLE;
	pause->rx_pause = 1;
	pause->tx_pause = 1;
}

const struct ethtool_ops mlxbf_gige_ethtool_ops = {
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= mlxbf_gige_get_ringparam,
	.get_regs_len           = mlxbf_gige_get_regs_len,
	.get_regs               = mlxbf_gige_get_regs,
	.get_strings            = mlxbf_gige_get_strings,
	.get_sset_count         = mlxbf_gige_get_sset_count,
	.get_ethtool_stats      = mlxbf_gige_get_ethtool_stats,
	.nway_reset		= phy_ethtool_nway_reset,
	.get_pauseparam		= mlxbf_gige_get_pauseparam,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
};
