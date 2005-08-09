/*
 * Port for PPC64 David Engebretsen, IBM Corp.
 * Contains common pci routines for ppc64 platform, pSeries and iSeries brands.
 * 
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *   Rework, based on alpha PCI code.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/list.h>

#include <asm/processor.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/udbg.h>

#include "pci.h"

#ifdef DEBUG
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...)
#endif

unsigned long pci_probe_only = 1;
unsigned long pci_assign_all_buses = 0;

/*
 * legal IO pages under MAX_ISA_PORT.  This is to ensure we don't touch
 * devices we don't have access to.
 */
unsigned long io_page_mask;

EXPORT_SYMBOL(io_page_mask);


unsigned int pcibios_assign_all_busses(void)
{
	return pci_assign_all_buses;
}

/* pci_io_base -- the base address from which io bars are offsets.
 * This is the lowest I/O base address (so bar values are always positive),
 * and it *must* be the start of ISA space if an ISA bus exists because
 * ISA drivers use hard coded offsets.  If no ISA bus exists a dummy
 * page is mapped and isa_io_limit prevents access to it.
 */
unsigned long isa_io_base;	/* NULL if no ISA bus */
EXPORT_SYMBOL(isa_io_base);
unsigned long pci_io_base;
EXPORT_SYMBOL(pci_io_base);

void iSeries_pcibios_init(void);

LIST_HEAD(hose_list);

struct dma_mapping_ops pci_dma_ops;
EXPORT_SYMBOL(pci_dma_ops);

int global_phb_number;		/* Global phb counter */

/* Cached ISA bridge dev. */
struct pci_dev *ppc64_isabridge_dev = NULL;

static void fixup_broken_pcnet32(struct pci_dev* dev)
{
	if ((dev->class>>8 == PCI_CLASS_NETWORK_ETHERNET)) {
		dev->vendor = PCI_VENDOR_ID_AMD;
		pci_write_config_word(dev, PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
		pci_name_device(dev);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TRIDENT, PCI_ANY_ID, fixup_broken_pcnet32);

void  pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			      struct resource *res)
{
	unsigned long offset = 0;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);

	if (!hose)
		return;

	if (res->flags & IORESOURCE_IO)
	        offset = (unsigned long)hose->io_base_virt - pci_io_base;

	if (res->flags & IORESOURCE_MEM)
		offset = hose->pci_mem_offset;

	region->start = res->start - offset;
	region->end = res->end - offset;
}

void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			      struct pci_bus_region *region)
{
	unsigned long offset = 0;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);

	if (!hose)
		return;

	if (res->flags & IORESOURCE_IO)
	        offset = (unsigned long)hose->io_base_virt - pci_io_base;

	if (res->flags & IORESOURCE_MEM)
		offset = hose->pci_mem_offset;

	res->start = region->start + offset;
	res->end = region->end + offset;
}

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pcibios_resource_to_bus);
EXPORT_SYMBOL(pcibios_bus_to_resource);
#endif

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
			    unsigned long size, unsigned long align)
{
	struct pci_dev *dev = data;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	unsigned long start = res->start;
	unsigned long alignto;

	if (res->flags & IORESOURCE_IO) {
	        unsigned long offset = (unsigned long)hose->io_base_virt -
					pci_io_base;
		/* Make sure we start at our min on all hoses */
		if (start - offset < PCIBIOS_MIN_IO)
			start = PCIBIOS_MIN_IO + offset;

		/*
		 * Put everything into 0x00-0xff region modulo 0x400
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;

	} else if (res->flags & IORESOURCE_MEM) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->pci_mem_offset < PCIBIOS_MIN_MEM)
			start = PCIBIOS_MIN_MEM + hose->pci_mem_offset;

		/* Align to multiple of size of minimum base.  */
		alignto = max(0x1000UL, align);
		start = ALIGN(start, alignto);
	}

	res->start = start;
}

static DEFINE_SPINLOCK(hose_spinlock);

