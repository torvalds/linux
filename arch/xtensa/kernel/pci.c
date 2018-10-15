// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/xtensa/kernel/pci.c
 *
 * PCI bios-type initialisation for PCI machines
 *
 * Copyright (C) 2001-2005 Tensilica Inc.
 *
 * Based largely on work from Cort (ppc/kernel/pci.c)
 * IO functions copied from sparc.
 *
 * Chris Zankel <chris@zankel.net>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/memblock.h>

#include <asm/pci-bridge.h>
#include <asm/platform.h>

/*
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
 * which might have be mirrored at 0x0100-0x03ff..
 */
resource_size_t
pcibios_align_resource(void *data, const struct resource *res,
		       resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (size > 0x100) {
			pr_err("PCI: I/O Region %s/%d too large (%u bytes)\n",
					pci_name(dev), dev->resource - res,
					size);
		}

		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}

	return start;
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
	if (bus->parent) {
		/* This is a subordinate bridge */
		pci_read_bridge_bases(bus);
	}
}

void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx<6; idx++) {
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			pci_err(dev, "can't enable device: resource collisions\n");
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		pci_info(dev, "enabling device (%04x -> %04x)\n", old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}

	return 0;
}

/*
 * Platform support for /proc/bus/pci/X/Y mmap()s.
 *  -- paulus.
 */

int pci_iobar_pfn(struct pci_dev *pdev, int bar, struct vm_area_struct *vma)
{
	struct pci_controller *pci_ctrl = (struct pci_controller*) pdev->sysdata;
	resource_size_t ioaddr = pci_resource_start(pdev, bar);

	if (pci_ctrl == 0)
		return -EINVAL;		/* should never happen */

	/* Convert to an offset within this PCI controller */
	ioaddr -= (unsigned long)pci_ctrl->io_space.base;

	vma->vm_pgoff += (ioaddr + pci_ctrl->io_space.start) >> PAGE_SHIFT;
	return 0;
}
