/*
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
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
#include <linux/memblock.h>
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
static LIST_HEAD(controllers);

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

static void pcibios_scanbus(struct pci_controller *hose)
{
	static int next_busno;
	static int need_domain_info;
	LIST_HEAD(resources);
	struct pci_bus *bus;
	struct pci_host_bridge *bridge;
	int ret;

	bridge = pci_alloc_host_bridge(0);
	if (!bridge)
		return;

	if (hose->get_busno && pci_has_flag(PCI_PROBE_ONLY))
		next_busno = (*hose->get_busno)();

	pci_add_resource_offset(&resources,
				hose->mem_resource, hose->mem_offset);
	pci_add_resource_offset(&resources,
				hose->io_resource, hose->io_offset);
	pci_add_resource(&resources, hose->busn_resource);
	list_splice_init(&resources, &bridge->windows);
	bridge->dev.parent = NULL;
	bridge->sysdata = hose;
	bridge->busnr = next_busno;
	bridge->ops = hose->pci_ops;
	bridge->swizzle_irq = pci_common_swizzle;
	bridge->map_irq = pcibios_map_irq;
	ret = pci_scan_root_bus_bridge(bridge);
	if (ret) {
		pci_free_host_bridge(bridge);
		return;
	}

	hose->bus = bus = bridge->bus;

	need_domain_info = need_domain_info || pci_domain_nr(bus);
	set_pci_need_domain_info(hose, need_domain_info);

	next_busno = bus->busn_res.end + 1;
	/* Don't allow 8-bit bus number overflow inside the hose -
	   reserve some space for bridges. */
	if (next_busno > 224) {
		next_busno = 0;
		need_domain_info = 1;
	}

	/*
	 * We insert PCI resources into the iomem_resource and
	 * ioport_resource trees in either pci_bus_claim_resources()
	 * or pci_bus_assign_resources().
	 */
	if (pci_has_flag(PCI_PROBE_ONLY)) {
		pci_bus_claim_resources(bus);
	} else {
		struct pci_bus *child;

		pci_bus_size_bridges(bus);
		pci_bus_assign_resources(bus);
		list_for_each_entry(child, &bus->children, node)
			pcie_bus_configure_settings(child);
	}
	pci_bus_add_devices(bus);
}

#ifdef CONFIG_OF
void pci_load_of_ranges(struct pci_controller *hose, struct device_node *node)
{
	struct of_pci_range range;
	struct of_pci_range_parser parser;

	pr_info("PCI host bridge %pOF ranges:\n", node);
	hose->of_node = node;

	if (of_pci_range_parser_init(&parser, node))
		return;

	for_each_of_pci_range(&parser, &range) {
		struct resource *res = NULL;

		switch (range.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			pr_info("  IO 0x%016llx..0x%016llx\n",
				range.cpu_addr,
				range.cpu_addr + range.size - 1);
			hose->io_map_base =
				(unsigned long)ioremap(range.cpu_addr,
						       range.size);
			res = hose->io_resource;
			break;
		case IORESOURCE_MEM:
			pr_info(" MEM 0x%016llx..0x%016llx\n",
				range.cpu_addr,
				range.cpu_addr + range.size - 1);
			res = hose->mem_resource;
			break;
		}
		if (res != NULL)
			of_pci_range_to_resource(&range, node, res);
	}
}

struct device_node *pcibios_get_phb_of_node(struct pci_bus *bus)
{
	struct pci_controller *hose = bus->sysdata;

	return of_node_get(hose->of_node);
}
#endif

static DEFINE_MUTEX(pci_scan_mutex);

void register_pci_controller(struct pci_controller *hose)
{
	struct resource *parent;

	parent = hose->mem_resource->parent;
	if (!parent)
		parent = &iomem_resource;

	if (request_resource(parent, hose->mem_resource) < 0)
		goto out;

	parent = hose->io_resource->parent;
	if (!parent)
		parent = &ioport_resource;

	if (request_resource(parent, hose->io_resource) < 0) {
		release_resource(hose->mem_resource);
		goto out;
	}

	INIT_LIST_HEAD(&hose->list);
	list_add_tail(&hose->list, &controllers);

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

static int __init pcibios_init(void)
{
	struct pci_controller *hose;

	/* Scan all of the recorded PCI controllers.  */
	list_for_each_entry(hose, &controllers, list)
		pcibios_scanbus(hose);

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
			pci_err(dev,
				"can't enable device: resource collisions\n");
			return -EINVAL;
		}
		if (r->flags & IORESOURCE_IO)
			cmd |= PCI_COMMAND_IO;
		if (r->flags & IORESOURCE_MEM)
			cmd |= PCI_COMMAND_MEMORY;
	}
	if (cmd != old_cmd) {
		pci_info(dev, "enabling device (%04x -> %04x)\n", old_cmd, cmd);
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	return 0;
}

int pcibios_enable_device(struct pci_dev *dev, int mask)
{
	int err;

	if ((err = pcibios_enable_resources(dev, mask)) < 0)
		return err;

	return pcibios_plat_dev_init(dev);
}

void pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;

	if (pci_has_flag(PCI_PROBE_ONLY) && dev &&
	    (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_read_bridge_bases(bus);
	}
}

char * (*pcibios_plat_setup)(char *str) __initdata;

char *__init pcibios_setup(char *str)
{
	if (pcibios_plat_setup)
		return pcibios_plat_setup(str);
	return str;
}
