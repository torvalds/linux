// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD SPI controller driver
 *
 * Copyright (c) 2025, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Authors: Krishnamoorthi M <krishnamoorthi.m@amd.com>
 *          Akshata MukundShetty <akshata.mukundshetty@amd.com>
 */

#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/pci.h>

#include "spi-amd.h"

#define AMD_PCI_DEVICE_ID_LPC_BRIDGE		0x1682
#define AMD_PCI_LPC_SPI_BASE_ADDR_REG		0xA0
#define AMD_SPI_BASE_ADDR_MASK			~0xFF
#define AMD_HID2_PCI_BAR_OFFSET			0x00002000
#define AMD_HID2_MEM_SIZE			0x200

static struct pci_device_id pci_spi_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, AMD_PCI_DEVICE_ID_LPC_BRIDGE) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pci_spi_ids);

static int amd_spi_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct spi_controller *host;
	struct amd_spi *amd_spi;
	u32 io_base_addr;

	/* Allocate storage for host and driver private data */
	host = devm_spi_alloc_host(dev, sizeof(struct amd_spi));
	if (!host)
		return -ENOMEM;

	amd_spi = spi_controller_get_devdata(host);

	pci_read_config_dword(pdev, AMD_PCI_LPC_SPI_BASE_ADDR_REG, &io_base_addr);
	io_base_addr = (io_base_addr & AMD_SPI_BASE_ADDR_MASK) + AMD_HID2_PCI_BAR_OFFSET;
	amd_spi->io_remap_addr = devm_ioremap(dev, io_base_addr, AMD_HID2_MEM_SIZE);

	if (!amd_spi->io_remap_addr)
		return -ENOMEM;

	dev_dbg(dev, "io_remap_address: %p\n", amd_spi->io_remap_addr);

	amd_spi->version = AMD_HID2_SPI;
	host->bus_num = 2;

	return amd_spi_probe_common(dev, host);
}

static struct pci_driver amd_spi_pci_driver = {
	.name = "amd_spi_pci",
	.id_table = pci_spi_ids,
	.probe = amd_spi_pci_probe,
};

module_pci_driver(amd_spi_pci_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AMD HID2 SPI Controller Driver");
