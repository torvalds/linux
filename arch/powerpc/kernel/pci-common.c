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

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/irq.h>
#include <linux/vmalloc.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/firmware.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

static DEFINE_SPINLOCK(hose_spinlock);

/* XXX kill that some day ... */
static int global_phb_number;		/* Global phb counter */

/* ISA Memory physical address */
resource_size_t isa_mem_base;

/* Default PCI flags is 0 */
unsigned int ppc_pci_flags;

struct pci_controller *pcibios_alloc_controller(struct device_node *dev)
{
	struct pci_controller *phb;

	phb = zalloc_maybe_bootmem(sizeof(struct pci_controller), GFP_KERNEL);
	if (phb == NULL)
		return NULL;
	spin_lock(&hose_spinlock);
	phb->global_number = global_phb_number++;
	list_add_tail(&phb->list_node, &hose_list);
	spin_unlock(&hose_spinlock);
	phb->dn = dev;
	phb->is_dynamic = mem_init_done;
#ifdef CONFIG_PPC64
	if (dev) {
		int nid = of_node_to_nid(dev);

		if (nid < 0 || !node_online(nid))
			nid = -1;

		PHB_SET_NODE(phb, nid);
	}
#endif
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

int pcibios_vaddr_is_ioport(void __iomem *address)
{
	int ret = 0;
	struct pci_controller *hose;
	unsigned long size;

	spin_lock(&hose_spinlock);
	list_for_each_entry(hose, &hose_list, list_node) {
#ifdef CONFIG_PPC64
		size = hose->pci_io_size;
#else
		size = hose->io_resource.end - hose->io_resource.start + 1;
#endif
		if (address >= hose->io_base_virt &&
		    address < (hose->io_base_virt + size)) {
			ret = 1;
			break;
		}
	}
	spin_unlock(&hose_spinlock);
	return ret;
}

/*
 * Return the domain number for this bus.
 */
int pci_domain_nr(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);

	return hose->global_number;
}
EXPORT_SYMBOL(pci_domain_nr);

#ifdef CONFIG_PPC_OF

/* This routine is meant to be used early during boot, when the
 * PCI bus numbers have not yet been assigned, and you need to
 * issue PCI config cycles to an OF device.
 * It could also be used to "fix" RTAS config cycles if you want
 * to set pci_assign_all_buses to 1 and still use RTAS for PCI
 * config cycles.
 */
struct pci_controller* pci_find_hose_for_OF_device(struct device_node* node)
{
	if (!have_of)
		return NULL;
	while(node) {
		struct pci_controller *hose, *tmp;
		list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
			if (hose->dn == node)
				return hose;
		node = node->parent;
	}
	return NULL;
}

static ssize_t pci_show_devspec(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev;
	struct device_node *np;

	pdev = to_pci_dev (dev);
	np = pci_device_to_OF_node(pdev);
	if (np == NULL || np->full_name == NULL)
		return 0;
	return sprintf(buf, "%s", np->full_name);
}
static DEVICE_ATTR(devspec, S_IRUGO, pci_show_devspec, NULL);
#endif /* CONFIG_PPC_OF */

/* Add sysfs properties */
int pcibios_add_platform_entries(struct pci_dev *pdev)
{
#ifdef CONFIG_PPC_OF
	return device_create_file(&pdev->dev, &dev_attr_devspec);
#else
	return 0;
#endif /* CONFIG_PPC_OF */

}

char __devinit *pcibios_setup(char *str)
{
	return str;
}

/*
 * Reads the interrupt pin to determine if interrupt is use by card.
 * If the interrupt is used, then gets the interrupt line from the
 * openfirmware and sets it in the pci_dev and pci_config line.
 */
