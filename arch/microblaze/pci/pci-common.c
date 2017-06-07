/*
 * Contains common pci routines for ALL ppc platform
 * (based on pci_32.c and pci_64.c)
 *
 * Port for PPC64 David Engebretsen, IBM Corp.
 * Contains common pci routines for ppc64 platform, pSeries and iSeries brands.
 *
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *   Rework, based on alpha PCI code.
 *
 * Common pmac/prep/chrp pci routines. -- Cort
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/shmem_fs.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/export.h>

#include <asm/processor.h>
#include <linux/io.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>

static DEFINE_SPINLOCK(hose_spinlock);
LIST_HEAD(hose_list);

/* XXX kill that some day ... */
static int global_phb_number;		/* Global phb counter */

/* ISA Memory physical address */
resource_size_t isa_mem_base;

unsigned long isa_io_base;
EXPORT_SYMBOL(isa_io_base);

static int pci_bus_count;

struct pci_controller *pcibios_alloc_controller(struct device_node *dev)
{
	struct pci_controller *phb;

	phb = zalloc_maybe_bootmem(sizeof(struct pci_controller), GFP_KERNEL);
	if (!phb)
		return NULL;
	spin_lock(&hose_spinlock);
	phb->global_number = global_phb_number++;
	list_add_tail(&phb->list_node, &hose_list);
	spin_unlock(&hose_spinlock);
	phb->dn = dev;
	phb->is_dynamic = mem_init_done;
	return phb;
}

void pcibios_free_controller(struct pci_controller *phb)
{
	spin_lock(&hose_spinlock);
	list_del(&phb->list_node);
	spin_unlock(&hose_spinlock);

	if (phb->is_dynamic)
		kfree(phb);
}

static resource_size_t pcibios_io_size(const struct pci_controller *hose)
{
	return resource_size(&hose->io_resource);
}

int pcibios_vaddr_is_ioport(void __iomem *address)
{
	int ret = 0;
	struct pci_controller *hose;
	resource_size_t size;

	spin_lock(&hose_spinlock);
	list_for_each_entry(hose, &hose_list, list_node) {
		size = pcibios_io_size(hose);
		if (address >= hose->io_base_virt &&
		    address < (hose->io_base_virt + size)) {
			ret = 1;
			break;
		}
	}
	spin_unlock(&hose_spinlock);
	return ret;
}

unsigned long pci_address_to_pio(phys_addr_t address)
{
	struct pci_controller *hose;
	resource_size_t size;
	unsigned long ret = ~0;

	spin_lock(&hose_spinlock);
	list_for_each_entry(hose, &hose_list, list_node) {
		size = pcibios_io_size(hose);
		if (address >= hose->io_base_phys &&
		    address < (hose->io_base_phys + size)) {
			unsigned long base =
				(unsigned long)hose->io_base_virt - _IO_BASE;
			ret = base + (address - hose->io_base_phys);
			break;
		}
	}
	spin_unlock(&hose_spinlock);

	return ret;
}
EXPORT_SYMBOL_GPL(pci_address_to_pio);

/* This routine is meant to be used early during boot, when the
 * PCI bus numbers have not yet been assigned, and you need to
 * issue PCI config cycles to an OF device.
 * It could also be used to "fix" RTAS config cycles if you want
 * to set pci_assign_all_buses to 1 and still use RTAS for PCI
 * config cycles.
 */
struct pci_controller *pci_find_hose_for_OF_device(struct device_node *node)
{
	while (node) {
		struct pci_controller *hose, *tmp;
		list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
			if (hose->dn == node)
				return hose;
		node = node->parent;
	}
	return NULL;
}

void pcibios_set_master(struct pci_dev *dev)
{
	/* No special bus mastering setup handling */
}

/*
 * Platform support for /proc/bus/pci/X/Y mmap()s,
 * modelled on the sparc64 implementation by Dave Miller.
 *  -- paulus.
 */

/*
 * Adjust vm_pgoff of VMA such that it is the physical page offset
 * corresponding to the 32-bit pci bus offset for DEV requested by the user.
 *
 * Basically, the user finds the base address for his device which he wishes
 * to mmap.  They read the 32-bit value from the config space base register,
 * add whatever PAGE_SIZE multiple offset they wish, and feed this into the
 * offset parameter of mmap on /proc/bus/pci/XXX for that device.
 *
 * Returns negative error code on failure, zero on success.
 */
static struct resource *__pci_mmap_make_offset(struct pci_dev *dev,
					       resource_size_t *offset,
					       enum pci_mmap_state mmap_state)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	unsigned long io_offset = 0;
	int i, res_bit;

	if (!hose)
		return NULL;		/* should never happen */

	/* If memory, add on the PCI bridge address offset */
	if (mmap_state == pci_mmap_mem) {
#if 0 /* See comment in pci_resource_to_user() for why this is disabled */
		*offset += hose->pci_mem_offset;
#endif
		res_bit = IORESOURCE_MEM;
	} else {
		io_offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		*offset += io_offset;
		res_bit = IORESOURCE_IO;
	}

	/*
	 * Check that the offset requested corresponds to one of the
	 * resources of the device.
	 */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		struct resource *rp = &dev->resource[i];
		int flags = rp->flags;

		/* treat ROM as memory (should be already) */
		if (i == PCI_ROM_RESOURCE)
			flags |= IORESOURCE_MEM;

		/* Active and same type? */
		if ((flags & res_bit) == 0)
			continue;

		/* In the range of this resource? */
		if (*offset < (rp->start & PAGE_MASK) || *offset > rp->end)
			continue;

		/* found it! construct the final physical address */
		if (mmap_state == pci_mmap_io)
			*offset += hose->io_base_phys - io_offset;
		return rp;
	}

	return NULL;
}

