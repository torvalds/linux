// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 - 2022 Beijing WangXun Technology Co., Ltd. */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/aer.h>
#include <linux/etherdevice.h>

#include "ngbe.h"
char ngbe_driver_name[] = "ngbe";

/* ngbe_pci_tbl - PCI Device ID Table
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id ngbe_pci_tbl[] = {
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL_W), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A2), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A2S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A4), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A4S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL2), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL2S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL4), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860AL4S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860LC), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A1), 0},
	{ PCI_VDEVICE(WANGXUN, NGBE_DEV_ID_EM_WX1860A1L), 0},
	/* required last entry */
	{ .device = 0 }
};

static void ngbe_dev_shutdown(struct pci_dev *pdev, bool *enable_wake)
{
	struct ngbe_adapter *adapter = pci_get_drvdata(pdev);
	struct net_device *netdev = adapter->netdev;

	netif_device_detach(netdev);

	pci_disable_device(pdev);
}

static void ngbe_shutdown(struct pci_dev *pdev)
{
	bool wake;

	ngbe_dev_shutdown(pdev, &wake);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, wake);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

/**
 * ngbe_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in ngbe_pci_tbl
 *
 * Returns 0 on success, negative on failure
 *
 * ngbe_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int ngbe_probe(struct pci_dev *pdev,
		      const struct pci_device_id __always_unused *ent)
{
	struct ngbe_adapter *adapter = NULL;
	struct net_device *netdev;
	int err;

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev,
			"No usable DMA configuration, aborting\n");
		goto err_pci_disable_dev;
	}

	err = pci_request_selected_regions(pdev,
					   pci_select_bars(pdev, IORESOURCE_MEM),
					   ngbe_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed %d\n", err);
		goto err_pci_disable_dev;
	}

	pci_enable_pcie_error_reporting(pdev);
	pci_set_master(pdev);

	netdev = devm_alloc_etherdev_mqs(&pdev->dev,
					 sizeof(struct ngbe_adapter),
					 NGBE_MAX_TX_QUEUES,
					 NGBE_MAX_RX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_pci_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	adapter = netdev_priv(netdev);
	adapter->netdev = netdev;
	adapter->pdev = pdev;

	adapter->io_addr = devm_ioremap(&pdev->dev,
					pci_resource_start(pdev, 0),
					pci_resource_len(pdev, 0));
	if (!adapter->io_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	netdev->features |= NETIF_F_HIGHDMA;

	pci_set_drvdata(pdev, adapter);

	return 0;

err_pci_release_regions:
	pci_disable_pcie_error_reporting(pdev);
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * ngbe_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * ngbe_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void ngbe_remove(struct pci_dev *pdev)
{
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));

	pci_disable_pcie_error_reporting(pdev);

	pci_disable_device(pdev);
}

static struct pci_driver ngbe_driver = {
	.name     = ngbe_driver_name,
	.id_table = ngbe_pci_tbl,
	.probe    = ngbe_probe,
	.remove   = ngbe_remove,
	.shutdown = ngbe_shutdown,
};

module_pci_driver(ngbe_driver);

MODULE_DEVICE_TABLE(pci, ngbe_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@net-swift.com>");
MODULE_DESCRIPTION("WangXun(R) Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
