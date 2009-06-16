/*
 *	drivers/pci/setup-bus.c
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Support routines for initializing a PCI subsystem.
 */

/*
 * Nov 2000, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     PCI-PCI bridges cleanup, sorted resource allocation.
 * Feb 2002, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     Converted to allocation in 3 passes, which gives
 *	     tighter packing. Prefetchable range support.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>
#include <linux/slab.h>


static void pbus_assign_resources_sorted(const struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct resource *res;
	struct resource_list head, *list, *tmp;
	int idx;

	head.next = NULL;
	list_for_each_entry(dev, &bus->devices, bus_list) {
		u16 class = dev->class >> 8;

		/* Don't touch classless devices or host bridges or ioapics.  */
		if (class == PCI_CLASS_NOT_DEFINED ||
		    class == PCI_CLASS_BRIDGE_HOST)
			continue;

		/* Don't touch ioapic devices already enabled by firmware */
		if (class == PCI_CLASS_SYSTEM_PIC) {
			u16 command;
			pci_read_config_word(dev, PCI_COMMAND, &command);
			if (command & (PCI_COMMAND_IO | PCI_COMMAND_MEMORY))
				continue;
		}

		pdev_sort_resources(dev, &head);
	}

	for (list = head.next; list;) {
		res = list->res;
		idx = res - &list->dev->resource[0];
		if (pci_assign_resource(list->dev, idx)) {
			/* FIXME: get rid of this */
			res->start = 0;
			res->end = 0;
			res->flags = 0;
		}
		tmp = list;
		list = list->next;
		kfree(tmp);
	}
}

void pci_setup_cardbus(struct pci_bus *bus)
{
	struct pci_dev *bridge = bus->self;
	struct pci_bus_region region;

	dev_info(&bridge->dev, "CardBus bridge, secondary bus %04x:%02x\n",
		 pci_domain_nr(bus), bus->number);

	pcibios_resource_to_bus(bridge, &region, bus->resource[0]);
	if (bus->resource[0]->flags & IORESOURCE_IO) {
		/*
		 * The IO resource is allocated a range twice as large as it
		 * would normally need.  This allows us to set both IO regs.
		 */
		dev_info(&bridge->dev, "  IO window: %#08lx-%#08lx\n",
		       (unsigned long)region.start,
		       (unsigned long)region.end);
		pci_write_config_dword(bridge, PCI_CB_IO_BASE_0,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_IO_LIMIT_0,
					region.end);
	}

	pcibios_resource_to_bus(bridge, &region, bus->resource[1]);
	if (bus->resource[1]->flags & IORESOURCE_IO) {
		dev_info(&bridge->dev, "  IO window: %#08lx-%#08lx\n",
		       (unsigned long)region.start,
		       (unsigned long)region.end);
		pci_write_config_dword(bridge, PCI_CB_IO_BASE_1,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_IO_LIMIT_1,
					region.end);
	}

	pcibios_resource_to_bus(bridge, &region, bus->resource[2]);
	if (bus->resource[2]->flags & IORESOURCE_MEM) {
		dev_info(&bridge->dev, "  PREFETCH window: %#08lx-%#08lx\n",
		       (unsigned long)region.start,
		       (unsigned long)region.end);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_BASE_0,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_LIMIT_0,
					region.end);
	}

	pcibios_resource_to_bus(bridge, &region, bus->resource[3]);
	if (bus->resource[3]->flags & IORESOURCE_MEM) {
		dev_info(&bridge->dev, "  MEM window: %#08lx-%#08lx\n",
		       (unsigned long)region.start,
		       (unsigned long)region.end);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_BASE_1,
					region.start);
		pci_write_config_dword(bridge, PCI_CB_MEMORY_LIMIT_1,
					region.end);
	}
}
EXPORT_SYMBOL(pci_setup_cardbus);