/*
 * This one is used by /dev/mem and fbdev who have no clue about the
 * PCI device, it tries to find the PCI device first and calls the
 * above routine
 */
pgprot_t pci_phys_mem_access_prot(struct file *file,
				  unsigned long pfn,
				  unsigned long size,
				  pgprot_t prot)
{
	struct pci_dev *pdev = NULL;
	struct resource *found = NULL;
	resource_size_t offset = ((resource_size_t)pfn) << PAGE_SHIFT;
	int i;

	if (page_is_ram(pfn))
		return prot;

	prot = pgprot_noncached(prot);
	for_each_pci_dev(pdev) {
		for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
			struct resource *rp = &pdev->resource[i];
			int flags = rp->flags;

			/* Active and same type? */
			if ((flags & IORESOURCE_MEM) == 0)
				continue;
			/* In the range of this resource? */
			if (offset < (rp->start & PAGE_MASK) ||
			    offset > rp->end)
				continue;
			found = rp;
			break;
		}
		if (found)
			break;
	}
	if (found) {
		if (found->flags & IORESOURCE_PREFETCH)
			prot = pgprot_noncached_wc(prot);
		pci_dev_put(pdev);
	}

	pr_debug("PCI: Non-PCI map for %llx, prot: %lx\n",
		 (unsigned long long)offset, pgprot_val(prot));

	return prot;
}

/*
 * Perform the actual remap of the pages for a PCI device mapping, as
 * appropriate for this architecture.  The region in the process to map
 * is described by vm_start and vm_end members of VMA, the base physical
 * address is found in vm_pgoff.
 * The pci device structure is provided so that architectures may make mapping
 * decisions on a per-device or per-bus basis.
 *
 * Returns a negative error code on failure, zero on success.
 */
int pci_mmap_page_range(struct pci_dev *dev, int bar, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	resource_size_t offset =
		((resource_size_t)vma->vm_pgoff) << PAGE_SHIFT;
	struct resource *rp;
	int ret;

	rp = __pci_mmap_make_offset(dev, &offset, mmap_state);
	if (rp == NULL)
		return -EINVAL;

	vma->vm_pgoff = offset >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);

	return ret;
}

/* This provides legacy IO read access on a bus */
int pci_legacy_read(struct pci_bus *bus, loff_t port, u32 *val, size_t size)
{
	unsigned long offset;
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct resource *rp = &hose->io_resource;
	void __iomem *addr;

	/* Check if port can be supported by that bus. We only check
	 * the ranges of the PHB though, not the bus itself as the rules
	 * for forwarding legacy cycles down bridges are not our problem
	 * here. So if the host bridge supports it, we do it.
	 */
	offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	offset += port;

	if (!(rp->flags & IORESOURCE_IO))
		return -ENXIO;
	if (offset < rp->start || (offset + size) > rp->end)
		return -ENXIO;
	addr = hose->io_base_virt + port;

	switch (size) {
	case 1:
		*((u8 *)val) = in_8(addr);
		return 1;
	case 2:
		if (port & 1)
			return -EINVAL;
		*((u16 *)val) = in_le16(addr);
		return 2;
	case 4:
		if (port & 3)
			return -EINVAL;
		*((u32 *)val) = in_le32(addr);
		return 4;
	}
	return -EINVAL;
}

/* This provides legacy IO write access on a bus */
int pci_legacy_write(struct pci_bus *bus, loff_t port, u32 val, size_t size)
{
	unsigned long offset;
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct resource *rp = &hose->io_resource;
	void __iomem *addr;

	/* Check if port can be supported by that bus. We only check
	 * the ranges of the PHB though, not the bus itself as the rules
	 * for forwarding legacy cycles down bridges are not our problem
	 * here. So if the host bridge supports it, we do it.
	 */
	offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	offset += port;

	if (!(rp->flags & IORESOURCE_IO))
		return -ENXIO;
	if (offset < rp->start || (offset + size) > rp->end)
		return -ENXIO;
	addr = hose->io_base_virt + port;

	/* WARNING: The generic code is idiotic. It gets passed a pointer
	 * to what can be a 1, 2 or 4 byte quantity and always reads that
	 * as a u32, which means that we have to correct the location of
	 * the data read within those 32 bits for size 1 and 2
	 */
	switch (size) {
	case 1:
		out_8(addr, val >> 24);
		return 1;
	case 2:
		if (port & 1)
			return -EINVAL;
		out_le16(addr, val >> 16);
		return 2;
	case 4:
		if (port & 3)
			return -EINVAL;
		out_le32(addr, val);
		return 4;
	}
	return -EINVAL;
}

