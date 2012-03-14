/*
 *  Atheros 724x PCI support
 *
 *  Copyright (C) 2011 René Bolldorf <xsecute@googlemail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <asm/mach-ath79/pci.h>

#define AR724X_PCI_CFG_BASE	0x14000000
#define AR724X_PCI_CFG_SIZE	0x1000
#define AR724X_PCI_MEM_BASE	0x10000000
#define AR724X_PCI_MEM_SIZE	0x08000000

static DEFINE_SPINLOCK(ar724x_pci_lock);
static void __iomem *ar724x_pci_devcfg_base;

static int ar724x_pci_read(struct pci_bus *bus, unsigned int devfn, int where,
			    int size, uint32_t *value)
{
	unsigned long flags, addr, tval, mask;
	void __iomem *base;

	if (devfn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	base = ar724x_pci_devcfg_base;

	spin_lock_irqsave(&ar724x_pci_lock, flags);

	switch (size) {
	case 1:
		addr = where & ~3;
		mask = 0xff000000 >> ((where % 4) * 8);
		tval = __raw_readl(base + addr);
		tval = tval & ~mask;
		*value = (tval >> ((4 - (where % 4))*8));
		break;
	case 2:
		addr = where & ~3;
		mask = 0xffff0000 >> ((where % 4)*8);
		tval = __raw_readl(base + addr);
		tval = tval & ~mask;
		*value = (tval >> ((4 - (where % 4))*8));
		break;
	case 4:
		*value = __raw_readl(base + where);
		break;
	default:
		spin_unlock_irqrestore(&ar724x_pci_lock, flags);

		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	spin_unlock_irqrestore(&ar724x_pci_lock, flags);

	return PCIBIOS_SUCCESSFUL;
}

static int ar724x_pci_write(struct pci_bus *bus, unsigned int devfn, int where,
			     int size, uint32_t value)
{
	unsigned long flags, tval, addr, mask;
	void __iomem *base;

	if (devfn)
		return PCIBIOS_DEVICE_NOT_FOUND;

	base = ar724x_pci_devcfg_base;

	spin_lock_irqsave(&ar724x_pci_lock, flags);

	switch (size) {
	case 1:
		addr = where & ~3;
		mask = 0xff000000 >> ((where % 4)*8);
		tval = __raw_readl(base + addr);
		tval = tval & ~mask;
		tval |= (value << ((4 - (where % 4))*8)) & mask;
		__raw_writel(tval, base + addr);
		break;
	case 2:
		addr = where & ~3;
		mask = 0xffff0000 >> ((where % 4)*8);
		tval = __raw_readl(base + addr);
		tval = tval & ~mask;
		tval |= (value << ((4 - (where % 4))*8)) & mask;
		__raw_writel(tval, base + addr);
		break;
	case 4:
		__raw_writel(value, (base + where));
		break;
	default:
		spin_unlock_irqrestore(&ar724x_pci_lock, flags);

		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

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

int __init ar724x_pcibios_init(void)
{
	ar724x_pci_devcfg_base = ioremap(AR724X_PCI_CFG_BASE,
					 AR724X_PCI_CFG_SIZE);
	if (ar724x_pci_devcfg_base == NULL)
		return -ENOMEM;

	register_pci_controller(&ar724x_pci_controller);

	return PCIBIOS_SUCCESSFUL;
}
