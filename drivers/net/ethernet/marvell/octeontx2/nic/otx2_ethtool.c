// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/stddef.h>
#include <linux/etherdevice.h>
#include <linux/log2.h>

#include "otx2_common.h"

#define DRV_NAME	"octeontx2-nicpf"

struct otx2_stat {
	char name[ETH_GSTRING_LEN];
	unsigned int index;
};

/* HW device stats */
#define OTX2_DEV_STAT(stat) { \
	.name = #stat, \
	.index = offsetof(struct otx2_dev_stats, stat) / sizeof(u64), \
}

static const struct otx2_stat otx2_dev_stats[] = {
	OTX2_DEV_STAT(rx_ucast_frames),
	OTX2_DEV_STAT(rx_bcast_frames),
	OTX2_DEV_STAT(rx_mcast_frames),

	OTX2_DEV_STAT(tx_ucast_frames),
	OTX2_DEV_STAT(tx_bcast_frames),
	OTX2_DEV_STAT(tx_mcast_frames),
};

/* Driver level stats */
#define OTX2_DRV_STAT(stat) { \
	.name = #stat, \
	.index = offsetof(struct otx2_drv_stats, stat) / sizeof(atomic_t), \
}

static const struct otx2_stat otx2_drv_stats[] = {
	OTX2_DRV_STAT(rx_fcs_errs),
	OTX2_DRV_STAT(rx_oversize_errs),
	OTX2_DRV_STAT(rx_undersize_errs),
	OTX2_DRV_STAT(rx_csum_errs),
	OTX2_DRV_STAT(rx_len_errs),
	OTX2_DRV_STAT(rx_other_errs),
};

static const struct otx2_stat otx2_queue_stats[] = {
	{ "bytes", 0 },
	{ "frames", 1 },
};

static const unsigned int otx2_n_dev_stats = ARRAY_SIZE(otx2_dev_stats);
static const unsigned int otx2_n_drv_stats = ARRAY_SIZE(otx2_drv_stats);
static const unsigned int otx2_n_queue_stats = ARRAY_SIZE(otx2_queue_stats);

static void otx2_dev_open(struct net_device *netdev)
{
	otx2_open(netdev);
}

static void otx2_dev_stop(struct net_device *netdev)
{
	otx2_stop(netdev);
}

static void otx2_get_drvinfo(struct net_device *netdev,
			     struct ethtool_drvinfo *info)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->bus_info, pci_name(pfvf->pdev), sizeof(info->bus_info));
}

static void otx2_get_qset_strings(struct otx2_nic *pfvf, u8 **data, int qset)
{
	int start_qidx = qset * pfvf->hw.rx_queues;
	int qidx, stats;

	for (qidx = 0; qidx < pfvf->hw.rx_queues; qidx++) {
		for (stats = 0; stats < otx2_n_queue_stats; stats++) {
			sprintf(*data, "rxq%d: %s", qidx + start_qidx,
				otx2_queue_stats[stats].name);
			*data += ETH_GSTRING_LEN;
		}
	}
	for (qidx = 0; qidx < pfvf->hw.tx_queues; qidx++) {
		for (stats = 0; stats < otx2_n_queue_stats; stats++) {
			sprintf(*data, "txq%d: %s", qidx + start_qidx,
				otx2_queue_stats[stats].name);
			*data += ETH_GSTRING_LEN;
		}
	}
}

static void otx2_get_strings(struct net_device *netdev, u32 sset, u8 *data)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	int stats;

	if (sset != ETH_SS_STATS)
		return;

	for (stats = 0; stats < otx2_n_dev_stats; stats++) {
		memcpy(data, otx2_dev_stats[stats].name, ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}

	for (stats = 0; stats < otx2_n_drv_stats; stats++) {
		memcpy(data, otx2_drv_stats[stats].name, ETH_GSTRING_LEN);
		data += ETH_GSTRING_LEN;
	}

	otx2_get_qset_strings(pfvf, &data, 0);

	for (stats = 0; stats < CGX_RX_STATS_COUNT; stats++) {
		sprintf(data, "cgx_rxstat%d: ", stats);
		data += ETH_GSTRING_LEN;
	}

	for (stats = 0; stats < CGX_TX_STATS_COUNT; stats++) {
		sprintf(data, "cgx_txstat%d: ", stats);
		data += ETH_GSTRING_LEN;
	}

	strcpy(data, "reset_count");
	data += ETH_GSTRING_LEN;
}

static void otx2_get_qset_stats(struct otx2_nic *pfvf,
				struct ethtool_stats *stats, u64 **data)
{
	int stat, qidx;

	if (!pfvf)
		return;
	for (qidx = 0; qidx < pfvf->hw.rx_queues; qidx++) {
		if (!otx2_update_rq_stats(pfvf, qidx)) {
			for (stat = 0; stat < otx2_n_queue_stats; stat++)
				*((*data)++) = 0;
			continue;
		}
		for (stat = 0; stat < otx2_n_queue_stats; stat++)
			*((*data)++) = ((u64 *)&pfvf->qset.rq[qidx].stats)
				[otx2_queue_stats[stat].index];
	}

