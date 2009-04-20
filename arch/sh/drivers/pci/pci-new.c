/*
 * New-style PCI core.
 *
 * Copyright (c) 2002  M. R. Brown
 * Copyright (c) 2004 - 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/dma-debug.h>
#include <linux/io.h>

static int __init pcibios_init(void)
{
	struct pci_channel *p;
	struct pci_bus *bus;
	int busno;

	/* init channels */
	busno = 0;
	for (p = board_pci_channels; p->init; p++) {
		if (p->init(p) == 0)
			p->enabled = 1;
		else
			pr_err("Unable to init pci channel %d\n", busno);
		busno++;
	}

	/* scan the buses */
	busno = 0;
	for (p = board_pci_channels; p->init; p++) {
		if (p->enabled) {
			bus = pci_scan_bus(busno, p->pci_ops, p);
			busno = bus->subordinate + 1;

			pci_bus_size_bridges(bus);
			pci_bus_assign_resources(bus);
			pci_enable_bridges(bus);
		}
	}

	pci_fixup_irqs(pci_common_swizzle, pcibios_map_platform_irq);

	dma_debug_add_bus(&pci_bus_type);

	return 0;
}
subsys_initcall(pcibios_init);

static void pcibios_fixup_device_resources(struct pci_dev *dev,
	struct pci_bus *bus)
{
	/* Update device resources.  */
	struct pci_channel *chan = bus->sysdata;
	unsigned long offset = 0;
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		if (!dev->resource[i].start)
			continue;
		if (dev->resource[i].flags & IORESOURCE_PCI_FIXED)
			continue;
		if (dev->resource[i].flags & IORESOURCE_IO)
			offset = chan->io_base;
		else if (dev->resource[i].flags & IORESOURCE_MEM)
			offset = 0;

		dev->resource[i].start += offset;
		dev->resource[i].end += offset;
	}
}


/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */
void __devinit __weak pcibios_fixup_bus(struct pci_bus *bus)
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

void pcibios_resource_to_bus(struct pci_dev *dev, struct pci_bus_region *region,
			 struct resource *res)
{
	struct pci_channel *chan = dev->sysdata;
	unsigned long offset = 0;

	if (res->flags & IORESOURCE_IO)
		offset = chan->io_base;
	else if (res->flags & IORESOURCE_MEM)
		offset = 0;

	region->start = res->start - offset;
	region->end = res->end - offset;
}

void __devinit
pcibios_bus_to_resource(struct pci_dev *dev, struct resource *res,
			struct pci_bus_region *region)
{
	struct pci_channel *chan = dev->sysdata;
	unsigned long offset = 0;

	if (res->flags & IORESOURCE_IO)
		offset = chan->io_base;
	else if (res->flags & IORESOURCE_MEM)
		offset = 0;

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
	for(idx=0; idx<6; idx++) {
		if (!(mask & (1 << idx)))
			continue;
		r = &dev->resource[idx];
		if (!r->start && r->end) {
			printk(KERN_ERR "PCI: Device %s not available because "
			       "of resource collisions\n", pci_name(dev));
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (dev->resource[PCI_ROM_RESOURCE].start)
		cmd |= PCI_COMMAND_MEMORY;
	if (cmd != old_cmd) {
		printk(KERN_INFO "PCI: Enabling device %s (%04x -> %04x)\n",
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

EXPORT_SYMBOL(board_pci_channels);
