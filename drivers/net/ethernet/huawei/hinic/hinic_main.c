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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/if_vlan.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <net/ip.h>
#include <linux/bitops.h>
#include <linux/bitmap.h>
#include <linux/delay.h>
#include <linux/err.h>

#include "hinic_hw_qp.h"
#include "hinic_hw_dev.h"
#include "hinic_port.h"
#include "hinic_tx.h"
#include "hinic_rx.h"
#include "hinic_dev.h"

MODULE_AUTHOR("Huawei Technologies CO., Ltd");
MODULE_DESCRIPTION("Huawei Intelligent NIC driver");
MODULE_LICENSE("GPL");

static unsigned int tx_weight = 64;
module_param(tx_weight, uint, 0644);
MODULE_PARM_DESC(tx_weight, "Number Tx packets for NAPI budget (default=64)");

static unsigned int rx_weight = 64;
module_param(rx_weight, uint, 0644);
MODULE_PARM_DESC(rx_weight, "Number Rx packets for NAPI budget (default=64)");

#define HINIC_DEV_ID_QUAD_PORT_25GE     0x1822
#define HINIC_DEV_ID_DUAL_PORT_25GE     0x0200
#define HINIC_DEV_ID_DUAL_PORT_100GE    0x0201

#define HINIC_WQ_NAME                   "hinic_dev"

#define MSG_ENABLE_DEFAULT              (NETIF_MSG_DRV | NETIF_MSG_PROBE | \
					 NETIF_MSG_IFUP |                  \
					 NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR)

#define VLAN_BITMAP_SIZE(nic_dev)       (ALIGN(VLAN_N_VID, 8) / 8)

#define work_to_rx_mode_work(work)      \
		container_of(work, struct hinic_rx_mode_work, work)

#define rx_mode_work_to_nic_dev(rx_mode_work) \
		container_of(rx_mode_work, struct hinic_dev, rx_mode_work)

static int change_mac_addr(struct net_device *netdev, const u8 *addr);

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

	link_ksettings->base.speed   = SPEED_UNKNOWN;
	link_ksettings->base.autoneg = AUTONEG_DISABLE;
	link_ksettings->base.duplex  = DUPLEX_UNKNOWN;

	err = hinic_port_get_cap(nic_dev, &port_cap);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to get port capabilities\n");
		return err;
	}

	err = hinic_port_link_state(nic_dev, &link_state);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to get port link state\n");
		return err;
	}

	if (link_state != HINIC_LINK_STATE_UP) {
		netif_info(nic_dev, drv, netdev, "No link\n");
		return err;
	}

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
	channels->max_other    = 0;
	channels->max_combined = 0;
	channels->rx_count = hinic_hwdev_num_qps(hwdev);
	channels->tx_count = hinic_hwdev_num_qps(hwdev);
	channels->other_count    = 0;
	channels->combined_count = 0;
}

static const struct ethtool_ops hinic_ethtool_ops = {
	.get_link_ksettings = hinic_get_link_ksettings,
	.get_drvinfo = hinic_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_ringparam = hinic_get_ringparam,
	.get_channels = hinic_get_channels,
};

static void update_rx_stats(struct hinic_dev *nic_dev, struct hinic_rxq *rxq)
{
	struct hinic_rxq_stats *nic_rx_stats = &nic_dev->rx_stats;
	struct hinic_rxq_stats rx_stats;

	u64_stats_init(&rx_stats.syncp);

	hinic_rxq_get_stats(rxq, &rx_stats);

	u64_stats_update_begin(&nic_rx_stats->syncp);
	nic_rx_stats->bytes += rx_stats.bytes;
	nic_rx_stats->pkts  += rx_stats.pkts;
	u64_stats_update_end(&nic_rx_stats->syncp);

	hinic_rxq_clean_stats(rxq);
}

