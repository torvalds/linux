// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 Broadcom Corporation
 * Copyright (C) 2015 Hauke Mehrtens <hauke@hauke-m.de>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/phy/phy.h>
#include <linux/bcma/bcma.h>
#include <linux/ioport.h>

#include "pcie-iproc.h"


/* NS: CLASS field is R/O, and set to wrong 0x200 value */
static void bcma_pcie2_fixup_class(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0x8011, bcma_pcie2_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0x8012, bcma_pcie2_fixup_class);

static int iproc_pcie_bcma_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	struct iproc_pcie *pcie = dev->sysdata;
	struct bcma_device *bdev = container_of(pcie->dev, struct bcma_device, dev);

	return bcma_core_irq(bdev, 5);
}

static int iproc_pcie_bcma_probe(struct bcma_device *bdev)
{
	struct device *dev = &bdev->dev;
	struct iproc_pcie *pcie;
	LIST_HEAD(resources);
	struct pci_host_bridge *bridge;
	int ret;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);

	pcie->dev = dev;

	pcie->type = IPROC_PCIE_PAXB_BCMA;
	pcie->base = bdev->io_addr;
	if (!pcie->base) {
		dev_err(dev, "no controller registers\n");
		return -ENOMEM;
	}

	pcie->base_addr = bdev->addr;

	pcie->mem.start = bdev->addr_s[0];
	pcie->mem.end = bdev->addr_s[0] + SZ_128M - 1;
	pcie->mem.name = "PCIe MEM space";
	pcie->mem.flags = IORESOURCE_MEM;
	pci_add_resource(&resources, &pcie->mem);

	pcie->map_irq = iproc_pcie_bcma_map_irq;

	ret = iproc_pcie_setup(pcie, &resources);
	if (ret) {
		dev_err(dev, "PCIe controller setup failed\n");
		pci_free_resource_list(&resources);
		return ret;
	}

	bcma_set_drvdata(bdev, pcie);
	return 0;
}

static void iproc_pcie_bcma_remove(struct bcma_device *bdev)
{
	struct iproc_pcie *pcie = bcma_get_drvdata(bdev);

	iproc_pcie_remove(pcie);
}

static const struct bcma_device_id iproc_pcie_bcma_table[] = {
	BCMA_CORE(BCMA_MANUF_BCM, BCMA_CORE_NS_PCIEG2, BCMA_ANY_REV, BCMA_ANY_CLASS),
	{},
};
MODULE_DEVICE_TABLE(bcma, iproc_pcie_bcma_table);

static struct bcma_driver iproc_pcie_bcma_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= iproc_pcie_bcma_table,
	.probe		= iproc_pcie_bcma_probe,
	.remove		= iproc_pcie_bcma_remove,
};
module_bcma_driver(iproc_pcie_bcma_driver);

MODULE_AUTHOR("Hauke Mehrtens");
MODULE_DESCRIPTION("Broadcom iProc PCIe BCMA driver");
MODULE_LICENSE("GPL v2");
