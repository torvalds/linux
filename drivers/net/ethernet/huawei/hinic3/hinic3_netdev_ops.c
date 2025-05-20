// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/etherdevice.h>
#include <linux/netdevice.h>

#include "hinic3_hwif.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"
#include "hinic3_rx.h"
#include "hinic3_tx.h"

static int hinic3_open(struct net_device *netdev)
{
	/* Completed by later submission due to LoC limit. */
	return -EFAULT;
}

static int hinic3_close(struct net_device *netdev)
{
	/* Completed by later submission due to LoC limit. */
	return -EFAULT;
}

static int hinic3_change_mtu(struct net_device *netdev, int new_mtu)
{
	int err;

	err = hinic3_set_port_mtu(netdev, new_mtu);
	if (err) {
		netdev_err(netdev, "Failed to change port mtu to %d\n",
			   new_mtu);
		return err;
	}

	netdev_dbg(netdev, "Change mtu from %u to %d\n", netdev->mtu, new_mtu);
	WRITE_ONCE(netdev->mtu, new_mtu);

	return 0;
}

static int hinic3_set_mac_addr(struct net_device *netdev, void *addr)
{
	struct hinic3_nic_dev *nic_dev = netdev_priv(netdev);
	struct sockaddr *saddr = addr;
	int err;

	if (!is_valid_ether_addr(saddr->sa_data))
		return -EADDRNOTAVAIL;

	if (ether_addr_equal(netdev->dev_addr, saddr->sa_data))
		return 0;

	err = hinic3_update_mac(nic_dev->hwdev, netdev->dev_addr,
				saddr->sa_data, 0,
				hinic3_global_func_id(nic_dev->hwdev));

	if (err)
		return err;

	eth_hw_addr_set(netdev, saddr->sa_data);

	return 0;
}

static const struct net_device_ops hinic3_netdev_ops = {
	.ndo_open             = hinic3_open,
	.ndo_stop             = hinic3_close,
	.ndo_change_mtu       = hinic3_change_mtu,
	.ndo_set_mac_address  = hinic3_set_mac_addr,
	.ndo_start_xmit       = hinic3_xmit_frame,
};

void hinic3_set_netdev_ops(struct net_device *netdev)
{
	netdev->netdev_ops = &hinic3_netdev_ops;
}
