/*
 * Copyright (C) 2015 Cavium, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

/* ETHTOOL Support for VNIC_VF Device*/

#include <linux/pci.h>

#include "nic_reg.h"
#include "nic.h"
#include "nicvf_queues.h"
#include "q_struct.h"
#include "thunder_bgx.h"

#define DRV_NAME	"thunder-nicvf"
#define DRV_VERSION     "1.0"

struct nicvf_stat {
	char name[ETH_GSTRING_LEN];
	unsigned int index;
};

#define NICVF_HW_STAT(stat) { \
	.name = #stat, \
	.index = offsetof(struct nicvf_hw_stats, stat) / sizeof(u64), \
}

#define NICVF_DRV_STAT(stat) { \
	.name = #stat, \
	.index = offsetof(struct nicvf_drv_stats, stat) / sizeof(u64), \
}

static const struct nicvf_stat nicvf_hw_stats[] = {
	NICVF_HW_STAT(rx_bytes_ok),
	NICVF_HW_STAT(rx_ucast_frames_ok),
	NICVF_HW_STAT(rx_bcast_frames_ok),
	NICVF_HW_STAT(rx_mcast_frames_ok),
	NICVF_HW_STAT(rx_fcs_errors),
	NICVF_HW_STAT(rx_l2_errors),
	NICVF_HW_STAT(rx_drop_red),
	NICVF_HW_STAT(rx_drop_red_bytes),
	NICVF_HW_STAT(rx_drop_overrun),
	NICVF_HW_STAT(rx_drop_overrun_bytes),
	NICVF_HW_STAT(rx_drop_bcast),
	NICVF_HW_STAT(rx_drop_mcast),
	NICVF_HW_STAT(rx_drop_l3_bcast),
	NICVF_HW_STAT(rx_drop_l3_mcast),
	NICVF_HW_STAT(tx_bytes_ok),
	NICVF_HW_STAT(tx_ucast_frames_ok),
	NICVF_HW_STAT(tx_bcast_frames_ok),
	NICVF_HW_STAT(tx_mcast_frames_ok),
};

static const struct nicvf_stat nicvf_drv_stats[] = {
	NICVF_DRV_STAT(rx_frames_ok),
	NICVF_DRV_STAT(rx_frames_64),
	NICVF_DRV_STAT(rx_frames_127),
	NICVF_DRV_STAT(rx_frames_255),
	NICVF_DRV_STAT(rx_frames_511),
	NICVF_DRV_STAT(rx_frames_1023),
	NICVF_DRV_STAT(rx_frames_1518),
	NICVF_DRV_STAT(rx_frames_jumbo),
	NICVF_DRV_STAT(rx_drops),
	NICVF_DRV_STAT(tx_frames_ok),
	NICVF_DRV_STAT(tx_busy),
	NICVF_DRV_STAT(tx_tso),
	NICVF_DRV_STAT(tx_drops),
};

static const struct nicvf_stat nicvf_queue_stats[] = {
	{ "bytes", 0 },
	{ "frames", 1 },
};

static const unsigned int nicvf_n_hw_stats = ARRAY_SIZE(nicvf_hw_stats);
static const unsigned int nicvf_n_drv_stats = ARRAY_SIZE(nicvf_drv_stats);
static const unsigned int nicvf_n_queue_stats = ARRAY_SIZE(nicvf_queue_stats);

static int nicvf_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *cmd)
{
	struct nicvf *nic = netdev_priv(netdev);

	cmd->supported = 0;
	cmd->transceiver = XCVR_EXTERNAL;
	if (nic->speed <= 1000) {
		cmd->port = PORT_MII;
		cmd->autoneg = AUTONEG_ENABLE;
	} else {
		cmd->port = PORT_FIBRE;
		cmd->autoneg = AUTONEG_DISABLE;
	}
	cmd->duplex = nic->duplex;
	ethtool_cmd_speed_set(cmd, nic->speed);

	return 0;
}

static void nicvf_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct nicvf *nic = netdev_priv(netdev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, pci_name(nic->pdev), sizeof(info->bus_info));
}

static u32 nicvf_get_msglevel(struct net_device *netdev)
{
	struct nicvf *nic = netdev_priv(netdev);

	return nic->msg_enable;
}

