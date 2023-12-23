// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
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

#include "octep_vf_config.h"
#include "octep_vf_main.h"

struct workqueue_struct *octep_vf_wq;

/* Supported Devices */
static const struct pci_device_id octep_vf_pci_id_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN93_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CNF95N_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN98_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN10KA_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CNF10KA_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CNF10KB_VF)},
	{PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, OCTEP_PCI_DEVICE_ID_CN10KB_VF)},
	{0, },
};
MODULE_DEVICE_TABLE(pci, octep_vf_pci_id_tbl);

MODULE_AUTHOR("Veerasenareddy Burru <vburru@marvell.com>");
MODULE_DESCRIPTION(OCTEP_VF_DRV_STRING);
MODULE_LICENSE("GPL");

static void octep_vf_link_up(struct net_device *netdev)
{
	netif_carrier_on(netdev);
	netif_tx_start_all_queues(netdev);
}

static void octep_vf_set_rx_state(struct octep_vf_device *oct, bool up)
{
	int err;

	err = octep_vf_mbox_set_rx_state(oct, up);
	if (err)
		netdev_err(oct->netdev, "Set Rx state to %d failed with err:%d\n", up, err);
}

static int octep_vf_get_link_status(struct octep_vf_device *oct)
{
	int err;

	err = octep_vf_mbox_get_link_status(oct, &oct->link_info.oper_up);
	if (err)
		netdev_err(oct->netdev, "Get link status failed with err:%d\n", err);
	return oct->link_info.oper_up;
}

static void octep_vf_set_link_status(struct octep_vf_device *oct, bool up)
{
	int err;

	err = octep_vf_mbox_set_link_status(oct, up);
	if (err) {
		netdev_err(oct->netdev, "Set link status to %d failed with err:%d\n", up, err);
		return;
	}
	oct->link_info.oper_up = up;
}

/**
 * octep_vf_open() - start the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * setup Tx/Rx queues, interrupts and enable hardware operation of Tx/Rx queues
 * and interrupts..
 *
 * Return: 0, on successfully setting up device and bring it up.
 *         -1, on any error.
 */
static int octep_vf_open(struct net_device *netdev)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	int err, ret;

	netdev_info(netdev, "Starting netdev ...\n");
	netif_carrier_off(netdev);

	oct->hw_ops.reset_io_queues(oct);

	if (octep_vf_setup_iqs(oct))
		goto setup_iq_err;
	if (octep_vf_setup_oqs(oct))
		goto setup_oq_err;

	err = netif_set_real_num_tx_queues(netdev, oct->num_oqs);
	if (err)
		goto set_queues_err;
	err = netif_set_real_num_rx_queues(netdev, oct->num_iqs);
	if (err)
		goto set_queues_err;

	oct->link_info.admin_up = 1;
	octep_vf_set_rx_state(oct, true);

	ret = octep_vf_get_link_status(oct);
	if (!ret)
		octep_vf_set_link_status(oct, true);

	/* Enable the input and output queues for this Octeon device */
	oct->hw_ops.enable_io_queues(oct);

	/* Enable Octeon device interrupts */
	oct->hw_ops.enable_interrupts(oct);

	octep_vf_oq_dbell_init(oct);

	ret = octep_vf_get_link_status(oct);
	if (ret)
		octep_vf_link_up(netdev);

	return 0;

set_queues_err:
	octep_vf_free_oqs(oct);
setup_oq_err:
	octep_vf_free_iqs(oct);
setup_iq_err:
	return -1;
}

/**
 * octep_vf_stop() - stop the octeon network device.
 *
 * @netdev: pointer to kernel network device.
 *
 * stop the device Tx/Rx operations, bring down the link and
 * free up all resources allocated for Tx/Rx queues and interrupts.
 */
static int octep_vf_stop(struct net_device *netdev)
{
	struct octep_vf_device *oct = netdev_priv(netdev);

	netdev_info(netdev, "Stopping the device ...\n");

	/* Stop Tx from stack */
	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);
	netif_tx_disable(netdev);

	octep_vf_set_link_status(oct, false);
	octep_vf_set_rx_state(oct, false);

	oct->link_info.admin_up = 0;
	oct->link_info.oper_up = 0;

	oct->hw_ops.disable_interrupts(oct);

	octep_vf_clean_iqs(oct);

	oct->hw_ops.disable_io_queues(oct);
	oct->hw_ops.reset_io_queues(oct);
	octep_vf_free_oqs(oct);
	octep_vf_free_iqs(oct);
	netdev_info(netdev, "Device stopped !!\n");
	return 0;
}