	for (qidx = 0; qidx < pfvf->hw.tx_queues; qidx++) {
		if (!otx2_update_sq_stats(pfvf, qidx)) {
			for (stat = 0; stat < otx2_n_queue_stats; stat++)
				*((*data)++) = 0;
			continue;
		}
		for (stat = 0; stat < otx2_n_queue_stats; stat++)
			*((*data)++) = ((u64 *)&pfvf->qset.sq[qidx].stats)
				[otx2_queue_stats[stat].index];
	}
}

/* Get device and per queue statistics */
static void otx2_get_ethtool_stats(struct net_device *netdev,
				   struct ethtool_stats *stats, u64 *data)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	int stat;

	otx2_get_dev_stats(pfvf);
	for (stat = 0; stat < otx2_n_dev_stats; stat++)
		*(data++) = ((u64 *)&pfvf->hw.dev_stats)
				[otx2_dev_stats[stat].index];

	for (stat = 0; stat < otx2_n_drv_stats; stat++)
		*(data++) = atomic_read(&((atomic_t *)&pfvf->hw.drv_stats)
						[otx2_drv_stats[stat].index]);

	otx2_get_qset_stats(pfvf, stats, &data);
	otx2_update_lmac_stats(pfvf);
	for (stat = 0; stat < CGX_RX_STATS_COUNT; stat++)
		*(data++) = pfvf->hw.cgx_rx_stats[stat];
	for (stat = 0; stat < CGX_TX_STATS_COUNT; stat++)
		*(data++) = pfvf->hw.cgx_tx_stats[stat];
	*(data++) = pfvf->reset_count;
}

static int otx2_get_sset_count(struct net_device *netdev, int sset)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	int qstats_count;

	if (sset != ETH_SS_STATS)
		return -EINVAL;

	qstats_count = otx2_n_queue_stats *
		       (pfvf->hw.rx_queues + pfvf->hw.tx_queues);

	return otx2_n_dev_stats + otx2_n_drv_stats + qstats_count +
		CGX_RX_STATS_COUNT + CGX_TX_STATS_COUNT + 1;
}

/* Get no of queues device supports and current queue count */
static void otx2_get_channels(struct net_device *dev,
			      struct ethtool_channels *channel)
{
	struct otx2_nic *pfvf = netdev_priv(dev);

	channel->max_rx = pfvf->hw.max_queues;
	channel->max_tx = pfvf->hw.max_queues;

	channel->rx_count = pfvf->hw.rx_queues;
	channel->tx_count = pfvf->hw.tx_queues;
}

/* Set no of Tx, Rx queues to be used */
static int otx2_set_channels(struct net_device *dev,
			     struct ethtool_channels *channel)
{
	struct otx2_nic *pfvf = netdev_priv(dev);
	bool if_up = netif_running(dev);
	int err = 0;

	if (!channel->rx_count || !channel->tx_count)
		return -EINVAL;

	if (if_up)
		otx2_dev_stop(dev);

	err = otx2_set_real_num_queues(dev, channel->tx_count,
				       channel->rx_count);
	if (err)
		goto fail;

	pfvf->hw.rx_queues = channel->rx_count;
	pfvf->hw.tx_queues = channel->tx_count;
	pfvf->qset.cq_cnt = pfvf->hw.tx_queues +  pfvf->hw.rx_queues;

fail:
	if (if_up)
		otx2_dev_open(dev);

	netdev_info(dev, "Setting num Tx rings to %d, Rx rings to %d success\n",
		    pfvf->hw.tx_queues, pfvf->hw.rx_queues);

	return err;
}

static void otx2_get_ringparam(struct net_device *netdev,
			       struct ethtool_ringparam *ring)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	struct otx2_qset *qs = &pfvf->qset;

	ring->rx_max_pending = Q_COUNT(Q_SIZE_MAX);
	ring->rx_pending = qs->rqe_cnt ? qs->rqe_cnt : Q_COUNT(Q_SIZE_256);
	ring->tx_max_pending = Q_COUNT(Q_SIZE_MAX);
	ring->tx_pending = qs->sqe_cnt ? qs->sqe_cnt : Q_COUNT(Q_SIZE_4K);
}

static int otx2_set_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *ring)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	bool if_up = netif_running(netdev);
	struct otx2_qset *qs = &pfvf->qset;
	u32 rx_count, tx_count;

	if (ring->rx_mini_pending || ring->rx_jumbo_pending)
		return -EINVAL;

	/* Permitted lengths are 16 64 256 1K 4K 16K 64K 256K 1M  */
	rx_count = ring->rx_pending;
	/* On some silicon variants a skid or reserved CQEs are
	 * needed to avoid CQ overflow.
	 */
	if (rx_count < pfvf->hw.rq_skid)
		rx_count =  pfvf->hw.rq_skid;
	rx_count = Q_COUNT(Q_SIZE(rx_count, 3));

	/* Due pipelining impact minimum 2000 unused SQ CQE's
	 * need to be maintained to avoid CQ overflow, hence the
	 * minimum 4K size.
	 */
	tx_count = clamp_t(u32, ring->tx_pending,
			   Q_COUNT(Q_SIZE_4K), Q_COUNT(Q_SIZE_MAX));
	tx_count = Q_COUNT(Q_SIZE(tx_count, 3));

	if (tx_count == qs->sqe_cnt && rx_count == qs->rqe_cnt)
		return 0;

	if (if_up)
		otx2_dev_stop(netdev);

	/* Assigned to the nearest possible exponent. */
	qs->sqe_cnt = tx_count;
	qs->rqe_cnt = rx_count;

	if (if_up)
		otx2_dev_open(netdev);
	return 0;
}