static void nicvf_set_msglevel(struct net_device *netdev, u32 lvl)
{
	struct nicvf *nic = netdev_priv(netdev);

	nic->msg_enable = lvl;
}

static void nicvf_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	int stats, qidx;

	if (sset != ETH_SS_STATS)
		return;

	for (stats = 0; stats < nicvf_n_hw_stats; stats++) {
		memcpy(data, nicvf_hw_stats[stats].name, ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}

	for (stats = 0; stats < nicvf_n_drv_stats; stats++) {
		memcpy(data, nicvf_drv_stats[stats].name, ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}

	for (qidx = 0; qidx < MAX_RCV_QUEUES_PER_QS; qidx++) {
		for (stats = 0; stats < nicvf_n_queue_stats; stats++) {
			sprintf(data, "rxq%d: %s", qidx,
				nicvf_queue_stats[stats].name);
			data += ETH_GSTRING_LEN;
		}
	}

	for (qidx = 0; qidx < MAX_SND_QUEUES_PER_QS; qidx++) {
		for (stats = 0; stats < nicvf_n_queue_stats; stats++) {
			sprintf(data, "txq%d: %s", qidx,
				nicvf_queue_stats[stats].name);
			data += ETH_GSTRING_LEN;
		}
	}

	for (stats = 0; stats < BGX_RX_STATS_COUNT; stats++) {
		sprintf(data, "bgx_rxstat%d: ", stats);
		data += ETH_GSTRING_LEN;
	}

	for (stats = 0; stats < BGX_TX_STATS_COUNT; stats++) {
		sprintf(data, "bgx_txstat%d: ", stats);
		data += ETH_GSTRING_LEN;
	}
}

static int nicvf_get_sset_count(struct net_device *netdev, int sset)
{
	if (sset != ETH_SS_STATS)
		return -EINVAL;

	return nicvf_n_hw_stats + nicvf_n_drv_stats +
		(nicvf_n_queue_stats *
		 (MAX_RCV_QUEUES_PER_QS + MAX_SND_QUEUES_PER_QS)) +
		BGX_RX_STATS_COUNT + BGX_TX_STATS_COUNT;
}

static void nicvf_get_ethtool_stats(struct net_device *netdev,
				    struct ethtool_stats *stats, u64 *data)
{
	struct nicvf *nic = netdev_priv(netdev);
	int stat, qidx;

	nicvf_update_stats(nic);

	/* Update LMAC stats */
	nicvf_update_lmac_stats(nic);

	for (stat = 0; stat < nicvf_n_hw_stats; stat++)
		*(data++) = ((u64 *)&nic->stats)
				[nicvf_hw_stats[stat].index];
	for (stat = 0; stat < nicvf_n_drv_stats; stat++)
		*(data++) = ((u64 *)&nic->drv_stats)
				[nicvf_drv_stats[stat].index];

	for (qidx = 0; qidx < MAX_RCV_QUEUES_PER_QS; qidx++) {
		for (stat = 0; stat < nicvf_n_queue_stats; stat++)
			*(data++) = ((u64 *)&nic->qs->rq[qidx].stats)
					[nicvf_queue_stats[stat].index];
	}

	for (qidx = 0; qidx < MAX_SND_QUEUES_PER_QS; qidx++) {
		for (stat = 0; stat < nicvf_n_queue_stats; stat++)
			*(data++) = ((u64 *)&nic->qs->sq[qidx].stats)
					[nicvf_queue_stats[stat].index];
	}

	for (stat = 0; stat < BGX_RX_STATS_COUNT; stat++)
		*(data++) = nic->bgx_stats.rx_stats[stat];
	for (stat = 0; stat < BGX_TX_STATS_COUNT; stat++)
		*(data++) = nic->bgx_stats.tx_stats[stat];
}

static int nicvf_get_regs_len(struct net_device *dev)
{
	return sizeof(u64) * NIC_VF_REG_COUNT;
}

static void nicvf_get_regs(struct net_device *dev,
			   struct ethtool_regs *regs, void *reg)
{
	struct nicvf *nic = netdev_priv(dev);
	u64 *p = (u64 *)reg;
	u64 reg_offset;
	int mbox, key, stat, q;
	int i = 0;

	regs->version = 0;
	memset(p, 0, NIC_VF_REG_COUNT);

	p[i++] = nicvf_reg_read(nic, NIC_VNIC_CFG);
	/* Mailbox registers */
	for (mbox = 0; mbox < NIC_PF_VF_MAILBOX_SIZE; mbox++)
		p[i++] = nicvf_reg_read(nic,
					NIC_VF_PF_MAILBOX_0_1 | (mbox << 3));

	p[i++] = nicvf_reg_read(nic, NIC_VF_INT);
	p[i++] = nicvf_reg_read(nic, NIC_VF_INT_W1S);
	p[i++] = nicvf_reg_read(nic, NIC_VF_ENA_W1C);
	p[i++] = nicvf_reg_read(nic, NIC_VF_ENA_W1S);
	p[i++] = nicvf_reg_read(nic, NIC_VNIC_RSS_CFG);

	for (key = 0; key < RSS_HASH_KEY_SIZE; key++)
		p[i++] = nicvf_reg_read(nic, NIC_VNIC_RSS_KEY_0_4 | (key << 3));

	/* Tx/Rx statistics */
	for (stat = 0; stat < TX_STATS_ENUM_LAST; stat++)
		p[i++] = nicvf_reg_read(nic,
					NIC_VNIC_TX_STAT_0_4 | (stat << 3));

	for (i = 0; i < RX_STATS_ENUM_LAST; i++)
		p[i++] = nicvf_reg_read(nic,
					NIC_VNIC_RX_STAT_0_13 | (stat << 3));

	p[i++] = nicvf_reg_read(nic, NIC_QSET_RQ_GEN_CFG);

	/* All completion queue's registers */
	for (q = 0; q < MAX_CMP_QUEUES_PER_QS; q++) {
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_CFG, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_CFG2, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_THRESH, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_BASE, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_HEAD, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_TAIL, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_DOOR, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_STATUS, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_STATUS2, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_CQ_0_7_DEBUG, q);
	}

	/* All receive queue's registers */
	for (q = 0; q < MAX_RCV_QUEUES_PER_QS; q++) {
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_RQ_0_7_CFG, q);
		p[i++] = nicvf_queue_reg_read(nic,
						  NIC_QSET_RQ_0_7_STAT_0_1, q);
		reg_offset = NIC_QSET_RQ_0_7_STAT_0_1 | (1 << 3);
		p[i++] = nicvf_queue_reg_read(nic, reg_offset, q);
	}

	for (q = 0; q < MAX_SND_QUEUES_PER_QS; q++) {
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_CFG, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_THRESH, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_BASE, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_HEAD, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_TAIL, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_DOOR, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_STATUS, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_DEBUG, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_CNM_CHG, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_SQ_0_7_STAT_0_1, q);
		reg_offset = NIC_QSET_SQ_0_7_STAT_0_1 | (1 << 3);
		p[i++] = nicvf_queue_reg_read(nic, reg_offset, q);
	}

	for (q = 0; q < MAX_RCV_BUF_DESC_RINGS_PER_QS; q++) {
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_CFG, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_THRESH, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_BASE, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_HEAD, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_TAIL, q);
		p[i++] = nicvf_queue_reg_read(nic, NIC_QSET_RBDR_0_1_DOOR, q);
		p[i++] = nicvf_queue_reg_read(nic,
					      NIC_QSET_RBDR_0_1_STATUS0, q);
		p[i++] = nicvf_queue_reg_read(nic,
					      NIC_QSET_RBDR_0_1_STATUS1, q);
		reg_offset = NIC_QSET_RBDR_0_1_PREFETCH_STATUS;
		p[i++] = nicvf_queue_reg_read(nic, reg_offset, q);
	}
}