/* This provides legacy IO or memory mmap access on a bus */
int pci_mmap_legacy_page_range(struct pci_bus *bus,
			       struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	resource_size_t offset =
		((resource_size_t)vma->vm_pgoff) << PAGE_SHIFT;
	resource_size_t size = vma->vm_end - vma->vm_start;
	struct resource *rp;

	pr_debug("pci_mmap_legacy_page_range(%04x:%02x, %s @%llx..%llx)\n",
		 pci_domain_nr(bus), bus->number,
		 mmap_state == pci_mmap_mem ? "MEM" : "IO",
		 (unsigned long long)offset,
		 (unsigned long long)(offset + size - 1));

	if (mmap_state == pci_mmap_mem) {
		/* Hack alert !
		 *
		 * Because X is lame and can fail starting if it gets an error
		 * trying to mmap legacy_mem (instead of just moving on without
		 * legacy memory access) we fake it here by giving it anonymous
		 * memory, effectively behaving just like /dev/zero
		 */
		if ((offset + size) > hose->isa_mem_size) {
#ifdef CONFIG_MMU
			pr_debug("Process %s (pid:%d) mapped non-existing PCI",
				current->comm, current->pid);
			pr_debug("legacy memory for 0%04x:%02x\n",
				pci_domain_nr(bus), bus->number);
#endif
			if (vma->vm_flags & VM_SHARED)
				return shmem_zero_setup(vma);
			return 0;
		}
		offset += hose->isa_mem_phys;
	} else {
		unsigned long io_offset = (unsigned long)hose->io_base_virt -
								_IO_BASE;
		unsigned long roffset = offset + io_offset;
		rp = &hose->io_resource;
		if (!(rp->flags & IORESOURCE_IO))
			return -ENXIO;
		if (roffset < rp->start || (roffset + size) > rp->end)
			return -ENXIO;
		offset += hose->io_base_phys;
	}
	pr_debug(" -> mapping phys %llx\n", (unsigned long long)offset);

	vma->vm_pgoff = offset >> PAGE_SHIFT;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

void pci_resource_to_user(const struct pci_dev *dev, int bar,
			  const struct resource *rsrc,
			  resource_size_t *start, resource_size_t *end)
{
	struct pci_bus_region region;

	if (rsrc->flags & IORESOURCE_IO) {
		pcibios_resource_to_bus(dev->bus, &region,
					(struct resource *) rsrc);
		*start = region.start;
		*end = region.end;
		return;
	}

	/* We pass a CPU physical address to userland for MMIO instead of a
	 * BAR value because X is lame and expects to be able to use that
	 * to pass to /dev/mem!
	 *
	 * That means we may have 64-bit values where some apps only expect
	 * 32 (like X itself since it thinks only Sparc has 64-bit MMIO).
	 */
	*start = rsrc->start;
	*end = rsrc->end;
}

/**
 * pci_process_bridge_OF_ranges - Parse PCI bridge resources from device tree
 * @hose: newly allocated pci_controller to be setup
 * @dev: device node of the host bridge
 * @primary: set if primary bus (32 bits only, soon to be deprecated)
 *
 * This function will parse the "ranges" property of a PCI host bridge device
 * node and setup the resource mapping of a pci controller based on its
 * content.
 *
 * Life would be boring if it wasn't for a few issues that we have to deal
 * with here:
 *
 *   - We can only cope with one IO space range and up to 3 Memory space
 *     ranges. However, some machines (thanks Apple !) tend to split their
 *     space into lots of small contiguous ranges. So we have to coalesce.
 *
 *   - We can only cope with all memory ranges having the same offset
 *     between CPU addresses and PCI addresses. Unfortunately, some bridges
 *     are setup for a large 1:1 mapping along with a small "window" which
 *     maps PCI address 0 to some arbitrary high address of the CPU space in
 *     order to give access to the ISA memory hole.
 *     The way out of here that I've chosen for now is to always set the
 *     offset based on the first resource found, then override it if we
 *     have a different offset and the previous was set by an ISA hole.
 *
 *   - Some busses have IO space not starting at 0, which causes trouble with
 *     the way we do our IO resource renumbering. The code somewhat deals with
 *     it for 64 bits but I would expect problems on 32 bits.
 *
 *   - Some 32 bits platforms such as 4xx can have physical space larger than
 *     32 bits so we need to use 64 bits values for the parsing
 */
void pci_process_bridge_OF_ranges(struct pci_controller *hose,
				  struct device_node *dev, int primary)
{
	int memno = 0, isa_hole = -1;
	unsigned long long isa_mb = 0;
	struct resource *res;
	struct of_pci_range range;
	struct of_pci_range_parser parser;

	pr_info("PCI host bridge %s %s ranges:\n",
	       dev->full_name, primary ? "(primary)" : "");

	/* Check for ranges property */
	if (of_pci_range_parser_init(&parser, dev))
		return;

	pr_debug("Parsing ranges property...\n");
	for_each_of_pci_range(&parser, &range) {
		/* Read next ranges element */
		pr_debug("pci_space: 0x%08x pci_addr:0x%016llx ",
				range.pci_space, range.pci_addr);
		pr_debug("cpu_addr:0x%016llx size:0x%016llx\n",
					range.cpu_addr, range.size);

		/* If we failed translation or got a zero-sized region
		 * (some FW try to feed us with non sensical zero sized regions
		 * such as power3 which look like some kind of attempt
		 * at exposing the VGA memory hole)
		 */
		if (range.cpu_addr == OF_BAD_ADDR || range.size == 0)
			continue;

		/* Act based on address space type */
		res = NULL;
		switch (range.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			pr_info("  IO 0x%016llx..0x%016llx -> 0x%016llx\n",
				range.cpu_addr, range.cpu_addr + range.size - 1,
				range.pci_addr);

			/* We support only one IO range */
			if (hose->pci_io_size) {
				pr_info(" \\--> Skipped (too many) !\n");
				continue;
			}
			/* On 32 bits, limit I/O space to 16MB */
			if (range.size > 0x01000000)
				range.size = 0x01000000;

			/* 32 bits needs to map IOs here */
			hose->io_base_virt = ioremap(range.cpu_addr,
						range.size);

			/* Expect trouble if pci_addr is not 0 */
			if (primary)
				isa_io_base =
					(unsigned long)hose->io_base_virt;
			/* pci_io_size and io_base_phys always represent IO
			 * space starting at 0 so we factor in pci_addr
			 */
			hose->pci_io_size = range.pci_addr + range.size;
			hose->io_base_phys = range.cpu_addr - range.pci_addr;

			/* Build resource */
			res = &hose->io_resource;
			range.cpu_addr = range.pci_addr;

			break;
		case IORESOURCE_MEM:
			pr_info(" MEM 0x%016llx..0x%016llx -> 0x%016llx %s\n",
				range.cpu_addr, range.cpu_addr + range.size - 1,
				range.pci_addr,
				(range.pci_space & 0x40000000) ?
				"Prefetch" : "");

			/* We support only 3 memory ranges */
			if (memno >= 3) {
				pr_info(" \\--> Skipped (too many) !\n");
				continue;
			}
			/* Handles ISA memory hole space here */
			if (range.pci_addr == 0) {
				isa_mb = range.cpu_addr;
				isa_hole = memno;
				if (primary || isa_mem_base == 0)
					isa_mem_base = range.cpu_addr;
				hose->isa_mem_phys = range.cpu_addr;
				hose->isa_mem_size = range.size;
			}

			/* We get the PCI/Mem offset from the first range or
			 * the, current one if the offset came from an ISA
			 * hole. If they don't match, bugger.
			 */
			if (memno == 0 ||
			    (isa_hole >= 0 && range.pci_addr != 0 &&
			     hose->pci_mem_offset == isa_mb))
				hose->pci_mem_offset = range.cpu_addr -
							range.pci_addr;
			else if (range.pci_addr != 0 &&
				 hose->pci_mem_offset != range.cpu_addr -
							range.pci_addr) {
				pr_info(" \\--> Skipped (offset mismatch) !\n");
				continue;
			}

			/* Build resource */
			res = &hose->mem_resources[memno++];
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->flags = range.flags;
			res->start = range.cpu_addr;
			res->end = range.cpu_addr + range.size - 1;
			res->parent = res->child = res->sibling = NULL;
		}
	}

	/* If there's an ISA hole and the pci_mem_offset is -not- matching
	 * the ISA hole offset, then we need to remove the ISA hole from
	 * the resource list for that brige
	 */
	if (isa_hole >= 0 && hose->pci_mem_offset != isa_mb) {
		unsigned int next = isa_hole + 1;
		pr_info(" Removing ISA hole at 0x%016llx\n", isa_mb);
		if (next < memno)
			memmove(&hose->mem_resources[isa_hole],
				&hose->mem_resources[next],
				sizeof(struct resource) * (memno - next));
		hose->mem_resources[--memno].flags = 0;
	}
}

/* Display the domain number in /proc */
int pci_proc_domain(struct pci_bus *bus)
{
	return pci_domain_nr(bus);
}

/* This header fixup will do the resource fixup for all devices as they are
 * probed, but not for bridge ranges
 */
static void pcibios_fixup_resources(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	int i;

	if (!hose) {
		pr_err("No host bridge for PCI dev %s !\n",
		       pci_name(dev));
		return;
	}
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		struct resource *res = dev->resource + i;
		if (!res->flags)
			continue;
		if (res->start == 0) {
			pr_debug("PCI:%s Resource %d %016llx-%016llx [%x]",
				 pci_name(dev), i,
				 (unsigned long long)res->start,
				 (unsigned long long)res->end,
				 (unsigned int)res->flags);
			pr_debug("is unassigned\n");
			res->end -= res->start;
			res->start = 0;
			res->flags |= IORESOURCE_UNSET;
			continue;
		}

		pr_debug("PCI:%s Resource %d %016llx-%016llx [%x]\n",
			 pci_name(dev), i,
			 (unsigned long long)res->start,
			 (unsigned long long)res->end,
			 (unsigned int)res->flags);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pcibios_fixup_resources);

/* This function tries to figure out if a bridge resource has been initialized
 * by the firmware or not. It doesn't have to be absolutely bullet proof, but
 * things go more smoothly when it gets it right. It should covers cases such
 * as Apple "closed" bridge resources and bare-metal pSeries unassigned bridges
 */
static int pcibios_uninitialized_bridge_resource(struct pci_bus *bus,
						 struct resource *res)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pci_dev *dev = bus->self;
	resource_size_t offset;
	u16 command;
	int i;

	/* Job is a bit different between memory and IO */
	if (res->flags & IORESOURCE_MEM) {
		/* If the BAR is non-0 (res != pci_mem_offset) then it's
		 * probably been initialized by somebody
		 */
		if (res->start != hose->pci_mem_offset)
			return 0;

		/* The BAR is 0, let's check if memory decoding is enabled on
		 * the bridge. If not, we consider it unassigned
		 */
		pci_read_config_word(dev, PCI_COMMAND, &command);
		if ((command & PCI_COMMAND_MEMORY) == 0)
			return 1;

		/* Memory decoding is enabled and the BAR is 0. If any of
		 * the bridge resources covers that starting address (0 then
		 * it's good enough for us for memory
		 */
		for (i = 0; i < 3; i++) {
			if ((hose->mem_resources[i].flags & IORESOURCE_MEM) &&
			   hose->mem_resources[i].start == hose->pci_mem_offset)
				return 0;
		}

		/* Well, it starts at 0 and we know it will collide so we may as
		 * well consider it as unassigned. That covers the Apple case.
		 */
		return 1;
	} else {
		/* If the BAR is non-0, then we consider it assigned */
		offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		if (((res->start - offset) & 0xfffffffful) != 0)
			return 0;

		/* Here, we are a bit different than memory as typically IO
		 * space starting at low addresses -is- valid. What we do
		 * instead if that we consider as unassigned anything that
		 * doesn't have IO enabled in the PCI command register,
		 * and that's it.
		 */
		pci_read_config_word(dev, PCI_COMMAND, &command);
		if (command & PCI_COMMAND_IO)
			return 0;

		/* It's starting at 0 and IO is disabled in the bridge, consider
		 * it unassigned
		 */
		return 1;
	}
}

