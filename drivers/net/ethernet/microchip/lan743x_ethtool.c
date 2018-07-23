/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2018 Microchip Technology Inc. */

#include <linux/netdevice.h>
#include "lan743x_main.h"
#include "lan743x_ethtool.h"
#include <linux/pci.h>
#include <linux/phy.h>

static void lan743x_ethtool_get_drvinfo(struct net_device *netdev,
					struct ethtool_drvinfo *info)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	strlcpy(info->driver, DRIVER_NAME, sizeof(info->driver));
	strlcpy(info->bus_info,
		pci_name(adapter->pdev), sizeof(info->bus_info));
}

static u32 lan743x_ethtool_get_msglevel(struct net_device *netdev)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void lan743x_ethtool_set_msglevel(struct net_device *netdev,
					 u32 msglevel)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = msglevel;
}

static const char lan743x_set0_hw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX FCS Errors",
	"RX Alignment Errors",
	"Rx Fragment Errors",
	"RX Jabber Errors",
	"RX Undersize Frame Errors",
	"RX Oversize Frame Errors",
	"RX Dropped Frames",
	"RX Unicast Byte Count",
	"RX Broadcast Byte Count",
	"RX Multicast Byte Count",
	"RX Unicast Frames",
	"RX Broadcast Frames",
	"RX Multicast Frames",
	"RX Pause Frames",
	"RX 64 Byte Frames",
	"RX 65 - 127 Byte Frames",
	"RX 128 - 255 Byte Frames",
	"RX 256 - 511 Bytes Frames",
	"RX 512 - 1023 Byte Frames",
	"RX 1024 - 1518 Byte Frames",
	"RX Greater 1518 Byte Frames",
};

static const char lan743x_set1_sw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX Queue 0 Frames",
	"RX Queue 1 Frames",
	"RX Queue 2 Frames",
	"RX Queue 3 Frames",
};

static const char lan743x_set2_hw_cnt_strings[][ETH_GSTRING_LEN] = {
	"RX Total Frames",
	"EEE RX LPI Transitions",
	"EEE RX LPI Time",
	"RX Counter Rollover Status",
	"TX FCS Errors",
	"TX Excess Deferral Errors",
	"TX Carrier Errors",
	"TX Bad Byte Count",
	"TX Single Collisions",
	"TX Multiple Collisions",
	"TX Excessive Collision",
	"TX Late Collisions",
	"TX Unicast Byte Count",
	"TX Broadcast Byte Count",
	"TX Multicast Byte Count",
	"TX Unicast Frames",
	"TX Broadcast Frames",
	"TX Multicast Frames",
	"TX Pause Frames",
	"TX 64 Byte Frames",
	"TX 65 - 127 Byte Frames",
	"TX 128 - 255 Byte Frames",
	"TX 256 - 511 Bytes Frames",
	"TX 512 - 1023 Byte Frames",
	"TX 1024 - 1518 Byte Frames",
	"TX Greater 1518 Byte Frames",
	"TX Total Frames",
	"EEE TX LPI Transitions",
	"EEE TX LPI Time",
	"TX Counter Rollover Status",
};

static const u32 lan743x_set0_hw_cnt_addr[] = {
	STAT_RX_FCS_ERRORS,
	STAT_RX_ALIGNMENT_ERRORS,
	STAT_RX_FRAGMENT_ERRORS,
	STAT_RX_JABBER_ERRORS,
	STAT_RX_UNDERSIZE_FRAME_ERRORS,
	STAT_RX_OVERSIZE_FRAME_ERRORS,
	STAT_RX_DROPPED_FRAMES,
	STAT_RX_UNICAST_BYTE_COUNT,
	STAT_RX_BROADCAST_BYTE_COUNT,
	STAT_RX_MULTICAST_BYTE_COUNT,
	STAT_RX_UNICAST_FRAMES,
	STAT_RX_BROADCAST_FRAMES,
	STAT_RX_MULTICAST_FRAMES,
	STAT_RX_PAUSE_FRAMES,
	STAT_RX_64_BYTE_FRAMES,
	STAT_RX_65_127_BYTE_FRAMES,
	STAT_RX_128_255_BYTE_FRAMES,
	STAT_RX_256_511_BYTES_FRAMES,
	STAT_RX_512_1023_BYTE_FRAMES,
	STAT_RX_1024_1518_BYTE_FRAMES,
	STAT_RX_GREATER_1518_BYTE_FRAMES,
};

