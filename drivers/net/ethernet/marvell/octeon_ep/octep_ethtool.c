// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/ethtool.h>

#include "octep_config.h"
#include "octep_main.h"
#include "octep_ctrl_net.h"

static const char octep_gstrings_global_stats[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"tx_packets",
	"rx_bytes",
	"tx_bytes",
	"rx_alloc_errors",
	"tx_busy_errors",
	"rx_dropped",
	"tx_dropped",
	"tx_hw_pkts",
	"tx_hw_octs",
	"tx_hw_bcast",
	"tx_hw_mcast",
	"tx_hw_underflow",
	"tx_hw_control",
	"tx_less_than_64",
	"tx_equal_64",
	"tx_equal_65_to_127",
	"tx_equal_128_to_255",
	"tx_equal_256_to_511",
	"tx_equal_512_to_1023",
	"tx_equal_1024_to_1518",
	"tx_greater_than_1518",
	"rx_hw_pkts",
	"rx_hw_bytes",
	"rx_hw_bcast",
	"rx_hw_mcast",
	"rx_pause_pkts",
	"rx_pause_bytes",
	"rx_dropped_pkts_fifo_full",
	"rx_dropped_bytes_fifo_full",
	"rx_err_pkts",
};

#define OCTEP_GLOBAL_STATS_CNT ARRAY_SIZE(octep_gstrings_global_stats)

static const char octep_gstrings_tx_q_stats[][ETH_GSTRING_LEN] = {
	"tx_packets_posted[Q-%u]",
	"tx_packets_completed[Q-%u]",
	"tx_bytes[Q-%u]",
	"tx_busy[Q-%u]",
};

#define OCTEP_TX_Q_STATS_CNT ARRAY_SIZE(octep_gstrings_tx_q_stats)

static const char octep_gstrings_rx_q_stats[][ETH_GSTRING_LEN] = {
	"rx_packets[Q-%u]",
	"rx_bytes[Q-%u]",
	"rx_alloc_errors[Q-%u]",
};

#define OCTEP_RX_Q_STATS_CNT ARRAY_SIZE(octep_gstrings_rx_q_stats)

static void octep_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct octep_device *oct = netdev_priv(netdev);

	strscpy(info->driver, OCTEP_DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, pci_name(oct->pdev), sizeof(info->bus_info));
}

static void octep_get_strings(struct net_device *netdev,
			      u32 stringset, u8 *data)
{
	struct octep_device *oct = netdev_priv(netdev);
	u16 num_queues = CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf);
	const char *str;
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < OCTEP_GLOBAL_STATS_CNT; i++)
			ethtool_puts(&data, octep_gstrings_global_stats[i]);

		for (i = 0; i < num_queues; i++)
			for (j = 0; j < OCTEP_TX_Q_STATS_CNT; j++) {
				str = octep_gstrings_tx_q_stats[j];
				ethtool_sprintf(&data, str, i);
			}

		for (i = 0; i < num_queues; i++)
			for (j = 0; j < OCTEP_RX_Q_STATS_CNT; j++) {
				str = octep_gstrings_rx_q_stats[j];
				ethtool_sprintf(&data, str, i);
			}
		break;
	default:
		break;
	}
}

static int octep_get_sset_count(struct net_device *netdev, int sset)
{
	struct octep_device *oct = netdev_priv(netdev);
	u16 num_queues = CFG_GET_PORTS_ACTIVE_IO_RINGS(oct->conf);

	switch (sset) {
	case ETH_SS_STATS:
		return OCTEP_GLOBAL_STATS_CNT + (num_queues *
		       (OCTEP_TX_Q_STATS_CNT + OCTEP_RX_Q_STATS_CNT));
		break;
	default:
		return -EOPNOTSUPP;
	}
}

