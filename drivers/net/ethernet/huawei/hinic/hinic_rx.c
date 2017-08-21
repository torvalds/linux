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
#include "hinic_rx.h"

/**
 * hinic_rxq_clean_stats - Clean the statistics of specific queue
 * @rxq: Logical Rx Queue
 **/
void hinic_rxq_clean_stats(struct hinic_rxq *rxq)
{
	struct hinic_rxq_stats *rxq_stats = &rxq->rxq_stats;

	u64_stats_update_begin(&rxq_stats->syncp);
	rxq_stats->pkts  = 0;
	rxq_stats->bytes = 0;
	u64_stats_update_end(&rxq_stats->syncp);
}

/**
 * rxq_stats_init - Initialize the statistics of specific queue
 * @rxq: Logical Rx Queue
 **/
static void rxq_stats_init(struct hinic_rxq *rxq)
{
	struct hinic_rxq_stats *rxq_stats = &rxq->rxq_stats;

	u64_stats_init(&rxq_stats->syncp);
	hinic_rxq_clean_stats(rxq);
}

/**
 * hinic_init_rxq - Initialize the Rx Queue
 * @rxq: Logical Rx Queue
 * @rq: Hardware Rx Queue to connect the Logical queue with
 * @netdev: network device to connect the Logical queue with
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_init_rxq(struct hinic_rxq *rxq, struct hinic_rq *rq,
		   struct net_device *netdev)
{
	rxq->netdev = netdev;
	rxq->rq = rq;

	rxq_stats_init(rxq);
	return 0;
}

/**
 * hinic_clean_rxq - Clean the Rx Queue
 * @rxq: Logical Rx Queue
 **/
void hinic_clean_rxq(struct hinic_rxq *rxq)
{
}