/**
 * octep_vf_start_xmit() - Enqueue packet to Octoen hardware Tx Queue.
 *
 * @skb: packet skbuff pointer.
 * @netdev: kernel network device.
 *
 * Return: NETDEV_TX_BUSY, if Tx Queue is full.
 *         NETDEV_TX_OK, if successfully enqueued to hardware Tx queue.
 */
static netdev_tx_t octep_vf_start_xmit(struct sk_buff *skb,
				       struct net_device *netdev)
{
	return NETDEV_TX_OK;
}

int octep_vf_get_if_stats(struct octep_vf_device *oct)
{
	struct octep_vf_iface_rxtx_stats vf_stats;
	int ret, size;

	memset(&vf_stats, 0, sizeof(struct octep_vf_iface_rxtx_stats));
	ret = octep_vf_mbox_bulk_read(oct, OCTEP_PFVF_MBOX_CMD_GET_STATS,
				      (u8 *)&vf_stats, &size);
	if (!ret) {
		memcpy(&oct->iface_rx_stats, &vf_stats.iface_rx_stats,
		       sizeof(struct octep_vf_iface_rx_stats));
		memcpy(&oct->iface_tx_stats, &vf_stats.iface_tx_stats,
		       sizeof(struct octep_vf_iface_tx_stats));
	}
	return ret;
}

int octep_vf_get_link_info(struct octep_vf_device *oct)
{
	int ret, size;

	ret = octep_vf_mbox_bulk_read(oct, OCTEP_PFVF_MBOX_CMD_GET_LINK_INFO,
				      (u8 *)&oct->link_info, &size);
	if (ret) {
		dev_err(&oct->pdev->dev, "Get VF link info failed via VF Mbox\n");
		return ret;
	}
	return 0;
}

/**
 * octep_vf_get_stats64() - Get Octeon network device statistics.
 *
 * @netdev: kernel network device.
 * @stats: pointer to stats structure to be filled in.
 */
static void octep_vf_get_stats64(struct net_device *netdev,
				 struct rtnl_link_stats64 *stats)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	u64 tx_packets, tx_bytes, rx_packets, rx_bytes;
	int q;

	tx_packets = 0;
	tx_bytes = 0;
	rx_packets = 0;
	rx_bytes = 0;
	for (q = 0; q < oct->num_oqs; q++) {
		struct octep_vf_iq *iq = oct->iq[q];
		struct octep_vf_oq *oq = oct->oq[q];

		tx_packets += iq->stats.instr_completed;
		tx_bytes += iq->stats.bytes_sent;
		rx_packets += oq->stats.packets;
		rx_bytes += oq->stats.bytes;
	}
	stats->tx_packets = tx_packets;
	stats->tx_bytes = tx_bytes;
	stats->rx_packets = rx_packets;
	stats->rx_bytes = rx_bytes;
	if (!octep_vf_get_if_stats(oct)) {
		stats->multicast = oct->iface_rx_stats.mcast_pkts;
		stats->rx_errors = oct->iface_rx_stats.err_pkts;
	}
}

/**
 * octep_vf_tx_timeout_task - work queue task to Handle Tx queue timeout.
 *
 * @work: pointer to Tx queue timeout work_struct
 *
 * Stop and start the device so that it frees up all queue resources
 * and restarts the queues, that potentially clears a Tx queue timeout
 * condition.
 **/
static void octep_vf_tx_timeout_task(struct work_struct *work)
{
	struct octep_vf_device *oct = container_of(work, struct octep_vf_device,
						tx_timeout_task);
	struct net_device *netdev = oct->netdev;

	rtnl_lock();
	if (netif_running(netdev)) {
		octep_vf_stop(netdev);
		octep_vf_open(netdev);
	}
	rtnl_unlock();
}

/**
 * octep_vf_tx_timeout() - Handle Tx Queue timeout.
 *
 * @netdev: pointer to kernel network device.
 * @txqueue: Timed out Tx queue number.
 *
 * Schedule a work to handle Tx queue timeout.
 */
static void octep_vf_tx_timeout(struct net_device *netdev, unsigned int txqueue)
{
	struct octep_vf_device *oct = netdev_priv(netdev);

	queue_work(octep_vf_wq, &oct->tx_timeout_task);
}

