/*
 * Implement the default iomap interfaces
 *
 * (C) Copyright 2004 Linus Torvalds
 * (C) Copyright 2006 Ralf Baechle <ralf@linux-mips.org>
 * (C) Copyright 2007 MIPS Technologies, Inc.
 *     written by Ralf Baechle <ralf@linux-mips.org>
 */
#include <linux/pci.h>
#include <linux/module.h>
#include <asm/io.h>

static void __iomem *ioport_map_pci(struct pci_dev *dev,
                                     unsigned long port, unsigned int nr)
{
	struct pci_controller *ctrl = dev->bus->sysdata;
	unsigned long base = ctrl->io_map_base;

	/* This will eventually become a BUG_ON but for now be gentle */
	if (unlikely(!ctrl->io_map_base)) {
		struct pci_bus *bus = dev->bus;
		char name[8];

		while (bus->parent)
			bus = bus->parent;

		ctrl->io_map_base = base = mips_io_port_base;

		sprintf(name, "%04x:%02x", pci_domain_nr(bus), bus->number);
		printk(KERN_WARNING "io_map_base of root PCI bus %s unset.  "
		       "Trying to continue but you better\nfix this issue or "
		       "report it to linux-mips@linux-mips.org or your "
		       "vendor.\n", name);
#ifdef CONFIG_PCI_DOMAINS
		panic("To avoid data corruption io_map_base MUST be set with "
		      "multiple PCI domains.");
#endif
	}

	return (void __iomem *) (ctrl->io_map_base + port);
}

/*
 * Create a virtual mapping cookie for a PCI BAR (memory or IO)
 */
void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	unsigned long start = pci_resource_start(dev, bar);
	unsigned long len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (!len || !start)
		return NULL;
	if (maxlen && len > maxlen)
		len = maxlen;
	if (flags & IORESOURCE_IO)
		return ioport_map_pci(dev, start, len);
	if (flags & IORESOURCE_MEM) {
		if (flags & IORESOURCE_CACHEABLE)
			return ioremap(start, len);
		return ioremap_nocache(start, len);
	}
	/* What? */
	return NULL;
}

EXPORT_SYMBOL(pci_iomap);

void pci_iounmap(struct pci_dev *dev, void __iomem * addr)
{
	iounmap(addr);
}

EXPORT_SYMBOL(pci_iounmap);