static void update_tx_stats(struct hinic_dev *nic_dev, struct hinic_txq *txq)
{
	struct hinic_txq_stats *nic_tx_stats = &nic_dev->tx_stats;
	struct hinic_txq_stats tx_stats;

	u64_stats_init(&tx_stats.syncp);

	hinic_txq_get_stats(txq, &tx_stats);

	u64_stats_update_begin(&nic_tx_stats->syncp);
	nic_tx_stats->bytes += tx_stats.bytes;
	nic_tx_stats->pkts += tx_stats.pkts;
	nic_tx_stats->tx_busy += tx_stats.tx_busy;
	nic_tx_stats->tx_wake += tx_stats.tx_wake;
	nic_tx_stats->tx_dropped += tx_stats.tx_dropped;
	u64_stats_update_end(&nic_tx_stats->syncp);

	hinic_txq_clean_stats(txq);
}

static void update_nic_stats(struct hinic_dev *nic_dev)
{
	int i, num_qps = hinic_hwdev_num_qps(nic_dev->hwdev);

	for (i = 0; i < num_qps; i++)
		update_rx_stats(nic_dev, &nic_dev->rxqs[i]);

	for (i = 0; i < num_qps; i++)
		update_tx_stats(nic_dev, &nic_dev->txqs[i]);
}

/**
 * create_txqs - Create the Logical Tx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 *
 * Return 0 - Success, negative - Failure
 **/
static int create_txqs(struct hinic_dev *nic_dev)
{
	int err, i, j, num_txqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;
	size_t txq_size;

	if (nic_dev->txqs)
		return -EINVAL;

	txq_size = num_txqs * sizeof(*nic_dev->txqs);
	nic_dev->txqs = devm_kzalloc(&netdev->dev, txq_size, GFP_KERNEL);
	if (!nic_dev->txqs)
		return -ENOMEM;

	for (i = 0; i < num_txqs; i++) {
		struct hinic_sq *sq = hinic_hwdev_get_sq(nic_dev->hwdev, i);

		err = hinic_init_txq(&nic_dev->txqs[i], sq, netdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to init Txq\n");
			goto err_init_txq;
		}
	}

	return 0;

err_init_txq:
	for (j = 0; j < i; j++)
		hinic_clean_txq(&nic_dev->txqs[j]);

	devm_kfree(&netdev->dev, nic_dev->txqs);
	return err;
}

/**
 * free_txqs - Free the Logical Tx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 **/
static void free_txqs(struct hinic_dev *nic_dev)
{
	int i, num_txqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;

	if (!nic_dev->txqs)
		return;

	for (i = 0; i < num_txqs; i++)
		hinic_clean_txq(&nic_dev->txqs[i]);

	devm_kfree(&netdev->dev, nic_dev->txqs);
	nic_dev->txqs = NULL;
}

/**
 * create_txqs - Create the Logical Rx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 *
 * Return 0 - Success, negative - Failure
 **/
static int create_rxqs(struct hinic_dev *nic_dev)
{
	int err, i, j, num_rxqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;
	size_t rxq_size;

	if (nic_dev->rxqs)
		return -EINVAL;

	rxq_size = num_rxqs * sizeof(*nic_dev->rxqs);
	nic_dev->rxqs = devm_kzalloc(&netdev->dev, rxq_size, GFP_KERNEL);
	if (!nic_dev->rxqs)
		return -ENOMEM;

	for (i = 0; i < num_rxqs; i++) {
		struct hinic_rq *rq = hinic_hwdev_get_rq(nic_dev->hwdev, i);

		err = hinic_init_rxq(&nic_dev->rxqs[i], rq, netdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to init rxq\n");
			goto err_init_rxq;
		}
	}

	return 0;

err_init_rxq:
	for (j = 0; j < i; j++)
		hinic_clean_rxq(&nic_dev->rxqs[j]);

	devm_kfree(&netdev->dev, nic_dev->rxqs);
	return err;
}

/**
 * free_txqs - Free the Logical Rx Queues of specific NIC device
 * @nic_dev: the specific NIC device
 **/
static void free_rxqs(struct hinic_dev *nic_dev)
{
	int i, num_rxqs = hinic_hwdev_num_qps(nic_dev->hwdev);
	struct net_device *netdev = nic_dev->netdev;

	if (!nic_dev->rxqs)
		return;

	for (i = 0; i < num_rxqs; i++)
		hinic_clean_rxq(&nic_dev->rxqs[i]);

	devm_kfree(&netdev->dev, nic_dev->rxqs);
	nic_dev->rxqs = NULL;
}