/* Fixup resources of a PCI<->PCI bridge */
static void pcibios_fixup_bridge(struct pci_bus *bus)
{
	struct resource *res;
	int i;

	struct pci_dev *dev = bus->self;

	pci_bus_for_each_resource(bus, res, i) {
		if (!res)
			continue;
		if (!res->flags)
			continue;
		if (i >= 3 && bus->self->transparent)
			continue;

		pr_debug("PCI:%s Bus rsrc %d %016llx-%016llx [%x] fixup...\n",
			 pci_name(dev), i,
			 (unsigned long long)res->start,
			 (unsigned long long)res->end,
			 (unsigned int)res->flags);

		/* Try to detect uninitialized P2P bridge resources,
		 * and clear them out so they get re-assigned later
		 */
		if (pcibios_uninitialized_bridge_resource(bus, res)) {
			res->flags = 0;
			pr_debug("PCI:%s            (unassigned)\n",
								pci_name(dev));
		} else {
			pr_debug("PCI:%s            %016llx-%016llx\n",
				 pci_name(dev),
				 (unsigned long long)res->start,
				 (unsigned long long)res->end);
		}
	}
}

void pcibios_setup_bus_self(struct pci_bus *bus)
{
	/* Fix up the bus resources for P2P bridges */
	if (bus->self != NULL)
		pcibios_fixup_bridge(bus);
}