static void
octep_get_ethtool_stats(struct net_device *netdev,
			struct ethtool_stats *stats, u64 *data)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct octep_iface_tx_stats *iface_tx_stats;
	struct octep_iface_rx_stats *iface_rx_stats;
	u64 rx_packets, rx_bytes;
	u64 tx_packets, tx_bytes;
	u64 rx_alloc_errors, tx_busy_errors;
	int q, i;

	rx_packets = 0;
	rx_bytes = 0;
	tx_packets = 0;
	tx_bytes = 0;
	rx_alloc_errors = 0;
	tx_busy_errors = 0;
	tx_packets = 0;
	tx_bytes = 0;
	rx_packets = 0;
	rx_bytes = 0;

	iface_tx_stats = &oct->iface_tx_stats;
	iface_rx_stats = &oct->iface_rx_stats;
	octep_ctrl_net_get_if_stats(oct,
				    OCTEP_CTRL_NET_INVALID_VFID,
				    iface_rx_stats,
				    iface_tx_stats);

	for (q = 0; q < OCTEP_MAX_QUEUES; q++) {
		tx_packets += oct->stats_iq[q].instr_completed;
		tx_bytes += oct->stats_iq[q].bytes_sent;
		tx_busy_errors += oct->stats_iq[q].tx_busy;

		rx_packets += oct->stats_oq[q].packets;
		rx_bytes += oct->stats_oq[q].bytes;
		rx_alloc_errors += oct->stats_oq[q].alloc_failures;
	}
	i = 0;
	data[i++] = rx_packets;
	data[i++] = tx_packets;
	data[i++] = rx_bytes;
	data[i++] = tx_bytes;
	data[i++] = rx_alloc_errors;
	data[i++] = tx_busy_errors;
	data[i++] = iface_rx_stats->dropped_pkts_fifo_full +
		    iface_rx_stats->err_pkts;
	data[i++] = iface_tx_stats->xscol +
		    iface_tx_stats->xsdef;
	data[i++] = iface_tx_stats->pkts;
	data[i++] = iface_tx_stats->octs;
	data[i++] = iface_tx_stats->bcst;
	data[i++] = iface_tx_stats->mcst;
	data[i++] = iface_tx_stats->undflw;
	data[i++] = iface_tx_stats->ctl;
	data[i++] = iface_tx_stats->hist_lt64;
	data[i++] = iface_tx_stats->hist_eq64;
	data[i++] = iface_tx_stats->hist_65to127;
	data[i++] = iface_tx_stats->hist_128to255;
	data[i++] = iface_tx_stats->hist_256to511;
	data[i++] = iface_tx_stats->hist_512to1023;
	data[i++] = iface_tx_stats->hist_1024to1518;
	data[i++] = iface_tx_stats->hist_gt1518;
	data[i++] = iface_rx_stats->pkts;
	data[i++] = iface_rx_stats->octets;
	data[i++] = iface_rx_stats->mcast_pkts;
	data[i++] = iface_rx_stats->bcast_pkts;
	data[i++] = iface_rx_stats->pause_pkts;
	data[i++] = iface_rx_stats->pause_octets;
	data[i++] = iface_rx_stats->dropped_pkts_fifo_full;
	data[i++] = iface_rx_stats->dropped_octets_fifo_full;
	data[i++] = iface_rx_stats->err_pkts;

	/* Per Tx Queue stats */
	for (q = 0; q < OCTEP_MAX_QUEUES; q++) {
		data[i++] = oct->stats_iq[q].instr_posted;
		data[i++] = oct->stats_iq[q].instr_completed;
		data[i++] = oct->stats_iq[q].bytes_sent;
		data[i++] = oct->stats_iq[q].tx_busy;
	}

	/* Per Rx Queue stats */
	for (q = 0; q < OCTEP_MAX_QUEUES; q++) {
		data[i++] = oct->stats_oq[q].packets;
		data[i++] = oct->stats_oq[q].bytes;
		data[i++] = oct->stats_oq[q].alloc_failures;
	}
}

#define OCTEP_SET_ETHTOOL_LINK_MODES_BITMAP(octep_speeds, ksettings, name) \
{ \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_10GBASE_T)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseT_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_10GBASE_R)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseR_FEC); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_10GBASE_CR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseCR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_10GBASE_KR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseKR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_10GBASE_LR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseLR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_10GBASE_SR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 10000baseSR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_25GBASE_CR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 25000baseCR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_25GBASE_KR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 25000baseKR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_25GBASE_SR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 25000baseSR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_40GBASE_CR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseCR4_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_40GBASE_KR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseKR4_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_40GBASE_LR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseLR4_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_40GBASE_SR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 40000baseSR4_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_50GBASE_CR2)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseCR2_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_50GBASE_KR2)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseKR2_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_50GBASE_SR2)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseSR2_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_50GBASE_CR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseCR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_50GBASE_KR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseKR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_50GBASE_LR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseLR_ER_FR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_50GBASE_SR)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 50000baseSR_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_100GBASE_CR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseCR4_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_100GBASE_KR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseKR4_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_100GBASE_LR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseLR4_ER4_Full); \
	if ((octep_speeds) & BIT(OCTEP_LINK_MODE_100GBASE_SR4)) \
		ethtool_link_ksettings_add_link_mode(ksettings, name, 100000baseSR4_Full); \
}

