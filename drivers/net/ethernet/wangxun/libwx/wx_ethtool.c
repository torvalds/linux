// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2023 Beijing WangXun Technology Co., Ltd. */

#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/ethtool.h>

#include "wx_type.h"
#include "wx_ethtool.h"
#include "wx_hw.h"
#include "wx_lib.h"

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

static const struct wx_stats wx_gstrings_fdir_stats[] = {
	WX_STAT("fdir_match", stats.fdirmatch),
	WX_STAT("fdir_miss", stats.fdirmiss),
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
#define WX_FDIR_STATS_LEN  ARRAY_SIZE(wx_gstrings_fdir_stats)
#define WX_STATS_LEN (WX_GLOBAL_STATS_LEN + WX_QUEUE_STATS_LEN)

int wx_get_sset_count(struct net_device *netdev, int sset)
{
	struct wx *wx = netdev_priv(netdev);

	switch (sset) {
	case ETH_SS_STATS:
		return (wx->mac.type == wx_mac_sp) ?
			WX_STATS_LEN + WX_FDIR_STATS_LEN : WX_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL(wx_get_sset_count);

void wx_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct wx *wx = netdev_priv(netdev);
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < WX_GLOBAL_STATS_LEN; i++)
			ethtool_puts(&p, wx_gstrings_stats[i].stat_string);
		if (wx->mac.type == wx_mac_sp) {
			for (i = 0; i < WX_FDIR_STATS_LEN; i++)
				ethtool_puts(&p, wx_gstrings_fdir_stats[i].stat_string);
		}
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
	int i, j, k;
	char *p;

	wx_update_stats(wx);

	for (i = 0; i < WX_GLOBAL_STATS_LEN; i++) {
		p = (char *)wx + wx_gstrings_stats[i].stat_offset;
		data[i] = (wx_gstrings_stats[i].sizeof_stat ==
			   sizeof(u64)) ? *(u64 *)p : *(u32 *)p;
	}

	if (wx->mac.type == wx_mac_sp) {
		for (k = 0; k < WX_FDIR_STATS_LEN; k++) {
			p = (char *)wx + wx_gstrings_fdir_stats[k].stat_offset;
			data[i++] = *(u64 *)p;
		}
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
	unsigned int stats_len = WX_STATS_LEN;
	struct wx *wx = netdev_priv(netdev);

	if (wx->mac.type == wx_mac_sp)
		stats_len += WX_FDIR_STATS_LEN;

	strscpy(info->driver, wx->driver_name, sizeof(info->driver));
	strscpy(info->fw_version, wx->eeprom_id, sizeof(info->fw_version));
	strscpy(info->bus_info, pci_name(wx->pdev), sizeof(info->bus_info));
	if (wx->num_tx_queues <= WX_NUM_TX_QUEUES) {
		info->n_stats = stats_len -
				   (WX_NUM_TX_QUEUES - wx->num_tx_queues) *
				   (sizeof(struct wx_queue_stats) / sizeof(u64)) * 2;
	} else {
		info->n_stats = stats_len;
	}
}
EXPORT_SYMBOL(wx_get_drvinfo);

int wx_nway_reset(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);

	return phylink_ethtool_nway_reset(wx->phylink);
}
EXPORT_SYMBOL(wx_nway_reset);

int wx_get_link_ksettings(struct net_device *netdev,
			  struct ethtool_link_ksettings *cmd)
{
	struct wx *wx = netdev_priv(netdev);

	return phylink_ethtool_ksettings_get(wx->phylink, cmd);
}
EXPORT_SYMBOL(wx_get_link_ksettings);

int wx_set_link_ksettings(struct net_device *netdev,
			  const struct ethtool_link_ksettings *cmd)
{
	struct wx *wx = netdev_priv(netdev);

	return phylink_ethtool_ksettings_set(wx->phylink, cmd);
}
EXPORT_SYMBOL(wx_set_link_ksettings);

void wx_get_pauseparam(struct net_device *netdev,
		       struct ethtool_pauseparam *pause)
{
	struct wx *wx = netdev_priv(netdev);

	phylink_ethtool_get_pauseparam(wx->phylink, pause);
}
EXPORT_SYMBOL(wx_get_pauseparam);

int wx_set_pauseparam(struct net_device *netdev,
		      struct ethtool_pauseparam *pause)
{
	struct wx *wx = netdev_priv(netdev);

	return phylink_ethtool_set_pauseparam(wx->phylink, pause);
}
EXPORT_SYMBOL(wx_set_pauseparam);

void wx_get_ringparam(struct net_device *netdev,
		      struct ethtool_ringparam *ring,
		      struct kernel_ethtool_ringparam *kernel_ring,
		      struct netlink_ext_ack *extack)
{
	struct wx *wx = netdev_priv(netdev);

	ring->rx_max_pending = WX_MAX_RXD;
	ring->tx_max_pending = WX_MAX_TXD;
	ring->rx_mini_max_pending = 0;
	ring->rx_jumbo_max_pending = 0;
	ring->rx_pending = wx->rx_ring_count;
	ring->tx_pending = wx->tx_ring_count;
	ring->rx_mini_pending = 0;
	ring->rx_jumbo_pending = 0;
}
EXPORT_SYMBOL(wx_get_ringparam);

int wx_get_coalesce(struct net_device *netdev,
		    struct ethtool_coalesce *ec,
		    struct kernel_ethtool_coalesce *kernel_coal,
		    struct netlink_ext_ack *extack)
{
	struct wx *wx = netdev_priv(netdev);

	ec->tx_max_coalesced_frames_irq = wx->tx_work_limit;
	/* only valid if in constant ITR mode */
	if (wx->rx_itr_setting <= 1)
		ec->rx_coalesce_usecs = wx->rx_itr_setting;
	else
		ec->rx_coalesce_usecs = wx->rx_itr_setting >> 2;

	/* if in mixed tx/rx queues per vector mode, report only rx settings */
	if (wx->q_vector[0]->tx.count && wx->q_vector[0]->rx.count)
		return 0;

	/* only valid if in constant ITR mode */
	if (wx->tx_itr_setting <= 1)
		ec->tx_coalesce_usecs = wx->tx_itr_setting;
	else
		ec->tx_coalesce_usecs = wx->tx_itr_setting >> 2;

	return 0;
}
EXPORT_SYMBOL(wx_get_coalesce);

int wx_set_coalesce(struct net_device *netdev,
		    struct ethtool_coalesce *ec,
		    struct kernel_ethtool_coalesce *kernel_coal,
		    struct netlink_ext_ack *extack)
{
	struct wx *wx = netdev_priv(netdev);
	u16 tx_itr_param, rx_itr_param;
	struct wx_q_vector *q_vector;
	u16 max_eitr;
	int i;

	if (wx->q_vector[0]->tx.count && wx->q_vector[0]->rx.count) {
		/* reject Tx specific changes in case of mixed RxTx vectors */
		if (ec->tx_coalesce_usecs)
			return -EOPNOTSUPP;
	}

	if (ec->tx_max_coalesced_frames_irq)
		wx->tx_work_limit = ec->tx_max_coalesced_frames_irq;

	if (wx->mac.type == wx_mac_sp)
		max_eitr = WX_SP_MAX_EITR;
	else
		max_eitr = WX_EM_MAX_EITR;

	if ((ec->rx_coalesce_usecs > (max_eitr >> 2)) ||
	    (ec->tx_coalesce_usecs > (max_eitr >> 2)))
		return -EINVAL;

	if (ec->rx_coalesce_usecs > 1)
		wx->rx_itr_setting = ec->rx_coalesce_usecs << 2;
	else
		wx->rx_itr_setting = ec->rx_coalesce_usecs;

	if (wx->rx_itr_setting == 1)
		rx_itr_param = WX_20K_ITR;
	else
		rx_itr_param = wx->rx_itr_setting;

	if (ec->tx_coalesce_usecs > 1)
		wx->tx_itr_setting = ec->tx_coalesce_usecs << 2;
	else
		wx->tx_itr_setting = ec->tx_coalesce_usecs;

	if (wx->tx_itr_setting == 1) {
		if (wx->mac.type == wx_mac_sp)
			tx_itr_param = WX_12K_ITR;
		else
			tx_itr_param = WX_20K_ITR;
	} else {
		tx_itr_param = wx->tx_itr_setting;
	}

	/* mixed Rx/Tx */
	if (wx->q_vector[0]->tx.count && wx->q_vector[0]->rx.count)
		wx->tx_itr_setting = wx->rx_itr_setting;

	for (i = 0; i < wx->num_q_vectors; i++) {
		q_vector = wx->q_vector[i];
		if (q_vector->tx.count && !q_vector->rx.count)
			/* tx only */
			q_vector->itr = tx_itr_param;
		else
			/* rx only or mixed */
			q_vector->itr = rx_itr_param;
		wx_write_eitr(q_vector);
	}

	return 0;
}
EXPORT_SYMBOL(wx_set_coalesce);

static unsigned int wx_max_channels(struct wx *wx)
{
	unsigned int max_combined;

	if (!wx->msix_q_entries) {
		/* We only support one q_vector without MSI-X */
		max_combined = 1;
	} else {
		/* support up to max allowed queues with RSS */
		if (wx->mac.type == wx_mac_sp)
			max_combined = 63;
		else
			max_combined = 8;
	}

	return max_combined;
}

void wx_get_channels(struct net_device *dev,
		     struct ethtool_channels *ch)
{
	struct wx *wx = netdev_priv(dev);

	/* report maximum channels */
	ch->max_combined = wx_max_channels(wx);

	/* report info for other vector */
	if (wx->msix_q_entries) {
		ch->max_other = 1;
		ch->other_count = 1;
	}

	/* record RSS queues */
	ch->combined_count = wx->ring_feature[RING_F_RSS].indices;

	if (test_bit(WX_FLAG_FDIR_CAPABLE, wx->flags))
		ch->combined_count = wx->ring_feature[RING_F_FDIR].indices;
}
EXPORT_SYMBOL(wx_get_channels);

int wx_set_channels(struct net_device *dev,
		    struct ethtool_channels *ch)
{
	unsigned int count = ch->combined_count;
	struct wx *wx = netdev_priv(dev);

	/* verify other_count has not changed */
	if (ch->other_count != 1)
		return -EINVAL;

	/* verify the number of channels does not exceed hardware limits */
	if (count > wx_max_channels(wx))
		return -EINVAL;

	if (test_bit(WX_FLAG_FDIR_CAPABLE, wx->flags))
		wx->ring_feature[RING_F_FDIR].limit = count;

	wx->ring_feature[RING_F_RSS].limit = count;

	return 0;
}
EXPORT_SYMBOL(wx_set_channels);

u32 wx_get_msglevel(struct net_device *netdev)
{
	struct wx *wx = netdev_priv(netdev);

	return wx->msg_enable;
}
EXPORT_SYMBOL(wx_get_msglevel);

void wx_set_msglevel(struct net_device *netdev, u32 data)
{
	struct wx *wx = netdev_priv(netdev);

	wx->msg_enable = data;
}
EXPORT_SYMBOL(wx_set_msglevel);
