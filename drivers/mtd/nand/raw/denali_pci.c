// SPDX-License-Identifier: GPL-2.0
/*
 * NAND Flash Controller Device Driver
 * Copyright © 2009-2010, Intel Corporation and its suppliers.
 */

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "denali.h"

#define DENALI_NAND_NAME    "denali-nand-pci"

#define INTEL_CE4100	1
#define INTEL_MRST	2

/* List of platforms this NAND controller has be integrated into */
static const struct pci_device_id denali_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0701), INTEL_CE4100 },
	{ PCI_VDEVICE(INTEL, 0x0809), INTEL_MRST },
	{ /* end: all zeroes */ }
};
MODULE_DEVICE_TABLE(pci, denali_pci_ids);

NAND_ECC_CAPS_SINGLE(denali_pci_ecc_caps, denali_calc_ecc_bytes, 512, 8, 15);

static int denali_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	resource_size_t csr_base, mem_base;
	unsigned long csr_len, mem_len;
	struct denali_controller *denali;
	struct denali_chip *dchip;
	int nsels, ret, i;

	denali = devm_kzalloc(&dev->dev, sizeof(*denali), GFP_KERNEL);
	if (!denali)
		return -ENOMEM;

	ret = pcim_enable_device(dev);
	if (ret) {
		dev_err(&dev->dev, "Spectra: pci_enable_device failed.\n");
		return ret;
	}

	if (id->driver_data == INTEL_CE4100) {
		mem_base = pci_resource_start(dev, 0);
		mem_len = pci_resource_len(dev, 1);
		csr_base = pci_resource_start(dev, 1);
		csr_len = pci_resource_len(dev, 1);
	} else {
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
	denali->ecc_caps = &denali_pci_ecc_caps;
	denali->clk_rate = 50000000;		/* 50 MHz */
	denali->clk_x_rate = 200000000;		/* 200 MHz */

	ret = pci_request_regions(dev, DENALI_NAND_NAME);
	if (ret) {
		dev_err(&dev->dev, "Spectra: Unable to request memory regions\n");
		return ret;
	}

	denali->reg = ioremap(csr_base, csr_len);
	if (!denali->reg) {
		dev_err(&dev->dev, "Spectra: Unable to remap memory region\n");
		return -ENOMEM;
	}

	denali->host = ioremap(mem_base, mem_len);
	if (!denali->host) {
		dev_err(&dev->dev, "Spectra: ioremap failed!");
		ret = -ENOMEM;
		goto out_unmap_reg;
	}

	ret = denali_init(denali);
	if (ret)
		goto out_unmap_host;

	nsels = denali->nbanks;

	dchip = devm_kzalloc(denali->dev, struct_size(dchip, sels, nsels),
			     GFP_KERNEL);
	if (!dchip) {
		ret = -ENOMEM;
		goto out_remove_denali;
	}

	dchip->chip.ecc.options |= NAND_ECC_MAXIMIZE;

	dchip->nsels = nsels;

	for (i = 0; i < nsels; i++)
		dchip->sels[i].bank = i;

	ret = denali_chip_init(denali, dchip);
	if (ret)
		goto out_remove_denali;

	pci_set_drvdata(dev, denali);

	return 0;

out_remove_denali:
	denali_remove(denali);
out_unmap_host:
	iounmap(denali->host);
out_unmap_reg:
	iounmap(denali->reg);
	return ret;
}

static void denali_pci_remove(struct pci_dev *dev)
{
	struct denali_controller *denali = pci_get_drvdata(dev);

	denali_remove(denali);
	iounmap(denali->reg);
	iounmap(denali->host);
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