/* Initialize bridges with base/limit values we have collected.
   PCI-to-PCI Bridge Architecture Specification rev. 1.1 (1998)
   requires that if there is no I/O ports or memory behind the
   bridge, corresponding range must be turned off by writing base
   value greater than limit to the bridge's base/limit registers.

   Note: care must be taken when updating I/O base/limit registers
   of bridges which support 32-bit I/O. This update requires two
   config space writes, so it's quite possible that an I/O window of
   the bridge will have some undesirable address (e.g. 0) after the
   first write. Ditto 64-bit prefetchable MMIO.  */
static void pci_setup_bridge(struct pci_bus *bus)
{
	struct pci_dev *bridge = bus->self;
	struct pci_bus_region region;
	u32 l, bu, lu, io_upper16;

	if (pci_is_enabled(bridge))
		return;

	dev_info(&bridge->dev, "PCI bridge, secondary bus %04x:%02x\n",
		 pci_domain_nr(bus), bus->number);

	/* Set up the top and bottom of the PCI I/O segment for this bus. */
	pcibios_resource_to_bus(bridge, &region, bus->resource[0]);
	if (bus->resource[0]->flags & IORESOURCE_IO) {
		pci_read_config_dword(bridge, PCI_IO_BASE, &l);
		l &= 0xffff0000;
		l |= (region.start >> 8) & 0x00f0;
		l |= region.end & 0xf000;
		/* Set up upper 16 bits of I/O base/limit. */
		io_upper16 = (region.end & 0xffff0000) | (region.start >> 16);
		dev_info(&bridge->dev, "  IO window: %#04lx-%#04lx\n",
		    (unsigned long)region.start,
		    (unsigned long)region.end);
	}
	else {
		/* Clear upper 16 bits of I/O base/limit. */
		io_upper16 = 0;
		l = 0x00f0;
		dev_info(&bridge->dev, "  IO window: disabled\n");
	}
	/* Temporarily disable the I/O range before updating PCI_IO_BASE. */
	pci_write_config_dword(bridge, PCI_IO_BASE_UPPER16, 0x0000ffff);
	/* Update lower 16 bits of I/O base/limit. */
	pci_write_config_dword(bridge, PCI_IO_BASE, l);
	/* Update upper 16 bits of I/O base/limit. */
	pci_write_config_dword(bridge, PCI_IO_BASE_UPPER16, io_upper16);

	/* Set up the top and bottom of the PCI Memory segment
	   for this bus. */
	pcibios_resource_to_bus(bridge, &region, bus->resource[1]);
	if (bus->resource[1]->flags & IORESOURCE_MEM) {
		l = (region.start >> 16) & 0xfff0;
		l |= region.end & 0xfff00000;
		dev_info(&bridge->dev, "  MEM window: %#08lx-%#08lx\n",
		    (unsigned long)region.start,
		    (unsigned long)region.end);
	}
	else {
		l = 0x0000fff0;
		dev_info(&bridge->dev, "  MEM window: disabled\n");
	}
	pci_write_config_dword(bridge, PCI_MEMORY_BASE, l);

	/* Clear out the upper 32 bits of PREF limit.
	   If PCI_PREF_BASE_UPPER32 was non-zero, this temporarily
	   disables PREF range, which is ok. */
	pci_write_config_dword(bridge, PCI_PREF_LIMIT_UPPER32, 0);

	/* Set up PREF base/limit. */
	bu = lu = 0;
	pcibios_resource_to_bus(bridge, &region, bus->resource[2]);
	if (bus->resource[2]->flags & IORESOURCE_PREFETCH) {
		l = (region.start >> 16) & 0xfff0;
		l |= region.end & 0xfff00000;
		bu = upper_32_bits(region.start);
		lu = upper_32_bits(region.end);
		dev_info(&bridge->dev, "  PREFETCH window: %#016llx-%#016llx\n",
		    (unsigned long long)region.start,
		    (unsigned long long)region.end);
	}
	else {
		l = 0x0000fff0;
		dev_info(&bridge->dev, "  PREFETCH window: disabled\n");
	}
	pci_write_config_dword(bridge, PCI_PREF_MEMORY_BASE, l);

	/* Set the upper 32 bits of PREF base & limit. */
	pci_write_config_dword(bridge, PCI_PREF_BASE_UPPER32, bu);
	pci_write_config_dword(bridge, PCI_PREF_LIMIT_UPPER32, lu);

	pci_write_config_word(bridge, PCI_BRIDGE_CONTROL, bus->bridge_ctl);
}