static int hinic_open(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	enum hinic_port_link_state link_state;
	int err, ret, num_qps;

	if (!(nic_dev->flags & HINIC_INTF_UP)) {
		err = hinic_hwdev_ifup(nic_dev->hwdev);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed - HW interface up\n");
			return err;
		}
	}

	err = create_txqs(nic_dev);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to create Tx queues\n");
		goto err_create_txqs;
	}

	err = create_rxqs(nic_dev);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to create Rx queues\n");
		goto err_create_rxqs;
	}

	num_qps = hinic_hwdev_num_qps(nic_dev->hwdev);
	netif_set_real_num_tx_queues(netdev, num_qps);
	netif_set_real_num_rx_queues(netdev, num_qps);

	err = hinic_port_set_state(nic_dev, HINIC_PORT_ENABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to set port state\n");
		goto err_port_state;
	}

	err = hinic_port_set_func_state(nic_dev, HINIC_FUNC_PORT_ENABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to set func port state\n");
		goto err_func_port_state;
	}

	/* Wait up to 3 sec between port enable to link state */
	msleep(3000);

	down(&nic_dev->mgmt_lock);

	err = hinic_port_link_state(nic_dev, &link_state);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to get link state\n");
		goto err_port_link;
	}

	if (link_state == HINIC_LINK_STATE_UP)
		nic_dev->flags |= HINIC_LINK_UP;

	nic_dev->flags |= HINIC_INTF_UP;

	if ((nic_dev->flags & (HINIC_LINK_UP | HINIC_INTF_UP)) ==
	    (HINIC_LINK_UP | HINIC_INTF_UP)) {
		netif_info(nic_dev, drv, netdev, "link + intf UP\n");
		netif_carrier_on(netdev);
		netif_tx_wake_all_queues(netdev);
	}

	up(&nic_dev->mgmt_lock);

	netif_info(nic_dev, drv, netdev, "HINIC_INTF is UP\n");
	return 0;

err_port_link:
	up(&nic_dev->mgmt_lock);
	ret = hinic_port_set_func_state(nic_dev, HINIC_FUNC_PORT_DISABLE);
	if (ret)
		netif_warn(nic_dev, drv, netdev,
			   "Failed to revert func port state\n");

err_func_port_state:
	ret = hinic_port_set_state(nic_dev, HINIC_PORT_DISABLE);
	if (ret)
		netif_warn(nic_dev, drv, netdev,
			   "Failed to revert port state\n");

err_port_state:
	free_rxqs(nic_dev);

err_create_rxqs:
	free_txqs(nic_dev);

err_create_txqs:
	if (!(nic_dev->flags & HINIC_INTF_UP))
		hinic_hwdev_ifdown(nic_dev->hwdev);
	return err;
}

static int hinic_close(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	unsigned int flags;
	int err;

	down(&nic_dev->mgmt_lock);

	flags = nic_dev->flags;
	nic_dev->flags &= ~HINIC_INTF_UP;

	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	update_nic_stats(nic_dev);

	up(&nic_dev->mgmt_lock);

	err = hinic_port_set_func_state(nic_dev, HINIC_FUNC_PORT_DISABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to set func port state\n");
		nic_dev->flags |= (flags & HINIC_INTF_UP);
		return err;
	}

	err = hinic_port_set_state(nic_dev, HINIC_PORT_DISABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to set port state\n");
		nic_dev->flags |= (flags & HINIC_INTF_UP);
		return err;
	}

	free_rxqs(nic_dev);
	free_txqs(nic_dev);

	if (flags & HINIC_INTF_UP)
		hinic_hwdev_ifdown(nic_dev->hwdev);

	netif_info(nic_dev, drv, netdev, "HINIC_INTF is DOWN\n");
	return 0;
}

static int hinic_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err;

	netif_info(nic_dev, drv, netdev, "set_mtu = %d\n", new_mtu);

	err = hinic_port_set_mtu(nic_dev, new_mtu);
	if (err)
		netif_err(nic_dev, drv, netdev, "Failed to set port mtu\n");
	else
		netdev->mtu = new_mtu;

	return err;
}

/**
 * change_mac_addr - change the main mac address of network device
 * @netdev: network device
 * @addr: mac address to set
 *
 * Return 0 - Success, negative - Failure
 **/