static int nicvf_get_coalesce(struct net_device *netdev,
			      struct ethtool_coalesce *cmd)
{
	struct nicvf *nic = netdev_priv(netdev);

	cmd->rx_coalesce_usecs = nic->cq_coalesce_usecs;
	return 0;
}

static void nicvf_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct nicvf *nic = netdev_priv(netdev);
	struct queue_set *qs = nic->qs;

	ring->rx_max_pending = MAX_RCV_BUF_COUNT;
	ring->rx_pending = qs->rbdr_len;
	ring->tx_max_pending = MAX_SND_QUEUE_LEN;
	ring->tx_pending = qs->sq_len;
}

static int nicvf_get_rss_hash_opts(struct nicvf *nic,
				   struct ethtool_rxnfc *info)
{
	info->data = 0;

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
	case SCTP_V4_FLOW:
	case SCTP_V6_FLOW:
		info->data |= RXH_L4_B_0_1 | RXH_L4_B_2_3;
	case IPV4_FLOW:
	case IPV6_FLOW:
		info->data |= RXH_IP_SRC | RXH_IP_DST;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nicvf_get_rxnfc(struct net_device *dev,
			   struct ethtool_rxnfc *info, u32 *rules)
{
	struct nicvf *nic = netdev_priv(dev);
	int ret = -EOPNOTSUPP;