static int octep_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings *cmd)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct octep_iface_link_info *link_info;
	u32 advertised_modes, supported_modes;

	ethtool_link_ksettings_zero_link_mode(cmd, supported);
	ethtool_link_ksettings_zero_link_mode(cmd, advertising);

	link_info = &oct->link_info;
	octep_ctrl_net_get_link_info(oct, OCTEP_CTRL_NET_INVALID_VFID, link_info);

	advertised_modes = oct->link_info.advertised_modes;
	supported_modes = oct->link_info.supported_modes;

	OCTEP_SET_ETHTOOL_LINK_MODES_BITMAP(supported_modes, cmd, supported);
	OCTEP_SET_ETHTOOL_LINK_MODES_BITMAP(advertised_modes, cmd, advertising);

	if (link_info->autoneg) {
		if (link_info->autoneg & OCTEP_LINK_MODE_AUTONEG_SUPPORTED)
			ethtool_link_ksettings_add_link_mode(cmd, supported, Autoneg);
		if (link_info->autoneg & OCTEP_LINK_MODE_AUTONEG_ADVERTISED) {
			ethtool_link_ksettings_add_link_mode(cmd, advertising, Autoneg);
			cmd->base.autoneg = AUTONEG_ENABLE;
		} else {
			cmd->base.autoneg = AUTONEG_DISABLE;
		}
	} else {
		cmd->base.autoneg = AUTONEG_DISABLE;
	}

	if (link_info->pause) {
		if (link_info->pause & OCTEP_LINK_MODE_PAUSE_SUPPORTED)
			ethtool_link_ksettings_add_link_mode(cmd, supported, Pause);
		if (link_info->pause & OCTEP_LINK_MODE_PAUSE_ADVERTISED)
			ethtool_link_ksettings_add_link_mode(cmd, advertising, Pause);
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

static int octep_set_link_ksettings(struct net_device *netdev,
				    const struct ethtool_link_ksettings *cmd)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct octep_iface_link_info link_info_new;
	struct octep_iface_link_info *link_info;
	u64 advertised = 0;
	u8 autoneg = 0;
	int err;

	link_info = &oct->link_info;
	memcpy(&link_info_new, link_info, sizeof(struct octep_iface_link_info));

	/* Only Full duplex is supported;
	 * Assume full duplex when duplex is unknown.
	 */
	if (cmd->base.duplex != DUPLEX_FULL &&
	    cmd->base.duplex != DUPLEX_UNKNOWN)
		return -EOPNOTSUPP;

	if (cmd->base.autoneg == AUTONEG_ENABLE) {
		if (!(link_info->autoneg & OCTEP_LINK_MODE_AUTONEG_SUPPORTED))
			return -EOPNOTSUPP;
		autoneg = 1;
	}

	if (!bitmap_subset(cmd->link_modes.advertising,
			   cmd->link_modes.supported,
			   __ETHTOOL_LINK_MODE_MASK_NBITS))
		return -EINVAL;

	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  10000baseT_Full))
		advertised |= BIT(OCTEP_LINK_MODE_10GBASE_T);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  10000baseR_FEC))
		advertised |= BIT(OCTEP_LINK_MODE_10GBASE_R);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  10000baseCR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_10GBASE_CR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  10000baseKR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_10GBASE_KR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  10000baseLR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_10GBASE_LR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  10000baseSR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_10GBASE_SR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  25000baseCR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_25GBASE_CR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  25000baseKR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_25GBASE_KR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  25000baseSR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_25GBASE_SR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  40000baseCR4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_40GBASE_CR4);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  40000baseKR4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_40GBASE_KR4);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  40000baseLR4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_40GBASE_LR4);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  40000baseSR4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_40GBASE_SR4);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  50000baseCR2_Full))
		advertised |= BIT(OCTEP_LINK_MODE_50GBASE_CR2);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  50000baseKR2_Full))
		advertised |= BIT(OCTEP_LINK_MODE_50GBASE_KR2);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  50000baseSR2_Full))
		advertised |= BIT(OCTEP_LINK_MODE_50GBASE_SR2);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  50000baseCR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_50GBASE_CR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  50000baseKR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_50GBASE_KR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  50000baseLR_ER_FR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_50GBASE_LR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  50000baseSR_Full))
		advertised |= BIT(OCTEP_LINK_MODE_50GBASE_SR);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  100000baseCR4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_100GBASE_CR4);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  100000baseKR4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_100GBASE_KR4);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  100000baseLR4_ER4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_100GBASE_LR4);
	if (ethtool_link_ksettings_test_link_mode(cmd, advertising,
						  100000baseSR4_Full))
		advertised |= BIT(OCTEP_LINK_MODE_100GBASE_SR4);

	if (advertised == link_info->advertised_modes &&
	    cmd->base.speed == link_info->speed &&
	    cmd->base.autoneg == link_info->autoneg)
		return 0;

	link_info_new.advertised_modes = advertised;
	link_info_new.speed = cmd->base.speed;
	link_info_new.autoneg = autoneg;

	err = octep_ctrl_net_set_link_info(oct, OCTEP_CTRL_NET_INVALID_VFID,
					   &link_info_new, true);
	if (err)
		return err;

	memcpy(link_info, &link_info_new, sizeof(struct octep_iface_link_info));
	return 0;
}

static const struct ethtool_ops octep_ethtool_ops = {
	.get_drvinfo = octep_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_strings = octep_get_strings,
	.get_sset_count = octep_get_sset_count,
	.get_ethtool_stats = octep_get_ethtool_stats,
	.get_link_ksettings = octep_get_link_ksettings,
	.set_link_ksettings = octep_set_link_ksettings,
};

void octep_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &octep_ethtool_ops;
}
