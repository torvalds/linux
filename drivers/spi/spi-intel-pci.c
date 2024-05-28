// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel PCH/PCU SPI flash PCI driver.
 *
 * Copyright (C) 2016 - 2022, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "spi-intel.h"

#define BCR		0xdc
#define BCR_WPD		BIT(0)

static bool intel_spi_pci_set_writeable(void __iomem *base, void *data)
{
	struct pci_dev *pdev = data;
	u32 bcr;

	/* Try to make the chip read/write */
	pci_read_config_dword(pdev, BCR, &bcr);
	if (!(bcr & BCR_WPD)) {
		bcr |= BCR_WPD;
		pci_write_config_dword(pdev, BCR, bcr);
		pci_read_config_dword(pdev, BCR, &bcr);
	}

	return bcr & BCR_WPD;
}

static const struct intel_spi_boardinfo bxt_info = {
	.type = INTEL_SPI_BXT,
	.set_writeable = intel_spi_pci_set_writeable,
};

static const struct intel_spi_boardinfo cnl_info = {
	.type = INTEL_SPI_CNL,
	.set_writeable = intel_spi_pci_set_writeable,
};

static int intel_spi_pci_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	struct intel_spi_boardinfo *info;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	info = devm_kmemdup(&pdev->dev, (void *)id->driver_data, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->data = pdev;
	return intel_spi_probe(&pdev->dev, &pdev->resource[0], info);
}

static const struct pci_device_id intel_spi_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x02a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x06a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x18e0), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0x19e0), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0x1bca), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0x34a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x38a4), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0x43a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x4b24), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0x4da4), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0x51a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x54a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x5794), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x7a24), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x7aa4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x7e23), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x7f24), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x9d24), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0x9da4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0xa0a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0xa1a4), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0xa224), (unsigned long)&bxt_info },
	{ PCI_VDEVICE(INTEL, 0xa2a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0xa324), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0xa3a4), (unsigned long)&cnl_info },
	{ PCI_VDEVICE(INTEL, 0xa823), (unsigned long)&cnl_info },
	{ },
};
MODULE_DEVICE_TABLE(pci, intel_spi_pci_ids);

static struct pci_driver intel_spi_pci_driver = {
	.name = "intel-spi",
	.id_table = intel_spi_pci_ids,
	.probe = intel_spi_pci_probe,
};

module_pci_driver(intel_spi_pci_driver);

MODULE_DESCRIPTION("Intel PCH/PCU SPI flash PCI driver");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_LICENSE("GPL v2");
