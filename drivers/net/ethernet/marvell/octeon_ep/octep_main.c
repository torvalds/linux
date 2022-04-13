// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/vmalloc.h>

#include "octep_config.h"
#include "octep_main.h"
#include "octep_ctrl_net.h"

struct workqueue_struct *octep_wq;

/* Supported Devices */
static const struct pci_device_id octep_pci_id_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN93_PF)},
	{0, },
};
MODULE_DEVICE_TABLE(pci, octep_pci_id_tbl);

MODULE_AUTHOR("Veerasenareddy Burru <vburru@marvell.com>");
MODULE_DESCRIPTION(OCTEP_DRV_STRING);
MODULE_LICENSE("GPL");
MODULE_VERSION(OCTEP_DRV_VERSION_STR);

static void octep_link_up(struct net_device *netdev)
{
	netif_carrier_on(netdev);
	netif_tx_start_all_queues(netdev);
}

/**
 * octep_open() - start the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * setup Tx/Rx queues, interrupts and enable hardware operation of Tx/Rx queues
 * and interrupts..
 *
 * Return: 0, on successfully setting up device and bring it up.
 *         -1, on any error.
 */
static int octep_open(struct net_device *netdev)
{
	struct octep_device *oct = netdev_priv(netdev);
	int err, ret;

	netdev_info(netdev, "Starting netdev ...\n");
	netif_carrier_off(netdev);

	oct->hw_ops.reset_io_queues(oct);

	if (octep_setup_iqs(oct))
		goto setup_iq_err;
	if (octep_setup_oqs(oct))
		goto setup_oq_err;

	err = netif_set_real_num_tx_queues(netdev, oct->num_oqs);
	if (err)
		goto set_queues_err;
	err = netif_set_real_num_rx_queues(netdev, oct->num_iqs);
	if (err)
		goto set_queues_err;

	oct->link_info.admin_up = 1;
	octep_set_rx_state(oct, true);

	ret = octep_get_link_status(oct);
	if (!ret)
		octep_set_link_status(oct, true);

	/* Enable the input and output queues for this Octeon device */
	oct->hw_ops.enable_io_queues(oct);

	/* Enable Octeon device interrupts */
	oct->hw_ops.enable_interrupts(oct);

	octep_oq_dbell_init(oct);

	ret = octep_get_link_status(oct);
	if (ret)
		octep_link_up(netdev);

	return 0;

set_queues_err:
	octep_free_oqs(oct);
setup_oq_err:
	octep_free_iqs(oct);
setup_iq_err:
	return -1;
}

/**
 * octep_stop() - stop the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * stop the device Tx/Rx operations, bring down the link and
 * free up all resources allocated for Tx/Rx queues and interrupts.
 */
static int octep_stop(struct net_device *netdev)
{
	struct octep_device *oct = netdev_priv(netdev);

	netdev_info(netdev, "Stopping the device ...\n");

	/* Stop Tx from stack */
	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	octep_set_link_status(oct, false);
	octep_set_rx_state(oct, false);

	oct->link_info.admin_up = 0;
	oct->link_info.oper_up = 0;

	oct->hw_ops.disable_interrupts(oct);

	octep_clean_iqs(oct);

	oct->hw_ops.disable_io_queues(oct);
	oct->hw_ops.reset_io_queues(oct);
	octep_free_oqs(oct);
	octep_free_iqs(oct);
	netdev_info(netdev, "Device stopped !!\n");
	return 0;
}

/**
 * octep_start_xmit() - Enqueue packet to Octoen hardware Tx Queue.
 *
 * @skb: packet skbuff pointer.
 * @netdev: kernel network device.
 *
 * Return: NETDEV_TX_BUSY, if Tx Queue is full.
 *         NETDEV_TX_OK, if successfully enqueued to hardware Tx queue.
 */
static netdev_tx_t octep_start_xmit(struct sk_buff *skb,
				    struct net_device *netdev)
{
	return NETDEV_TX_OK;
}

/**
 * octep_get_stats64() - Get Octeon network device statistics.
 *
 * @netdev: kernel network device.
 * @stats: pointer to stats structure to be filled in.
 */