int pci_read_irq_line(struct pci_dev *pci_dev)
{
	struct of_irq oirq;
	unsigned int virq;

	/* The current device-tree that iSeries generates from the HV
	 * PCI informations doesn't contain proper interrupt routing,
	 * and all the fallback would do is print out crap, so we
	 * don't attempt to resolve the interrupts here at all, some
	 * iSeries specific fixup does it.
	 *
	 * In the long run, we will hopefully fix the generated device-tree
	 * instead.
	 */
#ifdef CONFIG_PPC_ISERIES
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return -1;
#endif

	DBG("Try to map irq for %s...\n", pci_name(pci_dev));

#ifdef DEBUG
	memset(&oirq, 0xff, sizeof(oirq));
#endif
	/* Try to get a mapping from the device-tree */
	if (of_irq_map_pci(pci_dev, &oirq)) {
		u8 line, pin;

		/* If that fails, lets fallback to what is in the config
		 * space and map that through the default controller. We
		 * also set the type to level low since that's what PCI
		 * interrupts are. If your platform does differently, then
		 * either provide a proper interrupt tree or don't use this
		 * function.
		 */
		if (pci_read_config_byte(pci_dev, PCI_INTERRUPT_PIN, &pin))
			return -1;
		if (pin == 0)
			return -1;
		if (pci_read_config_byte(pci_dev, PCI_INTERRUPT_LINE, &line) ||
		    line == 0xff || line == 0) {
			return -1;
		}
		DBG(" -> no map ! Using line %d (pin %d) from PCI config\n",
		    line, pin);

		virq = irq_create_mapping(NULL, line);
		if (virq != NO_IRQ)
			set_irq_type(virq, IRQ_TYPE_LEVEL_LOW);
	} else {
		DBG(" -> got one, spec %d cells (0x%08x 0x%08x...) on %s\n",
		    oirq.size, oirq.specifier[0], oirq.specifier[1],
		    oirq.controller->full_name);

		virq = irq_create_of_mapping(oirq.controller, oirq.specifier,
					     oirq.size);
	}
	if(virq == NO_IRQ) {
		DBG(" -> failed to map !\n");
		return -1;
	}

	DBG(" -> mapped to linux irq %d\n", virq);

	pci_dev->irq = virq;

	return 0;
}
EXPORT_SYMBOL(pci_read_irq_line);

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

	if (hose == 0)
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
 * Set vm_page_prot of VMA, as appropriate for this architecture, for a pci
 * device mapping.
 */
static pgprot_t __pci_mmap_set_pgprot(struct pci_dev *dev, struct resource *rp,
				      pgprot_t protection,
				      enum pci_mmap_state mmap_state,
				      int write_combine)
{
	unsigned long prot = pgprot_val(protection);

	/* Write combine is always 0 on non-memory space mappings. On
	 * memory space, if the user didn't pass 1, we check for a
	 * "prefetchable" resource. This is a bit hackish, but we use
	 * this to workaround the inability of /sysfs to provide a write
	 * combine bit
	 */
	if (mmap_state != pci_mmap_mem)
		write_combine = 0;
	else if (write_combine == 0) {
		if (rp->flags & IORESOURCE_PREFETCH)
			write_combine = 1;
	}

	/* XXX would be nice to have a way to ask for write-through */
	prot |= _PAGE_NO_CACHE;
	if (write_combine)
		prot &= ~_PAGE_GUARDED;
	else
		prot |= _PAGE_GUARDED;

	return __pgprot(prot);
}

/*
 * This one is used by /dev/mem and fbdev who have no clue about the
 * PCI device, it tries to find the PCI device first and calls the
 * above routine
 */