/* Check whether the bridge supports optional I/O and
   prefetchable memory ranges. If not, the respective
   base/limit registers must be read-only and read as 0. */
static void pci_bridge_check_ranges(struct pci_bus *bus)
{
	u16 io;
	u32 pmem;
	struct pci_dev *bridge = bus->self;
	struct resource *b_res;

	b_res = &bridge->resource[PCI_BRIDGE_RESOURCES];
	b_res[1].flags |= IORESOURCE_MEM;

	pci_read_config_word(bridge, PCI_IO_BASE, &io);
	if (!io) {
		pci_write_config_word(bridge, PCI_IO_BASE, 0xf0f0);
		pci_read_config_word(bridge, PCI_IO_BASE, &io);
 		pci_write_config_word(bridge, PCI_IO_BASE, 0x0);
 	}
 	if (io)
		b_res[0].flags |= IORESOURCE_IO;
	/*  DECchip 21050 pass 2 errata: the bridge may miss an address
	    disconnect boundary by one PCI data phase.
	    Workaround: do not use prefetching on this device. */
	if (bridge->vendor == PCI_VENDOR_ID_DEC && bridge->device == 0x0001)
		return;
	pci_read_config_dword(bridge, PCI_PREF_MEMORY_BASE, &pmem);
	if (!pmem) {
		pci_write_config_dword(bridge, PCI_PREF_MEMORY_BASE,
					       0xfff0fff0);
		pci_read_config_dword(bridge, PCI_PREF_MEMORY_BASE, &pmem);
		pci_write_config_dword(bridge, PCI_PREF_MEMORY_BASE, 0x0);
	}
	if (pmem)
		b_res[2].flags |= IORESOURCE_MEM | IORESOURCE_PREFETCH;
}

/* Helper function for sizing routines: find first available
   bus resource of a given type. Note: we intentionally skip
   the bus resources which have already been assigned (that is,
   have non-NULL parent resource). */
static struct resource *find_free_bus_resource(struct pci_bus *bus, unsigned long type)
{
	int i;
	struct resource *r;
	unsigned long type_mask = IORESOURCE_IO | IORESOURCE_MEM |
				  IORESOURCE_PREFETCH;

	for (i = 0; i < PCI_BUS_NUM_RESOURCES; i++) {
		r = bus->resource[i];
		if (r == &ioport_resource || r == &iomem_resource)
			continue;
		if (r && (r->flags & type_mask) == type && !r->parent)
			return r;
	}
	return NULL;
}

/* Sizing the IO windows of the PCI-PCI bridge is trivial,
   since these windows have 4K granularity and the IO ranges
   of non-bridge PCI devices are limited to 256 bytes.
   We must be careful with the ISA aliasing though. */
static void pbus_size_io(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct resource *b_res = find_free_bus_resource(bus, IORESOURCE_IO);
	unsigned long size = 0, size1 = 0;

	if (!b_res)
 		return;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];
			unsigned long r_size;

			if (r->parent || !(r->flags & IORESOURCE_IO))
				continue;
			r_size = resource_size(r);

			if (r_size < 0x400)
				/* Might be re-aligned for ISA */
				size += r_size;
			else
				size1 += r_size;
		}
	}
/* To be fixed in 2.5: we should have sort of HAVE_ISA
   flag in the struct pci_bus. */
#if defined(CONFIG_ISA) || defined(CONFIG_EISA)
	size = (size & 0xff) + ((size & ~0xffUL) << 2);
#endif
	size = ALIGN(size + size1, 4096);
	if (!size) {
		b_res->flags = 0;
		return;
	}
	/* Alignment of the IO window is always 4K */
	b_res->start = 4096;
	b_res->end = b_res->start + size - 1;
	b_res->flags |= IORESOURCE_STARTALIGN;
}

