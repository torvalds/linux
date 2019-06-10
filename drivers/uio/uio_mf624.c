// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * UIO driver fo Humusoft MF624 DAQ card.
 * Copyright (C) 2011 Rostislav Lisovy <lisovy@gmail.com>,
 *                    Czech Technical University in Prague
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/uio_driver.h>

#define PCI_VENDOR_ID_HUMUSOFT		0x186c
#define PCI_DEVICE_ID_MF624		0x0624
#define PCI_SUBVENDOR_ID_HUMUSOFT	0x186c
#define PCI_SUBDEVICE_DEVICE		0x0624

/* BAR0 Interrupt control/status register */
#define INTCSR				0x4C
#define INTCSR_ADINT_ENABLE		(1 << 0)
#define INTCSR_CTR4INT_ENABLE		(1 << 3)
#define INTCSR_PCIINT_ENABLE		(1 << 6)
#define INTCSR_ADINT_STATUS		(1 << 2)
#define INTCSR_CTR4INT_STATUS		(1 << 5)

enum mf624_interrupt_source {ADC, CTR4, ALL};

static void mf624_disable_interrupt(enum mf624_interrupt_source source,
			     struct uio_info *info)
{
	void __iomem *INTCSR_reg = info->mem[0].internal_addr + INTCSR;

	switch (source) {
	case ADC:
		iowrite32(ioread32(INTCSR_reg)
			& ~(INTCSR_ADINT_ENABLE | INTCSR_PCIINT_ENABLE),
			INTCSR_reg);
		break;

	case CTR4:
		iowrite32(ioread32(INTCSR_reg)
			& ~(INTCSR_CTR4INT_ENABLE | INTCSR_PCIINT_ENABLE),
			INTCSR_reg);
		break;

	case ALL:
	default:
		iowrite32(ioread32(INTCSR_reg)
			& ~(INTCSR_ADINT_ENABLE | INTCSR_CTR4INT_ENABLE
			    | INTCSR_PCIINT_ENABLE),
			INTCSR_reg);
		break;
	}
}

static void mf624_enable_interrupt(enum mf624_interrupt_source source,
			    struct uio_info *info)
{
	void __iomem *INTCSR_reg = info->mem[0].internal_addr + INTCSR;

	switch (source) {
	case ADC:
		iowrite32(ioread32(INTCSR_reg)
			| INTCSR_ADINT_ENABLE | INTCSR_PCIINT_ENABLE,
			INTCSR_reg);
		break;

	case CTR4:
		iowrite32(ioread32(INTCSR_reg)
			| INTCSR_CTR4INT_ENABLE | INTCSR_PCIINT_ENABLE,
			INTCSR_reg);
		break;

	case ALL:
	default:
		iowrite32(ioread32(INTCSR_reg)
			| INTCSR_ADINT_ENABLE | INTCSR_CTR4INT_ENABLE
			| INTCSR_PCIINT_ENABLE,
			INTCSR_reg);
		break;
	}
}

static irqreturn_t mf624_irq_handler(int irq, struct uio_info *info)
{
	void __iomem *INTCSR_reg = info->mem[0].internal_addr + INTCSR;

	if ((ioread32(INTCSR_reg) & INTCSR_ADINT_ENABLE)
	    && (ioread32(INTCSR_reg) & INTCSR_ADINT_STATUS)) {
		mf624_disable_interrupt(ADC, info);
		return IRQ_HANDLED;
	}

	if ((ioread32(INTCSR_reg) & INTCSR_CTR4INT_ENABLE)
	    && (ioread32(INTCSR_reg) & INTCSR_CTR4INT_STATUS)) {
		mf624_disable_interrupt(CTR4, info);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int mf624_irqcontrol(struct uio_info *info, s32 irq_on)
{
	if (irq_on == 0)
		mf624_disable_interrupt(ALL, info);
	else if (irq_on == 1)
		mf624_enable_interrupt(ALL, info);

	return 0;
}

static int mf624_setup_mem(struct pci_dev *dev, int bar, struct uio_mem *mem, const char *name)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len = pci_resource_len(dev, bar);

	mem->name = name;
	mem->addr = start & PAGE_MASK;
	mem->offs = start & ~PAGE_MASK;
	if (!mem->addr)
		return -ENODEV;
	mem->size = ((start & ~PAGE_MASK) + len + PAGE_SIZE - 1) & PAGE_MASK;
	mem->memtype = UIO_MEM_PHYS;
	mem->internal_addr = pci_ioremap_bar(dev, bar);
	if (!mem->internal_addr)
		return -ENODEV;
	return 0;
}

static int mf624_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	struct uio_info *info;

	info = kzalloc(sizeof(struct uio_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (pci_enable_device(dev))
		goto out_free;

	if (pci_request_regions(dev, "mf624"))
		goto out_disable;

	info->name = "mf624";
	info->version = "0.0.1";

	/* Note: Datasheet says device uses BAR0, BAR1, BAR2 -- do not trust it */

	/* BAR0 */
	if (mf624_setup_mem(dev, 0, &info->mem[0], "PCI chipset, interrupts, status "
			    "bits, special functions"))
		goto out_release;
	/* BAR2 */
	if (mf624_setup_mem(dev, 2, &info->mem[1], "ADC, DAC, DIO"))
		goto out_unmap0;

	/* BAR4 */
	if (mf624_setup_mem(dev, 4, &info->mem[2], "Counter/timer chip"))
		goto out_unmap1;

	info->irq = dev->irq;
	info->irq_flags = IRQF_SHARED;
	info->handler = mf624_irq_handler;

	info->irqcontrol = mf624_irqcontrol;

	if (uio_register_device(&dev->dev, info))
		goto out_unmap2;

	pci_set_drvdata(dev, info);

	return 0;

out_unmap2:
	iounmap(info->mem[2].internal_addr);
out_unmap1:
	iounmap(info->mem[1].internal_addr);
out_unmap0:
	iounmap(info->mem[0].internal_addr);

out_release:
	pci_release_regions(dev);

out_disable:
	pci_disable_device(dev);

out_free:
	kfree(info);
	return -ENODEV;
}

static void mf624_pci_remove(struct pci_dev *dev)
{
	struct uio_info *info = pci_get_drvdata(dev);

	mf624_disable_interrupt(ALL, info);

	uio_unregister_device(info);
	pci_release_regions(dev);
	pci_disable_device(dev);

	iounmap(info->mem[0].internal_addr);
	iounmap(info->mem[1].internal_addr);
	iounmap(info->mem[2].internal_addr);

	kfree(info);
}

static const struct pci_device_id mf624_pci_id[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HUMUSOFT, PCI_DEVICE_ID_MF624) },
	{ 0, }
};

static struct pci_driver mf624_pci_driver = {
	.name = "mf624",
	.id_table = mf624_pci_id,
	.probe = mf624_pci_probe,
	.remove = mf624_pci_remove,
};
MODULE_DEVICE_TABLE(pci, mf624_pci_id);

module_pci_driver(mf624_pci_driver);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Rostislav Lisovy <lisovy@gmail.com>");
