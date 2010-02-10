/*
 *	Low-Level PCI Access for i386 machines
 *
 * Copyright 1993, 1994 Drew Eckhardt
 *      Visionary Computing
 *      (Unix and Linux consulting and custom programming)
 *      Drew@Colorado.EDU
 *      +1 (303) 786-7975
 *
 * Drew's work was sponsored by:
 *	iX Multiuser Multitasking Magazine
 *	Hannover, Germany
 *	hm@ix.de
 *
 * Copyright 1997--2000 Martin Mares <mj@ucw.cz>
 *
 * For more information, please consult the following manuals (look at
 * http://www.pcisig.com/ for how to get them):
 *
 * PCI BIOS Specification
 * PCI Local Bus Specification
 * PCI to PCI Bridge Specification
 * PCI System Design Guide
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/bootmem.h>

#include <asm/pat.h>
#include <asm/e820.h>
#include <asm/pci_x86.h>
#include <asm/io_apic.h>


static int
skip_isa_ioresource_align(struct pci_dev *dev) {

	if ((pci_probe & PCI_CAN_SKIP_ISA_ALIGN) &&
	    !(dev->bus->bridge_ctl & PCI_BRIDGE_CTL_ISA))
		return 1;
	return 0;
}

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
void
pcibios_align_resource(void *data, struct resource *res,
			resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;

	if (res->flags & IORESOURCE_IO) {
		resource_size_t start = res->start;

		if (skip_isa_ioresource_align(dev))
			return;
		if (start & 0x300) {
			start = (start + 0x3ff) & ~0x3ff;
			res->start = start;
		}
	}
}
EXPORT_SYMBOL(pcibios_align_resource);

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
	struct resource *r;

	/* Depth-First Search on bus tree */
	list_for_each_entry(bus, bus_list, node) {
		if ((dev = bus->self)) {
			for (idx = PCI_BRIDGE_RESOURCES;
			    idx < PCI_NUM_RESOURCES; idx++) {
				r = &dev->resource[idx];
				if (!r->flags)
					continue;
				if (!r->start ||
				    pci_claim_resource(dev, idx) < 0) {
					dev_info(&dev->dev,
						 "can't reserve window %pR\n",
						 r);
					/*
					 * Something is wrong with the region.
					 * Invalidate the resource to prevent
					 * child resource allocations in this
					 * range.
					 */
					r->flags = 0;
				}
			}
		}
		pcibios_allocate_bus_resources(&bus->children);
	}
}

struct pci_check_idx_range {
	int start;
	int end;
};

static void __init pcibios_allocate_resources(int pass)
{
	struct pci_dev *dev = NULL;
	int idx, disabled, i;
	u16 command;
	struct resource *r;

	struct pci_check_idx_range idx_range[] = {
		{ PCI_STD_RESOURCES, PCI_STD_RESOURCE_END },
#ifdef CONFIG_PCI_IOV
		{ PCI_IOV_RESOURCES, PCI_IOV_RESOURCE_END },
#endif
	};

	for_each_pci_dev(dev) {
		pci_read_config_word(dev, PCI_COMMAND, &command);
		for (i = 0; i < ARRAY_SIZE(idx_range); i++)
		for (idx = idx_range[i].start; idx <= idx_range[i].end; idx++) {
			r = &dev->resource[idx];
			if (r->parent)		/* Already allocated */
				continue;
			if (!r->start)		/* Address not assigned at all */
				continue;
			if (r->flags & IORESOURCE_IO)
				disabled = !(command & PCI_COMMAND_IO);
			else
				disabled = !(command & PCI_COMMAND_MEMORY);
			if (pass == disabled) {
				dev_dbg(&dev->dev,
					"BAR %d: reserving %pr (d=%d, p=%d)\n",
					idx, r, disabled, pass);
				if (pci_claim_resource(dev, idx) < 0) {
					dev_info(&dev->dev,
						 "can't reserve %pR\n", r);
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
				dev_dbg(&dev->dev, "disabling ROM %pR\n", r);
				r->flags &= ~IORESOURCE_ROM_ENABLE;
				pci_read_config_dword(dev,
						dev->rom_base_reg, &reg);
				pci_write_config_dword(dev, dev->rom_base_reg,
						reg & ~PCI_ROM_ADDRESS_ENABLE);
			}
		}
	}
}

static int __init pcibios_assign_resources(void)
{
	struct pci_dev *dev = NULL;
	struct resource *r;

	if (!(pci_probe & PCI_ASSIGN_ROMS)) {
		/*
		 * Try to use BIOS settings for ROMs, otherwise let
		 * pci_assign_unassigned_resources() allocate the new
		 * addresses.
		 */
		for_each_pci_dev(dev) {
			r = &dev->resource[PCI_ROM_RESOURCE];
			if (!r->flags || !r->start)
				continue;
			if (pci_claim_resource(dev, PCI_ROM_RESOURCE) < 0) {
				r->end -= r->start;
				r->start = 0;
			}
		}
	}

	pci_assign_unassigned_resources();

	return 0;
}

void __init pcibios_resource_survey(void)
{
	DBG("PCI: Allocating resources\n");
	pcibios_allocate_bus_resources(&pci_root_buses);
	pcibios_allocate_resources(0);
	pcibios_allocate_resources(1);

	e820_reserve_resources_late();
	/*
	 * Insert the IO APIC resources after PCI initialization has
	 * occured to handle IO APICS that are mapped in on a BAR in
	 * PCI space, but before trying to assign unassigned pci res.
	 */
	ioapic_insert_resources();
}

/**
 * called in fs_initcall (one below subsys_initcall),
 * give a chance for motherboard reserve resources
 */
fs_initcall(pcibios_assign_resources);

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
	dev_printk(KERN_DEBUG, &dev->dev, "setting latency timer to %d\n", lat);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}

static const struct vm_operations_struct pci_mmap_ops = {
	.access = generic_access_phys,
};

int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	unsigned long prot;

	/* I/O space cannot be accessed via normal processor loads and
	 * stores on this platform.
	 */
	if (mmap_state == pci_mmap_io)
		return -EINVAL;

	prot = pgprot_val(vma->vm_page_prot);

	/*
 	 * Return error if pat is not enabled and write_combine is requested.
 	 * Caller can followup with UC MINUS request and add a WC mtrr if there
 	 * is a free mtrr slot.
 	 */
	if (!pat_enabled && write_combine)
		return -EINVAL;

	if (pat_enabled && write_combine)
		prot |= _PAGE_CACHE_WC;
	else if (pat_enabled || boot_cpu_data.x86 > 3)
		/*
		 * ioremap() and ioremap_nocache() defaults to UC MINUS for now.
		 * To avoid attribute conflicts, request UC MINUS here
		 * aswell.
		 */
		prot |= _PAGE_CACHE_UC_MINUS;

	vma->vm_page_prot = __pgprot(prot);

	if (io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot))
		return -EAGAIN;

	vma->vm_ops = &pci_mmap_ops;

	return 0;
}
