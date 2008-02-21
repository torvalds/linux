/*
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * Copyright (C) 2003, 04 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

/*
 * Indicate whether we respect the PCI setup left by the firmware.
 *
 * Make this long-lived  so that we know when shutting down
 * whether we probed only or not.
 */
int pci_probe_only;

#define PCI_ASSIGN_ALL_BUSSES	1

unsigned int pci_probe = PCI_ASSIGN_ALL_BUSSES;

/*
 * The PCI controller list.
 */

struct pci_controller *hose_head, **hose_tail = &hose_head;
struct pci_controller *pci_isa_hose;

unsigned long PCIBIOS_MIN_IO	= 0x0000;
unsigned long PCIBIOS_MIN_MEM	= 0;

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

	res->start = start;
}

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
	 * Do not panic here but later - this might hapen before console init.
	 */
	if (!hose->io_map_base) {
		printk(KERN_WARNING
		       "registering PCI controller with io_map_base unset\n");
	}
	return;

out:
	printk(KERN_WARNING
	       "Skipping PCI bus scan due to resource conflict\n");
}

/* Most MIPS systems have straight-forward swizzling needs.  */

static inline u8 bridge_swizzle(u8 pin, u8 slot)
{
	return (((pin - 1) + slot) % 4) + 1;
}

static u8 __init common_swizzle(struct pci_dev *dev, u8 *pinp)
{
	u8 pin = *pinp;

	while (dev->bus->parent) {
		pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));
		/* Move up the chain of bridges. */
		dev = dev->bus->self;
        }
	*pinp = pin;

	/* The slot is the slot of the last bridge. */
	return PCI_SLOT(dev->devfn);
}

static int __init pcibios_init(void)
{
	struct pci_controller *hose;
	struct pci_bus *bus;
	int next_busno;
	int need_domain_info = 0;

	/* Scan all of the recorded PCI controllers.  */
	for (next_busno = 0, hose = hose_head; hose; hose = hose->next) {

		if (!hose->iommu)
			PCI_DMA_BUS_IS_PHYS = 1;

		if (hose->get_busno && pci_probe_only)
			next_busno = (*hose->get_busno)();

		bus = pci_scan_bus(next_busno, hose->pci_ops, hose);
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
		}
	}

	if (!pci_probe_only)
		pci_assign_unassigned_resources();
	pci_fixup_irqs(common_swizzle, pcibios_map_irq);

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
	printk(KERN_DEBUG "PCI: Setting latency timer of device %s to %d\n",
	       pci_name(dev), lat);
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, lat);
}

unsigned int pcibios_assign_all_busses(void)
{
	return (pci_probe & PCI_ASSIGN_ALL_BUSSES) ? 1 : 0;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	if ((err = pcibios_enable_resources(dev, mask)) < 0)
		return err;

	return pcibios_plat_dev_init(dev);
}

static void pcibios_fixup_device_resources(struct pci_dev *dev,
	struct pci_bus *bus)
{
	/* Update device resources.  */
	struct pci_controller *hose = (struct pci_controller *)bus->sysdata;
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

void pcibios_fixup_bus(struct pci_bus *bus)
{
	/* Propagate hose info into the subordinate devices.  */

	struct pci_controller *hose = bus->sysdata;
	struct list_head *ln;
	struct pci_dev *dev = bus->self;

	if (!dev) {
		bus->resource[0] = hose->io_resource;
		bus->resource[1] = hose->mem_resource;
	} else if (pci_probe_only &&
		   (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_read_bridge_bases(bus);
		pcibios_fixup_device_resources(dev, bus);
	}

	for (ln = bus->devices.next; ln != &bus->devices; ln = ln->next) {
		dev = pci_dev_b(ln);

		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
	}
}

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

void pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			 struct resource *res)
{
	struct pci_controller *hose = (struct pci_controller *)dev->sysdata;
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
	struct pci_controller *hose = (struct pci_controller *)dev->sysdata;
	unsigned long offset = 0;

	if (res->flags & IORESOURCE_IO)
		offset = hose->io_offset;
	else if (res->flags & IORESOURCE_MEM)
		offset = hose->mem_offset;

	res->start = region->start + offset;
	res->end = region->end + offset;
}

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pcibios_resource_to_bus);
EXPORT_SYMBOL(pcibios_bus_to_resource);
EXPORT_SYMBOL(PCIBIOS_MIN_IO);
EXPORT_SYMBOL(PCIBIOS_MIN_MEM);
#endif

char *pcibios_setup(char *str)
{
	return str;
}
