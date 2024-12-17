// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/pci.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_tlv.h"

struct fbnic_stat {
	u8 string[ETH_GSTRING_LEN];
	unsigned int size;
	unsigned int offset;
};

#define FBNIC_STAT_FIELDS(type, name, stat) { \
	.string = name, \
	.size = sizeof_field(struct type, stat), \
	.offset = offsetof(struct type, stat), \
}

/* Hardware statistics not captured in rtnl_link_stats */
#define FBNIC_HW_STAT(name, stat) \
	FBNIC_STAT_FIELDS(fbnic_hw_stats, name, stat)

static const struct fbnic_stat fbnic_gstrings_hw_stats[] = {
	/* RPC */
	FBNIC_HW_STAT("rpc_unkn_etype", rpc.unkn_etype),
	FBNIC_HW_STAT("rpc_unkn_ext_hdr", rpc.unkn_ext_hdr),
	FBNIC_HW_STAT("rpc_ipv4_frag", rpc.ipv4_frag),
	FBNIC_HW_STAT("rpc_ipv6_frag", rpc.ipv6_frag),
	FBNIC_HW_STAT("rpc_ipv4_esp", rpc.ipv4_esp),
	FBNIC_HW_STAT("rpc_ipv6_esp", rpc.ipv6_esp),
	FBNIC_HW_STAT("rpc_tcp_opt_err", rpc.tcp_opt_err),
	FBNIC_HW_STAT("rpc_out_of_hdr_err", rpc.out_of_hdr_err),
};

#define FBNIC_HW_FIXED_STATS_LEN ARRAY_SIZE(fbnic_gstrings_hw_stats)
#define FBNIC_HW_STATS_LEN	FBNIC_HW_FIXED_STATS_LEN

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

static void fbnic_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	int i;

	switch (sset) {
	case ETH_SS_STATS:
		for (i = 0; i < FBNIC_HW_STATS_LEN; i++)
			ethtool_puts(&data, fbnic_gstrings_hw_stats[i].string);
		break;
	}
}

static int fbnic_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return FBNIC_HW_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void fbnic_get_ethtool_stats(struct net_device *dev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct fbnic_net *fbn = netdev_priv(dev);
	const struct fbnic_stat *stat;
	int i;

	fbnic_get_hw_stats(fbn->fbd);

	for (i = 0; i < FBNIC_HW_STATS_LEN; i++) {
		stat = &fbnic_gstrings_hw_stats[i];
		data[i] = *(u64 *)((u8 *)&fbn->fbd->hw_stats + stat->offset);
	}
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
	.get_strings		= fbnic_get_strings,
	.get_ethtool_stats	= fbnic_get_ethtool_stats,
	.get_sset_count		= fbnic_get_sset_count,
	.get_ts_info		= fbnic_get_ts_info,
	.get_ts_stats		= fbnic_get_ts_stats,
	.get_eth_mac_stats	= fbnic_get_eth_mac_stats,
};

void fbnic_set_ethtool_ops(struct net_device *dev)
{
	dev->ethtool_ops = &fbnic_ethtool_ops;
}