static int change_mac_addr(struct net_device *netdev, const u8 *addr)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 vid = 0;
	int err;

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	netif_info(nic_dev, drv, netdev, "change mac addr = %02x %02x %02x %02x %02x %02x\n",
		   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	down(&nic_dev->mgmt_lock);

	do {
		err = hinic_port_del_mac(nic_dev, netdev->dev_addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to delete mac\n");
			break;
		}

		err = hinic_port_add_mac(nic_dev, addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev, "Failed to add mac\n");
			break;
		}

		vid = find_next_bit(nic_dev->vlan_bitmap, VLAN_N_VID, vid + 1);
	} while (vid != VLAN_N_VID);

	up(&nic_dev->mgmt_lock);
	return err;
}

static int hinic_set_mac_addr(struct net_device *netdev, void *addr)
{
	unsigned char new_mac[ETH_ALEN];
	struct sockaddr *saddr = addr;
	int err;

	memcpy(new_mac, saddr->sa_data, ETH_ALEN);

	err = change_mac_addr(netdev, new_mac);
	if (!err)
		memcpy(netdev->dev_addr, new_mac, ETH_ALEN);

	return err;
}

/**
 * add_mac_addr - add mac address to network device
 * @netdev: network device
 * @addr: mac address to add
 *
 * Return 0 - Success, negative - Failure
 **/
static int add_mac_addr(struct net_device *netdev, const u8 *addr)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 vid = 0;
	int err;

	netif_info(nic_dev, drv, netdev, "set mac addr = %02x %02x %02x %02x %02x %02x\n",
		   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	down(&nic_dev->mgmt_lock);

	do {
		err = hinic_port_add_mac(nic_dev, addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev, "Failed to add mac\n");
			break;
		}

		vid = find_next_bit(nic_dev->vlan_bitmap, VLAN_N_VID, vid + 1);
	} while (vid != VLAN_N_VID);

	up(&nic_dev->mgmt_lock);
	return err;
}

/**
 * remove_mac_addr - remove mac address from network device
 * @netdev: network device
 * @addr: mac address to remove
 *
 * Return 0 - Success, negative - Failure
 **/
static int remove_mac_addr(struct net_device *netdev, const u8 *addr)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	u16 vid = 0;
	int err;

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

	netif_info(nic_dev, drv, netdev, "remove mac addr = %02x %02x %02x %02x %02x %02x\n",
		   addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	down(&nic_dev->mgmt_lock);

	do {
		err = hinic_port_del_mac(nic_dev, addr, vid);
		if (err) {
			netif_err(nic_dev, drv, netdev,
				  "Failed to delete mac\n");
			break;
		}

		vid = find_next_bit(nic_dev->vlan_bitmap, VLAN_N_VID, vid + 1);
	} while (vid != VLAN_N_VID);

	up(&nic_dev->mgmt_lock);
	return err;
}

static int hinic_vlan_rx_add_vid(struct net_device *netdev,
				 __always_unused __be16 proto, u16 vid)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int ret, err;

	netif_info(nic_dev, drv, netdev, "add vid = %d\n", vid);

	down(&nic_dev->mgmt_lock);

	err = hinic_port_add_vlan(nic_dev, vid);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to add vlan\n");
		goto err_vlan_add;
	}

	err = hinic_port_add_mac(nic_dev, netdev->dev_addr, vid);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to set mac\n");
		goto err_add_mac;
	}

	bitmap_set(nic_dev->vlan_bitmap, vid, 1);

	up(&nic_dev->mgmt_lock);
	return 0;

err_add_mac:
	ret = hinic_port_del_vlan(nic_dev, vid);
	if (ret)
		netif_err(nic_dev, drv, netdev,
			  "Failed to revert by removing vlan\n");

err_vlan_add:
	up(&nic_dev->mgmt_lock);
	return err;
}

static int hinic_vlan_rx_kill_vid(struct net_device *netdev,
				  __always_unused __be16 proto, u16 vid)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	int err;

	netif_info(nic_dev, drv, netdev, "remove vid = %d\n", vid);

	down(&nic_dev->mgmt_lock);

	err = hinic_port_del_vlan(nic_dev, vid);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to delete vlan\n");
		goto err_del_vlan;
	}

	bitmap_clear(nic_dev->vlan_bitmap, vid, 1);

	up(&nic_dev->mgmt_lock);
	return 0;

