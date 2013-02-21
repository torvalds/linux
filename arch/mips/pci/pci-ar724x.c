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

#include <linux/irq.h>
#include <linux/pci.h>
#include <asm/mach-ath79/ath79.h>
#include <asm/mach-ath79/ar71xx_regs.h>
#include <asm/mach-ath79/pci.h>

#define AR724X_PCI_CFG_BASE	0x14000000
#define AR724X_PCI_CFG_SIZE	0x1000
#define AR724X_PCI_CTRL_BASE	(AR71XX_APB_BASE + 0x000f0000)
#define AR724X_PCI_CTRL_SIZE	0x100

#define AR724X_PCI_MEM_BASE	0x10000000
#define AR724X_PCI_MEM_SIZE	0x04000000

#define AR724X_PCI_REG_RESET		0x18
#define AR724X_PCI_REG_INT_STATUS	0x4c
#define AR724X_PCI_REG_INT_MASK		0x50

#define AR724X_PCI_RESET_LINK_UP	BIT(0)

#define AR724X_PCI_INT_DEV0		BIT(14)

#define AR724X_PCI_IRQ_COUNT		1

#define AR7240_BAR0_WAR_VALUE	0xffff

static DEFINE_SPINLOCK(ar724x_pci_lock);
static void __iomem *ar724x_pci_devcfg_base;
static void __iomem *ar724x_pci_ctrl_base;

static u32 ar724x_pci_bar0_value;
static bool ar724x_pci_bar0_is_cached;
static bool ar724x_pci_link_up;

static inline bool ar724x_pci_check_link(void)
{
	u32 reset;

	reset = __raw_readl(ar724x_pci_ctrl_base + AR724X_PCI_REG_RESET);
	return reset & AR724X_PCI_RESET_LINK_UP;
}

static int ar724x_pci_read(struct pci_bus *bus, unsigned int devfn, int where,
			    int size, uint32_t *value)
{
	unsigned long flags;
	void __iomem *base;
	u32 data;

	if (!ar724x_pci_link_up)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (devfn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	base = ar724x_pci_devcfg_base;

	spin_lock_irqsave(&ar724x_pci_lock, flags);
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
		spin_unlock_irqrestore(&ar724x_pci_lock, flags);

		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	spin_unlock_irqrestore(&ar724x_pci_lock, flags);

	if (where == PCI_BASE_ADDRESS_0 && size == 4 &&
	    ar724x_pci_bar0_is_cached) {
		/* use the cached value */
		*value = ar724x_pci_bar0_value;
	} else {
		*value = data;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int ar724x_pci_write(struct pci_bus *bus, unsigned int devfn, int where,
			     int size, uint32_t value)
{
	unsigned long flags;
	void __iomem *base;
	u32 data;
	int s;

	if (!ar724x_pci_link_up)
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
			ar724x_pci_bar0_is_cached = true;
			ar724x_pci_bar0_value = value;

			value = AR7240_BAR0_WAR_VALUE;
		} else {
			ar724x_pci_bar0_is_cached = false;
		}
	}

	base = ar724x_pci_devcfg_base;

	spin_lock_irqsave(&ar724x_pci_lock, flags);
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
		spin_unlock_irqrestore(&ar724x_pci_lock, flags);

		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	__raw_writel(data, base + (where & ~3));
	/* flush write */
	__raw_readl(base + (where & ~3));
	spin_unlock_irqrestore(&ar724x_pci_lock, flags);

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

static struct pci_controller ar724x_pci_controller = {
	.pci_ops        = &ar724x_pci_ops,
	.io_resource    = &ar724x_io_resource,
	.mem_resource	= &ar724x_mem_resource,
};

static void ar724x_pci_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	void __iomem *base;
	u32 pending;

	base = ar724x_pci_ctrl_base;

	pending = __raw_readl(base + AR724X_PCI_REG_INT_STATUS) &
		  __raw_readl(base + AR724X_PCI_REG_INT_MASK);

	if (pending & AR724X_PCI_INT_DEV0)
		generic_handle_irq(ATH79_PCI_IRQ(0));

	else
		spurious_interrupt();
}

static void ar724x_pci_irq_unmask(struct irq_data *d)
{
	void __iomem *base;
	u32 t;

	base = ar724x_pci_ctrl_base;

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
	void __iomem *base;
	u32 t;

	base = ar724x_pci_ctrl_base;

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

static void __init ar724x_pci_irq_init(int irq)
{
	void __iomem *base;
	int i;

	base = ar724x_pci_ctrl_base;

	__raw_writel(0, base + AR724X_PCI_REG_INT_MASK);
	__raw_writel(0, base + AR724X_PCI_REG_INT_STATUS);

	BUILD_BUG_ON(ATH79_PCI_IRQ_COUNT < AR724X_PCI_IRQ_COUNT);

	for (i = ATH79_PCI_IRQ_BASE;
	     i < ATH79_PCI_IRQ_BASE + AR724X_PCI_IRQ_COUNT; i++)
		irq_set_chip_and_handler(i, &ar724x_pci_irq_chip,
					 handle_level_irq);

	irq_set_chained_handler(irq, ar724x_pci_irq_handler);
}

int __init ar724x_pcibios_init(int irq)
{
	int ret;

	ret = -ENOMEM;

	ar724x_pci_devcfg_base = ioremap(AR724X_PCI_CFG_BASE,
					 AR724X_PCI_CFG_SIZE);
	if (ar724x_pci_devcfg_base == NULL)
		goto err;

	ar724x_pci_ctrl_base = ioremap(AR724X_PCI_CTRL_BASE,
				       AR724X_PCI_CTRL_SIZE);
	if (ar724x_pci_ctrl_base == NULL)
		goto err_unmap_devcfg;

	ar724x_pci_link_up = ar724x_pci_check_link();
	if (!ar724x_pci_link_up)
		pr_warn("ar724x: PCIe link is down\n");

	ar724x_pci_irq_init(irq);
	register_pci_controller(&ar724x_pci_controller);

	return PCIBIOS_SUCCESSFUL;

err_unmap_devcfg:
	iounmap(ar724x_pci_devcfg_base);
err:
	return ret;
}
