// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Gerhard Engleder <gerhard@engleder-embedded.com> */

#include "tsnep.h"

static const char tsnep_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_packets",
	"rx_bytes",
	"rx_dropped",
	"rx_multicast",
	"rx_phy_errors",
	"rx_forwarded_phy_errors",
	"rx_invalid_frame_errors",
	"tx_packets",
	"tx_bytes",
	"tx_dropped",
};

struct tsnep_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_dropped;
	u64 rx_multicast;
	u64 rx_phy_errors;
	u64 rx_forwarded_phy_errors;
	u64 rx_invalid_frame_errors;
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_dropped;
};

#define TSNEP_STATS_COUNT (sizeof(struct tsnep_stats) / sizeof(u64))

static const char tsnep_rx_queue_stats_strings[][ETH_GSTRING_LEN] = {
	"rx_%d_packets",
	"rx_%d_bytes",
	"rx_%d_dropped",
	"rx_%d_multicast",
	"rx_%d_no_descriptor_errors",
	"rx_%d_buffer_too_small_errors",
	"rx_%d_fifo_overflow_errors",
	"rx_%d_invalid_frame_errors",
};

struct tsnep_rx_queue_stats {
	u64 rx_packets;
	u64 rx_bytes;
	u64 rx_dropped;
	u64 rx_multicast;
	u64 rx_no_descriptor_errors;
	u64 rx_buffer_too_small_errors;
	u64 rx_fifo_overflow_errors;
	u64 rx_invalid_frame_errors;
};

#define TSNEP_RX_QUEUE_STATS_COUNT (sizeof(struct tsnep_rx_queue_stats) / \
				    sizeof(u64))

static const char tsnep_tx_queue_stats_strings[][ETH_GSTRING_LEN] = {
	"tx_%d_packets",
	"tx_%d_bytes",
	"tx_%d_dropped",
};

struct tsnep_tx_queue_stats {
	u64 tx_packets;
	u64 tx_bytes;
	u64 tx_dropped;
};

#define TSNEP_TX_QUEUE_STATS_COUNT (sizeof(struct tsnep_tx_queue_stats) / \
				    sizeof(u64))

static void tsnep_ethtool_get_drvinfo(struct net_device *netdev,
				      struct ethtool_drvinfo *drvinfo)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);

	strscpy(drvinfo->driver, TSNEP, sizeof(drvinfo->driver));
	strscpy(drvinfo->bus_info, dev_name(&adapter->pdev->dev),
		sizeof(drvinfo->bus_info));
}

static int tsnep_ethtool_get_regs_len(struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	int len;
	int num_additional_queues;

	len = TSNEP_MAC_SIZE;

	/* first queue pair is within TSNEP_MAC_SIZE, only queues additional to
	 * the first queue pair extend the register length by TSNEP_QUEUE_SIZE
	 */
	num_additional_queues =
		max(adapter->num_tx_queues, adapter->num_rx_queues) - 1;
	len += TSNEP_QUEUE_SIZE * num_additional_queues;

	return len;
}

static void tsnep_ethtool_get_regs(struct net_device *netdev,
				   struct ethtool_regs *regs,
				   void *p)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);

	regs->version = 1;

	memcpy_fromio(p, adapter->addr, regs->len);
}

static u32 tsnep_ethtool_get_msglevel(struct net_device *netdev)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);

	return adapter->msg_enable;
}

static void tsnep_ethtool_set_msglevel(struct net_device *netdev, u32 data)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);

	adapter->msg_enable = data;
}

