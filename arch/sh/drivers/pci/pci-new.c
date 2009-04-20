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
#include <linux/mutex.h>

/*
 * The PCI controller list.
 */
static struct pci_channel *hose_head, **hose_tail = &hose_head;

static int pci_initialized;

static void __devinit pcibios_scanbus(struct pci_channel *hose)
{
	static int next_busno;
	struct pci_bus *bus;

	/* Catch botched conversion attempts */
	BUG_ON(hose->init);

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