err_del_vlan:
	up(&nic_dev->mgmt_lock);
	return err;
}

static void set_rx_mode(struct work_struct *work)
{
	struct hinic_rx_mode_work *rx_mode_work = work_to_rx_mode_work(work);
	struct hinic_dev *nic_dev = rx_mode_work_to_nic_dev(rx_mode_work);

	netif_info(nic_dev, drv, nic_dev->netdev, "set rx mode work\n");

	hinic_port_set_rx_mode(nic_dev, rx_mode_work->rx_mode);

	__dev_uc_sync(nic_dev->netdev, add_mac_addr, remove_mac_addr);
	__dev_mc_sync(nic_dev->netdev, add_mac_addr, remove_mac_addr);
}

static void hinic_set_rx_mode(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_rx_mode_work *rx_mode_work;
	u32 rx_mode;

	rx_mode_work = &nic_dev->rx_mode_work;

	rx_mode = HINIC_RX_MODE_UC |
		  HINIC_RX_MODE_MC |
		  HINIC_RX_MODE_BC;

	if (netdev->flags & IFF_PROMISC)
		rx_mode |= HINIC_RX_MODE_PROMISC;
	else if (netdev->flags & IFF_ALLMULTI)
		rx_mode |= HINIC_RX_MODE_MC_ALL;

	rx_mode_work->rx_mode = rx_mode;

	queue_work(nic_dev->workq, &rx_mode_work->work);
}

static void hinic_tx_timeout(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);

	netif_err(nic_dev, drv, netdev, "Tx timeout\n");
}

static void hinic_get_stats64(struct net_device *netdev,
			      struct rtnl_link_stats64 *stats)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_rxq_stats *nic_rx_stats;
	struct hinic_txq_stats *nic_tx_stats;

	nic_rx_stats = &nic_dev->rx_stats;
	nic_tx_stats = &nic_dev->tx_stats;

	down(&nic_dev->mgmt_lock);

	if (nic_dev->flags & HINIC_INTF_UP)
		update_nic_stats(nic_dev);

	up(&nic_dev->mgmt_lock);

	stats->rx_bytes   = nic_rx_stats->bytes;
	stats->rx_packets = nic_rx_stats->pkts;

	stats->tx_bytes   = nic_tx_stats->bytes;
	stats->tx_packets = nic_tx_stats->pkts;
	stats->tx_errors  = nic_tx_stats->tx_dropped;
}

static const struct net_device_ops hinic_netdev_ops = {
	.ndo_open = hinic_open,
	.ndo_stop = hinic_close,
	.ndo_change_mtu = hinic_change_mtu,
	.ndo_set_mac_address = hinic_set_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_vlan_rx_add_vid = hinic_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid = hinic_vlan_rx_kill_vid,
	.ndo_set_rx_mode = hinic_set_rx_mode,
	.ndo_start_xmit = hinic_xmit_frame,
	.ndo_tx_timeout = hinic_tx_timeout,
	.ndo_get_stats64 = hinic_get_stats64,
};

static void netdev_features_init(struct net_device *netdev)
{
	netdev->hw_features = NETIF_F_SG | NETIF_F_HIGHDMA | NETIF_F_IP_CSUM |
			      NETIF_F_IPV6_CSUM | NETIF_F_TSO | NETIF_F_TSO6 |
			      NETIF_F_RXCSUM;

	netdev->vlan_features = netdev->hw_features;

	netdev->features = netdev->hw_features | NETIF_F_HW_VLAN_CTAG_FILTER;
}

/**
 * link_status_event_handler - link event handler
 * @handle: nic device for the handler
 * @buf_in: input buffer
 * @in_size: input size
 * @buf_in: output buffer
 * @out_size: returned output size
 *
 * Return 0 - Success, negative - Failure
 **/
