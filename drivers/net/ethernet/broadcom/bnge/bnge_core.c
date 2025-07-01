// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2025 Broadcom.

#include <linux/init.h>
#include <linux/crash_dump.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "bnge.h"
#include "bnge_devlink.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRV_SUMMARY);

char bnge_driver_name[] = DRV_NAME;

static const struct {
	char *name;
} board_info[] = {
	[BCM57708] = { "Broadcom BCM57708 50Gb/100Gb/200Gb/400Gb/800Gb Ethernet" },
};

static const struct pci_device_id bnge_pci_tbl[] = {
	{ PCI_VDEVICE(BROADCOM, 0x1780), .driver_data = BCM57708 },
	/* Required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, bnge_pci_tbl);

static void bnge_print_device_info(struct pci_dev *pdev, enum board_idx idx)
{
	struct device *dev = &pdev->dev;

	dev_info(dev, "%s found at mem %lx\n", board_info[idx].name,
		 (long)pci_resource_start(pdev, 0));

	pcie_print_link_status(pdev);
}

static void bnge_pci_disable(struct pci_dev *pdev)
{
	pci_release_regions(pdev);
	if (pci_is_enabled(pdev))
		pci_disable_device(pdev);
}

static int bnge_pci_enable(struct pci_dev *pdev)
{
	int rc;

	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting\n");
		return rc;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev,
			"Cannot find PCI device base address, aborting\n");
		rc = -ENODEV;
		goto err_pci_disable;
	}

	rc = pci_request_regions(pdev, bnge_driver_name);
	if (rc) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, aborting\n");
		goto err_pci_disable;
	}

	dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));

	pci_set_master(pdev);

	return 0;

err_pci_disable:
	pci_disable_device(pdev);
	return rc;
}

static void bnge_unmap_bars(struct pci_dev *pdev)
{
	struct bnge_dev *bd = pci_get_drvdata(pdev);

	if (bd->bar0) {
		pci_iounmap(pdev, bd->bar0);
		bd->bar0 = NULL;
	}
}

static int bnge_probe_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct bnge_dev *bd;
	int rc;

	if (pci_is_bridge(pdev))
		return -ENODEV;

	if (!pdev->msix_cap) {
		dev_err(&pdev->dev, "MSIX capability missing, aborting\n");
		return -ENODEV;
	}

	if (is_kdump_kernel()) {
		pci_clear_master(pdev);
		pcie_flr(pdev);
	}

	rc = bnge_pci_enable(pdev);
	if (rc)
		return rc;

	bnge_print_device_info(pdev, ent->driver_data);

	bd = bnge_devlink_alloc(pdev);
	if (!bd) {
		dev_err(&pdev->dev, "Devlink allocation failed\n");
		rc = -ENOMEM;
		goto err_pci_disable;
	}

	bd->bar0 = pci_ioremap_bar(pdev, 0);
	if (!bd->bar0) {
		dev_err(&pdev->dev, "Failed mapping BAR-0, aborting\n");
		rc = -ENOMEM;
		goto err_devl_free;
	}

	pci_save_state(pdev);

	return 0;

err_devl_free:
	bnge_devlink_free(bd);

err_pci_disable:
	bnge_pci_disable(pdev);
	return rc;
}

static void bnge_remove_one(struct pci_dev *pdev)
{
	struct bnge_dev *bd = pci_get_drvdata(pdev);

	bnge_unmap_bars(pdev);

	bnge_devlink_free(bd);

	bnge_pci_disable(pdev);
}

static void bnge_shutdown(struct pci_dev *pdev)
{
	pci_disable_device(pdev);

	if (system_state == SYSTEM_POWER_OFF) {
		pci_wake_from_d3(pdev, 0);
		pci_set_power_state(pdev, PCI_D3hot);
	}
}

static struct pci_driver bnge_driver = {
	.name		= bnge_driver_name,
	.id_table	= bnge_pci_tbl,
	.probe		= bnge_probe_one,
	.remove		= bnge_remove_one,
	.shutdown	= bnge_shutdown,
};

static int __init bnge_init_module(void)
{
	return pci_register_driver(&bnge_driver);
}
module_init(bnge_init_module);

static void __exit bnge_exit_module(void)
{
	pci_unregister_driver(&bnge_driver);
}
module_exit(bnge_exit_module);
