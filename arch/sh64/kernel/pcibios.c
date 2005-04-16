/*
 * $Id: pcibios.c,v 1.1 2001/08/24 12:38:19 dwmw2 Exp $
 *
 * arch/sh/kernel/pcibios.c
 *
 * Copyright (C) 2002 STMicroelectronics Limited
 *   Author : David J. McKay
 *
 * Copyright (C) 2004 Richard Curnow, SuperH UK Limited
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 * This is GPL'd.
 *
 * Provided here are generic versions of:
 *	pcibios_update_resource()
 *	pcibios_align_resource()
 *	pcibios_enable_device()
 *	pcibios_set_master()
 *	pcibios_update_irq()
 *
 * These functions are collected here to reduce duplication of common
 * code amongst the many platform-specific PCI support code files.
 *
 * Platform-specific files are expected to provide:
 *	pcibios_fixup_bus()
 *	pcibios_init()
 *	pcibios_setup()
 *	pcibios_fixup_pbus_ranges()
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

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

static void pcibios_enable_bridge(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->subordinate;
	u16 cmd, old_cmd;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;

	if (bus->resource[0]->flags & IORESOURCE_IO) {
		cmd |= PCI_COMMAND_IO;
	}
	if ((bus->resource[1]->flags & IORESOURCE_MEM) ||
	    (bus->resource[2]->flags & IORESOURCE_PREFETCH)) {
		cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != old_cmd) {
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}

	printk("PCI bridge %s, command register -> %04x\n",
		pci_name(dev), cmd);

}



int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	if ((dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pcibios_enable_bridge(dev);
	}

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for(idx=0; idx<6; idx++) {
		if (!(mask & (1 << idx)))
			continue;
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because of resource collisions\n", pci_name(dev));
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
		printk(KERN_INFO "PCI: Enabling device %s (%04x -> %04x)\n", pci_name(dev), old_cmd, cmd);
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