pgprot_t pci_phys_mem_access_prot(struct file *file,
				  unsigned long pfn,
				  unsigned long size,
				  pgprot_t protection)
{
	struct pci_dev *pdev = NULL;
	struct resource *found = NULL;
	unsigned long prot = pgprot_val(protection);
	unsigned long offset = pfn << PAGE_SHIFT;
	int i;

	if (page_is_ram(pfn))
		return __pgprot(prot);

	prot |= _PAGE_NO_CACHE | _PAGE_GUARDED;

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
			prot &= ~_PAGE_GUARDED;
		pci_dev_put(pdev);
	}

	DBG("non-PCI map for %lx, prot: %lx\n", offset, prot);

	return __pgprot(prot);
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
int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	resource_size_t offset = vma->vm_pgoff << PAGE_SHIFT;
	struct resource *rp;
	int ret;

	rp = __pci_mmap_make_offset(dev, &offset, mmap_state);
	if (rp == NULL)
		return -EINVAL;

	vma->vm_pgoff = offset >> PAGE_SHIFT;
	vma->vm_page_prot = __pci_mmap_set_pgprot(dev, rp,
						  vma->vm_page_prot,
						  mmap_state, write_combine);

	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);

	return ret;
}

void pci_resource_to_user(const struct pci_dev *dev, int bar,
			  const struct resource *rsrc,
			  resource_size_t *start, resource_size_t *end)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	resource_size_t offset = 0;

	if (hose == NULL)
		return;

	if (rsrc->flags & IORESOURCE_IO)
		offset = (unsigned long)hose->io_base_virt - _IO_BASE;

	/* We pass a fully fixed up address to userland for MMIO instead of
	 * a BAR value because X is lame and expects to be able to use that
	 * to pass to /dev/mem !
	 *
	 * That means that we'll have potentially 64 bits values where some
	 * userland apps only expect 32 (like X itself since it thinks only
	 * Sparc has 64 bits MMIO) but if we don't do that, we break it on
	 * 32 bits CHRPs :-(
	 *
	 * Hopefully, the sysfs insterface is immune to that gunk. Once X
	 * has been fixed (and the fix spread enough), we can re-enable the
	 * 2 lines below and pass down a BAR value to userland. In that case
	 * we'll also have to re-enable the matching code in
	 * __pci_mmap_make_offset().
	 *
	 * BenH.
	 */
#if 0
	else if (rsrc->flags & IORESOURCE_MEM)
		offset = hose->pci_mem_offset;