static void tsnep_ethtool_get_strings(struct net_device *netdev, u32 stringset,
				      u8 *data)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	int rx_count = adapter->num_rx_queues;
	int tx_count = adapter->num_tx_queues;
	int i, j;

	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(data, tsnep_stats_strings, sizeof(tsnep_stats_strings));
		data += sizeof(tsnep_stats_strings);

		for (i = 0; i < rx_count; i++) {
			for (j = 0; j < TSNEP_RX_QUEUE_STATS_COUNT; j++) {
				snprintf(data, ETH_GSTRING_LEN,
					 tsnep_rx_queue_stats_strings[j], i);
				data += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < tx_count; i++) {
			for (j = 0; j < TSNEP_TX_QUEUE_STATS_COUNT; j++) {
				snprintf(data, ETH_GSTRING_LEN,
					 tsnep_tx_queue_stats_strings[j], i);
				data += ETH_GSTRING_LEN;
			}
		}
		break;
	case ETH_SS_TEST:
		tsnep_ethtool_get_test_strings(data);
		break;
	}
}

static void tsnep_ethtool_get_ethtool_stats(struct net_device *netdev,
					    struct ethtool_stats *stats,
					    u64 *data)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	int rx_count = adapter->num_rx_queues;
	int tx_count = adapter->num_tx_queues;
	struct tsnep_stats tsnep_stats;
	struct tsnep_rx_queue_stats tsnep_rx_queue_stats;
	struct tsnep_tx_queue_stats tsnep_tx_queue_stats;
	u32 reg;
	int i;

	memset(&tsnep_stats, 0, sizeof(tsnep_stats));
	for (i = 0; i < adapter->num_rx_queues; i++) {
		tsnep_stats.rx_packets += adapter->rx[i].packets;
		tsnep_stats.rx_bytes += adapter->rx[i].bytes;
		tsnep_stats.rx_dropped += adapter->rx[i].dropped;
		tsnep_stats.rx_multicast += adapter->rx[i].multicast;
	}
	reg = ioread32(adapter->addr + ECM_STAT);
	tsnep_stats.rx_phy_errors =
		(reg & ECM_STAT_RX_ERR_MASK) >> ECM_STAT_RX_ERR_SHIFT;
	tsnep_stats.rx_forwarded_phy_errors =
		(reg & ECM_STAT_FWD_RX_ERR_MASK) >> ECM_STAT_FWD_RX_ERR_SHIFT;
	tsnep_stats.rx_invalid_frame_errors =
		(reg & ECM_STAT_INV_FRM_MASK) >> ECM_STAT_INV_FRM_SHIFT;
	for (i = 0; i < adapter->num_tx_queues; i++) {
		tsnep_stats.tx_packets += adapter->tx[i].packets;
		tsnep_stats.tx_bytes += adapter->tx[i].bytes;
		tsnep_stats.tx_dropped += adapter->tx[i].dropped;
	}
	memcpy(data, &tsnep_stats, sizeof(tsnep_stats));
	data += TSNEP_STATS_COUNT;

	for (i = 0; i < rx_count; i++) {
		memset(&tsnep_rx_queue_stats, 0, sizeof(tsnep_rx_queue_stats));
		tsnep_rx_queue_stats.rx_packets = adapter->rx[i].packets;
		tsnep_rx_queue_stats.rx_bytes = adapter->rx[i].bytes;
		tsnep_rx_queue_stats.rx_dropped = adapter->rx[i].dropped;
		tsnep_rx_queue_stats.rx_multicast = adapter->rx[i].multicast;
		reg = ioread32(adapter->addr + TSNEP_QUEUE(i) +
			       TSNEP_RX_STATISTIC);
		tsnep_rx_queue_stats.rx_no_descriptor_errors =
			(reg & TSNEP_RX_STATISTIC_NO_DESC_MASK) >>
			TSNEP_RX_STATISTIC_NO_DESC_SHIFT;
		tsnep_rx_queue_stats.rx_buffer_too_small_errors =
			(reg & TSNEP_RX_STATISTIC_BUFFER_TOO_SMALL_MASK) >>
			TSNEP_RX_STATISTIC_BUFFER_TOO_SMALL_SHIFT;
		tsnep_rx_queue_stats.rx_fifo_overflow_errors =
			(reg & TSNEP_RX_STATISTIC_FIFO_OVERFLOW_MASK) >>
			TSNEP_RX_STATISTIC_FIFO_OVERFLOW_SHIFT;
		tsnep_rx_queue_stats.rx_invalid_frame_errors =
			(reg & TSNEP_RX_STATISTIC_INVALID_FRAME_MASK) >>
			TSNEP_RX_STATISTIC_INVALID_FRAME_SHIFT;
		memcpy(data, &tsnep_rx_queue_stats,
		       sizeof(tsnep_rx_queue_stats));
		data += TSNEP_RX_QUEUE_STATS_COUNT;
	}

	for (i = 0; i < tx_count; i++) {
		memset(&tsnep_tx_queue_stats, 0, sizeof(tsnep_tx_queue_stats));
		tsnep_tx_queue_stats.tx_packets += adapter->tx[i].packets;
		tsnep_tx_queue_stats.tx_bytes += adapter->tx[i].bytes;
		tsnep_tx_queue_stats.tx_dropped += adapter->tx[i].dropped;
		memcpy(data, &tsnep_tx_queue_stats,
		       sizeof(tsnep_tx_queue_stats));
		data += TSNEP_TX_QUEUE_STATS_COUNT;
	}
}