static int octep_vf_set_mac(struct net_device *netdev, void *p)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	struct sockaddr *addr = (struct sockaddr *)p;
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = octep_vf_mbox_set_mac_addr(oct, addr->sa_data);
	if (err)
		return err;

	memcpy(oct->mac_addr, addr->sa_data, ETH_ALEN);
	eth_hw_addr_set(netdev, addr->sa_data);

	return 0;
}

static int octep_vf_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	struct octep_vf_iface_link_info *link_info;
	int err;

	link_info = &oct->link_info;
	if (link_info->mtu == new_mtu)
		return 0;

	err = octep_vf_mbox_set_mtu(oct, new_mtu);
	if (!err) {
		oct->link_info.mtu = new_mtu;
		netdev->mtu = new_mtu;
	}
	return err;
}

static int octep_vf_set_features(struct net_device *netdev,
				 netdev_features_t features)
{
	struct octep_vf_device *oct = netdev_priv(netdev);
	u16 rx_offloads = 0, tx_offloads = 0;
	int err;

	/* We only support features received from firmware */
	if ((features & netdev->hw_features) != features)
		return -EINVAL;

	if (features & NETIF_F_TSO)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_TSO;

	if (features & NETIF_F_TSO6)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_TSO;

	if (features & NETIF_F_IP_CSUM)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_CKSUM;

	if (features & NETIF_F_IPV6_CSUM)
		tx_offloads |= OCTEP_VF_TX_OFFLOAD_CKSUM;

	if (features & NETIF_F_RXCSUM)
		rx_offloads |= OCTEP_VF_RX_OFFLOAD_CKSUM;

	err = octep_vf_mbox_set_offloads(oct, tx_offloads, rx_offloads);
	if (!err)
		netdev->features = features;

	return err;
}

static const struct net_device_ops octep_vf_netdev_ops = {
	.ndo_open                = octep_vf_open,
	.ndo_stop                = octep_vf_stop,
	.ndo_start_xmit          = octep_vf_start_xmit,
	.ndo_get_stats64         = octep_vf_get_stats64,
	.ndo_tx_timeout          = octep_vf_tx_timeout,
	.ndo_set_mac_address     = octep_vf_set_mac,
	.ndo_change_mtu          = octep_vf_change_mtu,
	.ndo_set_features        = octep_vf_set_features,
};

static const char *octep_vf_devid_to_str(struct octep_vf_device *oct)
{
	switch (oct->chip_id) {
	case OCTEP_PCI_DEVICE_ID_CN93_VF:
		return "CN93XX";
	case OCTEP_PCI_DEVICE_ID_CNF95N_VF:
		return "CNF95N";
	case OCTEP_PCI_DEVICE_ID_CN10KA_VF:
		return "CN10KA";
	case OCTEP_PCI_DEVICE_ID_CNF10KA_VF:
		return "CNF10KA";
	case OCTEP_PCI_DEVICE_ID_CNF10KB_VF:
		return "CNF10KB";
	case OCTEP_PCI_DEVICE_ID_CN10KB_VF:
		return "CN10KB";
	default:
		return "Unsupported";
	}
}

/**
 * octep_vf_device_setup() - Setup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Setup Octeon device hardware operations, configuration, etc ...
 */