	switch (info->cmd) {
	case ETHTOOL_GRXRINGS:
		info->data = nic->qs->rq_cnt;
		ret = 0;
		break;
	case ETHTOOL_GRXFH:
		return nicvf_get_rss_hash_opts(nic, info);
	default:
		break;
	}
	return ret;
}

static int nicvf_set_rss_hash_opts(struct nicvf *nic,
				   struct ethtool_rxnfc *info)
{
	struct nicvf_rss_info *rss = &nic->rss_info;
	u64 rss_cfg = nicvf_reg_read(nic, NIC_VNIC_RSS_CFG);

	if (!rss->enable)
		netdev_err(nic->netdev,
			   "RSS is disabled, hash cannot be set\n");

	netdev_info(nic->netdev, "Set RSS flow type = %d, data = %lld\n",
		    info->flow_type, info->data);

	if (!(info->data & RXH_IP_SRC) || !(info->data & RXH_IP_DST))
		return -EINVAL;

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case TCP_V6_FLOW:
		switch (info->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			rss_cfg &= ~(1ULL << RSS_HASH_TCP);
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			rss_cfg |= (1ULL << RSS_HASH_TCP);
			break;
		default:
			return -EINVAL;
		}
		break;
	case UDP_V4_FLOW:
	case UDP_V6_FLOW:
		switch (info->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			rss_cfg &= ~(1ULL << RSS_HASH_UDP);
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			rss_cfg |= (1ULL << RSS_HASH_UDP);
			break;
		default:
			return -EINVAL;
		}
		break;
	case SCTP_V4_FLOW:
	case SCTP_V6_FLOW:
		switch (info->data & (RXH_L4_B_0_1 | RXH_L4_B_2_3)) {
		case 0:
			rss_cfg &= ~(1ULL << RSS_HASH_L4ETC);
			break;
		case (RXH_L4_B_0_1 | RXH_L4_B_2_3):
			rss_cfg |= (1ULL << RSS_HASH_L4ETC);
			break;
		default:
			return -EINVAL;
		}
		break;
	case IPV4_FLOW:
	case IPV6_FLOW:
		rss_cfg = RSS_HASH_IP;
		break;
	default:
		return -EINVAL;
	}

	nicvf_reg_write(nic, NIC_VNIC_RSS_CFG, rss_cfg);
	return 0;
}

static int nicvf_set_rxnfc(struct net_device *dev, struct ethtool_rxnfc *info)
{
	struct nicvf *nic = netdev_priv(dev);

	switch (info->cmd) {
	case ETHTOOL_SRXFH:
		return nicvf_set_rss_hash_opts(nic, info);
	default:
		break;
	}
	return -EOPNOTSUPP;
}

static u32 nicvf_get_rxfh_key_size(struct net_device *netdev)
{
	return RSS_HASH_KEY_SIZE * sizeof(u64);
}

static u32 nicvf_get_rxfh_indir_size(struct net_device *dev)
{
	struct nicvf *nic = netdev_priv(dev);

	return nic->rss_info.rss_size;
}

static int nicvf_get_rxfh(struct net_device *dev, u32 *indir, u8 *hkey,
			  u8 *hfunc)
{
	struct nicvf *nic = netdev_priv(dev);
	struct nicvf_rss_info *rss = &nic->rss_info;
	int idx;

	if (indir) {
		for (idx = 0; idx < rss->rss_size; idx++)
			indir[idx] = rss->ind_tbl[idx];
	}

	if (hkey)
		memcpy(hkey, rss->key, RSS_HASH_KEY_SIZE * sizeof(u64));

	if (hfunc)
		*hfunc = ETH_RSS_HASH_TOP;

	return 0;
}