static void link_status_event_handler(void *handle, void *buf_in, u16 in_size,
				      void *buf_out, u16 *out_size)
{
	struct hinic_port_link_status *link_status, *ret_link_status;
	struct hinic_dev *nic_dev = handle;

	link_status = buf_in;

	if (link_status->link == HINIC_LINK_STATE_UP) {
		down(&nic_dev->mgmt_lock);

		nic_dev->flags |= HINIC_LINK_UP;

		if ((nic_dev->flags & (HINIC_LINK_UP | HINIC_INTF_UP)) ==
		    (HINIC_LINK_UP | HINIC_INTF_UP)) {
			netif_carrier_on(nic_dev->netdev);
			netif_tx_wake_all_queues(nic_dev->netdev);
		}

		up(&nic_dev->mgmt_lock);

		netif_info(nic_dev, drv, nic_dev->netdev, "HINIC_Link is UP\n");
	} else {
		down(&nic_dev->mgmt_lock);

		nic_dev->flags &= ~HINIC_LINK_UP;

		netif_carrier_off(nic_dev->netdev);
		netif_tx_disable(nic_dev->netdev);

		up(&nic_dev->mgmt_lock);

		netif_info(nic_dev, drv, nic_dev->netdev, "HINIC_Link is DOWN\n");
	}

	ret_link_status = buf_out;
	ret_link_status->status = 0;

	*out_size = sizeof(*ret_link_status);
}

static int set_features(struct hinic_dev *nic_dev,
			netdev_features_t pre_features,
			netdev_features_t features, bool force_change)
{
	netdev_features_t changed = force_change ? ~0 : pre_features ^ features;
	u32 csum_en = HINIC_RX_CSUM_OFFLOAD_EN;
	int err = 0;

	if (changed & NETIF_F_TSO)
		err = hinic_port_set_tso(nic_dev, (features & NETIF_F_TSO) ?
					 HINIC_TSO_ENABLE : HINIC_TSO_DISABLE);

	if (changed & NETIF_F_RXCSUM)
		err = hinic_set_rx_csum_offload(nic_dev, csum_en);

	return err;
}

/**
 * nic_dev_init - Initialize the NIC device
 * @pdev: the NIC pci device
 *
 * Return 0 - Success, negative - Failure
 **/
static int nic_dev_init(struct pci_dev *pdev)
{
	struct hinic_rx_mode_work *rx_mode_work;
	struct hinic_txq_stats *tx_stats;
	struct hinic_rxq_stats *rx_stats;
	struct hinic_dev *nic_dev;
	struct net_device *netdev;
	struct hinic_hwdev *hwdev;
	int err, num_qps;

	hwdev = hinic_init_hwdev(pdev);
	if (IS_ERR(hwdev)) {
		dev_err(&pdev->dev, "Failed to initialize HW device\n");
		return PTR_ERR(hwdev);
	}

	num_qps = hinic_hwdev_num_qps(hwdev);
	if (num_qps <= 0) {
		dev_err(&pdev->dev, "Invalid number of QPS\n");
		err = -EINVAL;
		goto err_num_qps;
	}

	netdev = alloc_etherdev_mq(sizeof(*nic_dev), num_qps);
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to allocate Ethernet device\n");
		err = -ENOMEM;
		goto err_alloc_etherdev;
	}

	netdev->netdev_ops = &hinic_netdev_ops;
	netdev->ethtool_ops = &hinic_ethtool_ops;
	netdev->max_mtu = ETH_MAX_MTU;

	nic_dev = netdev_priv(netdev);
	nic_dev->netdev = netdev;
	nic_dev->hwdev  = hwdev;
	nic_dev->msg_enable = MSG_ENABLE_DEFAULT;
	nic_dev->flags = 0;
	nic_dev->txqs = NULL;
	nic_dev->rxqs = NULL;
	nic_dev->tx_weight = tx_weight;
	nic_dev->rx_weight = rx_weight;

	sema_init(&nic_dev->mgmt_lock, 1);

	tx_stats = &nic_dev->tx_stats;
	rx_stats = &nic_dev->rx_stats;

	u64_stats_init(&tx_stats->syncp);
	u64_stats_init(&rx_stats->syncp);

	nic_dev->vlan_bitmap = devm_kzalloc(&pdev->dev,
					    VLAN_BITMAP_SIZE(nic_dev),
					    GFP_KERNEL);
	if (!nic_dev->vlan_bitmap) {
		err = -ENOMEM;
		goto err_vlan_bitmap;
	}

	nic_dev->workq = create_singlethread_workqueue(HINIC_WQ_NAME);
	if (!nic_dev->workq) {
		err = -ENOMEM;
		goto err_workq;
	}

	pci_set_drvdata(pdev, netdev);

	err = hinic_port_get_mac(nic_dev, netdev->dev_addr);
	if (err)
		dev_warn(&pdev->dev, "Failed to get mac address\n");

	err = hinic_port_add_mac(nic_dev, netdev->dev_addr, 0);
	if (err) {
		dev_err(&pdev->dev, "Failed to add mac\n");
		goto err_add_mac;
	}

	err = hinic_port_set_mtu(nic_dev, netdev->mtu);
	if (err) {
		dev_err(&pdev->dev, "Failed to set mtu\n");
		goto err_set_mtu;
	}

	rx_mode_work = &nic_dev->rx_mode_work;
	INIT_WORK(&rx_mode_work->work, set_rx_mode);

	netdev_features_init(netdev);

	netif_carrier_off(netdev);

	hinic_hwdev_cb_register(nic_dev->hwdev, HINIC_MGMT_MSG_CMD_LINK_STATUS,
				nic_dev, link_status_event_handler);

	err = set_features(nic_dev, 0, nic_dev->netdev->features, true);
	if (err)
		goto err_set_features;

	SET_NETDEV_DEV(netdev, &pdev->dev);

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_reg_netdev;
	}

	return 0;