#endif

	*start = rsrc->start - offset;
	*end = rsrc->end - offset;
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
void __devinit pci_process_bridge_OF_ranges(struct pci_controller *hose,
					    struct device_node *dev,
					    int primary)
{
	const u32 *ranges;
	int rlen;
	int pna = of_n_addr_cells(dev);
	int np = pna + 5;
	int memno = 0, isa_hole = -1;
	u32 pci_space;
	unsigned long long pci_addr, cpu_addr, pci_next, cpu_next, size;
	unsigned long long isa_mb = 0;
	struct resource *res;

	printk(KERN_INFO "PCI host bridge %s %s ranges:\n",
	       dev->full_name, primary ? "(primary)" : "");

	/* Get ranges property */
	ranges = of_get_property(dev, "ranges", &rlen);
	if (ranges == NULL)
		return;

	/* Parse it */
	while ((rlen -= np * 4) >= 0) {
		/* Read next ranges element */
		pci_space = ranges[0];
		pci_addr = of_read_number(ranges + 1, 2);
		cpu_addr = of_translate_address(dev, ranges + 3);
		size = of_read_number(ranges + pna + 3, 2);
		ranges += np;
		if (cpu_addr == OF_BAD_ADDR || size == 0)
			continue;

		/* Now consume following elements while they are contiguous */
		for (; rlen >= np * sizeof(u32);
		     ranges += np, rlen -= np * 4) {
			if (ranges[0] != pci_space)
				break;
			pci_next = of_read_number(ranges + 1, 2);
			cpu_next = of_translate_address(dev, ranges + 3);
			if (pci_next != pci_addr + size ||
			    cpu_next != cpu_addr + size)
				break;
			size += of_read_number(ranges + pna + 3, 2);
		}

		/* Act based on address space type */
		res = NULL;
		switch ((pci_space >> 24) & 0x3) {
		case 1:		/* PCI IO space */
			printk(KERN_INFO
			       "  IO 0x%016llx..0x%016llx -> 0x%016llx\n",
			       cpu_addr, cpu_addr + size - 1, pci_addr);

			/* We support only one IO range */
			if (hose->pci_io_size) {
				printk(KERN_INFO
				       " \\--> Skipped (too many) !\n");
				continue;
			}
#ifdef CONFIG_PPC32
			/* On 32 bits, limit I/O space to 16MB */
			if (size > 0x01000000)
				size = 0x01000000;

			/* 32 bits needs to map IOs here */
			hose->io_base_virt = ioremap(cpu_addr, size);

			/* Expect trouble if pci_addr is not 0 */
			if (primary)
				isa_io_base =
					(unsigned long)hose->io_base_virt;
#endif /* CONFIG_PPC32 */
			/* pci_io_size and io_base_phys always represent IO
			 * space starting at 0 so we factor in pci_addr
			 */
			hose->pci_io_size = pci_addr + size;
			hose->io_base_phys = cpu_addr - pci_addr;

			/* Build resource */
			res = &hose->io_resource;
			res->flags = IORESOURCE_IO;
			res->start = pci_addr;
			break;
		case 2:		/* PCI Memory space */
		case 3:		/* PCI 64 bits Memory space */
			printk(KERN_INFO
			       " MEM 0x%016llx..0x%016llx -> 0x%016llx %s\n",
			       cpu_addr, cpu_addr + size - 1, pci_addr,
			       (pci_space & 0x40000000) ? "Prefetch" : "");

			/* We support only 3 memory ranges */
			if (memno >= 3) {
				printk(KERN_INFO
				       " \\--> Skipped (too many) !\n");
				continue;
			}
			/* Handles ISA memory hole space here */
			if (pci_addr == 0) {
				isa_mb = cpu_addr;
				isa_hole = memno;
				if (primary || isa_mem_base == 0)
					isa_mem_base = cpu_addr;
			}

			/* We get the PCI/Mem offset from the first range or
			 * the, current one if the offset came from an ISA
			 * hole. If they don't match, bugger.
			 */
			if (memno == 0 ||
			    (isa_hole >= 0 && pci_addr != 0 &&
			     hose->pci_mem_offset == isa_mb))
				hose->pci_mem_offset = cpu_addr - pci_addr;
			else if (pci_addr != 0 &&
				 hose->pci_mem_offset != cpu_addr - pci_addr) {
				printk(KERN_INFO
				       " \\--> Skipped (offset mismatch) !\n");
				continue;
			}

			/* Build resource */
			res = &hose->mem_resources[memno++];
			res->flags = IORESOURCE_MEM;
			if (pci_space & 0x40000000)
				res->flags |= IORESOURCE_PREFETCH;
			res->start = cpu_addr;
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->end = res->start + size - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
	}

	/* If there's an ISA hole and the pci_mem_offset is -not- matching
	 * the ISA hole offset, then we need to remove the ISA hole from
	 * the resource list for that brige
	 */
	if (isa_hole >= 0 && hose->pci_mem_offset != isa_mb) {
		unsigned int next = isa_hole + 1;
		printk(KERN_INFO " Removing ISA hole at 0x%016llx\n", isa_mb);
		if (next < memno)
			memmove(&hose->mem_resources[isa_hole],
				&hose->mem_resources[next],
				sizeof(struct resource) * (memno - next));
		hose->mem_resources[--memno].flags = 0;
	}
}

/* Decide whether to display the domain number in /proc */
int pci_proc_domain(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
#ifdef CONFIG_PPC64
	return hose->buid != 0;
#else
	if (!(ppc_pci_flags & PPC_PCI_ENABLE_PROC_DOMAINS))
		return 0;
	if (ppc_pci_flags & PPC_PCI_COMPAT_DOMAIN_0)
		return hose->global_number != 0;
	return 1;
#endif
}

void pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			     struct resource *res)
{
	resource_size_t offset = 0, mask = (resource_size_t)-1;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);

	if (!hose)
		return;
	if (res->flags & IORESOURCE_IO) {
		offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		mask = 0xffffffffu;
	} else if (res->flags & IORESOURCE_MEM)
		offset = hose->pci_mem_offset;

	region->start = (res->start - offset) & mask;
	region->end = (res->end - offset) & mask;
}
EXPORT_SYMBOL(pcibios_resource_to_bus);

