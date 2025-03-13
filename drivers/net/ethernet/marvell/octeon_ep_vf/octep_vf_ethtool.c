// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include "octep_vf_config.h"
#include "octep_vf_main.h"

static const char octep_vf_gstrings_global_stats[][ETH_GSTRING_LEN] = {
	"rx_alloc_errors",
	"tx_busy_errors",
	"tx_hw_pkts",
	"tx_hw_octs",
	"tx_hw_bcast",
	"tx_hw_mcast",
	"rx_hw_pkts",
	"rx_hw_bytes",
	"rx_hw_bcast",
	"rx_dropped_bytes_fifo_full",
};

#define OCTEP_VF_GLOBAL_STATS_CNT ARRAY_SIZE(octep_vf_gstrings_global_stats)

static const char octep_vf_gstrings_tx_q_stats[][ETH_GSTRING_LEN] = {
	"tx_packets_posted[Q-%u]",
	"tx_packets_completed[Q-%u]",
	"tx_bytes[Q-%u]",
	"tx_busy[Q-%u]",
};

#define OCTEP_VF_TX_Q_STATS_CNT ARRAY_SIZE(octep_vf_gstrings_tx_q_stats)

static const char octep_vf_gstrings_rx_q_stats[][ETH_GSTRING_LEN] = {
	"rx_packets[Q-%u]",
	"rx_bytes[Q-%u]",
	"rx_alloc_errors[Q-%u]",
};

#define OCTEP_VF_RX_Q_STATS_CNT ARRAY_SIZE(octep_vf_gstrings_rx_q_stats)

static void octep_vf_get_drvinfo(struct net_device *netdev,
				 struct ethtool_drvinfo *info)
{
	struct octep_vf_device *oct = netdev_priv(netdev);

	strscpy(info->driver, OCTEP_VF_DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(oct->pdev), sizeof(info->bus_info));
}

static void octep_vf_get_strings(struct net_device *netdev,
				 u32 stringset, u8 *data)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	u16 num_queues = CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf);
	const char *str;
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < OCTEP_VF_GLOBAL_STATS_CNT; i++)
			ethtool_puts(&data, octep_vf_gstrings_global_stats[i]);

		for (i = 0; i < num_queues; i++)
			for (j = 0; j < OCTEP_VF_TX_Q_STATS_CNT; j++) {
				str = octep_vf_gstrings_tx_q_stats[j];
				ethtool_sprintf(&data, str, i);
			}

		for (i = 0; i < num_queues; i++)
			for (j = 0; j < OCTEP_VF_RX_Q_STATS_CNT; j++) {
				str = octep_vf_gstrings_rx_q_stats[j];
				ethtool_sprintf(&data, str, i);
			}
		break;
	default:
		break;
	}
}

static int octep_vf_get_sset_count(struct net_device *netdev, int sset)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	u16 num_queues = CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf);

	switch (sset) {
	case ETH_SS_STATS:
		return OCTEP_VF_GLOBAL_STATS_CNT + (num_queues *
		       (OCTEP_VF_TX_Q_STATS_CNT + OCTEP_VF_RX_Q_STATS_CNT));
		break;
	default:
		return -EOPNOTSUPP;
	}
}

static void octep_vf_get_ethtool_stats(struct net_device *netdev,
				       struct ethtool_stats *stats, u64 *data)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	struct octep_vf_iface_tx_stats *iface_tx_stats;
	struct octep_vf_iface_rx_stats *iface_rx_stats;
	u64 rx_alloc_errors, tx_busy_errors;
	int q, i;

	rx_alloc_errors = 0;
	tx_busy_errors = 0;

	octep_vf_get_if_stats(oct);
	iface_tx_stats = &oct->iface_tx_stats;
	iface_rx_stats = &oct->iface_rx_stats;

	for (q = 0; q < OCTEP_VF_MAX_QUEUES; q++) {
		tx_busy_errors += oct->stats_iq[q].tx_busy;
		rx_alloc_errors += oct->stats_oq[q].alloc_failures;
	}
	i = 0;
	data[i++] = rx_alloc_errors;
	data[i++] = tx_busy_errors;
	data[i++] = iface_tx_stats->pkts;
	data[i++] = iface_tx_stats->octs;
	data[i++] = iface_tx_stats->bcst;
	data[i++] = iface_tx_stats->mcst;
	data[i++] = iface_rx_stats->pkts;
	data[i++] = iface_rx_stats->octets;
	data[i++] = iface_rx_stats->bcast_pkts;
	data[i++] = iface_rx_stats->dropped_octets_fifo_full;

	/* Per Tx Queue stats */
	for (q = 0; q < OCTEP_VF_MAX_QUEUES; q++) {
		data[i++] = oct->stats_iq[q].instr_posted;
		data[i++] = oct->stats_iq[q].instr_completed;
		data[i++] = oct->stats_iq[q].bytes_sent;
		data[i++] = oct->stats_iq[q].tx_busy;
	}

	/* Per Rx Queue stats */
	for (q = 0; q < oct->num_oqs; q++) {
		data[i++] = oct->stats_oq[q].packets;
		data[i++] = oct->stats_oq[q].bytes;
		data[i++] = oct->stats_oq[q].alloc_failures;
	}
}