void pcibios_setup_bus_devices(struct pci_bus *bus)
{
	struct pci_dev *dev;

	pr_debug("PCI: Fixup bus devices %d (%s)\n",
		 bus->number, bus->self ? pci_name(bus->self) : "PHB");

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/* Setup OF node pointer in archdata */
		dev->dev.of_node = pci_device_to_OF_node(dev);

		/* Fixup NUMA node as it may not be setup yet by the generic
		 * code and is needed by the DMA init
		 */
		set_dev_node(&dev->dev, pcibus_to_node(dev->bus));

		/* Read default IRQs and fixup if necessary */
		dev->irq = of_irq_parse_and_map_pci(dev, 0, 0);
	}
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
	/* nothing to do */
}
EXPORT_SYMBOL(pcibios_fixup_bus);

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
	return res->start;
}
EXPORT_SYMBOL(pcibios_align_resource);

int pcibios_add_device(struct pci_dev *dev)
{
	dev->irq = of_irq_parse_and_map_pci(dev, 0, 0);

	return 0;
}
EXPORT_SYMBOL(pcibios_add_device);

/*
 * Reparent resource children of pr that conflict with res
 * under res, and make res replace those children.
 */
static int __init reparent_resources(struct resource *parent,
				     struct resource *res)
{
	struct resource *p, **pp;
	struct resource **firstpp = NULL;

