/*
 *  Atheros AR724X PCI host controller driver
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *  Copyright (C) 2009-2011 Gabor Juhos <juhosg@openwrt.org>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>

#define AR724X_PCI_REG_RESET		0x18
#define AR724X_PCI_REG_INT_STATUS	0x4c
#define AR724X_PCI_REG_INT_MASK		0x50

#define AR724X_PCI_RESET_LINK_UP	BIT(0)

#define AR724X_PCI_INT_DEV0		BIT(14)

#define AR724X_PCI_IRQ_COUNT		1

#define AR7240_BAR0_WAR_VALUE	0xffff

struct ar724x_pci_controller {
	void __iomem *devcfg_base;
	void __iomem *ctrl_base;

	int irq;

	bool link_up;
	bool bar0_is_cached;
	u32  bar0_value;

	spinlock_t lock;

	struct pci_controller pci_controller;
};

static inline bool ar724x_pci_check_link(struct ar724x_pci_controller *apc)
{
	u32 reset;

	reset = __raw_readl(apc->ctrl_base + AR724X_PCI_REG_RESET);
	return reset & AR724X_PCI_RESET_LINK_UP;
}

static inline struct ar724x_pci_controller *
pci_bus_to_ar724x_controller(struct pci_bus *bus)
{
	struct pci_controller *hose;

	hose = (struct pci_controller *) bus->sysdata;
	return container_of(hose, struct ar724x_pci_controller, pci_controller);
}

static int ar724x_pci_read(struct pci_bus *bus, unsigned int devfn, int where,
			    int size, uint32_t *value)
{
	struct ar724x_pci_controller *apc;
	unsigned long flags;
	void __iomem *base;
	u32 data;

	apc = pci_bus_to_ar724x_controller(bus);
	if (!apc->link_up)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (devfn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	base = apc->devcfg_base;

	spin_lock_irqsave(&apc->lock, flags);
	data = __raw_readl(base + (where & ~3));

	switch (size) {
	case 1:
		if (where & 1)
			data >>= 8;
		if (where & 2)
			data >>= 16;
		data &= 0xff;
		break;
	case 2:
		if (where & 2)
			data >>= 16;
		data &= 0xffff;
		break;
	case 4:
		break;
	default:
		spin_unlock_irqrestore(&apc->lock, flags);

		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	spin_unlock_irqrestore(&apc->lock, flags);

	if (where == PCI_BASE_ADDRESS_0 && size == 4 &&
	    apc->bar0_is_cached) {
		/* use the cached value */
		*value = apc->bar0_value;
	} else {
		*value = data;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int ar724x_pci_write(struct pci_bus *bus, unsigned int devfn, int where,
			     int size, uint32_t value)
{
	struct ar724x_pci_controller *apc;
	unsigned long flags;
	void __iomem *base;
	u32 data;
	int s;

	apc = pci_bus_to_ar724x_controller(bus);
	if (!apc->link_up)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (devfn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (soc_is_ar7240() && where == PCI_BASE_ADDRESS_0 && size == 4) {
		if (value != 0xffffffff) {
			/*
			 * WAR for a hw issue. If the BAR0 register of the
			 * device is set to the proper base address, the
			 * memory space of the device is not accessible.
			 *
			 * Cache the intended value so it can be read back,
			 * and write a SoC specific constant value to the
			 * BAR0 register in order to make the device memory
			 * accessible.
			 */
			apc->bar0_is_cached = true;
			apc->bar0_value = value;

			value = AR7240_BAR0_WAR_VALUE;
		} else {
			apc->bar0_is_cached = false;
		}
	}

	base = apc->devcfg_base;

	spin_lock_irqsave(&apc->lock, flags);
	data = __raw_readl(base + (where & ~3));

	switch (size) {
	case 1:
		s = ((where & 3) * 8);
		data &= ~(0xff << s);
		data |= ((value & 0xff) << s);
		break;
	case 2:
		s = ((where & 2) * 8);
		data &= ~(0xffff << s);
		data |= ((value & 0xffff) << s);
		break;
	case 4:
		data = value;
		break;
	default:
		spin_unlock_irqrestore(&apc->lock, flags);

		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	__raw_writel(data, base + (where & ~3));
	/* flush write */
	__raw_readl(base + (where & ~3));
	spin_unlock_irqrestore(&apc->lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops ar724x_pci_ops = {
	.read	= ar724x_pci_read,
	.write	= ar724x_pci_write,
};

static struct resource ar724x_io_resource = {
	.name   = "PCI IO space",
	.start  = 0,
	.end    = 0,
	.flags  = IORESOURCE_IO,
};

static struct resource ar724x_mem_resource = {
	.name   = "PCI memory space",
	.start  = AR724X_PCI_MEM_BASE,
	.end    = AR724X_PCI_MEM_BASE + AR724X_PCI_MEM_SIZE - 1,
	.flags  = IORESOURCE_MEM,
};

static void ar724x_pci_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct ar724x_pci_controller *apc;
	void __iomem *base;
	u32 pending;

	apc = irq_get_handler_data(irq);
	base = apc->ctrl_base;

	pending = __raw_readl(base + AR724X_PCI_REG_INT_STATUS) &
		  __raw_readl(base + AR724X_PCI_REG_INT_MASK);

	if (pending & AR724X_PCI_INT_DEV0)
		generic_handle_irq(ATH79_PCI_IRQ(0));

	else
		spurious_interrupt();
}

static void ar724x_pci_irq_unmask(struct irq_data *d)
{
	struct ar724x_pci_controller *apc;
	void __iomem *base;
	u32 t;

	apc = irq_data_get_irq_chip_data(d);
	base = apc->ctrl_base;

	switch (d->irq) {
	case ATH79_PCI_IRQ(0):
		t = __raw_readl(base + AR724X_PCI_REG_INT_MASK);
		__raw_writel(t | AR724X_PCI_INT_DEV0,
			     base + AR724X_PCI_REG_INT_MASK);
		/* flush write */
		__raw_readl(base + AR724X_PCI_REG_INT_MASK);
	}
}

static void ar724x_pci_irq_mask(struct irq_data *d)
{
	struct ar724x_pci_controller *apc;
	void __iomem *base;
	u32 t;

	apc = irq_data_get_irq_chip_data(d);
	base = apc->ctrl_base;

	switch (d->irq) {
	case ATH79_PCI_IRQ(0):
		t = __raw_readl(base + AR724X_PCI_REG_INT_MASK);
		__raw_writel(t & ~AR724X_PCI_INT_DEV0,
			     base + AR724X_PCI_REG_INT_MASK);

		/* flush write */
		__raw_readl(base + AR724X_PCI_REG_INT_MASK);

		t = __raw_readl(base + AR724X_PCI_REG_INT_STATUS);
		__raw_writel(t | AR724X_PCI_INT_DEV0,
			     base + AR724X_PCI_REG_INT_STATUS);

		/* flush write */
		__raw_readl(base + AR724X_PCI_REG_INT_STATUS);
	}
}

static struct irq_chip ar724x_pci_irq_chip = {
	.name		= "AR724X PCI ",
	.irq_mask	= ar724x_pci_irq_mask,
	.irq_unmask	= ar724x_pci_irq_unmask,
	.irq_mask_ack	= ar724x_pci_irq_mask,
};

static void ar724x_pci_irq_init(struct ar724x_pci_controller *apc)
{
	void __iomem *base;
	int i;

	base = apc->ctrl_base;

	__raw_writel(0, base + AR724X_PCI_REG_INT_MASK);
	__raw_writel(0, base + AR724X_PCI_REG_INT_STATUS);

	BUILD_BUG_ON(ATH79_PCI_IRQ_COUNT < AR724X_PCI_IRQ_COUNT);

	for (i = ATH79_PCI_IRQ_BASE;
	     i < ATH79_PCI_IRQ_BASE + AR724X_PCI_IRQ_COUNT; i++) {
		irq_set_chip_and_handler(i, &ar724x_pci_irq_chip,
					 handle_level_irq);
		irq_set_chip_data(i, apc);
	}

	irq_set_handler_data(apc->irq, apc);
	irq_set_chained_handler(apc->irq, ar724x_pci_irq_handler);
}

static int ar724x_pci_probe(struct platform_device *pdev)
{
	struct ar724x_pci_controller *apc;
	struct resource *res;

	apc = devm_kzalloc(&pdev->dev, sizeof(struct ar724x_pci_controller),
			    GFP_KERNEL);
	if (!apc)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "ctrl_base");
	if (!res)
		return -EINVAL;

	apc->ctrl_base = devm_request_and_ioremap(&pdev->dev, res);
	if (apc->ctrl_base == NULL)
		return -EBUSY;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg_base");
	if (!res)
		return -EINVAL;

	apc->devcfg_base = devm_request_and_ioremap(&pdev->dev, res);
	if (!apc->devcfg_base)
		return -EBUSY;

	apc->irq = platform_get_irq(pdev, 0);
	if (apc->irq < 0)
		return -EINVAL;

	spin_lock_init(&apc->lock);

	apc->pci_controller.pci_ops = &ar724x_pci_ops;
	apc->pci_controller.io_resource = &ar724x_io_resource;
	apc->pci_controller.mem_resource = &ar724x_mem_resource;

	apc->link_up = ar724x_pci_check_link(apc);
	if (!apc->link_up)
		dev_warn(&pdev->dev, "PCIe link is down\n");

	ar724x_pci_irq_init(apc);

	register_pci_controller(&apc->pci_controller);

	return 0;
}

static struct platform_driver ar724x_pci_driver = {
	.probe = ar724x_pci_probe,
	.driver = {
		.name = "ar724x-pci",
		.owner = THIS_MODULE,
	},
};

static int __init ar724x_pci_init(void)
{
	return platform_driver_register(&ar724x_pci_driver);
}

postcore_initcall(ar724x_pci_init);