/*
 * pci_controller(phb) initialized common variables.
 */
void __devinit pci_setup_pci_controller(struct pci_controller *hose)
{
	memset(hose, 0, sizeof(struct pci_controller));

	spin_lock(&hose_spinlock);
	hose->global_number = global_phb_number++;
	list_add_tail(&hose->list_node, &hose_list);
	spin_unlock(&hose_spinlock);
}

static void __init pcibios_claim_one_bus(struct pci_bus *b)
{
	struct pci_dev *dev;
	struct pci_bus *child_bus;

	list_for_each_entry(dev, &b->devices, bus_list) {
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;
			pci_claim_resource(dev, i);
		}
	}

	list_for_each_entry(child_bus, &b->children, node)
		pcibios_claim_one_bus(child_bus);
}

#ifndef CONFIG_PPC_ISERIES
static void __init pcibios_claim_of_setup(void)
{
	struct pci_bus *b;

	list_for_each_entry(b, &pci_root_buses, node)
		pcibios_claim_one_bus(b);
}
#endif

static int __init pcibios_init(void)
{
	struct pci_controller *hose, *tmp;
	struct pci_bus *bus;

	/* For now, override phys_mem_access_prot. If we need it,
	 * later, we may move that initialization to each ppc_md
	 */
	ppc_md.phys_mem_access_prot = pci_phys_mem_access_prot;

#ifdef CONFIG_PPC_ISERIES
	iSeries_pcibios_init(); 
#endif

	printk("PCI: Probing PCI hardware\n");

	/* Scan all of the recorded PCI controllers.  */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		hose->last_busno = 0xff;
		bus = pci_scan_bus(hose->first_busno, hose->ops,
				   hose->arch_data);
		hose->bus = bus;
		hose->last_busno = bus->subordinate;
	}

#ifndef CONFIG_PPC_ISERIES
	if (pci_probe_only)
		pcibios_claim_of_setup();
	else
		/* FIXME: `else' will be removed when
		   pci_assign_unassigned_resources() is able to work
		   correctly with [partially] allocated PCI tree. */
		pci_assign_unassigned_resources();
#endif /* !CONFIG_PPC_ISERIES */

	/* Call machine dependent final fixup */
	if (ppc_md.pcibios_fixup)
		ppc_md.pcibios_fixup();

	/* Cache the location of the ISA bridge (if we have one) */
	ppc64_isabridge_dev = pci_get_class(PCI_CLASS_BRIDGE_ISA << 8, NULL);
	if (ppc64_isabridge_dev != NULL)
		printk("ISA bridge at %s\n", pci_name(ppc64_isabridge_dev));

	printk("PCI: Probing PCI hardware done\n");

	return 0;
}

subsys_initcall(pcibios_init);