/* Calculate the size of the bus and minimal alignment which
   guarantees that all child resources fit in this size. */
static int pbus_size_mem(struct pci_bus *bus, unsigned long mask, unsigned long type)
{
	struct pci_dev *dev;
	resource_size_t min_align, align, size;
	resource_size_t aligns[12];	/* Alignments from 1Mb to 2Gb */
	int order, max_order;
	struct resource *b_res = find_free_bus_resource(bus, type);

	if (!b_res)
		return 0;

	memset(aligns, 0, sizeof(aligns));
	max_order = 0;
	size = 0;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		int i;
		
		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];
			resource_size_t r_size;

			if (r->parent || (r->flags & mask) != type)
				continue;
			r_size = resource_size(r);
			/* For bridges size != alignment */
			align = resource_alignment(r);
			order = __ffs(align) - 20;
			if (order > 11) {
				dev_warn(&dev->dev, "BAR %d bad alignment %llx: "
					 "%pR\n", i, (unsigned long long)align, r);
				r->flags = 0;
				continue;
			}
			size += r_size;
			if (order < 0)
				order = 0;
			/* Exclude ranges with size > align from
			   calculation of the alignment. */
			if (r_size == align)
				aligns[order] += align;
			if (order > max_order)
				max_order = order;
		}
	}

	align = 0;
	min_align = 0;
	for (order = 0; order <= max_order; order++) {
		resource_size_t align1 = 1;

		align1 <<= (order + 20);

		if (!align)
			min_align = align1;
		else if (ALIGN(align + min_align, min_align) < align1)
			min_align = align1 >> 1;
		align += aligns[order];
	}
	size = ALIGN(size, min_align);
	if (!size) {
		b_res->flags = 0;
		return 1;
	}
	b_res->start = min_align;
	b_res->end = size + min_align - 1;
	b_res->flags |= IORESOURCE_STARTALIGN;
	return 1;
}

static void pci_bus_size_cardbus(struct pci_bus *bus)
{
	struct pci_dev *bridge = bus->self;
	struct resource *b_res = &bridge->resource[PCI_BRIDGE_RESOURCES];
	u16 ctrl;

	/*
	 * Reserve some resources for CardBus.  We reserve
	 * a fixed amount of bus space for CardBus bridges.
	 */
	b_res[0].start = 0;
	b_res[0].end = pci_cardbus_io_size - 1;
	b_res[0].flags |= IORESOURCE_IO | IORESOURCE_SIZEALIGN;

	b_res[1].start = 0;
	b_res[1].end = pci_cardbus_io_size - 1;
	b_res[1].flags |= IORESOURCE_IO | IORESOURCE_SIZEALIGN;

	/*
	 * Check whether prefetchable memory is supported
	 * by this bridge.
	 */
	pci_read_config_word(bridge, PCI_CB_BRIDGE_CONTROL, &ctrl);
	if (!(ctrl & PCI_CB_BRIDGE_CTL_PREFETCH_MEM0)) {
		ctrl |= PCI_CB_BRIDGE_CTL_PREFETCH_MEM0;
		pci_write_config_word(bridge, PCI_CB_BRIDGE_CONTROL, ctrl);
		pci_read_config_word(bridge, PCI_CB_BRIDGE_CONTROL, &ctrl);
	}

	/*
	 * If we have prefetchable memory support, allocate
	 * two regions.  Otherwise, allocate one region of
	 * twice the size.
	 */
	if (ctrl & PCI_CB_BRIDGE_CTL_PREFETCH_MEM0) {
		b_res[2].start = 0;
		b_res[2].end = pci_cardbus_mem_size - 1;
		b_res[2].flags |= IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_SIZEALIGN;

		b_res[3].start = 0;
		b_res[3].end = pci_cardbus_mem_size - 1;
		b_res[3].flags |= IORESOURCE_MEM | IORESOURCE_SIZEALIGN;
	} else {
		b_res[3].start = 0;
		b_res[3].end = pci_cardbus_mem_size * 2 - 1;
		b_res[3].flags |= IORESOURCE_MEM | IORESOURCE_SIZEALIGN;
	}
}