void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			     struct pci_bus_region *region)
{
	resource_size_t offset = 0, mask = (resource_size_t)-1;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);

	if (!hose)
		return;
	if (res->flags & IORESOURCE_IO) {
		offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		mask = 0xffffffffu;
	} else if (res->flags & IORESOURCE_MEM)
		offset = hose->pci_mem_offset;
	res->start = (region->start + offset) & mask;
	res->end = (region->end + offset) & mask;
}
EXPORT_SYMBOL(pcibios_bus_to_resource);

/* Fixup a bus resource into a linux resource */
static void __devinit fixup_resource(struct resource *res, struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	resource_size_t offset = 0, mask = (resource_size_t)-1;

	if (res->flags & IORESOURCE_IO) {
		offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		mask = 0xffffffffu;
	} else if (res->flags & IORESOURCE_MEM)
		offset = hose->pci_mem_offset;

	res->start = (res->start + offset) & mask;
	res->end = (res->end + offset) & mask;

	pr_debug("PCI:%s            %016llx-%016llx\n",
		 pci_name(dev),
		 (unsigned long long)res->start,
		 (unsigned long long)res->end);
}


/* This header fixup will do the resource fixup for all devices as they are
 * probed, but not for bridge ranges
 */
static void __devinit pcibios_fixup_resources(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	int i;

	if (!hose) {
		printk(KERN_ERR "No host bridge for PCI dev %s !\n",
		       pci_name(dev));
		return;
	}
	for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
		struct resource *res = dev->resource + i;
		if (!res->flags)
			continue;
		/* On platforms that have PPC_PCI_PROBE_ONLY set, we don't
		 * consider 0 as an unassigned BAR value. It's technically
		 * a valid value, but linux doesn't like it... so when we can
		 * re-assign things, we do so, but if we can't, we keep it
		 * around and hope for the best...
		 */
		if (res->start == 0 && !(ppc_pci_flags & PPC_PCI_PROBE_ONLY)) {
			pr_debug("PCI:%s Resource %d %016llx-%016llx [%x] is unassigned\n",
				 pci_name(dev), i,
				 (unsigned long long)res->start,
				 (unsigned long long)res->end,
				 (unsigned int)res->flags);
			res->end -= res->start;
			res->start = 0;
			res->flags |= IORESOURCE_UNSET;
			continue;
		}

		pr_debug("PCI:%s Resource %d %016llx-%016llx [%x] fixup...\n",
			 pci_name(dev), i,
			 (unsigned long long)res->start,\
			 (unsigned long long)res->end,
			 (unsigned int)res->flags);

		fixup_resource(res, dev);
	}

	/* Call machine specific resource fixup */
	if (ppc_md.pcibios_fixup_resources)
		ppc_md.pcibios_fixup_resources(dev);
}
DECLARE_PCI_FIXUP_HEADER(PCI_ANY_ID, PCI_ANY_ID, pcibios_fixup_resources);