char __init *pcibios_setup(char *str)
{
	return str;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	u16 cmd, oldcmd;
	int i;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	oldcmd = cmd;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = &dev->resource[i];

		/* Only set up the requested stuff */
		if (!(mask & (1<<i)))
			continue;

		if (res->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (res->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}

	if (cmd != oldcmd) {
		printk(KERN_DEBUG "PCI: Enabling device: (%s), cmd %x\n",
		       pci_name(dev), cmd);
                /* Enable the appropriate bits in the PCI command register.  */
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

/*
 * Return the domain number for this bus.
 */
int pci_domain_nr(struct pci_bus *bus)
{
#ifdef CONFIG_PPC_ISERIES
	return 0;
#else
	struct pci_controller *hose = pci_bus_to_host(bus);

	return hose->global_number;
#endif
}

EXPORT_SYMBOL(pci_domain_nr);

/* Decide whether to display the domain number in /proc */
int pci_proc_domain(struct pci_bus *bus)
{
#ifdef CONFIG_PPC_ISERIES
	return 0;
#else
	struct pci_controller *hose = pci_bus_to_host(bus);
	return hose->buid;
#endif
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
					       unsigned long *offset,
					       enum pci_mmap_state mmap_state)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	unsigned long io_offset = 0;
	int i, res_bit;

	if (hose == 0)
		return NULL;		/* should never happen */

	/* If memory, add on the PCI bridge address offset */
	if (mmap_state == pci_mmap_mem) {
		*offset += hose->pci_mem_offset;
		res_bit = IORESOURCE_MEM;
	} else {
		io_offset = (unsigned long)hose->io_base_virt - pci_io_base;
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

	printk("PCI map for %s:%lx, prot: %lx\n", pci_name(dev), rp->start,
	       prot);

	return __pgprot(prot);
}

/*
 * This one is used by /dev/mem and fbdev who have no clue about the
 * PCI device, it tries to find the PCI device first and calls the
 * above routine
 */
pgprot_t pci_phys_mem_access_prot(struct file *file,
				  unsigned long offset,
				  unsigned long size,
				  pgprot_t protection)
{
	struct pci_dev *pdev = NULL;
	struct resource *found = NULL;
	unsigned long prot = pgprot_val(protection);
	int i;

	if (page_is_ram(offset >> PAGE_SHIFT))
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
			enum pci_mmap_state mmap_state,
			int write_combine)
{
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	struct resource *rp;
	int ret;

	rp = __pci_mmap_make_offset(dev, &offset, mmap_state);
	if (rp == NULL)
		return -EINVAL;

	vma->vm_pgoff = offset >> PAGE_SHIFT;
	vma->vm_flags |= VM_SHM | VM_LOCKED | VM_IO;
	vma->vm_page_prot = __pci_mmap_set_pgprot(dev, rp,
						  vma->vm_page_prot,
						  mmap_state, write_combine);

	ret = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);

	return ret;
}

#ifdef CONFIG_PPC_MULTIPLATFORM
static ssize_t pci_show_devspec(struct device *dev, struct device_attribute *attr, char *buf)
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
#endif /* CONFIG_PPC_MULTIPLATFORM */

void pcibios_add_platform_entries(struct pci_dev *pdev)
{
#ifdef CONFIG_PPC_MULTIPLATFORM
	device_create_file(&pdev->dev, &dev_attr_devspec);
#endif /* CONFIG_PPC_MULTIPLATFORM */
}

#ifdef CONFIG_PPC_MULTIPLATFORM

#define ISA_SPACE_MASK 0x1
#define ISA_SPACE_IO 0x1

static void __devinit pci_process_ISA_OF_ranges(struct device_node *isa_node,
				      unsigned long phb_io_base_phys,
				      void __iomem * phb_io_base_virt)
{
	struct isa_range *range;
	unsigned long pci_addr;
	unsigned int isa_addr;
	unsigned int size;
	int rlen = 0;

	range = (struct isa_range *) get_property(isa_node, "ranges", &rlen);
	if (range == NULL || (rlen < sizeof(struct isa_range))) {
		printk(KERN_ERR "no ISA ranges or unexpected isa range size,"
		       "mapping 64k\n");
		__ioremap_explicit(phb_io_base_phys,
				   (unsigned long)phb_io_base_virt,
				   0x10000, _PAGE_NO_CACHE | _PAGE_GUARDED);
		return;	
	}
	
	/* From "ISA Binding to 1275"
	 * The ranges property is laid out as an array of elements,
	 * each of which comprises:
	 *   cells 0 - 1:	an ISA address
	 *   cells 2 - 4:	a PCI address 
	 *			(size depending on dev->n_addr_cells)
	 *   cell 5:		the size of the range
	 */
	if ((range->isa_addr.a_hi && ISA_SPACE_MASK) == ISA_SPACE_IO) {
		isa_addr = range->isa_addr.a_lo;
		pci_addr = (unsigned long) range->pci_addr.a_mid << 32 | 
			range->pci_addr.a_lo;

		/* Assume these are both zero */
		if ((pci_addr != 0) || (isa_addr != 0)) {
			printk(KERN_ERR "unexpected isa to pci mapping: %s\n",
					__FUNCTION__);
			return;
		}
		
		size = PAGE_ALIGN(range->size);

		__ioremap_explicit(phb_io_base_phys, 
				   (unsigned long) phb_io_base_virt, 
				   size, _PAGE_NO_CACHE | _PAGE_GUARDED);
	}
}

void __devinit pci_process_bridge_OF_ranges(struct pci_controller *hose,
					    struct device_node *dev)
{
	unsigned int *ranges;
	unsigned long size;
	int rlen = 0;
	int memno = 0;
	struct resource *res;
	int np, na = prom_n_addr_cells(dev);
	unsigned long pci_addr, cpu_phys_addr;

	np = na + 5;

	/* From "PCI Binding to 1275"
	 * The ranges property is laid out as an array of elements,
	 * each of which comprises:
	 *   cells 0 - 2:	a PCI address
	 *   cells 3 or 3+4:	a CPU physical address
	 *			(size depending on dev->n_addr_cells)
	 *   cells 4+5 or 5+6:	the size of the range
	 */
	rlen = 0;
	hose->io_base_phys = 0;
	ranges = (unsigned int *) get_property(dev, "ranges", &rlen);
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		res = NULL;
		pci_addr = (unsigned long)ranges[1] << 32 | ranges[2];

		cpu_phys_addr = ranges[3];
		if (na == 2)
			cpu_phys_addr = cpu_phys_addr << 32 | ranges[4];

		size = (unsigned long)ranges[na+3] << 32 | ranges[na+4];
		if (size == 0)
			continue;
		switch ((ranges[0] >> 24) & 0x3) {
		case 1:		/* I/O space */
			hose->io_base_phys = cpu_phys_addr;
			hose->pci_io_size = size;

			res = &hose->io_resource;
			res->flags = IORESOURCE_IO;
			res->start = pci_addr;
			DBG("phb%d: IO 0x%lx -> 0x%lx\n", hose->global_number,
				    res->start, res->start + size - 1);
			break;
		case 2:		/* memory space */
			memno = 0;
			while (memno < 3 && hose->mem_resources[memno].flags)
				++memno;

			if (memno == 0)
				hose->pci_mem_offset = cpu_phys_addr - pci_addr;
			if (memno < 3) {
				res = &hose->mem_resources[memno];
				res->flags = IORESOURCE_MEM;
				res->start = cpu_phys_addr;
				DBG("phb%d: MEM 0x%lx -> 0x%lx\n", hose->global_number,
					    res->start, res->start + size - 1);
			}
			break;
		}
		if (res != NULL) {
			res->name = dev->full_name;
			res->end = res->start + size - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
		ranges += np;
	}
}

void __init pci_setup_phb_io(struct pci_controller *hose, int primary)
{
	unsigned long size = hose->pci_io_size;
	unsigned long io_virt_offset;
	struct resource *res;
	struct device_node *isa_dn;

	hose->io_base_virt = reserve_phb_iospace(size);
	DBG("phb%d io_base_phys 0x%lx io_base_virt 0x%lx\n",
		hose->global_number, hose->io_base_phys,
		(unsigned long) hose->io_base_virt);

	if (primary) {
		pci_io_base = (unsigned long)hose->io_base_virt;
		isa_dn = of_find_node_by_type(NULL, "isa");
		if (isa_dn) {
			isa_io_base = pci_io_base;
			pci_process_ISA_OF_ranges(isa_dn, hose->io_base_phys,
						hose->io_base_virt);
			of_node_put(isa_dn);
			/* Allow all IO */
			io_page_mask = -1;
		}
	}

	io_virt_offset = (unsigned long)hose->io_base_virt - pci_io_base;
	res = &hose->io_resource;
	res->start += io_virt_offset;
	res->end += io_virt_offset;
}

void __devinit pci_setup_phb_io_dynamic(struct pci_controller *hose,
					int primary)
{
	unsigned long size = hose->pci_io_size;
	unsigned long io_virt_offset;
	struct resource *res;

	hose->io_base_virt = __ioremap(hose->io_base_phys, size,
					_PAGE_NO_CACHE | _PAGE_GUARDED);
	DBG("phb%d io_base_phys 0x%lx io_base_virt 0x%lx\n",
		hose->global_number, hose->io_base_phys,
		(unsigned long) hose->io_base_virt);

	if (primary)
		pci_io_base = (unsigned long)hose->io_base_virt;

	io_virt_offset = (unsigned long)hose->io_base_virt - pci_io_base;
	res = &hose->io_resource;
	res->start += io_virt_offset;
	res->end += io_virt_offset;
}


static int get_bus_io_range(struct pci_bus *bus, unsigned long *start_phys,
				unsigned long *start_virt, unsigned long *size)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pci_bus_region region;
	struct resource *res;

	if (bus->self) {
		res = bus->resource[0];
		pcibios_resource_to_bus(bus->self, &region, res);
		*start_phys = hose->io_base_phys + region.start;
		*start_virt = (unsigned long) hose->io_base_virt + 
				region.start;
		if (region.end > region.start) 
			*size = region.end - region.start + 1;
		else {
			printk("%s(): unexpected region 0x%lx->0x%lx\n", 
					__FUNCTION__, region.start, region.end);
			return 1;
		}
		
	} else {
		/* Root Bus */
		res = &hose->io_resource;
		*start_phys = hose->io_base_phys;
		*start_virt = (unsigned long) hose->io_base_virt;
		if (res->end > res->start)
			*size = res->end - res->start + 1;
		else {
			printk("%s(): unexpected region 0x%lx->0x%lx\n", 
					__FUNCTION__, res->start, res->end);
			return 1;
		}
	}

	return 0;
}

int unmap_bus_range(struct pci_bus *bus)
{
	unsigned long start_phys;
	unsigned long start_virt;
	unsigned long size;

	if (!bus) {
		printk(KERN_ERR "%s() expected bus\n", __FUNCTION__);
		return 1;
	}
	
	if (get_bus_io_range(bus, &start_phys, &start_virt, &size))
		return 1;
	if (iounmap_explicit((void __iomem *) start_virt, size))
		return 1;

	return 0;
}
EXPORT_SYMBOL(unmap_bus_range);

int remap_bus_range(struct pci_bus *bus)
{
	unsigned long start_phys;
	unsigned long start_virt;
	unsigned long size;

	if (!bus) {
		printk(KERN_ERR "%s() expected bus\n", __FUNCTION__);
		return 1;
	}
	
	
	if (get_bus_io_range(bus, &start_phys, &start_virt, &size))
		return 1;
	printk("mapping IO %lx -> %lx, size: %lx\n", start_phys, start_virt, size);
	if (__ioremap_explicit(start_phys, start_virt, size,
			       _PAGE_NO_CACHE | _PAGE_GUARDED))
		return 1;

	return 0;
}
EXPORT_SYMBOL(remap_bus_range);

void phbs_remap_io(void)
{
	struct pci_controller *hose, *tmp;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node)
		remap_bus_range(hose->bus);
}

