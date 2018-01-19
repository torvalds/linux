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

#ifndef HINIC_DEV_H
#define HINIC_DEV_H

#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>

#include "hinic_hw_dev.h"
#include "hinic_tx.h"
#include "hinic_rx.h"

#define HINIC_DRV_NAME          "hinic"

enum hinic_flags {
	HINIC_LINK_UP = BIT(0),
	HINIC_INTF_UP = BIT(1),
};

struct hinic_rx_mode_work {
	struct work_struct      work;
	u32                     rx_mode;
};

struct hinic_dev {
	struct net_device               *netdev;
	struct hinic_hwdev              *hwdev;

	u32                             msg_enable;
	unsigned int                    tx_weight;
	unsigned int                    rx_weight;

	unsigned int                    flags;

	struct semaphore                mgmt_lock;
	unsigned long                   *vlan_bitmap;

	struct hinic_rx_mode_work       rx_mode_work;
	struct workqueue_struct         *workq;

	struct hinic_txq                *txqs;
	struct hinic_rxq                *rxqs;

	struct hinic_txq_stats          tx_stats;
	struct hinic_rxq_stats          rx_stats;
};

#endif