	for (pp = &parent->child; (p = *pp) != NULL; pp = &p->sibling) {
		if (p->end < res->start)
			continue;
		if (res->end < p->start)
			break;
		if (p->start < res->start || p->end > res->end)
			return -1;	/* not completely contained */
		if (firstpp == NULL)
			firstpp = pp;
	}
	if (firstpp == NULL)
		return -1;	/* didn't find any conflicting entries? */
	res->parent = parent;
	res->child = *firstpp;
	res->sibling = *pp;
	*firstpp = res;
	*pp = NULL;
	for (p = res->child; p != NULL; p = p->sibling) {
		p->parent = res;
		pr_debug("PCI: Reparented %s [%llx..%llx] under %s\n",
			 p->name,
			 (unsigned long long)p->start,
			 (unsigned long long)p->end, res->name);
	}
	return 0;
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

static void pcibios_allocate_bus_resources(struct pci_bus *bus)
{
	struct pci_bus *b;
	int i;
	struct resource *res, *pr;

	pr_debug("PCI: Allocating bus resources for %04x:%02x...\n",
		 pci_domain_nr(bus), bus->number);

	pci_bus_for_each_resource(bus, res, i) {
		if (!res || !res->flags
		    || res->start > res->end || res->parent)
			continue;
		if (bus->parent == NULL)
			pr = (res->flags & IORESOURCE_IO) ?
				&ioport_resource : &iomem_resource;
		else {
			/* Don't bother with non-root busses when
			 * re-assigning all resources. We clear the
			 * resource flags as if they were colliding
			 * and as such ensure proper re-allocation
			 * later.
			 */
			pr = pci_find_parent_resource(bus->self, res);
			if (pr == res) {
				/* this happens when the generic PCI
				 * code (wrongly) decides that this
				 * bridge is transparent  -- paulus
				 */
				continue;
			}
		}

		pr_debug("PCI: %s (bus %d) bridge rsrc %d: %016llx-%016llx ",
			 bus->self ? pci_name(bus->self) : "PHB",
			 bus->number, i,
			 (unsigned long long)res->start,
			 (unsigned long long)res->end);
		pr_debug("[0x%x], parent %p (%s)\n",
			 (unsigned int)res->flags,
			 pr, (pr && pr->name) ? pr->name : "nil");

		if (pr && !(pr->flags & IORESOURCE_UNSET)) {
			struct pci_dev *dev = bus->self;

			if (request_resource(pr, res) == 0)
				continue;
			/*
			 * Must be a conflict with an existing entry.
			 * Move that entry (or entries) under the
			 * bridge resource and try again.
			 */
			if (reparent_resources(pr, res) == 0)
				continue;

			if (dev && i < PCI_BRIDGE_RESOURCE_NUM &&
			    pci_claim_bridge_resource(dev,
						 i + PCI_BRIDGE_RESOURCES) == 0)
				continue;

		}
		pr_warn("PCI: Cannot allocate resource region ");
		pr_cont("%d of PCI bridge %d, will remap\n", i, bus->number);
		res->start = res->end = 0;
		res->flags = 0;
	}

	list_for_each_entry(b, &bus->children, node)
		pcibios_allocate_bus_resources(b);
}

static inline void alloc_resource(struct pci_dev *dev, int idx)
{
	struct resource *pr, *r = &dev->resource[idx];

	pr_debug("PCI: Allocating %s: Resource %d: %016llx..%016llx [%x]\n",
		 pci_name(dev), idx,
		 (unsigned long long)r->start,
		 (unsigned long long)r->end,
		 (unsigned int)r->flags);

	pr = pci_find_parent_resource(dev, r);
	if (!pr || (pr->flags & IORESOURCE_UNSET) ||
	    request_resource(pr, r) < 0) {
		pr_warn("PCI: Cannot allocate resource region %d ", idx);
		pr_cont("of device %s, will remap\n", pci_name(dev));
		if (pr)
			pr_debug("PCI:  parent is %p: %016llx-%016llx [%x]\n",
				 pr,
				 (unsigned long long)pr->start,
				 (unsigned long long)pr->end,
				 (unsigned int)pr->flags);
		/* We'll assign a new address later */
		r->flags |= IORESOURCE_UNSET;
		r->end -= r->start;
		r->start = 0;
	}
}

static void __init pcibios_allocate_resources(int pass)
{
	struct pci_dev *dev = NULL;
	int idx, disabled;
	u16 command;
	struct resource *r;

	for_each_pci_dev(dev) {
		pci_read_config_word(dev, PCI_COMMAND, &command);
		for (idx = 0; idx <= PCI_ROM_RESOURCE; idx++) {
			r = &dev->resource[idx];
			if (r->parent)		/* Already allocated */
				continue;
			if (!r->flags || (r->flags & IORESOURCE_UNSET))
				continue;	/* Not assigned at all */
			/* We only allocate ROMs on pass 1 just in case they
			 * have been screwed up by firmware
			 */
			if (idx == PCI_ROM_RESOURCE)
				disabled = 1;
			if (r->flags & IORESOURCE_IO)
				disabled = !(command & PCI_COMMAND_IO);
			else
				disabled = !(command & PCI_COMMAND_MEMORY);
			if (pass == disabled)
				alloc_resource(dev, idx);
		}
		if (pass)
			continue;
		r = &dev->resource[PCI_ROM_RESOURCE];
		if (r->flags) {
			/* Turn the ROM off, leave the resource region,
			 * but keep it unregistered.
			 */
			u32 reg;
			pci_read_config_dword(dev, dev->rom_base_reg, &reg);
			if (reg & PCI_ROM_ADDRESS_ENABLE) {
				pr_debug("PCI: Switching off ROM of %s\n",
					 pci_name(dev));
				r->flags &= ~IORESOURCE_ROM_ENABLE;
				pci_write_config_dword(dev, dev->rom_base_reg,
						reg & ~PCI_ROM_ADDRESS_ENABLE);
			}
		}
	}
}

static void __init pcibios_reserve_legacy_regions(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	resource_size_t	offset;
	struct resource *res, *pres;
	int i;

	pr_debug("Reserving legacy ranges for domain %04x\n",
							pci_domain_nr(bus));

	/* Check for IO */
	if (!(hose->io_resource.flags & IORESOURCE_IO))
		goto no_io;
	offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	BUG_ON(res == NULL);
	res->name = "Legacy IO";
	res->flags = IORESOURCE_IO;
	res->start = offset;
	res->end = (offset + 0xfff) & 0xfffffffful;
	pr_debug("Candidate legacy IO: %pR\n", res);
	if (request_resource(&hose->io_resource, res)) {
		pr_debug("PCI %04x:%02x Cannot reserve Legacy IO %pR\n",
		       pci_domain_nr(bus), bus->number, res);
		kfree(res);
	}

 no_io:
	/* Check for memory */
	offset = hose->pci_mem_offset;
	pr_debug("hose mem offset: %016llx\n", (unsigned long long)offset);
	for (i = 0; i < 3; i++) {
		pres = &hose->mem_resources[i];
		if (!(pres->flags & IORESOURCE_MEM))
			continue;
		pr_debug("hose mem res: %pR\n", pres);
		if ((pres->start - offset) <= 0xa0000 &&
		    (pres->end - offset) >= 0xbffff)
			break;
	}
	if (i >= 3)
		return;
	res = kzalloc(sizeof(struct resource), GFP_KERNEL);
	BUG_ON(res == NULL);
	res->name = "Legacy VGA memory";
	res->flags = IORESOURCE_MEM;
	res->start = 0xa0000 + offset;
	res->end = 0xbffff + offset;
	pr_debug("Candidate VGA memory: %pR\n", res);
	if (request_resource(pres, res)) {
		pr_debug("PCI %04x:%02x Cannot reserve VGA memory %pR\n",
		       pci_domain_nr(bus), bus->number, res);
		kfree(res);
	}
}

void __init pcibios_resource_survey(void)
{
	struct pci_bus *b;

	/* Allocate and assign resources. If we re-assign everything, then
	 * we skip the allocate phase
	 */
	list_for_each_entry(b, &pci_root_buses, node)
		pcibios_allocate_bus_resources(b);

	pcibios_allocate_resources(0);
	pcibios_allocate_resources(1);

	/* Before we start assigning unassigned resource, we try to reserve
	 * the low IO area and the VGA memory area if they intersect the
	 * bus available resources to avoid allocating things on top of them
	 */
	list_for_each_entry(b, &pci_root_buses, node)
		pcibios_reserve_legacy_regions(b);

	/* Now proceed to assigning things that were left unassigned */
	pr_debug("PCI: Assigning unassigned resources...\n");
	pci_assign_unassigned_resources();
}

/* This is used by the PCI hotplug driver to allocate resource
 * of newly plugged busses. We can try to consolidate with the
 * rest of the code later, for now, keep it as-is as our main
 * resource allocation function doesn't deal with sub-trees yet.
 */
void pcibios_claim_one_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child_bus;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;

