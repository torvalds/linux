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

#include "hinic_hw_dev.h"
#include "hinic_port.h"
#include "hinic_dev.h"

MODULE_AUTHOR("Huawei Technologies CO., Ltd");
MODULE_DESCRIPTION("Huawei Intelligent NIC driver");
MODULE_LICENSE("GPL");

#define PCI_DEVICE_ID_HI1822_PF         0x1822

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

static int hinic_open(struct net_device *netdev)
{
	struct hinic_dev *nic_dev = netdev_priv(netdev);
	enum hinic_port_link_state link_state;
	int err, ret;

	err = hinic_port_set_state(nic_dev, HINIC_PORT_ENABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev,
			  "Failed to set port state\n");
		return err;
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
	ret = hinic_port_set_state(nic_dev, HINIC_PORT_DISABLE);
	if (ret)
		netif_warn(nic_dev, drv, netdev,
			   "Failed to revert port state\n");
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

	up(&nic_dev->mgmt_lock);

	err = hinic_port_set_state(nic_dev, HINIC_PORT_DISABLE);
	if (err) {
		netif_err(nic_dev, drv, netdev, "Failed to set port state\n");
		nic_dev->flags |= (flags & HINIC_INTF_UP);
		return err;
	}

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

	if (!is_valid_ether_addr(addr))
		return -EADDRNOTAVAIL;

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

netdev_tx_t hinic_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	return NETDEV_TX_BUSY;
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
	/* more operations should be filled */
};

static void netdev_features_init(struct net_device *netdev)
{
	netdev->hw_features = NETIF_F_SG | NETIF_F_HIGHDMA;

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

/**
 * nic_dev_init - Initialize the NIC device
 * @pdev: the NIC pci device
 *
 * Return 0 - Success, negative - Failure
 **/
static int nic_dev_init(struct pci_dev *pdev)
{
	struct hinic_rx_mode_work *rx_mode_work;
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

	nic_dev = netdev_priv(netdev);
	nic_dev->netdev = netdev;
	nic_dev->hwdev  = hwdev;
	nic_dev->msg_enable = MSG_ENABLE_DEFAULT;
	nic_dev->flags = 0;

	sema_init(&nic_dev->mgmt_lock, 1);

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

	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_reg_netdev;
	}

	return 0;

err_reg_netdev:
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

static const struct pci_device_id hinic_pci_table[] = {
	{ PCI_VDEVICE(HUAWEI, PCI_DEVICE_ID_HI1822_PF), 0},
	{ 0, 0}
};
MODULE_DEVICE_TABLE(pci, hinic_pci_table);

static struct pci_driver hinic_driver = {
	.name           = HINIC_DRV_NAME,
	.id_table       = hinic_pci_table,
	.probe          = hinic_probe,
	.remove         = hinic_remove,
};

module_pci_driver(hinic_driver);
