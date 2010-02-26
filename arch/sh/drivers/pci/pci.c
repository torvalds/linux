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
	struct pci_bus *bus;

	bus = pci_scan_bus(next_busno, hose->pci_ops, hose);
	if (bus) {
		next_busno = bus->subordinate + 1;
		/* Don't allow 8-bit bus number overflow inside the hose -
		   reserve some space for bridges. */
		if (next_busno > 224)
			next_busno = 0;

		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);
		pci_enable_bridges(bus);
	}
}

static DEFINE_MUTEX(pci_scan_mutex);

void __devinit register_pci_controller(struct pci_channel *hose)
{
	request_resource(&iomem_resource, hose->mem_resource);
	request_resource(&ioport_resource, hose->io_resource);

	*hose_tail = hose;
	hose_tail = &hose->next;

	/*
	 * Do not panic here but later - this might hapen before console init.
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
		if (dev->resource[i].flags & IORESOURCE_PCI_FIXED)
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
	struct pci_dev *dev = bus->self;
	struct list_head *ln;
	struct pci_channel *chan = bus->sysdata;

	if (!dev) {
		bus->resource[0] = chan->io_resource;
		bus->resource[1] = chan->mem_resource;
	}

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
	struct pci_channel *chan = dev->sysdata;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		if (start < PCIBIOS_MIN_IO + chan->io_resource->start)
			start = PCIBIOS_MIN_IO + chan->io_resource->start;

		/*
                 * Put everything into 0x00-0xff region modulo 0x400.
		 */
		if (start & 0x300) {
			start = (start + 0x3ff) & ~0x3ff;
			res->start = start;
		}
	} else if (res->flags & IORESOURCE_MEM) {
		if (start < PCIBIOS_MIN_MEM + chan->mem_resource->start)
			start = PCIBIOS_MIN_MEM + chan->mem_resource->start;
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

void __devinit
pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
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

/*
 *  If we set up a device for bus mastering, we need to check and set
 *  the latency timer as it may not be properly set.
 */
static unsigned int pcibios_max_latency = 255;

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
	printk(KERN_INFO "PCI: Setting latency timer of device %s to %d\n",
	       pci_name(dev), lat);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}

void __init pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

char * __devinit pcibios_setup(char *str)
{
	return str;
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

	if (!chan->io_map_base)
		chan->io_map_base = generic_io_base;

	return (void __iomem *)(chan->io_map_base + port);
}

void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
	resource_size_t start = pci_resource_start(dev, bar);
	resource_size_t len = pci_resource_len(dev, bar);
	unsigned long flags = pci_resource_flags(dev, bar);

	if (unlikely(!len || !start))
		return NULL;
	if (maxlen && len > maxlen)
		len = maxlen;

	if (flags & IORESOURCE_IO)
		return ioport_map_pci(dev, start, len);

	/*
	 * Presently the IORESOURCE_MEM case is a bit special, most
	 * SH7751 style PCI controllers have PCI memory at a fixed
	 * location in the address space where no remapping is desired.
	 * With the IORESOURCE_MEM case more care has to be taken
	 * to inhibit page table mapping for legacy cores, but this is
	 * punted off to __ioremap().
	 *					-- PFM.
	 */
	if (flags & IORESOURCE_MEM) {
		if (flags & IORESOURCE_CACHEABLE)
			return ioremap(start, len);

		return ioremap_nocache(start, len);
	}

	return NULL;
}
EXPORT_SYMBOL(pci_iomap);

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