			pr_debug("PCI: Claiming %s: ", pci_name(dev));
			pr_debug("Resource %d: %016llx..%016llx [%x]\n",
				 i, (unsigned long long)r->start,
				 (unsigned long long)r->end,
				 (unsigned int)r->flags);

			if (pci_claim_resource(dev, i) == 0)
				continue;

			pci_claim_bridge_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &bus->children, node)
		pcibios_claim_one_bus(child_bus);
}
EXPORT_SYMBOL_GPL(pcibios_claim_one_bus);


/* pcibios_finish_adding_to_bus
 *
 * This is to be called by the hotplug code after devices have been
 * added to a bus, this include calling it for a PHB that is just
 * being added
 */
void pcibios_finish_adding_to_bus(struct pci_bus *bus)
{
	pr_debug("PCI: Finishing adding to hotplug bus %04x:%02x\n",
		 pci_domain_nr(bus), bus->number);

	/* Allocate bus and devices resources */
	pcibios_allocate_bus_resources(bus);
	pcibios_claim_one_bus(bus);

	/* Add new devices to global lists.  Register in proc, sysfs. */
	pci_bus_add_devices(bus);

	/* Fixup EEH */
	/* eeh_add_device_tree_late(bus); */
}
EXPORT_SYMBOL_GPL(pcibios_finish_adding_to_bus);

static void pcibios_setup_phb_resources(struct pci_controller *hose,
					struct list_head *resources)
{
	unsigned long io_offset;
	struct resource *res;
	int i;

	/* Hookup PHB IO resource */
	res = &hose->io_resource;

	/* Fixup IO space offset */
	io_offset = (unsigned long)hose->io_base_virt - isa_io_base;
	res->start = (res->start + io_offset) & 0xffffffffu;
	res->end = (res->end + io_offset) & 0xffffffffu;

	if (!res->flags) {
		pr_warn("PCI: I/O resource not set for host ");
		pr_cont("bridge %s (domain %d)\n",
			hose->dn->full_name, hose->global_number);
		/* Workaround for lack of IO resource only on 32-bit */
		res->start = (unsigned long)hose->io_base_virt - isa_io_base;
		res->end = res->start + IO_SPACE_LIMIT;
		res->flags = IORESOURCE_IO;
	}
	pci_add_resource_offset(resources, res,
		(__force resource_size_t)(hose->io_base_virt - _IO_BASE));

	pr_debug("PCI: PHB IO resource    = %016llx-%016llx [%lx]\n",
		 (unsigned long long)res->start,
		 (unsigned long long)res->end,
		 (unsigned long)res->flags);