static int nicvf_set_rxfh(struct net_device *dev, const u32 *indir,
			  const u8 *hkey, u8 hfunc)
{
	struct nicvf *nic = netdev_priv(dev);
	struct nicvf_rss_info *rss = &nic->rss_info;
	int idx;

	if ((nic->qs->rq_cnt <= 1) || (nic->cpi_alg != CPI_ALG_NONE)) {
		rss->enable = false;
		rss->hash_bits = 0;
		return -EIO;
	}

	/* We do not allow change in unsupported parameters */
	if (hfunc != ETH_RSS_HASH_NO_CHANGE && hfunc != ETH_RSS_HASH_TOP)
		return -EOPNOTSUPP;

	rss->enable = true;
	if (indir) {
		for (idx = 0; idx < rss->rss_size; idx++)
			rss->ind_tbl[idx] = indir[idx];
	}

	if (hkey) {
		memcpy(rss->key, hkey, RSS_HASH_KEY_SIZE * sizeof(u64));
		nicvf_set_rss_key(nic);
	}

	nicvf_config_rss(nic);
	return 0;
}

/* Get no of queues device supports and current queue count */
static void nicvf_get_channels(struct net_device *dev,
			       struct ethtool_channels *channel)
{
	struct nicvf *nic = netdev_priv(dev);

	memset(channel, 0, sizeof(*channel));

	channel->max_rx = MAX_RCV_QUEUES_PER_QS;
	channel->max_tx = MAX_SND_QUEUES_PER_QS;

	channel->rx_count = nic->qs->rq_cnt;
	channel->tx_count = nic->qs->sq_cnt;
}

/* Set no of Tx, Rx queues to be used */
static int nicvf_set_channels(struct net_device *dev,
			      struct ethtool_channels *channel)
{
	struct nicvf *nic = netdev_priv(dev);
	int err = 0;

	if (!channel->rx_count || !channel->tx_count)
		return -EINVAL;
	if (channel->rx_count > MAX_RCV_QUEUES_PER_QS)
		return -EINVAL;
	if (channel->tx_count > MAX_SND_QUEUES_PER_QS)
		return -EINVAL;

	nic->qs->rq_cnt = channel->rx_count;
	nic->qs->sq_cnt = channel->tx_count;
	nic->qs->cq_cnt = max(nic->qs->rq_cnt, nic->qs->sq_cnt);

	err = nicvf_set_real_num_queues(dev, nic->qs->sq_cnt, nic->qs->rq_cnt);
	if (err)
		return err;

	if (!netif_running(dev))
		return err;

	nicvf_stop(dev);
	nicvf_open(dev);
	netdev_info(dev, "Setting num Tx rings to %d, Rx rings to %d success\n",
		    nic->qs->sq_cnt, nic->qs->rq_cnt);

	return err;
}

static const struct ethtool_ops nicvf_ethtool_ops = {
	.get_settings		= nicvf_get_settings,
	.get_link		= ethtool_op_get_link,
	.get_drvinfo		= nicvf_get_drvinfo,
	.get_msglevel		= nicvf_get_msglevel,
	.set_msglevel		= nicvf_set_msglevel,
	.get_strings		= nicvf_get_strings,
	.get_sset_count		= nicvf_get_sset_count,
	.get_ethtool_stats	= nicvf_get_ethtool_stats,
	.get_regs_len		= nicvf_get_regs_len,
	.get_regs		= nicvf_get_regs,
	.get_coalesce		= nicvf_get_coalesce,
	.get_ringparam		= nicvf_get_ringparam,
	.get_rxnfc		= nicvf_get_rxnfc,
	.set_rxnfc		= nicvf_set_rxnfc,
	.get_rxfh_key_size	= nicvf_get_rxfh_key_size,
	.get_rxfh_indir_size	= nicvf_get_rxfh_indir_size,
	.get_rxfh		= nicvf_get_rxfh,
	.set_rxfh		= nicvf_set_rxfh,
	.get_channels		= nicvf_get_channels,
	.set_channels		= nicvf_set_channels,
	.get_ts_info		= ethtool_op_get_ts_info,
};

void nicvf_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &nicvf_ethtool_ops;
}
