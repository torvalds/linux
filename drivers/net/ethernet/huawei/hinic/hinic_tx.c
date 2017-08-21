/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/netdevice.h>
#include <linux/u64_stats_sync.h>

#include "hinic_hw_qp.h"
#include "hinic_tx.h"

/**
 * hinic_txq_clean_stats - Clean the statistics of specific queue
 * @txq: Logical Tx Queue
 **/
void hinic_txq_clean_stats(struct hinic_txq *txq)
{
	struct hinic_txq_stats *txq_stats = &txq->txq_stats;

	u64_stats_update_begin(&txq_stats->syncp);
	txq_stats->pkts    = 0;
	txq_stats->bytes   = 0;
	txq_stats->tx_busy = 0;
	txq_stats->tx_wake = 0;
	txq_stats->tx_dropped = 0;
	u64_stats_update_end(&txq_stats->syncp);
}

/**
 * txq_stats_init - Initialize the statistics of specific queue
 * @txq: Logical Tx Queue
 **/
static void txq_stats_init(struct hinic_txq *txq)
{
	struct hinic_txq_stats *txq_stats = &txq->txq_stats;

	u64_stats_init(&txq_stats->syncp);
	hinic_txq_clean_stats(txq);
}

/**
 * hinic_init_txq - Initialize the Tx Queue
 * @txq: Logical Tx Queue
 * @sq: Hardware Tx Queue to connect the Logical queue with
 * @netdev: network device to connect the Logical queue with
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_init_txq(struct hinic_txq *txq, struct hinic_sq *sq,
		   struct net_device *netdev)
{
	txq->netdev = netdev;
	txq->sq = sq;

	txq_stats_init(txq);
	return 0;
}

/**
 * hinic_clean_txq - Clean the Tx Queue
 * @txq: Logical Tx Queue
 **/
void hinic_clean_txq(struct hinic_txq *txq)
{
}