void __ref pci_bus_size_bridges(struct pci_bus *bus)
{
	struct pci_dev *dev;
	unsigned long mask, prefmask;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		struct pci_bus *b = dev->subordinate;
		if (!b)
			continue;

		switch (dev->class >> 8) {
		case PCI_CLASS_BRIDGE_CARDBUS:
			pci_bus_size_cardbus(b);
			break;

		case PCI_CLASS_BRIDGE_PCI:
		default:
			pci_bus_size_bridges(b);
			break;
		}
	}

	/* The root bus? */
	if (!bus->self)
		return;

	switch (bus->self->class >> 8) {
	case PCI_CLASS_BRIDGE_CARDBUS:
		/* don't size cardbuses yet. */
		break;

	case PCI_CLASS_BRIDGE_PCI:
		pci_bridge_check_ranges(bus);
	default:
		pbus_size_io(bus);
		/* If the bridge supports prefetchable range, size it
		   separately. If it doesn't, or its prefetchable window
		   has already been allocated by arch code, try
		   non-prefetchable range for both types of PCI memory
		   resources. */
		mask = IORESOURCE_MEM;
		prefmask = IORESOURCE_MEM | IORESOURCE_PREFETCH;
		if (pbus_size_mem(bus, prefmask, prefmask))
			mask = prefmask; /* Success, size non-prefetch only. */
		pbus_size_mem(bus, mask, IORESOURCE_MEM);
		break;
	}
}
EXPORT_SYMBOL(pci_bus_size_bridges);

void __ref pci_bus_assign_resources(const struct pci_bus *bus)
{
	struct pci_bus *b;
	struct pci_dev *dev;

	pbus_assign_resources_sorted(bus);

	list_for_each_entry(dev, &bus->devices, bus_list) {
		b = dev->subordinate;
		if (!b)
			continue;

		pci_bus_assign_resources(b);

		switch (dev->class >> 8) {
		case PCI_CLASS_BRIDGE_PCI:
			pci_setup_bridge(b);
			break;

		case PCI_CLASS_BRIDGE_CARDBUS:
			pci_setup_cardbus(b);
			break;

		default:
			dev_info(&dev->dev, "not setting up bridge for bus "
				 "%04x:%02x\n", pci_domain_nr(b), b->number);
			break;
		}
	}
}
EXPORT_SYMBOL(pci_bus_assign_resources);

static void pci_bus_dump_res(struct pci_bus *bus)
{
        int i;

        for (i = 0; i < PCI_BUS_NUM_RESOURCES; i++) {
                struct resource *res = bus->resource[i];
                if (!res || !res->end)
                        continue;

		dev_printk(KERN_DEBUG, &bus->dev, "resource %d %s %pR\n", i,
			   (res->flags & IORESOURCE_IO) ? "io: " :
			    ((res->flags & IORESOURCE_PREFETCH)? "pref mem":"mem:"),
			   res);
        }
}

static void pci_bus_dump_resources(struct pci_bus *bus)
{
	struct pci_bus *b;
	struct pci_dev *dev;


	pci_bus_dump_res(bus);

	list_for_each_entry(dev, &bus->devices, bus_list) {
		b = dev->subordinate;
		if (!b)
			continue;

		pci_bus_dump_resources(b);
	}
}

void __init
pci_assign_unassigned_resources(void)
{
	struct pci_bus *bus;

	/* Depth first, calculate sizes and alignments of all
	   subordinate buses. */
	list_for_each_entry(bus, &pci_root_buses, node) {
		pci_bus_size_bridges(bus);
	}
	/* Depth last, allocate resources and update the hardware. */
	list_for_each_entry(bus, &pci_root_buses, node) {
		pci_bus_assign_resources(bus);
		pci_enable_bridges(bus);
	}

	/* dump the resource on buses */
	list_for_each_entry(bus, &pci_root_buses, node) {
		pci_bus_dump_resources(bus);
	}
}