static void octep_get_stats64(struct net_device *netdev,
			      struct rtnl_link_stats64 *stats)
{
	u64 tx_packets, tx_bytes, rx_packets, rx_bytes;
	struct octep_device *oct = netdev_priv(netdev);
	int q;

	octep_get_if_stats(oct);
	tx_packets = 0;
	tx_bytes = 0;
	rx_packets = 0;
	rx_bytes = 0;
	for (q = 0; q < oct->num_oqs; q++) {
		struct octep_iq *iq = oct->iq[q];
		struct octep_oq *oq = oct->oq[q];

		tx_packets += iq->stats.instr_completed;
		tx_bytes += iq->stats.bytes_sent;
		rx_packets += oq->stats.packets;
		rx_bytes += oq->stats.bytes;
	}
	stats->tx_packets = tx_packets;
	stats->tx_bytes = tx_bytes;
	stats->rx_packets = rx_packets;
	stats->rx_bytes = rx_bytes;
	stats->multicast = oct->iface_rx_stats.mcast_pkts;
	stats->rx_errors = oct->iface_rx_stats.err_pkts;
	stats->collisions = oct->iface_tx_stats.xscol;
	stats->tx_fifo_errors = oct->iface_tx_stats.undflw;
}

/**
 * octep_tx_timeout_task - work queue task to Handle Tx queue timeout.
 *
 * @work: pointer to Tx queue timeout work_struct
 *
 * Stop and start the device so that it frees up all queue resources
 * and restarts the queues, that potentially clears a Tx queue timeout
 * condition.
 **/
static void octep_tx_timeout_task(struct work_struct *work)
{
	struct octep_device *oct = container_of(work, struct octep_device,
						tx_timeout_task);
	struct net_device *netdev = oct->netdev;

	rtnl_lock();
	if (netif_running(netdev)) {
		octep_stop(netdev);
		octep_open(netdev);
	}
	rtnl_unlock();
}

/**
 * octep_tx_timeout() - Handle Tx Queue timeout.
 *
 * @netdev: pointer to kernel network device.
 * @txqueue: Timed out Tx queue number.
 *
 * Schedule a work to handle Tx queue timeout.
 */
static void octep_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct octep_device *oct = netdev_priv(netdev);

	queue_work(octep_wq, &oct->tx_timeout_task);
}

static int octep_set_mac(struct net_device *netdev, void *p)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct sockaddr *addr = (struct sockaddr *)p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = octep_set_mac_addr(oct, addr->sa_data);
	if (err)
		return err;

	memcpy(oct->mac_addr, addr->sa_data, ETH_ALEN);
	eth_hw_addr_set(netdev, addr->sa_data);

	return 0;
}

static int octep_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct octep_device *oct = netdev_priv(netdev);
	struct octep_iface_link_info *link_info;
	int err = 0;

	link_info = &oct->link_info;
	if (link_info->mtu == new_mtu)
		return 0;

	err = octep_set_mtu(oct, new_mtu);
	if (!err) {
		oct->link_info.mtu = new_mtu;
		netdev->mtu = new_mtu;
	}

	return err;
}

static const struct net_device_ops octep_netdev_ops = {
	.ndo_open                = octep_open,
	.ndo_stop                = octep_stop,
	.ndo_start_xmit          = octep_start_xmit,
	.ndo_get_stats64         = octep_get_stats64,
	.ndo_tx_timeout          = octep_tx_timeout,
	.ndo_set_mac_address     = octep_set_mac,
	.ndo_change_mtu          = octep_change_mtu,
};

/**
 * octep_ctrl_mbox_task - work queue task to handle ctrl mbox messages.
 *
 * @work: pointer to ctrl mbox work_struct
 *
 * Poll ctrl mbox message queue and handle control messages from firmware.
 **/
static void octep_ctrl_mbox_task(struct work_struct *work)
{
	struct octep_device *oct = container_of(work, struct octep_device,
						ctrl_mbox_task);
	struct net_device *netdev = oct->netdev;
	struct octep_ctrl_net_f2h_req req = {};
	struct octep_ctrl_mbox_msg msg;
	int ret = 0;

	msg.msg = &req;
	while (true) {
		ret = octep_ctrl_mbox_recv(&oct->ctrl_mbox, &msg);
		if (ret)
			break;

		switch (req.hdr.cmd) {
		case OCTEP_CTRL_NET_F2H_CMD_LINK_STATUS:
			if (netif_running(netdev)) {
				if (req.link.state) {
					dev_info(&oct->pdev->dev, "netif_carrier_on\n");
					netif_carrier_on(netdev);
				} else {
					dev_info(&oct->pdev->dev, "netif_carrier_off\n");
					netif_carrier_off(netdev);
				}
			}
			break;
		default:
			pr_info("Unknown mbox req : %u\n", req.hdr.cmd);
			break;
		}
	}
}

