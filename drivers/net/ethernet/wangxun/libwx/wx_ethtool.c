// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/ethtool.h>

#include "wx_type.h"
#include "wx_ethtool.h"
#include "wx_hw.h"

struct wx_stats {
	char stat_string[ETH_GSTRING_LEN];
	size_t sizeof_stat;
	off_t stat_offset;
};

#define WX_STAT(str, m) { \
		.stat_string = str, \
		.sizeof_stat = sizeof(((struct wx *)0)->m), \
		.stat_offset = offsetof(struct wx, m) }

static const struct wx_stats wx_gstrings_stats[] = {
	WX_STAT("rx_dma_pkts", stats.gprc),
	WX_STAT("tx_dma_pkts", stats.gptc),
	WX_STAT("rx_dma_bytes", stats.gorc),
	WX_STAT("tx_dma_bytes", stats.gotc),
	WX_STAT("rx_total_pkts", stats.tpr),
	WX_STAT("tx_total_pkts", stats.tpt),
	WX_STAT("rx_long_length_count", stats.roc),
	WX_STAT("rx_short_length_count", stats.ruc),
	WX_STAT("os2bmc_rx_by_bmc", stats.o2bgptc),
	WX_STAT("os2bmc_tx_by_bmc", stats.b2ospc),
	WX_STAT("os2bmc_tx_by_host", stats.o2bspc),
	WX_STAT("os2bmc_rx_by_host", stats.b2ogprc),
	WX_STAT("rx_no_dma_resources", stats.rdmdrop),
	WX_STAT("tx_busy", tx_busy),
	WX_STAT("non_eop_descs", non_eop_descs),
	WX_STAT("tx_restart_queue", restart_queue),
	WX_STAT("rx_csum_offload_good_count", hw_csum_rx_good),
	WX_STAT("rx_csum_offload_errors", hw_csum_rx_error),
	WX_STAT("alloc_rx_buff_failed", alloc_rx_buff_failed),
};

/* drivers allocates num_tx_queues and num_rx_queues symmetrically so
 * we set the num_rx_queues to evaluate to num_tx_queues. This is
 * used because we do not have a good way to get the max number of
 * rx queues with CONFIG_RPS disabled.
 */
#define WX_NUM_RX_QUEUES netdev->num_tx_queues
#define WX_NUM_TX_QUEUES netdev->num_tx_queues

#define WX_QUEUE_STATS_LEN ( \
		(WX_NUM_TX_QUEUES + WX_NUM_RX_QUEUES) * \
		(sizeof(struct wx_queue_stats) / sizeof(u64)))
#define WX_GLOBAL_STATS_LEN  ARRAY_SIZE(wx_gstrings_stats)
#define WX_STATS_LEN (WX_GLOBAL_STATS_LEN + WX_QUEUE_STATS_LEN)

int wx_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return WX_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(wx_get_sset_count);

void wx_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < WX_GLOBAL_STATS_LEN; i++)
			ethtool_sprintf(&p, wx_gstrings_stats[i].stat_string);
		for (i = 0; i < netdev->num_tx_queues; i++) {
			ethtool_sprintf(&p, "tx_queue_%u_packets", i);
			ethtool_sprintf(&p, "tx_queue_%u_bytes", i);
		}
		for (i = 0; i < WX_NUM_RX_QUEUES; i++) {
			ethtool_sprintf(&p, "rx_queue_%u_packets", i);
			ethtool_sprintf(&p, "rx_queue_%u_bytes", i);
		}
		break;
	}
}
EXPORT_SYMBOL(wx_get_strings);

void wx_get_ethtool_stats(struct net_device *netdev,
			  struct ethtool_stats *stats, u64 *data)
{
	struct wx *wx = netdev_priv(netdev);
	struct wx_ring *ring;
	unsigned int start;
	int i, j;
	char *p;

	wx_update_stats(wx);

	for (i = 0; i < WX_GLOBAL_STATS_LEN; i++) {
		p = (char *)wx + wx_gstrings_stats[i].stat_offset;
		data[i] = (wx_gstrings_stats[i].sizeof_stat ==
			   sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	for (j = 0; j < netdev->num_tx_queues; j++) {
		ring = wx->tx_ring[j];
		if (!ring) {
			data[i++] = 0;
			data[i++] = 0;
			continue;
		}

		do {
			start = u64_stats_fetch_begin(&ring->syncp);
			data[i] = ring->stats.packets;
			data[i + 1] = ring->stats.bytes;
		} while (u64_stats_fetch_retry(&ring->syncp, start));
		i += 2;
	}
	for (j = 0; j < WX_NUM_RX_QUEUES; j++) {
		ring = wx->rx_ring[j];
		if (!ring) {
			data[i++] = 0;
			data[i++] = 0;
			continue;
		}

		do {
			start = u64_stats_fetch_begin(&ring->syncp);
			data[i] = ring->stats.packets;
			data[i + 1] = ring->stats.bytes;
		} while (u64_stats_fetch_retry(&ring->syncp, start));
		i += 2;
	}
}
EXPORT_SYMBOL(wx_get_ethtool_stats);

void wx_get_mac_stats(struct net_device *netdev,
		      struct ethtool_eth_mac_stats *mac_stats)
{
	struct wx *wx = netdev_priv(netdev);
	struct wx_hw_stats *hwstats;

	wx_update_stats(wx);

	hwstats = &wx->stats;
	mac_stats->MulticastFramesXmittedOK = hwstats->mptc;
	mac_stats->BroadcastFramesXmittedOK = hwstats->bptc;
	mac_stats->MulticastFramesReceivedOK = hwstats->mprc;
	mac_stats->BroadcastFramesReceivedOK = hwstats->bprc;
}
EXPORT_SYMBOL(wx_get_mac_stats);

void wx_get_pause_stats(struct net_device *netdev,
			struct ethtool_pause_stats *stats)
{
	struct wx *wx = netdev_priv(netdev);
	struct wx_hw_stats *hwstats;

	wx_update_stats(wx);

	hwstats = &wx->stats;
	stats->tx_pause_frames = hwstats->lxontxc + hwstats->lxofftxc;
	stats->rx_pause_frames = hwstats->lxonoffrxc;
}
EXPORT_SYMBOL(wx_get_pause_stats);

void wx_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *info)
{
	struct wx *wx = netdev_priv(netdev);

	strscpy(info->driver, wx->driver_name, sizeof(info->driver));
	strscpy(info->fw_version, wx->eeprom_id, sizeof(info->fw_version));
	strscpy(info->bus_info, pci_name(wx->pdev), sizeof(info->bus_info));
	if (wx->num_tx_queues <= WX_NUM_TX_QUEUES) {
		info->n_stats = WX_STATS_LEN -
				   (WX_NUM_TX_QUEUES - wx->num_tx_queues) *
				   (sizeof(struct wx_queue_stats) / sizeof(u64)) * 2;
	} else {
		info->n_stats = WX_STATS_LEN;
	}
}
EXPORT_SYMBOL(wx_get_drvinfo);
