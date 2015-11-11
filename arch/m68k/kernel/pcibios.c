/*
 * pci.c -- basic PCI support code
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * (C) Copyright 2011, Greg Ungerer <gerg@uclinux.org>
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pci.h>

/*
 * From arch/i386/kernel/pci-i386.c:
 *
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 *
 * Why? Because some silly external IO cards only decode
 * the low 10 bits of the IO address. The 0x00-0xff region
 * is reserved for motherboard devices that decode all 16
 * bits, so it's ok to allocate at, say, 0x2800-0x28ff,
 * but we want to try to avoid allocating at 0x2900-0x2bff
 * which might be mirrored at 0x0100-0x03ff..
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
	resource_size_t size, resource_size_t align)
{
	resource_size_t start = res->start;

	if ((res->flags & IORESOURCE_IO) && (start & 0x300))
		start = (start + 0x3ff) & ~0x3ff;

	start = (start + align - 1) & ~(align - 1);

	return start;
}

/*
 * This is taken from the ARM code for this.
 */
int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	struct resource *r;
	u16 cmd, newcmd;
	int idx;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	newcmd = cmd;

	for (idx = 0; idx < 6; idx++) {
		/* Only set up the requested stuff */
		if (!(mask & (1 << idx)))
			continue;

		r = dev->resource + idx;
		if (!r->start && r->end) {
			pr_err("PCI: Device %s not available because of resource collisions\n",
				pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			newcmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			newcmd |= PCI_COMMAND_MEMORY;
	}

	/*
	 * Bridges (eg, cardbus bridges) need to be fully enabled
	 */
	if ((dev->class >> 16) == PCI_BASE_CLASS_BRIDGE)
		newcmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY;


	if (newcmd != cmd) {
		pr_info("PCI: enabling device %s (0x%04x -> 0x%04x)\n",
			pci_name(dev), cmd, newcmd);
		pci_write_config_word(dev, PCI_COMMAND, newcmd);
	}
	return 0;
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 8);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 32);
	}
}

char *pcibios_setup(char *str)
{
	return str;
}