/**
 * octep_device_setup() - Setup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Setup Octeon device hardware operations, configuration, etc ...
 */
int octep_device_setup(struct octep_device *oct)
{
	struct octep_ctrl_mbox *ctrl_mbox;
	struct pci_dev *pdev = oct->pdev;
	int i, ret;

	/* allocate memory for oct->conf */
	oct->conf = kzalloc(sizeof(*oct->conf), GFP_KERNEL);
	if (!oct->conf)
		return -ENOMEM;

	/* Map BAR regions */
	for (i = 0; i < OCTEP_MMIO_REGIONS; i++) {
		oct->mmio[i].hw_addr =
			ioremap(pci_resource_start(oct->pdev, i * 2),
				pci_resource_len(oct->pdev, i * 2));
		oct->mmio[i].mapped = 1;
	}

	oct->chip_id = pdev->device;
	oct->rev_id = pdev->revision;
	dev_info(&pdev->dev, "chip_id = 0x%x\n", pdev->device);

	switch (oct->chip_id) {
	case OCTEP_PCI_DEVICE_ID_CN93_PF:
		dev_info(&pdev->dev,
			 "Setting up OCTEON CN93XX PF PASS%d.%d\n",
			 OCTEP_MAJOR_REV(oct), OCTEP_MINOR_REV(oct));
		octep_device_setup_cn93_pf(oct);
		break;
	default:
		dev_err(&pdev->dev,
			"%s: unsupported device\n", __func__);
		goto unsupported_dev;
	}

	oct->pkind = CFG_GET_IQ_PKIND(oct->conf);

	/* Initialize control mbox */
	ctrl_mbox = &oct->ctrl_mbox;
	ctrl_mbox->version = OCTEP_DRV_VERSION;
	ctrl_mbox->barmem = CFG_GET_CTRL_MBOX_MEM_ADDR(oct->conf);
	ret = octep_ctrl_mbox_init(ctrl_mbox);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize control mbox\n");
		return -1;
	}
	oct->ctrl_mbox_ifstats_offset = OCTEP_CTRL_MBOX_SZ(ctrl_mbox->h2fq.elem_sz,
							   ctrl_mbox->h2fq.elem_cnt,
							   ctrl_mbox->f2hq.elem_sz,
							   ctrl_mbox->f2hq.elem_cnt);

	return 0;

unsupported_dev:
	return -1;
}

/**
 * octep_device_cleanup() - Cleanup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Cleanup Octeon device allocated resources.
 */
static void octep_device_cleanup(struct octep_device *oct)
{
	int i;

	dev_info(&oct->pdev->dev, "Cleaning up Octeon Device ...\n");

	for (i = 0; i < OCTEP_MAX_VF; i++) {
		if (oct->mbox[i])
			vfree(oct->mbox[i]);
		oct->mbox[i] = NULL;
	}

	octep_ctrl_mbox_uninit(&oct->ctrl_mbox);

	oct->hw_ops.soft_reset(oct);
	for (i = 0; i < OCTEP_MMIO_REGIONS; i++) {
		if (oct->mmio[i].mapped)
			iounmap(oct->mmio[i].hw_addr);
	}

	kfree(oct->conf);
	oct->conf = NULL;
}

/**
 * octep_probe() - Octeon PCI device probe handler.
 *
 * @pdev: PCI device structure.
 * @ent: entry in Octeon PCI device ID table.
 *
 * Initializes and enables the Octeon PCI device for network operations.
 * Initializes Octeon private data structure and registers a network device.
 */