static int otx2_get_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *cmd)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	struct otx2_hw *hw = &pfvf->hw;

	cmd->rx_coalesce_usecs = hw->cq_time_wait;
	cmd->rx_max_coalesced_frames = hw->cq_ecount_wait;
	cmd->tx_coalesce_usecs = hw->cq_time_wait;
	cmd->tx_max_coalesced_frames = hw->cq_ecount_wait;

	return 0;
}

static int otx2_set_coalesce(struct net_device *netdev,
			     struct ethtool_coalesce *ec)
{
	struct otx2_nic *pfvf = netdev_priv(netdev);
	struct otx2_hw *hw = &pfvf->hw;
	int qidx;

	if (ec->use_adaptive_rx_coalesce || ec->use_adaptive_tx_coalesce ||
	    ec->rx_coalesce_usecs_irq || ec->rx_max_coalesced_frames_irq ||
	    ec->tx_coalesce_usecs_irq || ec->tx_max_coalesced_frames_irq ||
	    ec->stats_block_coalesce_usecs || ec->pkt_rate_low ||
	    ec->rx_coalesce_usecs_low || ec->rx_max_coalesced_frames_low ||
	    ec->tx_coalesce_usecs_low || ec->tx_max_coalesced_frames_low ||
	    ec->pkt_rate_high || ec->rx_coalesce_usecs_high ||
	    ec->rx_max_coalesced_frames_high || ec->tx_coalesce_usecs_high ||
	    ec->tx_max_coalesced_frames_high || ec->rate_sample_interval)
		return -EOPNOTSUPP;

	if (!ec->rx_max_coalesced_frames || !ec->tx_max_coalesced_frames)
		return 0;

	/* 'cq_time_wait' is 8bit and is in multiple of 100ns,
	 * so clamp the user given value to the range of 1 to 25usec.
	 */
	ec->rx_coalesce_usecs = clamp_t(u32, ec->rx_coalesce_usecs,
					1, CQ_TIMER_THRESH_MAX);
	ec->tx_coalesce_usecs = clamp_t(u32, ec->tx_coalesce_usecs,
					1, CQ_TIMER_THRESH_MAX);

	/* Rx and Tx are mapped to same CQ, check which one
	 * is changed, if both then choose the min.
	 */
	if (hw->cq_time_wait == ec->rx_coalesce_usecs)
		hw->cq_time_wait = ec->tx_coalesce_usecs;
	else if (hw->cq_time_wait == ec->tx_coalesce_usecs)
		hw->cq_time_wait = ec->rx_coalesce_usecs;
	else
		hw->cq_time_wait = min_t(u8, ec->rx_coalesce_usecs,
					 ec->tx_coalesce_usecs);

	/* Max ecount_wait supported is 16bit,
	 * so clamp the user given value to the range of 1 to 64k.
	 */
	ec->rx_max_coalesced_frames = clamp_t(u32, ec->rx_max_coalesced_frames,
					      1, U16_MAX);
	ec->tx_max_coalesced_frames = clamp_t(u32, ec->tx_max_coalesced_frames,
					      1, U16_MAX);

	/* Rx and Tx are mapped to same CQ, check which one
	 * is changed, if both then choose the min.
	 */
	if (hw->cq_ecount_wait == ec->rx_max_coalesced_frames)
		hw->cq_ecount_wait = ec->tx_max_coalesced_frames;
	else if (hw->cq_ecount_wait == ec->tx_max_coalesced_frames)
		hw->cq_ecount_wait = ec->rx_max_coalesced_frames;
	else
		hw->cq_ecount_wait = min_t(u16, ec->rx_max_coalesced_frames,
					   ec->tx_max_coalesced_frames);

	if (netif_running(netdev)) {
		for (qidx = 0; qidx < pfvf->hw.cint_cnt; qidx++)
			otx2_config_irq_coalescing(pfvf, qidx);
	}

	return 0;
}

static const struct ethtool_ops otx2_ethtool_ops = {
	.get_drvinfo		= otx2_get_drvinfo,
	.get_strings		= otx2_get_strings,
	.get_ethtool_stats	= otx2_get_ethtool_stats,
	.get_sset_count		= otx2_get_sset_count,
	.set_channels		= otx2_set_channels,
	.get_channels		= otx2_get_channels,
	.get_ringparam		= otx2_get_ringparam,
	.set_ringparam		= otx2_set_ringparam,
	.get_coalesce		= otx2_get_coalesce,
	.set_coalesce		= otx2_set_coalesce,
};

void otx2_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &otx2_ethtool_ops;
}
