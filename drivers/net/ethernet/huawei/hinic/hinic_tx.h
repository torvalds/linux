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

#ifndef HINIC_TX_H
#define HINIC_TX_H

#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/u64_stats_sync.h>

#include "hinic_common.h"
#include "hinic_hw_qp.h"

struct hinic_txq_stats {
	u64     pkts;
	u64     bytes;
	u64     tx_busy;
	u64     tx_wake;
	u64     tx_dropped;

	struct u64_stats_sync   syncp;
};

struct hinic_txq {
	struct net_device       *netdev;
	struct hinic_sq         *sq;

	struct hinic_txq_stats  txq_stats;

	int                     max_sges;
	struct hinic_sge        *sges;
	struct hinic_sge        *free_sges;

	char                    *irq_name;
	struct napi_struct      napi;
};

void hinic_txq_clean_stats(struct hinic_txq *txq);

void hinic_txq_get_stats(struct hinic_txq *txq, struct hinic_txq_stats *stats);

netdev_tx_t hinic_xmit_frame(struct sk_buff *skb, struct net_device *netdev);

int hinic_init_txq(struct hinic_txq *txq, struct hinic_sq *sq,
		   struct net_device *netdev);

void hinic_clean_txq(struct hinic_txq *txq);

#endif