int octep_vf_device_setup(struct octep_vf_device *oct)
{
	struct pci_dev *pdev = oct->pdev;

	/* allocate memory for oct->conf */
	oct->conf = kzalloc(sizeof(*oct->conf), GFP_KERNEL);
	if (!oct->conf)
		return -ENOMEM;

	/* Map BAR region 0 */
	oct->mmio.hw_addr = ioremap(pci_resource_start(oct->pdev, 0),
				    pci_resource_len(oct->pdev, 0));
	if (!oct->mmio.hw_addr) {
		dev_err(&pdev->dev,
			"Failed to remap BAR0; start=0x%llx len=0x%llx\n",
			pci_resource_start(oct->pdev, 0),
			pci_resource_len(oct->pdev, 0));
		goto ioremap_err;
	}
	oct->mmio.mapped = 1;

	oct->chip_id = pdev->device;
	oct->rev_id = pdev->revision;
	dev_info(&pdev->dev, "chip_id = 0x%x\n", pdev->device);

	switch (oct->chip_id) {
	case OCTEP_PCI_DEVICE_ID_CN93_VF:
	case OCTEP_PCI_DEVICE_ID_CNF95N_VF:
	case OCTEP_PCI_DEVICE_ID_CN98_VF:
		dev_info(&pdev->dev, "Setting up OCTEON %s VF PASS%d.%d\n",
			 octep_vf_devid_to_str(oct), OCTEP_VF_MAJOR_REV(oct),
			 OCTEP_VF_MINOR_REV(oct));
		octep_vf_device_setup_cn93(oct);
		break;
	case OCTEP_PCI_DEVICE_ID_CNF10KA_VF:
	case OCTEP_PCI_DEVICE_ID_CN10KA_VF:
	case OCTEP_PCI_DEVICE_ID_CNF10KB_VF:
	case OCTEP_PCI_DEVICE_ID_CN10KB_VF:
		dev_info(&pdev->dev, "Setting up OCTEON %s VF PASS%d.%d\n",
			 octep_vf_devid_to_str(oct), OCTEP_VF_MAJOR_REV(oct),
			 OCTEP_VF_MINOR_REV(oct));
		octep_vf_device_setup_cnxk(oct);
		break;
	default:
		dev_err(&pdev->dev, "Unsupported device\n");
		goto unsupported_dev;
	}

	return 0;

unsupported_dev:
	iounmap(oct->mmio.hw_addr);
ioremap_err:
	kfree(oct->conf);
	return -EOPNOTSUPP;
}

/**
 * octep_vf_device_cleanup() - Cleanup Octeon Device.
 *
 * @oct: Octeon device private data structure.
 *
 * Cleanup Octeon device allocated resources.
 */
static void octep_vf_device_cleanup(struct octep_vf_device *oct)
{
	dev_info(&oct->pdev->dev, "Cleaning up Octeon Device ...\n");

	if (oct->mmio.mapped)
		iounmap(oct->mmio.hw_addr);

	kfree(oct->conf);
	oct->conf = NULL;
}

static int octep_vf_get_mac_addr(struct octep_vf_device *oct, u8 *addr)
{
	return octep_vf_mbox_get_mac_addr(oct, addr);
}

/**
 * octep_vf_probe() - Octeon PCI device probe handler.
 *
 * @pdev: PCI device structure.
 * @ent: entry in Octeon PCI device ID table.
 *
 * Initializes and enables the Octeon PCI device for network operations.
 * Initializes Octeon private data structure and registers a network device.
 */
static int octep_vf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct octep_vf_device *octep_vf_dev;
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

	err = pci_request_mem_regions(pdev, OCTEP_VF_DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to map PCI memory regions\n");
		goto err_pci_regions;
	}

	pci_set_master(pdev);

	netdev = alloc_etherdev_mq(sizeof(struct octep_vf_device),
				   OCTEP_VF_MAX_QUEUES);
	if (!netdev) {
		dev_err(&pdev->dev, "Failed to allocate netdev\n");
		err = -ENOMEM;
		goto err_alloc_netdev;
	}
	SET_NETDEV_DEV(netdev, &pdev->dev);

	octep_vf_dev = netdev_priv(netdev);
	octep_vf_dev->netdev = netdev;
	octep_vf_dev->pdev = pdev;
	octep_vf_dev->dev = &pdev->dev;
	pci_set_drvdata(pdev, octep_vf_dev);

	err = octep_vf_device_setup(octep_vf_dev);
	if (err) {
		dev_err(&pdev->dev, "Device setup failed\n");
		goto err_octep_vf_config;
	}
	INIT_WORK(&octep_vf_dev->tx_timeout_task, octep_vf_tx_timeout_task);

	netdev->netdev_ops = &octep_vf_netdev_ops;
	netif_carrier_off(netdev);

	if (octep_vf_setup_mbox(octep_vf_dev)) {
		dev_err(&pdev->dev, "VF Mailbox setup failed\n");
		err = -ENOMEM;
		goto err_setup_mbox;
	}

	if (octep_vf_mbox_version_check(octep_vf_dev)) {
		dev_err(&pdev->dev, "PF VF Mailbox version mismatch\n");
		err = -EINVAL;
		goto err_mbox_version;
	}

	if (octep_vf_mbox_get_fw_info(octep_vf_dev)) {
		dev_err(&pdev->dev, "unable to get fw info\n");
		err = -EINVAL;
		goto err_mbox_version;
	}

	netdev->hw_features = NETIF_F_SG;
	if (OCTEP_VF_TX_IP_CSUM(octep_vf_dev->fw_info.tx_ol_flags))
		netdev->hw_features |= (NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM);

	if (OCTEP_VF_RX_IP_CSUM(octep_vf_dev->fw_info.rx_ol_flags))
		netdev->hw_features |= NETIF_F_RXCSUM;

	netdev->min_mtu = OCTEP_VF_MIN_MTU;
	netdev->max_mtu = OCTEP_VF_MAX_MTU;
	netdev->mtu = OCTEP_VF_DEFAULT_MTU;

	if (OCTEP_VF_TX_TSO(octep_vf_dev->fw_info.tx_ol_flags)) {
		netdev->hw_features |= NETIF_F_TSO;
		netif_set_tso_max_size(netdev, netdev->max_mtu);
	}

	netdev->features |= netdev->hw_features;
	octep_vf_get_mac_addr(octep_vf_dev, octep_vf_dev->mac_addr);
	eth_hw_addr_set(netdev, octep_vf_dev->mac_addr);
	err = register_netdev(netdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_register_dev;
	}
	dev_info(&pdev->dev, "Device probe successful\n");
	return 0;