/*
 * ppc64 can have multifunction devices that do not respond to function 0.
 * In this case we must scan all functions.
 */
int pcibios_scan_all_fns(struct pci_bus *bus, int devfn)
{
       struct device_node *busdn, *dn;

       if (bus->self)
               busdn = pci_device_to_OF_node(bus->self);
       else
               busdn = bus->sysdata;   /* must be a phb */

       if (busdn == NULL)
	       return 0;

       /*
        * Check to see if there is any of the 8 functions are in the
        * device tree.  If they are then we need to scan all the
        * functions of this slot.
        */
       for (dn = busdn->child; dn; dn = dn->sibling)
               if ((dn->devfn >> 3) == (devfn >> 3))
                       return 1;

       return 0;
}


void __devinit pcibios_fixup_device_resources(struct pci_dev *dev,
					   struct pci_bus *bus)
{
	/* Update device resources.  */
	struct pci_controller *hose = pci_bus_to_host(bus);
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (dev->resource[i].flags & IORESOURCE_IO) {
			unsigned long offset = (unsigned long)hose->io_base_virt
				- pci_io_base;
                        unsigned long start, end, mask;

                        start = dev->resource[i].start += offset;
                        end = dev->resource[i].end += offset;

                        /* Need to allow IO access to pages that are in the
                           ISA range */
                        if (start < MAX_ISA_PORT) {
                                if (end > MAX_ISA_PORT)
                                        end = MAX_ISA_PORT;

                                start >>= PAGE_SHIFT;
                                end >>= PAGE_SHIFT;

                                /* get the range of pages for the map */
                                mask = ((1 << (end+1))-1) ^ ((1 << start)-1);
                                io_page_mask |= mask;
                        }
		}
                else if (dev->resource[i].flags & IORESOURCE_MEM) {
			dev->resource[i].start += hose->pci_mem_offset;
			dev->resource[i].end += hose->pci_mem_offset;
		}
        }
}
EXPORT_SYMBOL(pcibios_fixup_device_resources);