err_reg_netdev:
err_set_features:
	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_LINK_STATUS);
	cancel_work_sync(&rx_mode_work->work);

err_set_mtu:
err_add_mac:
	pci_set_drvdata(pdev, NULL);
	destroy_workqueue(nic_dev->workq);

err_workq:
err_vlan_bitmap:
	free_netdev(netdev);

err_alloc_etherdev:
err_num_qps:
	hinic_free_hwdev(hwdev);
	return err;
}

static int hinic_probe(struct pci_dev *pdev,
		       const struct pci_device_id *id)
{
	int err = pci_enable_device(pdev);

	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return err;
	}

	err = pci_request_regions(pdev, HINIC_DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		goto err_pci_regions;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev, "Couldn't set 64-bit DMA mask\n");
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "Failed to set DMA mask\n");
			goto err_dma_mask;
		}
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	if (err) {
		dev_warn(&pdev->dev,
			 "Couldn't set 64-bit consistent DMA mask\n");
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev,
				"Failed to set consistent DMA mask\n");
			goto err_dma_consistent_mask;
		}
	}

	err = nic_dev_init(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize NIC device\n");
		goto err_nic_dev_init;
	}

	dev_info(&pdev->dev, "HiNIC driver - probed\n");
	return 0;

err_nic_dev_init:
err_dma_consistent_mask:
err_dma_mask:
	pci_release_regions(pdev);

err_pci_regions:
	pci_disable_device(pdev);
	return err;
}

static void hinic_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	struct hinic_rx_mode_work *rx_mode_work;

	unregister_netdev(netdev);

	hinic_hwdev_cb_unregister(nic_dev->hwdev,
				  HINIC_MGMT_MSG_CMD_LINK_STATUS);

	rx_mode_work = &nic_dev->rx_mode_work;
	cancel_work_sync(&rx_mode_work->work);

	pci_set_drvdata(pdev, NULL);

	destroy_workqueue(nic_dev->workq);

	hinic_free_hwdev(nic_dev->hwdev);

	free_netdev(netdev);

	pci_release_regions(pdev);
	pci_disable_device(pdev);

	dev_info(&pdev->dev, "HiNIC driver - removed\n");
}

static void hinic_shutdown(struct pci_dev *pdev)
{
	pci_disable_device(pdev);
}

static const struct pci_device_id hinic_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_QUAD_PORT_25GE), 0},
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_DUAL_PORT_25GE), 0},
	{ PCI_VDEVICE(HUAWEI, HINIC_DEV_ID_DUAL_PORT_100GE), 0},
	{ 0, 0}
};
MODULE_DEVICE_TABLE(pci, hinic_pci_table);

static struct pci_driver hinic_driver = {
	.name           = HINIC_DRV_NAME,
	.id_table       = hinic_pci_table,
	.probe          = hinic_probe,
	.remove         = hinic_remove,
	.shutdown       = hinic_shutdown,
};

module_pci_driver(hinic_driver);
