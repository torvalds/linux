// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/etherdevice.h>

#include "../libwx/wx_type.h"
#include "../libwx/wx_vf_common.h"
#include "txgbevf_type.h"

/* txgbevf_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id txgbevf_pci_tbl[] = {
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_SP1000), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_WX1820), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_AML500F), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_AML510F), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_AML5024), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_AML5124), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_AML503F), 0},
	{ PCI_VDEVICE(WANGXUN, TXGBEVF_DEV_ID_AML513F), 0},
	/* required last entry */
	{ .device = 0 }
};

/**
 * txgbevf_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in txgbevf_pci_tbl
 *
 * Return: return 0 on success, negative on failure
 *
 * txgbevf_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int txgbevf_probe(struct pci_dev *pdev,
			 const struct pci_device_id __always_unused *ent)
{
	struct net_device *netdev;
	struct wx *wx = NULL;
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
					   dev_driver_string(&pdev->dev));
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed 0x%x\n", err);
		goto err_pci_disable_dev;
	}

	pci_set_master(pdev);

	netdev = devm_alloc_etherdev_mqs(&pdev->dev,
					 sizeof(struct wx),
					 TXGBEVF_MAX_TX_QUEUES,
					 TXGBEVF_MAX_RX_QUEUES);
	if (!netdev) {
		err = -ENOMEM;
		goto err_pci_release_regions;
	}

	SET_NETDEV_DEV(netdev, &pdev->dev);

	wx = netdev_priv(netdev);
	wx->netdev = netdev;
	wx->pdev = pdev;

	wx->msg_enable = netif_msg_init(-1, NETIF_MSG_DRV |
					NETIF_MSG_PROBE | NETIF_MSG_LINK);
	wx->hw_addr = devm_ioremap(&pdev->dev,
				   pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
	if (!wx->hw_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	wx->b4_addr = devm_ioremap(&pdev->dev,
				   pci_resource_start(pdev, 4),
				   pci_resource_len(pdev, 4));
	if (!wx->b4_addr) {
		err = -EIO;
		goto err_pci_release_regions;
	}

	netdev->features |= NETIF_F_HIGHDMA;

	pci_set_drvdata(pdev, wx);

	return 0;

err_pci_release_regions:
	pci_release_selected_regions(pdev,
				     pci_select_bars(pdev, IORESOURCE_MEM));
err_pci_disable_dev:
	pci_disable_device(pdev);
	return err;
}

/**
 * txgbevf_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * txgbevf_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void txgbevf_remove(struct pci_dev *pdev)
{
	wxvf_remove(pdev);
}

static DEFINE_SIMPLE_DEV_PM_OPS(txgbevf_pm_ops, wxvf_suspend, wxvf_resume);

static struct pci_driver txgbevf_driver = {
	.name     = KBUILD_MODNAME,
	.id_table = txgbevf_pci_tbl,
	.probe    = txgbevf_probe,
	.remove   = txgbevf_remove,
	.shutdown = wxvf_shutdown,
	/* Power Management Hooks */
	.driver.pm	= pm_sleep_ptr(&txgbevf_pm_ops)
};

module_pci_driver(txgbevf_driver);

MODULE_DEVICE_TABLE(pci, txgbevf_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@trustnetic.com>");
MODULE_DESCRIPTION("WangXun(R) 10/25/40 Gigabit Virtual Function Network Driver");
MODULE_LICENSE("GPL");