#define OCTEP_VF_SET_ETHTOOL_LINK_MODES_BITMAP(octep_vf_speeds, ksettings, name) \
{ \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_10GBASE_T)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseT_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_10GBASE_R)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseR_FEC); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_10GBASE_CR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseCR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_10GBASE_KR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseKR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_10GBASE_LR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseLR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_10GBASE_SR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseSR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_25GBASE_CR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 25000baseCR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_25GBASE_KR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 25000baseKR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_25GBASE_SR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 25000baseSR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_40GBASE_CR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseCR4_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_40GBASE_KR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseKR4_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_40GBASE_LR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseLR4_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_40GBASE_SR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseSR4_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_50GBASE_CR2)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseCR2_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_50GBASE_KR2)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseKR2_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_50GBASE_SR2)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseSR2_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_50GBASE_CR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseCR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_50GBASE_KR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseKR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_50GBASE_LR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseLR_ER_FR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_50GBASE_SR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseSR_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_100GBASE_CR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseCR4_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_100GBASE_KR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseKR4_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_100GBASE_LR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseLR4_ER4_Full); \
	if ((octep_vf_speeds) & BIT(OCTEP_VF_LINK_MODE_100GBASE_SR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseSR4_Full); \
}

static int octep_vf_get_link_ksettings(struct net_device *netdev,
				       struct ethtool_link_ksettings *cmd)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	struct octep_vf_iface_link_info *link_info;
	u32 advertised_modes, supported_modes;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);

	octep_vf_get_link_info(oct);

	advertised_modes = oct->link_info.advertised_modes;
	supported_modes = oct->link_info.supported_modes;
	link_info = &oct->link_info;

	OCTEP_VF_SET_ETHTOOL_LINK_MODES_BITMAP(supported_modes, cmd, supported);
	OCTEP_VF_SET_ETHTOOL_LINK_MODES_BITMAP(advertised_modes, cmd, advertising);

	if (link_info->autoneg) {
		if (link_info->autoneg & OCTEP_VF_LINK_MODE_AUTONEG_SUPPORTED)
			ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
		if (link_info->autoneg & OCTEP_VF_LINK_MODE_AUTONEG_ADVERTISED) {
			ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);
			cmd->base.autoneg = AUTONEG_ENABLE;
		} else {
			cmd->base.autoneg = AUTONEG_DISABLE;
		}
	} else {
		cmd->base.autoneg = AUTONEG_DISABLE;
	}

	cmd->base.port = PORT_FIBRE;
	ethtool_link_ksettings_add_link_mode(cmd, supported, FIBRE);
	ethtool_link_ksettings_add_link_mode(cmd, advertising, FIBRE);

	if (netif_carrier_ok(netdev)) {
		cmd->base.speed = link_info->speed;
		cmd->base.duplex = DUPLEX_FULL;
	} else {
		cmd->base.speed = SPEED_UNKNOWN;
		cmd->base.duplex = DUPLEX_UNKNOWN;
	}
	return 0;
}

static const struct ethtool_ops octep_vf_ethtool_ops = {
	.get_drvinfo = octep_vf_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_strings = octep_vf_get_strings,
	.get_sset_count = octep_vf_get_sset_count,
	.get_ethtool_stats = octep_vf_get_ethtool_stats,
	.get_link_ksettings = octep_vf_get_link_ksettings,
};

void octep_vf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &octep_vf_ethtool_ops;
}
