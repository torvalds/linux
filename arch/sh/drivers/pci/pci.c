/*
 * New-style PCI core.
 *
 * Copyright (c) 2004 - 2009  Paul Mundt
 * Copyright (c) 2002  M. R. Brown
 *
 * Modelled after arch/mips/pci/pci.c:
 *  Copyright (C) 2003, 04 Ralf Baechle (ralf@linux-mips.org)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dma-debug.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/export.h>

unsigned long PCIBIOS_MIN_IO = 0x0000;
unsigned long PCIBIOS_MIN_MEM = 0;

/*
 * The PCI controller list.
 */
static struct pci_channel *hose_head, **hose_tail = &hose_head;

static int pci_initialized;

static void __devinit pcibios_scanbus(struct pci_channel *hose)
{
	static int next_busno;
	static int need_domain_info;
	LIST_HEAD(resources);
	int i;
	struct pci_bus *bus;

	for (i = 0; i < hose->nr_resources; i++)
		pci_add_resource(&resources, hose->resources + i);

	bus = pci_scan_root_bus(NULL, next_busno, hose->pci_ops, hose,
				&resources);
	hose->bus = bus;

	need_domain_info = need_domain_info || hose->index;
	hose->need_domain_info = need_domain_info;
	if (bus) {
		next_busno = bus->subordinate + 1;
		/* Don't allow 8-bit bus number overflow inside the hose -
		   reserve some space for bridges. */
		if (next_busno > 224) {
			next_busno = 0;
			need_domain_info = 1;
		}

		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);
		pci_enable_bridges(bus);
	} else {
		pci_free_resource_list(&resources);
	}
}

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */
DEFINE_RAW_SPINLOCK(pci_config_lock);
static DEFINE_MUTEX(pci_scan_mutex);

int __devinit register_pci_controller(struct pci_channel *hose)
{
	int i;

	for (i = 0; i < hose->nr_resources; i++) {
		struct resource *res = hose->resources + i;

		if (res->flags & IORESOURCE_IO) {
			if (request_resource(&ioport_resource, res) < 0)
				goto out;
		} else {
			if (request_resource(&iomem_resource, res) < 0)
				goto out;
		}
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
	 * Setup the ERR/PERR and SERR timers, if available.
	 */
	pcibios_enable_timers(hose);

	/*
	 * Scan the bus if it is register after the PCI subsystem
	 * initialization.
	 */
	if (pci_initialized) {
		mutex_lock(&pci_scan_mutex);
		pcibios_scanbus(hose);
		mutex_unlock(&pci_scan_mutex);
	}

	return 0;

out:
	for (--i; i >= 0; i--)
		release_resource(&hose->resources[i]);

	printk(KERN_WARNING "Skipping PCI bus scan due to resource conflict\n");
	return -1;
}

static int __init pcibios_init(void)
{
	struct pci_channel *hose;

	/* Scan all of the recorded PCI controllers.  */
	for (hose = hose_head; hose; hose = hose->next)
		pcibios_scanbus(hose);

	pci_fixup_irqs(pci_common_swizzle, pcibios_map_platform_irq);

	dma_debug_add_bus(&pci_bus_type);

	pci_initialized = 1;

	return 0;
}
subsys_initcall(pcibios_init);

static void pcibios_fixup_device_resources(struct pci_dev *dev,
	struct pci_bus *bus)
{
	/* Update device resources.  */
	struct pci_channel *hose = bus->sysdata;
	unsigned long offset = 0;
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!dev->resource[i].start)
			continue;
		if (dev->resource[i].flags & IORESOURCE_IO)
			offset = hose->io_offset;
		else if (dev->resource[i].flags & IORESOURCE_MEM)
			offset = hose->mem_offset;

		dev->resource[i].start += offset;
		dev->resource[i].end += offset;
	}
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct list_head *ln;

	for (ln = bus->devices.next; ln != &bus->devices; ln = ln->next) {
		dev = pci_dev_b(ln);

		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
	}
}

/*
 * We need to avoid collisions with `mirrored' VGA ports
 * and other strange ISA hardware, so we always want the
 * addresses to be allocated in the 0x000-0x0ff region
 * modulo 0x400.
 */
