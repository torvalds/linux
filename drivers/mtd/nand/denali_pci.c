/*
 * NAND Flash Controller Device Driver
 * Copyright © 2009-2010, Intel Corporation and its suppliers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "denali.h"

#define DENALI_NAND_NAME    "denali-nand-pci"

/* List of platforms this NAND controller has be integrated into */
static const struct pci_device_id denali_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0701), INTEL_CE4100 },
	{ PCI_VDEVICE(INTEL, 0x0809), INTEL_MRST },
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, denali_pci_ids);

static int denali_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	int ret;
	resource_size_t csr_base, mem_base;
	unsigned long csr_len, mem_len;
	struct denali_nand_info *denali;

	denali = devm_kzalloc(&dev->dev, sizeof(*denali), GFP_KERNEL);
	if (!denali)
		return -ENOMEM;

	ret = pcim_enable_device(dev);
	if (ret) {
		dev_err(&dev->dev, "Spectra: pci_enable_device failed.\n");
		return ret;
	}

	if (id->driver_data == INTEL_CE4100) {
		denali->platform = INTEL_CE4100;
		mem_base = pci_resource_start(dev, 0);
		mem_len = pci_resource_len(dev, 1);
		csr_base = pci_resource_start(dev, 1);
		csr_len = pci_resource_len(dev, 1);
	} else {
		denali->platform = INTEL_MRST;
		csr_base = pci_resource_start(dev, 0);
		csr_len = pci_resource_len(dev, 0);
		mem_base = pci_resource_start(dev, 1);
		mem_len = pci_resource_len(dev, 1);
		if (!mem_len) {
			mem_base = csr_base + csr_len;
			mem_len = csr_len;
		}
	}

	pci_set_master(dev);
	denali->dev = &dev->dev;
	denali->irq = dev->irq;

	ret = pci_request_regions(dev, DENALI_NAND_NAME);
	if (ret) {
		dev_err(&dev->dev, "Spectra: Unable to request memory regions\n");
		return ret;
	}

	denali->flash_reg = ioremap_nocache(csr_base, csr_len);
	if (!denali->flash_reg) {
		dev_err(&dev->dev, "Spectra: Unable to remap memory region\n");
		return -ENOMEM;
	}

	denali->flash_mem = ioremap_nocache(mem_base, mem_len);
	if (!denali->flash_mem) {
		dev_err(&dev->dev, "Spectra: ioremap_nocache failed!");
		ret = -ENOMEM;
		goto failed_remap_reg;
	}

	ret = denali_init(denali);
	if (ret)
		goto failed_remap_mem;

	pci_set_drvdata(dev, denali);

	return 0;

failed_remap_mem:
	iounmap(denali->flash_mem);
failed_remap_reg:
	iounmap(denali->flash_reg);
	return ret;
}

/* driver exit point */
static void denali_pci_remove(struct pci_dev *dev)
{
	struct denali_nand_info *denali = pci_get_drvdata(dev);

	denali_remove(denali);
	iounmap(denali->flash_reg);
	iounmap(denali->flash_mem);
}

static struct pci_driver denali_pci_driver = {
	.name = DENALI_NAND_NAME,
	.id_table = denali_pci_ids,
	.probe = denali_pci_probe,
	.remove = denali_pci_remove,
};

module_pci_driver(denali_pci_driver);

MODULE_DESCRIPTION("PCI driver for Denali NAND controller");
MODULE_AUTHOR("Intel Corporation and its suppliers");
MODULE_LICENSE("GPL v2");