static void __devinit __pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pci_dev *dev = bus->self;

	pr_debug("PCI: Fixup bus %d (%s)\n", bus->number, dev ? pci_name(dev) : "PHB");

	/* Fixup PCI<->PCI bridges. Host bridges are handled separately, for
	 * now differently between 32 and 64 bits.
	 */
	if (dev != NULL) {
		struct resource *res;
		int i;

		for (i = 0; i < PCI_BUS_NUM_RESOURCES; ++i) {
			if ((res = bus->resource[i]) == NULL)
				continue;
			if (!res->flags)
				continue;
			if (i >= 3 && bus->self->transparent)
				continue;
			/* On PowerMac, Apple leaves bridge windows open over
			 * an inaccessible region of memory space (0...fffff)
			 * which is somewhat bogus, but that's what they think
			 * means disabled...
			 *
			 * We clear those to force them to be reallocated later
			 *
			 * We detect such regions by the fact that the base is
			 * equal to the pci_mem_offset of the host bridge and
			 * their size is smaller than 1M.
			 */
			if (res->flags & IORESOURCE_MEM &&
			    res->start == hose->pci_mem_offset &&
			    res->end < 0x100000) {
				printk(KERN_INFO
				       "PCI: Closing bogus Apple Firmware"
				       " region %d on bus 0x%02x\n",
				       i, bus->number);
				res->flags = 0;
				continue;
			}

			pr_debug("PCI:%s Bus rsrc %d %016llx-%016llx [%x] fixup...\n",
				 pci_name(dev), i,
				 (unsigned long long)res->start,\
				 (unsigned long long)res->end,
				 (unsigned int)res->flags);

			fixup_resource(res, dev);
		}
	}

	/* Additional setup that is different between 32 and 64 bits for now */
	pcibios_do_bus_setup(bus);

	/* Platform specific bus fixups */
	if (ppc_md.pcibios_fixup_bus)
		ppc_md.pcibios_fixup_bus(bus);

	/* Read default IRQs and fixup if necessary */
	list_for_each_entry(dev, &bus->devices, bus_list) {
		pci_read_irq_line(dev);
		if (ppc_md.pci_irq_fixup)
			ppc_md.pci_irq_fixup(dev);
	}
}

void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	/* When called from the generic PCI probe, read PCI<->PCI bridge
	 * bases before proceeding
	 */
	if (bus->self != NULL)
		pci_read_bridge_bases(bus);
	__pcibios_fixup_bus(bus);
}
EXPORT_SYMBOL(pcibios_fixup_bus);

/* When building a bus from the OF tree rather than probing, we need a
 * slightly different version of the fixup which doesn't read the
 * bridge bases using config space accesses
 */
void __devinit pcibios_fixup_of_probed_bus(struct pci_bus *bus)
{
	__pcibios_fixup_bus(bus);
}

