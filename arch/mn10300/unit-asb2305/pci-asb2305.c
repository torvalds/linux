/* ASB2305 PCI resource stuff
 *
 * Copyright (C) 2001 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from arch/i386/pci-i386.c
 *   - Copyright 1997--2000 Martin Mares <mj@suse.cz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include "pci-asb2305.h"

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
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	resource_size_t start = res->start;

#if 0
	struct pci_dev *dev = data;

	printk(KERN_DEBUG
	       "### PCIBIOS_ALIGN_RESOURCE(%s,,{%08lx-%08lx,%08lx},%lx)\n",
	       pci_name(dev),
	       res->start,
	       res->end,
	       res->flags,
	       size
	       );
#endif

	if ((res->flags & IORESOURCE_IO) && (start & 0x300))
		start = (start + 0x3ff) & ~0x3ff;

	return start;
}


/*
 *  Handle resources of PCI devices.  If the world were perfect, we could
 *  just allocate all the resource regions and do nothing more.  It isn't.
 *  On the other hand, we cannot just re-allocate all devices, as it would
 *  require us to know lots of host bridge internals.  So we attempt to
 *  keep as much of the original configuration as possible, but tweak it
 *  when it's found to be wrong.
 *
 *  Known BIOS problems we have to work around:
 *	- I/O or memory regions not configured
 *	- regions configured, but not enabled in the command register
 *	- bogus I/O addresses above 64K used
 *	- expansion ROMs left enabled (this may sound harmless, but given
 *	  the fact the PCI specs explicitly allow address decoders to be
 *	  shared between expansion ROMs and other resource regions, it's
 *	  at least dangerous)
 *
 *  Our solution:
 *	(1) Allocate resources for all buses behind PCI-to-PCI bridges.
 *	    This gives us fixed barriers on where we can allocate.
 *	(2) Allocate resources for all enabled devices.  If there is
 *	    a collision, just mark the resource as unallocated. Also
 *	    disable expansion ROMs during this step.
 *	(3) Try to allocate resources for disabled devices.  If the
 *	    resources were assigned correctly, everything goes well,
 *	    if they weren't, they won't disturb allocation of other
 *	    resources.
 *	(4) Assign new addresses to resources which were either
 *	    not configured at all or misconfigured.  If explicitly
 *	    requested by the user, configure expansion ROM address
 *	    as well.
 */
static void __init pcibios_allocate_bus_resources(struct list_head *bus_list)
{
	struct pci_bus *bus;
	struct pci_dev *dev;
	int idx;
	struct resource *r, *pr;

	/* Depth-First Search on bus tree */
	list_for_each_entry(bus, bus_list, node) {
		dev = bus->self;
		if (dev) {
			for (idx = PCI_BRIDGE_RESOURCES;
			     idx < PCI_NUM_RESOURCES;
			     idx++) {
				r = &dev->resource[idx];
				if (!r->flags)
					continue;
				pr = pci_find_parent_resource(dev, r);
				if (!r->start ||
				    !pr ||
				    request_resource(pr, r) < 0) {
					printk(KERN_ERR "PCI:"
					       " Cannot allocate resource"
					       " region %d of bridge %s\n",
					       idx, pci_name(dev));
					/* Something is wrong with the region.
					 * Invalidate the resource to prevent
					 * child resource allocations in this
					 * range. */
					r->start = r->end = 0;
					r->flags = 0;
				}
			}
		}
		pcibios_allocate_bus_resources(&bus->children);
	}
}

static void __init pcibios_allocate_resources(int pass)
{
	struct pci_dev *dev = NULL;
	int idx, disabled;
	u16 command;
	struct resource *r, *pr;

	for_each_pci_dev(dev) {
		pci_read_config_word(dev, PCI_COMMAND, &command);
		for (idx = 0; idx < 6; idx++) {
			r = &dev->resource[idx];
			if (r->parent)		/* Already allocated */
				continue;
			if (!r->start)		/* Address not assigned */
				continue;
			if (r->flags & IORESOURCE_IO)
				disabled = !(command & PCI_COMMAND_IO);
			else
				disabled = !(command & PCI_COMMAND_MEMORY);
			if (pass == disabled) {
				DBG("PCI[%s]: Resource %08lx-%08lx"
				    " (f=%lx, d=%d, p=%d)\n",
				    pci_name(dev), r->start, r->end, r->flags,
				    disabled, pass);
				pr = pci_find_parent_resource(dev, r);
				if (!pr || request_resource(pr, r) < 0) {
					printk(KERN_ERR "PCI:"
					       " Cannot allocate resource"
					       " region %d of device %s\n",
					       idx, pci_name(dev));
					/* We'll assign a new address later */
					r->end -= r->start;
					r->start = 0;
				}
			}
		}
		if (!pass) {
			r = &dev->resource[PCI_ROM_RESOURCE];
			if (r->flags & IORESOURCE_ROM_ENABLE) {
				/* Turn the ROM off, leave the resource region,
				 * but keep it unregistered. */
				u32 reg;
				DBG("PCI: Switching off ROM of %s\n",
				    pci_name(dev));
				r->flags &= ~IORESOURCE_ROM_ENABLE;
				pci_read_config_dword(
					dev, dev->rom_base_reg, &reg);
				pci_write_config_dword(
					dev, dev->rom_base_reg,
					reg & ~PCI_ROM_ADDRESS_ENABLE);
			}
		}
	}
}

static int __init pcibios_assign_resources(void)
{
	struct pci_dev *dev = NULL;
	struct resource *r, *pr;

	if (!(pci_probe & PCI_ASSIGN_ROMS)) {
		/* Try to use BIOS settings for ROMs, otherwise let
		   pci_assign_unassigned_resources() allocate the new
		   addresses. */
		for_each_pci_dev(dev) {
			r = &dev->resource[PCI_ROM_RESOURCE];
			if (!r->flags || !r->start)
				continue;
			pr = pci_find_parent_resource(dev, r);
			if (!pr || request_resource(pr, r) < 0) {
				r->end -= r->start;
				r->start = 0;
			}
		}
	}

	pci_assign_unassigned_resources();

	return 0;
}

fs_initcall(pcibios_assign_resources);

void __init pcibios_resource_survey(void)
{
	DBG("PCI: Allocating resources\n");
	pcibios_allocate_bus_resources(&pci_root_buses);
	pcibios_allocate_resources(0);
	pcibios_allocate_resources(1);
}

/*
 *  If we set up a device for bus mastering, we need to check the latency
 *  timer as certain crappy BIOSes forget to set it properly.
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

	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}

int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	unsigned long prot;

	/* Leave vm_pgoff as-is, the PCI space address is the physical
	 * address on this platform.
	 */
	vma->vm_flags |= VM_LOCKED | VM_IO;

	prot = pgprot_val(vma->vm_page_prot);
	prot &= ~_PAGE_CACHE;
	vma->vm_page_prot = __pgprot(prot);

	/* Write-combine setting is ignored */
	if (io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot))
		return -EAGAIN;

	return 0;
}
