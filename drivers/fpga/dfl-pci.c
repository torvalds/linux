// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Device Feature List (DFL) PCIe device
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Zhang Yi <Yi.Z.Zhang@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/errno.h>
#include <linux/aer.h>

#define DRV_VERSION	"0.8"
#define DRV_NAME	"dfl-pci"

/* PCI Device ID */
#define PCIE_DEVICE_ID_PF_INT_5_X	0xBCBD
#define PCIE_DEVICE_ID_PF_INT_6_X	0xBCC0
#define PCIE_DEVICE_ID_PF_DSC_1_X	0x09C4
/* VF Device */
#define PCIE_DEVICE_ID_VF_INT_5_X	0xBCBF
#define PCIE_DEVICE_ID_VF_INT_6_X	0xBCC1
#define PCIE_DEVICE_ID_VF_DSC_1_X	0x09C5

static struct pci_device_id cci_pcie_id_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCIE_DEVICE_ID_PF_INT_5_X),},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCIE_DEVICE_ID_VF_INT_5_X),},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCIE_DEVICE_ID_PF_INT_6_X),},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCIE_DEVICE_ID_VF_INT_6_X),},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCIE_DEVICE_ID_PF_DSC_1_X),},
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCIE_DEVICE_ID_VF_DSC_1_X),},
	{0,}
};
MODULE_DEVICE_TABLE(pci, cci_pcie_id_tbl);

static
int cci_pci_probe(struct pci_dev *pcidev, const struct pci_device_id *pcidevid)
{
	int ret;

	ret = pcim_enable_device(pcidev);
	if (ret < 0) {
		dev_err(&pcidev->dev, "Failed to enable device %d.\n", ret);
		return ret;
	}

	ret = pci_enable_pcie_error_reporting(pcidev);
	if (ret && ret != -EINVAL)
		dev_info(&pcidev->dev, "PCIE AER unavailable %d.\n", ret);

	pci_set_master(pcidev);

	if (!pci_set_dma_mask(pcidev, DMA_BIT_MASK(64))) {
		ret = pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(64));
		if (ret)
			goto disable_error_report_exit;
	} else if (!pci_set_dma_mask(pcidev, DMA_BIT_MASK(32))) {
		ret = pci_set_consistent_dma_mask(pcidev, DMA_BIT_MASK(32));
		if (ret)
			goto disable_error_report_exit;
	} else {
		ret = -EIO;
		dev_err(&pcidev->dev, "No suitable DMA support available.\n");
		goto disable_error_report_exit;
	}

	/* TODO: create and add the platform device per feature list */
	return 0;

disable_error_report_exit:
	pci_disable_pcie_error_reporting(pcidev);
	return ret;
}

static void cci_pci_remove(struct pci_dev *pcidev)
{
	pci_disable_pcie_error_reporting(pcidev);
}

static struct pci_driver cci_pci_driver = {
	.name = DRV_NAME,
	.id_table = cci_pcie_id_tbl,
	.probe = cci_pci_probe,
	.remove = cci_pci_remove,
};

module_pci_driver(cci_pci_driver);

MODULE_DESCRIPTION("FPGA DFL PCIe Device Driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
