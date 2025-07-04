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
#include "ngbevf_type.h"

/* ngbevf_pci_tbl - PCI Device ID Table
 *
 * Wildcard entries (PCI_ANY_ID) should come last
 * Last entry must be all 0s
 *
 * { Vendor ID, Device ID, SubVendor ID, SubDevice ID,
 *   Class, Class Mask, private data (not used) }
 */
static const struct pci_device_id ngbevf_pci_tbl[] = {
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860AL_W), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860A2), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860A2S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860A4), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860A4S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860AL2), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860AL2S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860AL4), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860AL4S), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860NCSI), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860A1), 0},
	{ PCI_VDEVICE(WANGXUN, NGBEVF_DEV_ID_EM_WX1860AL1), 0},
	/* required last entry */
	{ .device = 0 }
};

/**
 * ngbevf_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in ngbevf_pci_tbl
 *
 * Return: return 0 on success, negative on failure
 *
 * ngbevf_probe initializes an adapter identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 **/
static int ngbevf_probe(struct pci_dev *pdev,
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
					 NGBEVF_MAX_TX_QUEUES,
					 NGBEVF_MAX_RX_QUEUES);
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
 * ngbevf_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * ngbevf_remove is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.  The could be caused by a
 * Hot-Plug event, or because the driver is going to be removed from
 * memory.
 **/
static void ngbevf_remove(struct pci_dev *pdev)
{
	wxvf_remove(pdev);
}

static DEFINE_SIMPLE_DEV_PM_OPS(ngbevf_pm_ops, wxvf_suspend, wxvf_resume);

static struct pci_driver ngbevf_driver = {
	.name     = KBUILD_MODNAME,
	.id_table = ngbevf_pci_tbl,
	.probe    = ngbevf_probe,
	.remove   = ngbevf_remove,
	.shutdown = wxvf_shutdown,
	/* Power Management Hooks */
	.driver.pm	= pm_sleep_ptr(&ngbevf_pm_ops)
};

module_pci_driver(ngbevf_driver);

MODULE_DEVICE_TABLE(pci, ngbevf_pci_tbl);
MODULE_AUTHOR("Beijing WangXun Technology Co., Ltd, <software@trustnetic.com>");
MODULE_DESCRIPTION("WangXun(R) Gigabit PCI Express Network Driver");
MODULE_LICENSE("GPL");
