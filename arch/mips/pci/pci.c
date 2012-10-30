/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2003, 04, 11 Ralf Baechle (ralf@linux-mips.org)
 * Copyright (C) 2011 Wind River Systems,
 *   written by Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/of_address.h>

#include <asm/cpu-info.h>

/*
 * If PCI_PROBE_ONLY in pci_flags is set, we don't change any PCI resource
 * assignments.
 */

/*
 * The PCI controller list.
 */

static struct pci_controller *hose_head, **hose_tail = &hose_head;

unsigned long PCIBIOS_MIN_IO;
unsigned long PCIBIOS_MIN_MEM;

static int pci_initialized;

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
	struct pci_controller *hose = dev->sysdata;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		/* Make sure we start at our min on all hoses */
		if (start < PCIBIOS_MIN_IO + hose->io_resource->start)
			start = PCIBIOS_MIN_IO + hose->io_resource->start;

		/*
		 * Put everything into 0x00-0xff region modulo 0x400
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	} else if (res->flags & IORESOURCE_MEM) {
		/* Make sure we start at our min on all hoses */
		if (start < PCIBIOS_MIN_MEM + hose->mem_resource->start)
			start = PCIBIOS_MIN_MEM + hose->mem_resource->start;
	}

	return start;
}

static void __devinit pcibios_scanbus(struct pci_controller *hose)
{
	static int next_busno;
	static int need_domain_info;
	LIST_HEAD(resources);
	struct pci_bus *bus;

	if (!hose->iommu)
		PCI_DMA_BUS_IS_PHYS = 1;

	if (hose->get_busno && pci_has_flag(PCI_PROBE_ONLY))
		next_busno = (*hose->get_busno)();

	pci_add_resource_offset(&resources,
				hose->mem_resource, hose->mem_offset);
	pci_add_resource_offset(&resources, hose->io_resource, hose->io_offset);
	bus = pci_scan_root_bus(NULL, next_busno, hose->pci_ops, hose,
				&resources);
	if (!bus)
		pci_free_resource_list(&resources);

	hose->bus = bus;

	need_domain_info = need_domain_info || hose->index;
	hose->need_domain_info = need_domain_info;
	if (bus) {
		next_busno = bus->busn_res.end + 1;
		/* Don't allow 8-bit bus number overflow inside the hose -
		   reserve some space for bridges. */
		if (next_busno > 224) {
			next_busno = 0;
			need_domain_info = 1;
		}

		if (!pci_has_flag(PCI_PROBE_ONLY)) {
			pci_bus_size_bridges(bus);
			pci_bus_assign_resources(bus);
			pci_enable_bridges(bus);
		}
		bus->dev.of_node = hose->of_node;
	}
}

#ifdef CONFIG_OF
void __devinit pci_load_of_ranges(struct pci_controller *hose,
				struct device_node *node)
{
	const __be32 *ranges;
	int rlen;
	int pna = of_n_addr_cells(node);
	int np = pna + 5;

	pr_info("PCI host bridge %s ranges:\n", node->full_name);
	ranges = of_get_property(node, "ranges", &rlen);
	if (ranges == NULL)
		return;
	hose->of_node = node;

	while ((rlen -= np * 4) >= 0) {
		u32 pci_space;
		struct resource *res = NULL;
		u64 addr, size;

		pci_space = be32_to_cpup(&ranges[0]);
		addr = of_translate_address(node, ranges + 3);
		size = of_read_number(ranges + pna + 3, 2);
		ranges += np;
		switch ((pci_space >> 24) & 0x3) {
		case 1:		/* PCI IO space */
			pr_info("  IO 0x%016llx..0x%016llx\n",
					addr, addr + size - 1);
			hose->io_map_base =
				(unsigned long)ioremap(addr, size);
			res = hose->io_resource;
			res->flags = IORESOURCE_IO;
			break;
		case 2:		/* PCI Memory space */
		case 3:		/* PCI 64 bits Memory space */
			pr_info(" MEM 0x%016llx..0x%016llx\n",
					addr, addr + size - 1);
			res = hose->mem_resource;
			res->flags = IORESOURCE_MEM;
			break;
		}
		if (res != NULL) {
			res->start = addr;
			res->name = node->full_name;
			res->end = res->start + size - 1;
			res->parent = NULL;
			res->sibling = NULL;
			res->child = NULL;
		}
	}
}
#endif