static int skip_isa_ioresource_align(struct pci_dev *dev)
{
	if ((ppc_pci_flags & PPC_PCI_CAN_SKIP_ISA_ALIGN) &&
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
void pcibios_align_resource(void *data, struct resource *res,
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
		DBG(KERN_INFO "PCI: reparented %s [%llx..%llx] under %s\n",
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

static void __init pcibios_allocate_bus_resources(struct list_head *bus_list)
{
	struct pci_bus *bus;
	int i;
	struct resource *res, *pr;

	/* Depth-First Search on bus tree */
	list_for_each_entry(bus, bus_list, node) {
		for (i = 0; i < PCI_BUS_NUM_RESOURCES; ++i) {
			if ((res = bus->resource[i]) == NULL || !res->flags
			    || res->start > res->end)
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
				if (ppc_pci_flags & PPC_PCI_REASSIGN_ALL_RSRC)
					goto clear_resource;
				pr = pci_find_parent_resource(bus->self, res);
				if (pr == res) {
					/* this happens when the generic PCI
					 * code (wrongly) decides that this
					 * bridge is transparent  -- paulus
					 */
					continue;
				}
			}

			DBG("PCI: %s (bus %d) bridge rsrc %d: %016llx-%016llx "
			    "[0x%x], parent %p (%s)\n",
			    bus->self ? pci_name(bus->self) : "PHB",
			    bus->number, i,
			    (unsigned long long)res->start,
			    (unsigned long long)res->end,
			    (unsigned int)res->flags,
			    pr, (pr && pr->name) ? pr->name : "nil");

			if (pr && !(pr->flags & IORESOURCE_UNSET)) {
				if (request_resource(pr, res) == 0)
					continue;
				/*
				 * Must be a conflict with an existing entry.
				 * Move that entry (or entries) under the
				 * bridge resource and try again.
				 */
				if (reparent_resources(pr, res) == 0)
					continue;
			}
			printk(KERN_WARNING
			       "PCI: Cannot allocate resource region "
			       "%d of PCI bridge %d, will remap\n",
			       i, bus->number);
clear_resource:
			res->flags = 0;
		}
		pcibios_allocate_bus_resources(&bus->children);
	}
}

static inline void __devinit alloc_resource(struct pci_dev *dev, int idx)
{
	struct resource *pr, *r = &dev->resource[idx];

	DBG("PCI: Allocating %s: Resource %d: %016llx..%016llx [%x]\n",
	    pci_name(dev), idx,
	    (unsigned long long)r->start,
	    (unsigned long long)r->end,
	    (unsigned int)r->flags);

	pr = pci_find_parent_resource(dev, r);
	if (!pr || (pr->flags & IORESOURCE_UNSET) ||
	    request_resource(pr, r) < 0) {
		printk(KERN_WARNING "PCI: Cannot allocate resource region %d"
		       " of device %s, will remap\n", idx, pci_name(dev));
		if (pr)
			DBG("PCI:  parent is %p: %016llx-%016llx [%x]\n", pr,
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
		for (idx = 0; idx < 6; idx++) {
			r = &dev->resource[idx];
			if (r->parent)		/* Already allocated */
				continue;
			if (!r->flags || (r->flags & IORESOURCE_UNSET))
				continue;	/* Not assigned at all */
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
		if (r->flags & IORESOURCE_ROM_ENABLE) {
			/* Turn the ROM off, leave the resource region,
			 * but keep it unregistered.
			 */
			u32 reg;
			DBG("PCI: Switching off ROM of %s\n", pci_name(dev));
			r->flags &= ~IORESOURCE_ROM_ENABLE;
			pci_read_config_dword(dev, dev->rom_base_reg, &reg);
			pci_write_config_dword(dev, dev->rom_base_reg,
					       reg & ~PCI_ROM_ADDRESS_ENABLE);
		}
	}
}

void __init pcibios_resource_survey(void)
{
	/* Allocate and assign resources. If we re-assign everything, then
	 * we skip the allocate phase
	 */
	pcibios_allocate_bus_resources(&pci_root_buses);

	if (!(ppc_pci_flags & PPC_PCI_REASSIGN_ALL_RSRC)) {
		pcibios_allocate_resources(0);
		pcibios_allocate_resources(1);
	}

	if (!(ppc_pci_flags & PPC_PCI_PROBE_ONLY)) {
		DBG("PCI: Assigning unassigned resouces...\n");
		pci_assign_unassigned_resources();
	}

	/* Call machine dependent fixup */
	if (ppc_md.pcibios_fixup)
		ppc_md.pcibios_fixup();
}

#ifdef CONFIG_HOTPLUG
/* This is used by the pSeries hotplug driver to allocate resource
 * of newly plugged busses. We can try to consolidate with the
 * rest of the code later, for now, keep it as-is
 */
void __devinit pcibios_claim_one_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child_bus;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;
			pci_claim_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &bus->children, node)
		pcibios_claim_one_bus(child_bus);
}
EXPORT_SYMBOL_GPL(pcibios_claim_one_bus);
#endif /* CONFIG_HOTPLUG */

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	if (ppc_md.pcibios_enable_device_hook)
		if (ppc_md.pcibios_enable_device_hook(dev))
			return -EINVAL;

	return pci_enable_resources(dev, mask);
}
