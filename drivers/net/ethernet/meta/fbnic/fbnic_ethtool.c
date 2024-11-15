// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_tlv.h"

static int
fbnic_get_ts_info(struct net_device *netdev,
		  struct kernel_ethtool_ts_info *tsinfo)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	tsinfo->phc_index = ptp_clock_index(fbn->fbd->ptp);

	tsinfo->so_timestamping =
		SOF_TIMESTAMPING_TX_SOFTWARE |
		SOF_TIMESTAMPING_TX_HARDWARE |
		SOF_TIMESTAMPING_RX_HARDWARE |
		SOF_TIMESTAMPING_RAW_HARDWARE;

	tsinfo->tx_types =
		BIT(HWTSTAMP_TX_OFF) |
		BIT(HWTSTAMP_TX_ON);

	tsinfo->rx_filters =
		BIT(HWTSTAMP_FILTER_NONE) |
		BIT(HWTSTAMP_FILTER_PTP_V1_L4_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L4_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_L2_EVENT) |
		BIT(HWTSTAMP_FILTER_PTP_V2_EVENT) |
		BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

static void
fbnic_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *drvinfo)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_dev *fbd = fbn->fbd;

	fbnic_get_fw_ver_commit_str(fbd, drvinfo->fw_version,
				    sizeof(drvinfo->fw_version));
}

static void fbnic_set_counter(u64 *stat, struct fbnic_stat_counter *counter)
{
	if (counter->reported)
		*stat = counter->value;
}

static void
fbnic_get_eth_mac_stats(struct net_device *netdev,
			struct ethtool_eth_mac_stats *eth_mac_stats)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	struct fbnic_mac_stats *mac_stats;
	struct fbnic_dev *fbd = fbn->fbd;
	const struct fbnic_mac *mac;

	mac_stats = &fbd->hw_stats.mac;
	mac = fbd->mac;

	mac->get_eth_mac_stats(fbd, false, &mac_stats->eth_mac);

	fbnic_set_counter(&eth_mac_stats->FramesTransmittedOK,
			  &mac_stats->eth_mac.FramesTransmittedOK);
	fbnic_set_counter(&eth_mac_stats->FramesReceivedOK,
			  &mac_stats->eth_mac.FramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FrameCheckSequenceErrors,
			  &mac_stats->eth_mac.FrameCheckSequenceErrors);
	fbnic_set_counter(&eth_mac_stats->AlignmentErrors,
			  &mac_stats->eth_mac.AlignmentErrors);
	fbnic_set_counter(&eth_mac_stats->OctetsTransmittedOK,
			  &mac_stats->eth_mac.OctetsTransmittedOK);
	fbnic_set_counter(&eth_mac_stats->FramesLostDueToIntMACXmitError,
			  &mac_stats->eth_mac.FramesLostDueToIntMACXmitError);
	fbnic_set_counter(&eth_mac_stats->OctetsReceivedOK,
			  &mac_stats->eth_mac.OctetsReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FramesLostDueToIntMACRcvError,
			  &mac_stats->eth_mac.FramesLostDueToIntMACRcvError);
	fbnic_set_counter(&eth_mac_stats->MulticastFramesXmittedOK,
			  &mac_stats->eth_mac.MulticastFramesXmittedOK);
	fbnic_set_counter(&eth_mac_stats->BroadcastFramesXmittedOK,
			  &mac_stats->eth_mac.BroadcastFramesXmittedOK);
	fbnic_set_counter(&eth_mac_stats->MulticastFramesReceivedOK,
			  &mac_stats->eth_mac.MulticastFramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->BroadcastFramesReceivedOK,
			  &mac_stats->eth_mac.BroadcastFramesReceivedOK);
	fbnic_set_counter(&eth_mac_stats->FrameTooLongErrors,
			  &mac_stats->eth_mac.FrameTooLongErrors);
}

static void fbnic_get_ts_stats(struct net_device *netdev,
			       struct ethtool_ts_stats *ts_stats)
{
	struct fbnic_net *fbn = netdev_priv(netdev);
	u64 ts_packets, ts_lost;
	struct fbnic_ring *ring;
	unsigned int start;
	int i;

	ts_stats->pkts = fbn->tx_stats.ts_packets;
	ts_stats->lost = fbn->tx_stats.ts_lost;
	for (i = 0; i < fbn->num_tx_queues; i++) {
		ring = fbn->tx[i];
		do {
			start = u64_stats_fetch_begin(&ring->stats.syncp);
			ts_packets = ring->stats.ts_packets;
			ts_lost = ring->stats.ts_lost;
		} while (u64_stats_fetch_retry(&ring->stats.syncp, start));
		ts_stats->pkts += ts_packets;
		ts_stats->lost += ts_lost;
	}
}

static void fbnic_get_regs(struct net_device *netdev,
			   struct ethtool_regs *regs, void *data)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	fbnic_csr_get_regs(fbn->fbd, data, &regs->version);
}

static int fbnic_get_regs_len(struct net_device *netdev)
{
	struct fbnic_net *fbn = netdev_priv(netdev);

	return fbnic_csr_regs_len(fbn->fbd) * sizeof(u32);
}

static const struct ethtool_ops fbnic_ethtool_ops = {
	.get_drvinfo		= fbnic_get_drvinfo,
	.get_regs_len		= fbnic_get_regs_len,
	.get_regs		= fbnic_get_regs,
	.get_ts_info		= fbnic_get_ts_info,
	.get_ts_stats		= fbnic_get_ts_stats,
	.get_eth_mac_stats	= fbnic_get_eth_mac_stats,
};

void fbnic_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &fbnic_ethtool_ops;
}
