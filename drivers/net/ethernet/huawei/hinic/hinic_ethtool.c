// SPDX-License-Identifier: GPL-2.0
/* Huawei HiNIC PCI Express Linux driver
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

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>

#include "hinic_hw_qp.h"
#include "hinic_hw_dev.h"
#include "hinic_port.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_dev.h"

static void set_link_speed(struct ethtool_link_ksettings *link_ksettings,
			   enum hinic_speed speed)
{
	switch (speed) {
	case HINIC_SPEED_10MB_LINK:
		link_ksettings->base.speed = SPEED_10;
		break;

	case HINIC_SPEED_100MB_LINK:
		link_ksettings->base.speed = SPEED_100;
		break;

	case HINIC_SPEED_1000MB_LINK:
		link_ksettings->base.speed = SPEED_1000;
		break;

	case HINIC_SPEED_10GB_LINK:
		link_ksettings->base.speed = SPEED_10000;
		break;

	case HINIC_SPEED_25GB_LINK:
		link_ksettings->base.speed = SPEED_25000;
		break;

	case HINIC_SPEED_40GB_LINK:
		link_ksettings->base.speed = SPEED_40000;
		break;

	case HINIC_SPEED_100GB_LINK:
		link_ksettings->base.speed = SPEED_100000;
		break;

	default:
		link_ksettings->base.speed = SPEED_UNKNOWN;
		break;
	}
}

static int hinic_get_link_ksettings(struct net_device *netdev,
				    struct ethtool_link_ksettings
				    *link_ksettings)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	enum hinic_port_link_state link_state;
	struct hinic_port_cap port_cap;
	int err;

	ethtool_link_ksettings_zero_link_mode(link_ksettings, advertising);
	ethtool_link_ksettings_add_link_mode(link_ksettings, supported,
					     Autoneg);

	link_ksettings->base.speed = SPEED_UNKNOWN;
	link_ksettings->base.autoneg = AUTONEG_DISABLE;
	link_ksettings->base.duplex = DUPLEX_UNKNOWN;

	err = hinic_port_get_cap(nic_dev, &port_cap);
	if (err)
		return err;

	err = hinic_port_link_state(nic_dev, &link_state);
	if (err)
		return err;

	if (link_state != HINIC_LINK_STATE_UP)
		return err;

	set_link_speed(link_ksettings, port_cap.speed);

	if (!!(port_cap.autoneg_cap & HINIC_AUTONEG_SUPPORTED))
		ethtool_link_ksettings_add_link_mode(link_ksettings,
						     advertising, Autoneg);

	if (port_cap.autoneg_state == HINIC_AUTONEG_ACTIVE)
		link_ksettings->base.autoneg = AUTONEG_ENABLE;

	link_ksettings->base.duplex = (port_cap.duplex == HINIC_DUPLEX_FULL) ?
					   DUPLEX_FULL : DUPLEX_HALF;
	return 0;
}

static void hinic_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *info)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_hwdev *hwdev = nic_dev->hwdev;
	struct hinic_hwif *hwif = hwdev->hwif;

	strlcpy(info->driver, HINIC_DRV_NAME, sizeof(info->driver));
	strlcpy(info->bus_info, pci_name(hwif->pdev), sizeof(info->bus_info));
}

static void hinic_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	ring->rx_max_pending = HINIC_RQ_DEPTH;
	ring->tx_max_pending = HINIC_SQ_DEPTH;
	ring->rx_pending = HINIC_RQ_DEPTH;
	ring->tx_pending = HINIC_SQ_DEPTH;
}

static void hinic_get_channels(struct net_device *netdev,
			       struct ethtool_channels *channels)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_hwdev *hwdev = nic_dev->hwdev;

	channels->max_rx = hwdev->nic_cap.max_qps;
	channels->max_tx = hwdev->nic_cap.max_qps;
	channels->max_other = 0;
	channels->max_combined = 0;
	channels->rx_count = hinic_hwdev_num_qps(hwdev);
	channels->tx_count = hinic_hwdev_num_qps(hwdev);
	channels->other_count = 0;
	channels->combined_count = 0;
}

static const struct ethtool_ops hinic_ethtool_ops = {
	.get_link_ksettings = hinic_get_link_ksettings,
	.get_drvinfo = hinic_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ringparam = hinic_get_ringparam,
	.get_channels = hinic_get_channels,
};

void hinic_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &hinic_ethtool_ops;
}