void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pci_dev *dev = bus->self;
	struct resource *res;
	int i;

	if (!dev) {
		/* Root bus. */

		hose->bus = bus;
		bus->resource[0] = res = &hose->io_resource;

		if (res->flags && request_resource(&ioport_resource, res))
			printk(KERN_ERR "Failed to request IO on "
					"PCI domain %d\n", pci_domain_nr(bus));

		for (i = 0; i < 3; ++i) {
			res = &hose->mem_resources[i];
			bus->resource[i+1] = res;
			if (res->flags && request_resource(&iomem_resource, res))
				printk(KERN_ERR "Failed to request MEM on "
						"PCI domain %d\n",
						pci_domain_nr(bus));
		}
	} else if (pci_probe_only &&
		   (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		/* This is a subordinate bridge */

		pci_read_bridge_bases(bus);
		pcibios_fixup_device_resources(dev, bus);
	}

	ppc_md.iommu_bus_setup(bus);

	list_for_each_entry(dev, &bus->devices, bus_list)
		ppc_md.iommu_dev_setup(dev);

	if (ppc_md.irq_bus_setup)
		ppc_md.irq_bus_setup(bus);

	if (!pci_probe_only)
		return;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
	}
}
EXPORT_SYMBOL(pcibios_fixup_bus);

