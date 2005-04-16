/* arch/sh/kernel/pci.c
 * $Id: pci.c,v 1.1 2003/08/24 19:15:45 lethal Exp $
 *
 * Copyright (c) 2002 M. R. Brown  <mrbrown@linux-sh.org>
 * 
 * 
 * These functions are collected here to reduce duplication of common
 * code amongst the many platform-specific PCI support code files.
 * 
 * These routines require the following board-specific routines:
 * void pcibios_fixup_irqs();
 *
 * See include/asm-sh/pci.h for more information.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

static int __init pcibios_init(void)
{
	struct pci_channel *p;
	struct pci_bus *bus;
	int busno;

#ifdef CONFIG_PCI_AUTO
	/* assign resources */
	busno = 0;
	for (p = board_pci_channels; p->pci_ops != NULL; p++) {
		busno = pciauto_assign_resources(busno, p) + 1;
	}
#endif

	/* scan the buses */
	busno = 0;
	for (p= board_pci_channels; p->pci_ops != NULL; p++) {
		bus = pci_scan_bus(busno, p->pci_ops, p);
		busno = bus->subordinate+1;
	}

	/* board-specific fixups */
	pcibios_fixup_irqs();

	return 0;
}

subsys_initcall(pcibios_init);

void
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
	u32 new, check;
	int reg;

	new = res->start | (res->flags & PCI_REGION_FLAG_MASK);
	if (resource < 6) {
		reg = PCI_BASE_ADDRESS_0 + 4*resource;
	} else if (resource == PCI_ROM_RESOURCE) {
		res->flags |= IORESOURCE_ROM_ENABLE;
		new |= PCI_ROM_ADDRESS_ENABLE;
		reg = dev->rom_base_reg;
	} else {
		/* Somebody might have asked allocation of a non-standard resource */
		return;
	}
	
	pci_write_config_dword(dev, reg, new);
	pci_read_config_dword(dev, reg, &check);
	if ((new ^ check) & ((new & PCI_BASE_ADDRESS_SPACE_IO) ? PCI_BASE_ADDRESS_IO_MASK : PCI_BASE_ADDRESS_MEM_MASK)) {
		printk(KERN_ERR "PCI: Error while updating region "
		       "%s/%d (%08x != %08x)\n", pci_name(dev), resource,
		       new, check);
	}
}

void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
			    __attribute__ ((weak));

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 */
void pcibios_align_resource(void *data, struct resource *res,
			    unsigned long size, unsigned long align)
{
	if (res->flags & IORESOURCE_IO) {
		unsigned long start = res->start;

		if (start & 0x300) {
			start = (start + 0x3ff) & ~0x3ff;
			res->start = start;
		}
	}
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for(idx=0; idx<6; idx++) {
		if (!(mask & (1 << idx)))
			continue;
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because "
			       "of resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk(KERN_INFO "PCI: Enabling device %s (%04x -> %04x)\n",
		       pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

/*
 *  If we set up a device for bus mastering, we need to check and set
 *  the latency timer as it may not be properly set.
 */
unsigned int pcibios_max_latency = 255;

void pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat < 16)
		lat = (64 <= pcibios_max_latency) ? 64 : pcibios_max_latency;
	else if (lat > pcibios_max_latency)
		lat = pcibios_max_latency;
	else
		return;
	printk(KERN_INFO "PCI: Setting latency timer of device %s to %d\n", pci_name(dev), lat);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}