err_register_dev:
err_mbox_version:
	octep_vf_delete_mbox(octep_vf_dev);
err_setup_mbox:
	octep_vf_device_cleanup(octep_vf_dev);
err_octep_vf_config:
	free_netdev(netdev);
err_alloc_netdev:
	pci_release_mem_regions(pdev);
err_pci_regions:
err_dma_mask:
	pci_disable_device(pdev);
	dev_err(&pdev->dev, "Device probe failed\n");
	return err;
}

/**
 * octep_vf_remove() - Remove Octeon PCI device from driver control.
 *
 * @pdev: PCI device structure of the Octeon device.
 *
 * Cleanup all resources allocated for the Octeon device.
 * Unregister from network device and disable the PCI device.
 */
static void octep_vf_remove(struct pci_dev *pdev)
{
	struct octep_vf_device *oct = pci_get_drvdata(pdev);
	struct net_device *netdev;

	if (!oct)
		return;

	octep_vf_mbox_dev_remove(oct);
	cancel_work_sync(&oct->tx_timeout_task);
	netdev = oct->netdev;
	if (netdev->reg_state == NETREG_REGISTERED)
		unregister_netdev(netdev);
	octep_vf_delete_mbox(oct);
	octep_vf_device_cleanup(oct);
	pci_release_mem_regions(pdev);
	free_netdev(netdev);
	pci_disable_device(pdev);
}

static struct pci_driver octep_vf_driver = {
	.name = OCTEP_VF_DRV_NAME,
	.id_table = octep_vf_pci_id_tbl,
	.probe = octep_vf_probe,
	.remove = octep_vf_remove,
};

/**
 * octep_vf_init_module() - Module initialization.
 *
 * create common resource for the driver and register PCI driver.
 */
static int __init octep_vf_init_module(void)
{
	int ret;

	pr_info("%s: Loading %s ...\n", OCTEP_VF_DRV_NAME, OCTEP_VF_DRV_STRING);

	/* work queue for all deferred tasks */
	octep_vf_wq = create_singlethread_workqueue(OCTEP_VF_DRV_NAME);
	if (!octep_vf_wq) {
		pr_err("%s: Failed to create common workqueue\n",
		       OCTEP_VF_DRV_NAME);
		return -ENOMEM;
	}

	ret = pci_register_driver(&octep_vf_driver);
	if (ret < 0) {
		pr_err("%s: Failed to register PCI driver; err=%d\n",
		       OCTEP_VF_DRV_NAME, ret);
		return ret;
	}

	pr_info("%s: Loaded successfully !\n", OCTEP_VF_DRV_NAME);

	return ret;
}

/**
 * octep_vf_exit_module() - Module exit routine.
 *
 * unregister the driver with PCI subsystem and cleanup common resources.
 */
static void __exit octep_vf_exit_module(void)
{
	pr_info("%s: Unloading ...\n", OCTEP_VF_DRV_NAME);

	pci_unregister_driver(&octep_vf_driver);
	destroy_workqueue(octep_vf_wq);

	pr_info("%s: Unloading complete\n", OCTEP_VF_DRV_NAME);
}

module_init(octep_vf_init_module);
module_exit(octep_vf_exit_module);