static DEFINE_MUTEX(pci_scan_mutex);

void __devinit register_pci_controller(struct pci_controller *hose)
{
	if (request_resource(&iomem_resource, hose->mem_resource) < 0)
		goto out;
	if (request_resource(&ioport_resource, hose->io_resource) < 0) {
		release_resource(hose->mem_resource);
		goto out;
	}

	*hose_tail = hose;
	hose_tail = &hose->next;

	/*
	 * Do not panic here but later - this might happen before console init.
	 */
	if (!hose->io_map_base) {
		printk(KERN_WARNING
		       "registering PCI controller with io_map_base unset\n");
	}

	/*
	 * Scan the bus if it is register after the PCI subsystem
	 * initialization.
	 */
	if (pci_initialized) {
		mutex_lock(&pci_scan_mutex);
		pcibios_scanbus(hose);
		mutex_unlock(&pci_scan_mutex);
	}

	return;

out:
	printk(KERN_WARNING
	       "Skipping PCI bus scan due to resource conflict\n");
}

static void __init pcibios_set_cache_line_size(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int lsize;

	/*
	 * Set PCI cacheline size to that of the highest level in the
	 * cache hierarchy.
	 */
	lsize = c->dcache.linesz;
	lsize = c->scache.linesz ? : lsize;
	lsize = c->tcache.linesz ? : lsize;

	BUG_ON(!lsize);

	pci_dfl_cache_line_size = lsize >> 2;

	pr_debug("PCI: pci_cache_line_size set to %d bytes\n", lsize);
}

static int __init pcibios_init(void)
{
	struct pci_controller *hose;

	pcibios_set_cache_line_size();

	/* Scan all of the recorded PCI controllers.  */
	for (hose = hose_head; hose; hose = hose->next)
		pcibios_scanbus(hose);

	pci_fixup_irqs(pci_common_swizzle, pcibios_map_irq);

	pci_initialized = 1;

	return 0;
}

subsys_initcall(pcibios_init);

static int pcibios_enable_resources(struct pci_dev *dev, int mask)
{
	u16 cmd, old_cmd;
	int idx;
	struct resource *r;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	old_cmd = cmd;
	for (idx=0; idx < PCI_NUM_RESOURCES; idx++) {
		/* Only set up the requested stuff */
		if (!(mask & (1<<idx)))
			continue;

		r = &dev->resource[idx];
		if (!(r->flags & (IORESOURCE_IO | IORESOURCE_MEM)))
			continue;
		if ((idx == PCI_ROM_RESOURCE) &&
				(!(r->flags & IORESOURCE_ROM_ENABLE)))
			continue;
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available "
			       "because of resource collisions\n",
			       pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		printk("PCI: Enabling device %s (%04x -> %04x)\n",
		       pci_name(dev), old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

unsigned int pcibios_assign_all_busses(void)
{
	return 1;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	if ((err = pcibios_enable_resources(dev, mask)) < 0)
		return err;

	return pcibios_plat_dev_init(dev);
}

void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;

	if (pci_has_flag(PCI_PROBE_ONLY) && dev &&
	    (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_read_bridge_bases(bus);
	}
}

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(PCIBIOS_MIN_IO);
EXPORT_SYMBOL(PCIBIOS_MIN_MEM);
#endif

int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
	unsigned long prot;

	/*
	 * I/O space can be accessed via normal processor loads and stores on
	 * this platform but for now we elect not to do this and portable
	 * drivers should not do this anyway.
	 */
	if (mmap_state == pci_mmap_io)
		return -EINVAL;

	/*
	 * Ignore write-combine; for now only return uncached mappings.
	 */
	prot = pgprot_val(vma->vm_page_prot);
	prot = (prot & ~_CACHE_MASK) | _CACHE_UNCACHED;
	vma->vm_page_prot = __pgprot(prot);

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
		vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

char * (*pcibios_plat_setup)(char *str) __initdata;

char *__init pcibios_setup(char *str)
{
	if (pcibios_plat_setup)
		return pcibios_plat_setup(str);
	return str;
}