resource_size_t pcibios_align_resource(void *data, const struct resource *res,
				resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	struct pci_channel *hose = dev->sysdata;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (start < PCIBIOS_MIN_IO + hose->resources[0].start)
			start = PCIBIOS_MIN_IO + hose->resources[0].start;

		/*
                 * Put everything into 0x00-0xff region modulo 0x400.
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}

	return start;
}

void pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			     struct resource *res)
{
	struct pci_channel *hose = dev->sysdata;
	unsigned long offset = 0;

	if (res->flags & IORESOURCE_IO)
		offset = hose->io_offset;
	else if (res->flags & IORESOURCE_MEM)
		offset = hose->mem_offset;

	region->start = res->start - offset;
	region->end = res->end - offset;
}

void pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			     struct pci_bus_region *region)
{
	struct pci_channel *hose = dev->sysdata;
	unsigned long offset = 0;

	if (res->flags & IORESOURCE_IO)
		offset = hose->io_offset;
	else if (res->flags & IORESOURCE_MEM)
		offset = hose->mem_offset;

	res->start = region->start + offset;
	res->end = region->end + offset;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	return pci_enable_resources(dev, mask);
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

char * __devinit __weak pcibios_setup(char *str)
{
	return str;
}

static void __init
pcibios_bus_report_status_early(struct pci_channel *hose,
				int top_bus, int current_bus,
				unsigned int status_mask, int warn)
{
	unsigned int pci_devfn;
	u16 status;
	int ret;

	for (pci_devfn = 0; pci_devfn < 0xff; pci_devfn++) {
		if (PCI_FUNC(pci_devfn))
			continue;
		ret = early_read_config_word(hose, top_bus, current_bus,
					     pci_devfn, PCI_STATUS, &status);
		if (ret != PCIBIOS_SUCCESSFUL)
			continue;
		if (status == 0xffff)
			continue;

		early_write_config_word(hose, top_bus, current_bus,
					pci_devfn, PCI_STATUS,
					status & status_mask);
		if (warn)
			printk("(%02x:%02x: %04X) ", current_bus,
			       pci_devfn, status);
	}
}

/*
 * We can't use pci_find_device() here since we are
 * called from interrupt context.
 */
static void __init_refok
pcibios_bus_report_status(struct pci_bus *bus, unsigned int status_mask,
			  int warn)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		u16 status;

		/*
		 * ignore host bridge - we handle
		 * that separately
		 */
		if (dev->bus->number == 0 && dev->devfn == 0)
			continue;

		pci_read_config_word(dev, PCI_STATUS, &status);
		if (status == 0xffff)
			continue;

		if ((status & status_mask) == 0)
			continue;

		/* clear the status errors */
		pci_write_config_word(dev, PCI_STATUS, status & status_mask);

		if (warn)
			printk("(%s: %04X) ", pci_name(dev), status);
	}

	list_for_each_entry(dev, &bus->devices, bus_list)
		if (dev->subordinate)
			pcibios_bus_report_status(dev->subordinate, status_mask, warn);
}

void __init_refok pcibios_report_status(unsigned int status_mask, int warn)
{
	struct pci_channel *hose;

	for (hose = hose_head; hose; hose = hose->next) {
		if (unlikely(!hose->bus))
			pcibios_bus_report_status_early(hose, hose_head->index,
					hose->index, status_mask, warn);
		else
			pcibios_bus_report_status(hose->bus, status_mask, warn);
	}
}

int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state, int write_combine)
{
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
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

#ifndef CONFIG_GENERIC_IOMAP

static void __iomem *ioport_map_pci(struct pci_dev *dev,
				    unsigned long port, unsigned int nr)
{
	struct pci_channel *chan = dev->sysdata;

	if (unlikely(!chan->io_map_base)) {
		chan->io_map_base = sh_io_port_base;

		if (pci_domains_supported)
			panic("To avoid data corruption io_map_base MUST be "
			      "set with multiple PCI domains.");
	}

	return (void __iomem *)(chan->io_map_base + port);
}

void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
	iounmap(addr);
}
EXPORT_SYMBOL(pci_iounmap);

#endif /* CONFIG_GENERIC_IOMAP */

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pcibios_resource_to_bus);
EXPORT_SYMBOL(pcibios_bus_to_resource);
EXPORT_SYMBOL(PCIBIOS_MIN_IO);
EXPORT_SYMBOL(PCIBIOS_MIN_MEM);
#endif