static int octep_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct octep_device *octep_dev = NULL;
	struct net_device *netdev;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		return  err;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "Failed to set DMA mask !!\n");
		goto err_dma_mask;
	}

	err = pci_request_mem_regions(pdev, OCTEP_DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to map PCI memory regions\n");
		goto err_pci_regions;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	netdev = alloc_etherdev_mq(sizeof(struct octep_device),
				   OCTEP_MAX_QUEUES);
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to allocate netdev\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	octep_dev = netdev_priv(netdev);
	octep_dev->netdev = netdev;
	octep_dev->pdev = pdev;
	octep_dev->dev = &pdev->dev;
	pci_set_drvdata(pdev, octep_dev);

	err = octep_device_setup(octep_dev);
	if (err) {
		dev_err(&pdev->dev, "Device setup failed\n");
		goto err_octep_config;
	}
	INIT_WORK(&octep_dev->tx_timeout_task, octep_tx_timeout_task);
	INIT_WORK(&octep_dev->ctrl_mbox_task, octep_ctrl_mbox_task);

	netdev->netdev_ops = &octep_netdev_ops;
	netif_carrier_off(netdev);

	netdev->hw_features = NETIF_F_SG;
	netdev->features |= netdev->hw_features;
	netdev->min_mtu = OCTEP_MIN_MTU;
	netdev->max_mtu = OCTEP_MAX_MTU;
	netdev->mtu = OCTEP_DEFAULT_MTU;

	octep_get_mac_addr(octep_dev, octep_dev->mac_addr);
	eth_hw_addr_set(netdev, octep_dev->mac_addr);

	if (register_netdev(netdev)) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto register_dev_err;
	}
	dev_info(&pdev->dev, "Device probe successful\n");
	return 0;

register_dev_err:
	octep_device_cleanup(octep_dev);
err_octep_config:
	free_netdev(netdev);
err_alloc_netdev:
	pci_disable_pcie_error_reporting(pdev);
	pci_release_mem_regions(pdev);
err_pci_regions:
err_dma_mask:
	pci_disable_device(pdev);
	return err;
}

/**
 * octep_remove() - Remove Octeon PCI device from driver control.
 *
 * @pdev: PCI device structure of the Octeon device.
 *
 * Cleanup all resources allocated for the Octeon device.
 * Unregister from network device and disable the PCI device.
 */
static void octep_remove(struct pci_dev *pdev)
{
	struct octep_device *oct = pci_get_drvdata(pdev);
	struct net_device *netdev;

	if (!oct)
		return;

	cancel_work_sync(&oct->tx_timeout_task);
	cancel_work_sync(&oct->ctrl_mbox_task);
	netdev = oct->netdev;
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);

	octep_device_cleanup(oct);
	pci_release_mem_regions(pdev);
	free_netdev(netdev);
	pci_disable_pcie_error_reporting(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver octep_driver = {
	.name = OCTEP_DRV_NAME,
	.id_table = octep_pci_id_tbl,
	.probe = octep_probe,
	.remove = octep_remove,
};

/**
 * octep_init_module() - Module initialiation.
 *
 * create common resource for the driver and register PCI driver.
 */
static int __init octep_init_module(void)
{
	int ret;

	pr_info("%s: Loading %s ...\n", OCTEP_DRV_NAME, OCTEP_DRV_STRING);

	/* work queue for all deferred tasks */
	octep_wq = create_singlethread_workqueue(OCTEP_DRV_NAME);
	if (!octep_wq) {
		pr_err("%s: Failed to create common workqueue\n",
		       OCTEP_DRV_NAME);
		return -ENOMEM;
	}

	ret = pci_register_driver(&octep_driver);
	if (ret < 0) {
		pr_err("%s: Failed to register PCI driver; err=%d\n",
		       OCTEP_DRV_NAME, ret);
		return ret;
	}

	pr_info("%s: Loaded successfully !\n", OCTEP_DRV_NAME);

	return ret;
}

/**
 * octep_exit_module() - Module exit routine.
 *
 * unregister the driver with PCI subsystem and cleanup common resources.
 */
static void __exit octep_exit_module(void)
{
	pr_info("%s: Unloading ...\n", OCTEP_DRV_NAME);

	pci_unregister_driver(&octep_driver);
	destroy_workqueue(octep_wq);

	pr_info("%s: Unloading complete\n", OCTEP_DRV_NAME);
}

module_init(octep_init_module);
module_exit(octep_exit_module);