/*
 * Reads the interrupt pin to determine if interrupt is use by card.
 * If the interrupt is used, then gets the interrupt line from the 
 * openfirmware and sets it in the pci_dev and pci_config line.
 */
int pci_read_irq_line(struct pci_dev *pci_dev)
{
	u8 intpin;
	struct device_node *node;

    	pci_read_config_byte(pci_dev, PCI_INTERRUPT_PIN, &intpin);
	if (intpin == 0)
		return 0;

	node = pci_device_to_OF_node(pci_dev);
	if (node == NULL)
		return -1;

	if (node->n_intrs == 0)
		return -1;

	pci_dev->irq = node->intrs[0].line;

	pci_write_config_byte(pci_dev, PCI_INTERRUPT_LINE, pci_dev->irq);

	return 0;
}
EXPORT_SYMBOL(pci_read_irq_line);

void pci_resource_to_user(const struct pci_dev *dev, int bar,
			  const struct resource *rsrc,
			  u64 *start, u64 *end)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	unsigned long offset = 0;

	if (hose == NULL)
		return;

	if (rsrc->flags & IORESOURCE_IO)
		offset = pci_io_base - (unsigned long)hose->io_base_virt +
			hose->io_base_phys;

	*start = rsrc->start + offset;
	*end = rsrc->end + offset;
}

#endif /* CONFIG_PPC_MULTIPLATFORM */
