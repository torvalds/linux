// SPDX-License-Identifier: GPL-2.0
/*
 * UIO driver for Hilscher NetX based fieldbus cards (cifX, comX).
 * See http://www.hilscher.com for details.
 *
 * (C) 2007 Hans J. Koch <hjk@hansjkoch.de>
 * (C) 2008 Manuel Traut <manut@linutronix.de>
 *
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uio_driver.h>

#define PCI_VENDOR_ID_HILSCHER		0x15CF
#define PCI_DEVICE_ID_HILSCHER_NETX	0x0000
#define PCI_DEVICE_ID_HILSCHER_NETPLC	0x0010
#define PCI_SUBDEVICE_ID_NETPLC_RAM	0x0000
#define PCI_SUBDEVICE_ID_NETPLC_FLASH	0x0001
#define PCI_SUBDEVICE_ID_NXSB_PCA	0x3235
#define PCI_SUBDEVICE_ID_NXPCA		0x3335

#define DPM_HOST_INT_EN0	0xfff0
#define DPM_HOST_INT_STAT0	0xffe0

#define DPM_HOST_INT_MASK	0xe600ffff
#define DPM_HOST_INT_GLOBAL_EN	0x80000000

static irqreturn_t netx_handler(int irq, struct uio_info *dev_info)
{
	void __iomem *int_enable_reg = dev_info->mem[0].internal_addr
					+ DPM_HOST_INT_EN0;
	void __iomem *int_status_reg = dev_info->mem[0].internal_addr
					+ DPM_HOST_INT_STAT0;

	/* Is one of our interrupts enabled and active ? */
	if (!(ioread32(int_enable_reg) & ioread32(int_status_reg)
		& DPM_HOST_INT_MASK))
		return IRQ_NONE;

	/* Disable interrupt */
	iowrite32(ioread32(int_enable_reg) & ~DPM_HOST_INT_GLOBAL_EN,
		int_enable_reg);
	return IRQ_HANDLED;
}

static int netx_pci_probe(struct pci_dev *dev,
					const struct pci_device_id *id)
{
	struct uio_info *info;
	int bar;

	info = devm_kzalloc(&dev->dev, sizeof(struct uio_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (pci_enable_device(dev))
		return -ENODEV;

	if (pci_request_regions(dev, "netx"))
		goto out_disable;

	switch (id->device) {
	case PCI_DEVICE_ID_HILSCHER_NETX:
		bar = 0;
		info->name = "netx";
		break;
	case PCI_DEVICE_ID_HILSCHER_NETPLC:
		bar = 0;
		info->name = "netplc";
		break;
	default:
		bar = 2;
		info->name = "netx_plx";
	}

	/* BAR0 or 2 points to the card's dual port memory */
	info->mem[0].addr = pci_resource_start(dev, bar);
	if (!info->mem[0].addr)
		goto out_release;
	info->mem[0].internal_addr = ioremap(pci_resource_start(dev, bar),
						pci_resource_len(dev, bar));

	if (!info->mem[0].internal_addr)
			goto out_release;

	info->mem[0].size = pci_resource_len(dev, bar);
	info->mem[0].memtype = UIO_MEM_PHYS;
	info->irq = dev->irq;
	info->irq_flags = IRQF_SHARED;
	info->handler = netx_handler;
	info->version = "0.0.1";

	/* Make sure all interrupts are disabled */
	iowrite32(0, info->mem[0].internal_addr + DPM_HOST_INT_EN0);

	if (uio_register_device(&dev->dev, info))
		goto out_unmap;

	pci_set_drvdata(dev, info);
	dev_info(&dev->dev, "Found %s card, registered UIO device.\n",
				info->name);

	return 0;

out_unmap:
	iounmap(info->mem[0].internal_addr);
out_release:
	pci_release_regions(dev);
out_disable:
	pci_disable_device(dev);
	return -ENODEV;
}

static void netx_pci_remove(struct pci_dev *dev)
{
	struct uio_info *info = pci_get_drvdata(dev);

	/* Disable all interrupts */
	iowrite32(0, info->mem[0].internal_addr + DPM_HOST_INT_EN0);
	uio_unregister_device(info);
	pci_release_regions(dev);
	pci_disable_device(dev);
	iounmap(info->mem[0].internal_addr);
}

static struct pci_device_id netx_pci_ids[] = {
	{
		.vendor =	PCI_VENDOR_ID_HILSCHER,
		.device =	PCI_DEVICE_ID_HILSCHER_NETX,
		.subvendor =	0,
		.subdevice =	0,
	},
	{
		.vendor =       PCI_VENDOR_ID_HILSCHER,
		.device =       PCI_DEVICE_ID_HILSCHER_NETPLC,
		.subvendor =    PCI_VENDOR_ID_HILSCHER,
		.subdevice =    PCI_SUBDEVICE_ID_NETPLC_RAM,
	},
	{
		.vendor =       PCI_VENDOR_ID_HILSCHER,
		.device =       PCI_DEVICE_ID_HILSCHER_NETPLC,
		.subvendor =    PCI_VENDOR_ID_HILSCHER,
		.subdevice =    PCI_SUBDEVICE_ID_NETPLC_FLASH,
	},
	{
		.vendor =	PCI_VENDOR_ID_PLX,
		.device =	PCI_DEVICE_ID_PLX_9030,
		.subvendor =	PCI_VENDOR_ID_PLX,
		.subdevice =	PCI_SUBDEVICE_ID_NXSB_PCA,
	},
	{
		.vendor =	PCI_VENDOR_ID_PLX,
		.device =	PCI_DEVICE_ID_PLX_9030,
		.subvendor =	PCI_VENDOR_ID_PLX,
		.subdevice =	PCI_SUBDEVICE_ID_NXPCA,
	},
	{ 0, }
};

static struct pci_driver netx_pci_driver = {
	.name = "netx",
	.id_table = netx_pci_ids,
	.probe = netx_pci_probe,
	.remove = netx_pci_remove,
};

module_pci_driver(netx_pci_driver);
MODULE_DEVICE_TABLE(pci, netx_pci_ids);
MODULE_DESCRIPTION("UIO driver for Hilscher NetX based fieldbus cards");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Hans J. Koch, Manuel Traut");