static int tsnep_ethtool_get_sset_count(struct net_device *netdev, int sset)
{
	struct tsnep_adapter *adapter = netdev_priv(netdev);
	int rx_count;
	int tx_count;

	switch (sset) {
	case ETH_SS_STATS:
		rx_count = adapter->num_rx_queues;
		tx_count = adapter->num_tx_queues;
		return TSNEP_STATS_COUNT +
		       TSNEP_RX_QUEUE_STATS_COUNT * rx_count +
		       TSNEP_TX_QUEUE_STATS_COUNT * tx_count;
	case ETH_SS_TEST:
		return tsnep_ethtool_get_test_count();
	default:
		return -EOPNOTSUPP;
	}
}

static int tsnep_ethtool_get_ts_info(struct net_device *dev,
				     struct ethtool_ts_info *info)
{
	struct tsnep_adapter *adapter = netdev_priv(dev);

	info->so_timestamping = SOF_TIMESTAMPING_TX_SOFTWARE |
				SOF_TIMESTAMPING_RX_SOFTWARE |
				SOF_TIMESTAMPING_SOFTWARE |
				SOF_TIMESTAMPING_TX_HARDWARE |
				SOF_TIMESTAMPING_RX_HARDWARE |
				SOF_TIMESTAMPING_RAW_HARDWARE;

	if (adapter->ptp_clock)
		info->phc_index = ptp_clock_index(adapter->ptp_clock);
	else
		info->phc_index = -1;

	info->tx_types = BIT(HWTSTAMP_TX_OFF) |
			 BIT(HWTSTAMP_TX_ON);
	info->rx_filters = BIT(HWTSTAMP_FILTER_NONE) |
			   BIT(HWTSTAMP_FILTER_ALL);

	return 0;
}

const struct ethtool_ops tsnep_ethtool_ops = {
	.get_drvinfo = tsnep_ethtool_get_drvinfo,
	.get_regs_len = tsnep_ethtool_get_regs_len,
	.get_regs = tsnep_ethtool_get_regs,
	.get_msglevel = tsnep_ethtool_get_msglevel,
	.set_msglevel = tsnep_ethtool_set_msglevel,
	.nway_reset = phy_ethtool_nway_reset,
	.get_link = ethtool_op_get_link,
	.self_test = tsnep_ethtool_self_test,
	.get_strings = tsnep_ethtool_get_strings,
	.get_ethtool_stats = tsnep_ethtool_get_ethtool_stats,
	.get_sset_count = tsnep_ethtool_get_sset_count,
	.get_ts_info = tsnep_ethtool_get_ts_info,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};