static const u32 lan743x_set2_hw_cnt_addr[] = {
	STAT_RX_TOTAL_FRAMES,
	STAT_EEE_RX_LPI_TRANSITIONS,
	STAT_EEE_RX_LPI_TIME,
	STAT_RX_COUNTER_ROLLOVER_STATUS,
	STAT_TX_FCS_ERRORS,
	STAT_TX_EXCESS_DEFERRAL_ERRORS,
	STAT_TX_CARRIER_ERRORS,
	STAT_TX_BAD_BYTE_COUNT,
	STAT_TX_SINGLE_COLLISIONS,
	STAT_TX_MULTIPLE_COLLISIONS,
	STAT_TX_EXCESSIVE_COLLISION,
	STAT_TX_LATE_COLLISIONS,
	STAT_TX_UNICAST_BYTE_COUNT,
	STAT_TX_BROADCAST_BYTE_COUNT,
	STAT_TX_MULTICAST_BYTE_COUNT,
	STAT_TX_UNICAST_FRAMES,
	STAT_TX_BROADCAST_FRAMES,
	STAT_TX_MULTICAST_FRAMES,
	STAT_TX_PAUSE_FRAMES,
	STAT_TX_64_BYTE_FRAMES,
	STAT_TX_65_127_BYTE_FRAMES,
	STAT_TX_128_255_BYTE_FRAMES,
	STAT_TX_256_511_BYTES_FRAMES,
	STAT_TX_512_1023_BYTE_FRAMES,
	STAT_TX_1024_1518_BYTE_FRAMES,
	STAT_TX_GREATER_1518_BYTE_FRAMES,
	STAT_TX_TOTAL_FRAMES,
	STAT_EEE_TX_LPI_TRANSITIONS,
	STAT_EEE_TX_LPI_TIME,
	STAT_TX_COUNTER_ROLLOVER_STATUS
};

static void lan743x_ethtool_get_strings(struct net_device *netdev,
					u32 stringset, u8 *data)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, lan743x_set0_hw_cnt_strings,
		       sizeof(lan743x_set0_hw_cnt_strings));
		memcpy(&data[sizeof(lan743x_set0_hw_cnt_strings)],
		       lan743x_set1_sw_cnt_strings,
		       sizeof(lan743x_set1_sw_cnt_strings));
		memcpy(&data[sizeof(lan743x_set0_hw_cnt_strings) +
		       sizeof(lan743x_set1_sw_cnt_strings)],
		       lan743x_set2_hw_cnt_strings,
		       sizeof(lan743x_set2_hw_cnt_strings));
		break;
	}
}

static void lan743x_ethtool_get_ethtool_stats(struct net_device *netdev,
					      struct ethtool_stats *stats,
					      u64 *data)
{
	struct lan743x_adapter *adapter = netdev_priv(netdev);
	int data_index = 0;
	u32 buf;
	int i;

	for (i = 0; i < ARRAY_SIZE(lan743x_set0_hw_cnt_addr); i++) {
		buf = lan743x_csr_read(adapter, lan743x_set0_hw_cnt_addr[i]);
		data[data_index++] = (u64)buf;
	}
	for (i = 0; i < ARRAY_SIZE(adapter->rx); i++)
		data[data_index++] = (u64)(adapter->rx[i].frame_count);
	for (i = 0; i < ARRAY_SIZE(lan743x_set2_hw_cnt_addr); i++) {
		buf = lan743x_csr_read(adapter, lan743x_set2_hw_cnt_addr[i]);
		data[data_index++] = (u64)buf;
	}
}

static int lan743x_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
	{
		int ret;

		ret = ARRAY_SIZE(lan743x_set0_hw_cnt_strings);
		ret += ARRAY_SIZE(lan743x_set1_sw_cnt_strings);
		ret += ARRAY_SIZE(lan743x_set2_hw_cnt_strings);
		return ret;
	}
	default:
		return -EOPNOTSUPP;
	}
}

const struct ethtool_ops lan743x_ethtool_ops = {
	.get_drvinfo = lan743x_ethtool_get_drvinfo,
	.get_msglevel = lan743x_ethtool_get_msglevel,
	.set_msglevel = lan743x_ethtool_set_msglevel,
	.get_link = ethtool_op_get_link,

	.get_strings = lan743x_ethtool_get_strings,
	.get_ethtool_stats = lan743x_ethtool_get_ethtool_stats,
	.get_sset_count = lan743x_ethtool_get_sset_count,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};