	/* Hookup PHB Memory resources */
	for (i = 0; i < 3; ++i) {
		res = &hose->mem_resources[i];
		if (!res->flags) {
			if (i > 0)
				continue;
			pr_err("PCI: Memory resource 0 not set for ");
			pr_cont("host bridge %s (domain %d)\n",
				hose->dn->full_name, hose->global_number);

			/* Workaround for lack of MEM resource only on 32-bit */
			res->start = hose->pci_mem_offset;
			res->end = (resource_size_t)-1LL;
			res->flags = IORESOURCE_MEM;

		}
		pci_add_resource_offset(resources, res, hose->pci_mem_offset);

		pr_debug("PCI: PHB MEM resource %d = %016llx-%016llx [%lx]\n",
			i, (unsigned long long)res->start,
			(unsigned long long)res->end,
			(unsigned long)res->flags);
	}

	pr_debug("PCI: PHB MEM offset     = %016llx\n",
		 (unsigned long long)hose->pci_mem_offset);
	pr_debug("PCI: PHB IO  offset     = %08lx\n",
		 (unsigned long)hose->io_base_virt - _IO_BASE);
}

static void pcibios_scan_phb(struct pci_controller *hose)
{
	LIST_HEAD(resources);
	struct pci_bus *bus;
	struct device_node *node = hose->dn;

	pr_debug("PCI: Scanning PHB %s\n", of_node_full_name(node));

	pcibios_setup_phb_resources(hose, &resources);

	bus = pci_scan_root_bus(hose->parent, hose->first_busno,
				hose->ops, hose, &resources);
	if (bus == NULL) {
		pr_err("Failed to create bus for PCI domain %04x\n",
		       hose->global_number);
		pci_free_resource_list(&resources);
		return;
	}
	bus->busn_res.start = hose->first_busno;
	hose->bus = bus;

	hose->last_busno = bus->busn_res.end;
}

static int __init pcibios_init(void)
{
	struct pci_controller *hose, *tmp;
	int next_busno = 0;

	pr_info("PCI: Probing PCI hardware\n");

	/* Scan all of the recorded PCI controllers.  */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		hose->last_busno = 0xff;
		pcibios_scan_phb(hose);
		if (next_busno <= hose->last_busno)
			next_busno = hose->last_busno + 1;
	}
	pci_bus_count = next_busno;

	/* Call common code to handle resource allocation */
	pcibios_resource_survey();
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		if (hose->bus)
			pci_bus_add_devices(hose->bus);
	}

	return 0;
}

subsys_initcall(pcibios_init);

static struct pci_controller *pci_bus_to_hose(int bus)
{
	struct pci_controller *hose, *tmp;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
		if (bus >= hose->first_busno && bus <= hose->last_busno)
			return hose;
	return NULL;
}

/* Provide information on locations of various I/O regions in physical
 * memory.  Do this on a per-card basis so that we choose the right
 * root bridge.
 * Note that the returned IO or memory base is a physical address
 */

long sys_pciconfig_iobase(long which, unsigned long bus, unsigned long devfn)
{
	struct pci_controller *hose;
	long result = -EOPNOTSUPP;

	hose = pci_bus_to_hose(bus);
	if (!hose)
		return -ENODEV;

	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)hose->first_busno;
	case IOBASE_MEMORY:
		return (long)hose->pci_mem_offset;
	case IOBASE_IO:
		return (long)hose->io_base_phys;
	case IOBASE_ISA_IO:
		return (long)isa_io_base;
	case IOBASE_ISA_MEM:
		return (long)isa_mem_base;
	}

	return result;
}

/*
 * Null PCI config access functions, for the case when we can't
 * find a hose.
 */
#define NULL_PCI_OP(rw, size, type)					\
static int								\
null_##rw##_config_##size(struct pci_dev *dev, int offset, type val)	\
{									\
	return PCIBIOS_DEVICE_NOT_FOUND;				\
}

static int
null_read_config(struct pci_bus *bus, unsigned int devfn, int offset,
		 int len, u32 *val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static int
null_write_config(struct pci_bus *bus, unsigned int devfn, int offset,
		  int len, u32 val)
{
	return PCIBIOS_DEVICE_NOT_FOUND;
}

static struct pci_ops null_pci_ops = {
	.read = null_read_config,
	.write = null_write_config,
};

/*
 * These functions are used early on before PCI scanning is done
 * and all of the pci_dev and pci_bus structures have been created.
 */
static struct pci_bus *
fake_pci_bus(struct pci_controller *hose, int busnr)
{
	static struct pci_bus bus;

	if (!hose)
		pr_err("Can't find hose for PCI bus %d!\n", busnr);

	bus.number = busnr;
	bus.sysdata = hose;
	bus.ops = hose ? hose->ops : &null_pci_ops;
	return &bus;
}

#define EARLY_PCI_OP(rw, size, type)					\
int early_##rw##_config_##size(struct pci_controller *hose, int bus,	\
			       int devfn, int offset, type value)	\
{									\
	return pci_bus_##rw##_config_##size(fake_pci_bus(hose, bus),	\
					    devfn, offset, value);	\
}

EARLY_PCI_OP(read, byte, u8 *)
EARLY_PCI_OP(read, word, u16 *)
EARLY_PCI_OP(read, dword, u32 *)
EARLY_PCI_OP(write, byte, u8)
EARLY_PCI_OP(write, word, u16)
EARLY_PCI_OP(write, dword, u32)

int early_find_capability(struct pci_controller *hose, int bus, int devfn,
			  int cap)
{
	return pci_bus_find_capability(fake_pci_bus(hose, bus), devfn, cap);
}

