// SPDX-License-Identifier: GPL-2.0-only
/*
 * Helper routines to scan the device tree for PCI devices and busses
 *
 * Migrated out of PowerPC architecture pci_64.c file by Grant Likely
 * <grant.likely@secretlab.ca> so that these routines are available for
 * 32 bit also.
 *
 * Copyright (C) 2003 Anton Blanchard <anton@au.ibm.com>, IBM
 *   Rework, based on alpha PCI code.
 * Copyright (c) 2009 Secret Lab Technologies Ltd.
 */

#include <linux/pci.h>
#include <linux/export.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>

/**
 * get_int_prop - Decode a u32 from a device tree property
 */
static u32 get_int_prop(struct device_node *np, const char *name, u32 def)
{
	const __be32 *prop;
	int len;

	prop = of_get_property(np, name, &len);
	if (prop && len >= 4)
		return of_read_number(prop, 1);
	return def;
}

/**
 * pci_parse_of_flags - Parse the flags cell of a device tree PCI address
 * @addr0: value of 1st cell of a device tree PCI address.
 * @bridge: Set this flag if the address is from a bridge 'ranges' property
 *
 * PCI Bus Binding to IEEE Std 1275-1994
 *
 * Bit#            33222222 22221111 11111100 00000000
 *                 10987654 32109876 54321098 76543210
 * phys.hi cell:   npt000ss bbbbbbbb dddddfff rrrrrrrr
 * phys.mid cell:  hhhhhhhh hhhhhhhh hhhhhhhh hhhhhhhh
 * phys.lo cell:   llllllll llllllll llllllll llllllll
 *
 * where:
 * n        is 0 if the address is relocatable, 1 otherwise
 * p        is 1 if the addressable region is "prefetchable", 0 otherwise
 * t        is 1 if the address is aliased (for non-relocatable I/O),
 *          below 1 MB (for Memory),or below 64 KB (for relocatable I/O).
 * ss       is the space code, denoting the address space:
 *              00 denotes Configuration Space
 *              01 denotes I/O Space
 *              10 denotes 32-bit-address Memory Space
 *              11 denotes 64-bit-address Memory Space
 * bbbbbbbb is the 8-bit Bus Number
 * ddddd    is the 5-bit Device Number
 * fff      is the 3-bit Function Number
 * rrrrrrrr is the 8-bit Register Number
 */
#define OF_PCI_ADDR0_SPACE(ss)		(((ss)&3)<<24)
#define OF_PCI_ADDR0_SPACE_CFG		OF_PCI_ADDR0_SPACE(0)
#define OF_PCI_ADDR0_SPACE_IO		OF_PCI_ADDR0_SPACE(1)
#define OF_PCI_ADDR0_SPACE_MMIO32	OF_PCI_ADDR0_SPACE(2)
#define OF_PCI_ADDR0_SPACE_MMIO64	OF_PCI_ADDR0_SPACE(3)
#define OF_PCI_ADDR0_SPACE_MASK		OF_PCI_ADDR0_SPACE(3)
#define OF_PCI_ADDR0_RELOC		(1UL<<31)
#define OF_PCI_ADDR0_PREFETCH		(1UL<<30)
#define OF_PCI_ADDR0_ALIAS		(1UL<<29)
#define OF_PCI_ADDR0_BUS		0x00FF0000UL
#define OF_PCI_ADDR0_DEV		0x0000F800UL
#define OF_PCI_ADDR0_FN			0x00000700UL
#define OF_PCI_ADDR0_BARREG		0x000000FFUL

unsigned int pci_parse_of_flags(u32 addr0, int bridge)
{
	unsigned int flags = 0, as = addr0 & OF_PCI_ADDR0_SPACE_MASK;

	if (as == OF_PCI_ADDR0_SPACE_MMIO32 || as == OF_PCI_ADDR0_SPACE_MMIO64) {
		flags = IORESOURCE_MEM | PCI_BASE_ADDRESS_SPACE_MEMORY;

		if (as == OF_PCI_ADDR0_SPACE_MMIO64)
			flags |= PCI_BASE_ADDRESS_MEM_TYPE_64 | IORESOURCE_MEM_64;

		if (addr0 & OF_PCI_ADDR0_ALIAS)
			flags |= PCI_BASE_ADDRESS_MEM_TYPE_1M;

		if (addr0 & OF_PCI_ADDR0_PREFETCH)
			flags |= IORESOURCE_PREFETCH |
				 PCI_BASE_ADDRESS_MEM_PREFETCH;

		/* Note: We don't know whether the ROM has been left enabled
		 * by the firmware or not. We mark it as disabled (ie, we do
		 * not set the IORESOURCE_ROM_ENABLE flag) for now rather than
		 * do a config space read, it will be force-enabled if needed
		 */
		if (!bridge && (addr0 & OF_PCI_ADDR0_BARREG) == PCI_ROM_ADDRESS)
			flags |= IORESOURCE_READONLY;

	} else if (as == OF_PCI_ADDR0_SPACE_IO)
		flags = IORESOURCE_IO | PCI_BASE_ADDRESS_SPACE_IO;

	if (flags)
		flags |= IORESOURCE_SIZEALIGN;

	return flags;
}

/**
 * of_pci_parse_addrs - Parse PCI addresses assigned in the device tree node
 * @node: device tree node for the PCI device
 * @dev: pci_dev structure for the device
 *
 * This function parses the 'assigned-addresses' property of a PCI devices'
 * device tree node and writes them into the associated pci_dev structure.
 */
static void of_pci_parse_addrs(struct device_node *node, struct pci_dev *dev)
{
	u64 base, size;
	unsigned int flags;
	struct pci_bus_region region;
	struct resource *res;
	const __be32 *addrs;
	u32 i;
	int proplen;
	bool mark_unset = false;

	addrs = of_get_property(node, "assigned-addresses", &proplen);
	if (!addrs || !proplen) {
		addrs = of_get_property(node, "reg", &proplen);
		if (!addrs || !proplen)
			return;
		mark_unset = true;
	}

	pr_debug("    parse addresses (%d bytes) @ %p\n", proplen, addrs);
	for (; proplen >= 20; proplen -= 20, addrs += 5) {
		flags = pci_parse_of_flags(of_read_number(addrs, 1), 0);
		if (!flags)
			continue;
		base = of_read_number(&addrs[1], 2);
		size = of_read_number(&addrs[3], 2);
		if (!size)
			continue;
		i = of_read_number(addrs, 1) & 0xff;
		pr_debug("  base: %llx, size: %llx, i: %x\n",
			 (unsigned long long)base,
			 (unsigned long long)size, i);

		if (PCI_BASE_ADDRESS_0 <= i && i <= PCI_BASE_ADDRESS_5) {
			res = &dev->resource[(i - PCI_BASE_ADDRESS_0) >> 2];
		} else if (i == dev->rom_base_reg) {
			res = &dev->resource[PCI_ROM_RESOURCE];
			flags |= IORESOURCE_READONLY;
		} else {
			printk(KERN_ERR "PCI: bad cfg reg num 0x%x\n", i);
			continue;
		}
		res->flags = flags;
		if (mark_unset)
			res->flags |= IORESOURCE_UNSET;
		res->name = pci_name(dev);
		region.start = base;
		region.end = base + size - 1;
		pcibios_bus_to_resource(dev->bus, res, &region);
	}
}

/**
 * of_create_pci_dev - Given a device tree node on a pci bus, create a pci_dev
 * @node: device tree node pointer
 * @bus: bus the device is sitting on
 * @devfn: PCI function number, extracted from device tree by caller.
 */
struct pci_dev *of_create_pci_dev(struct device_node *node,
				 struct pci_bus *bus, int devfn)
{
	struct pci_dev *dev;

	dev = pci_alloc_dev(bus);
	if (!dev)
		return NULL;

	pr_debug("    create device, devfn: %x, type: %s\n", devfn,
		 of_node_get_device_type(node));

	dev->dev.of_node = of_node_get(node);
	dev->dev.parent = bus->bridge;
	dev->dev.bus = &pci_bus_type;
	dev->devfn = devfn;
	dev->multifunction = 0;		/* maybe a lie? */
	dev->needs_freset = 0;		/* pcie fundamental reset required */
	set_pcie_port_type(dev);

	pci_dev_assign_slot(dev);
	dev->vendor = get_int_prop(node, "vendor-id", 0xffff);
	dev->device = get_int_prop(node, "device-id", 0xffff);
	dev->subsystem_vendor = get_int_prop(node, "subsystem-vendor-id", 0);
	dev->subsystem_device = get_int_prop(node, "subsystem-id", 0);

	dev->cfg_size = pci_cfg_space_size(dev);

	dev_set_name(&dev->dev, "%04x:%02x:%02x.%d", pci_domain_nr(bus),
		dev->bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));
	dev->class = get_int_prop(node, "class-code", 0);
	dev->revision = get_int_prop(node, "revision-id", 0);

	pr_debug("    class: 0x%x\n", dev->class);
	pr_debug("    revision: 0x%x\n", dev->revision);

	dev->current_state = PCI_UNKNOWN;	/* unknown power state */
	dev->error_state = pci_channel_io_normal;
	dev->dma_mask = 0xffffffff;

	/* Early fixups, before probing the BARs */
	pci_fixup_device(pci_fixup_early, dev);

	if (of_node_is_type(node, "pci") || of_node_is_type(node, "pciex")) {
		/* a PCI-PCI bridge */
		dev->hdr_type = PCI_HEADER_TYPE_BRIDGE;
		dev->rom_base_reg = PCI_ROM_ADDRESS1;
		set_pcie_hotplug_bridge(dev);
	} else if (of_node_is_type(node, "cardbus")) {
		dev->hdr_type = PCI_HEADER_TYPE_CARDBUS;
	} else {
		dev->hdr_type = PCI_HEADER_TYPE_NORMAL;
		dev->rom_base_reg = PCI_ROM_ADDRESS;
		/* Maybe do a default OF mapping here */
		dev->irq = 0;
	}

	of_pci_parse_addrs(node, dev);

	pr_debug("    adding to system ...\n");

	pci_device_add(dev, bus);

	return dev;
}
EXPORT_SYMBOL(of_create_pci_dev);

/**
 * of_scan_pci_bridge - Set up a PCI bridge and scan for child nodes
 * @dev: pci_dev structure for the bridge
 *
 * of_scan_bus() calls this routine for each PCI bridge that it finds, and
 * this routine in turn call of_scan_bus() recusively to scan for more child
 * devices.
 */
void of_scan_pci_bridge(struct pci_dev *dev)
{
	struct device_node *node = dev->dev.of_node;
	struct pci_bus *bus;
	struct pci_controller *phb;
	const __be32 *busrange, *ranges;
	int len, i, mode;
	struct pci_bus_region region;
	struct resource *res;
	unsigned int flags;
	u64 size;

	pr_debug("of_scan_pci_bridge(%pOF)\n", node);

	/* parse bus-range property */
	busrange = of_get_property(node, "bus-range", &len);
	if (busrange == NULL || len != 8) {
		printk(KERN_DEBUG "Can't get bus-range for PCI-PCI bridge %pOF\n",
		       node);
		return;
	}
	ranges = of_get_property(node, "ranges", &len);
	if (ranges == NULL) {
		printk(KERN_DEBUG "Can't get ranges for PCI-PCI bridge %pOF\n",
		       node);
		return;
	}

	bus = pci_find_bus(pci_domain_nr(dev->bus),
			   of_read_number(busrange, 1));
	if (!bus) {
		bus = pci_add_new_bus(dev->bus, dev,
				      of_read_number(busrange, 1));
		if (!bus) {
			printk(KERN_ERR "Failed to create pci bus for %pOF\n",
			       node);
			return;
		}
	}

	bus->primary = dev->bus->number;
	pci_bus_insert_busn_res(bus, of_read_number(busrange, 1),
				of_read_number(busrange+1, 1));
	bus->bridge_ctl = 0;

	/* parse ranges property */
	/* PCI #address-cells == 3 and #size-cells == 2 always */
	res = &dev->resource[PCI_BRIDGE_RESOURCES];
	for (i = 0; i < PCI_NUM_RESOURCES - PCI_BRIDGE_RESOURCES; ++i) {
		res->flags = 0;
		bus->resource[i] = res;
		++res;
	}
	i = 1;
	for (; len >= 32; len -= 32, ranges += 8) {
		flags = pci_parse_of_flags(of_read_number(ranges, 1), 1);
		size = of_read_number(&ranges[6], 2);
		if (flags == 0 || size == 0)
			continue;
		if (flags & IORESOURCE_IO) {
			res = bus->resource[0];
			if (res->flags) {
				printk(KERN_ERR "PCI: ignoring extra I/O range"
				       " for bridge %pOF\n", node);
				continue;
			}
		} else {
			if (i >= PCI_NUM_RESOURCES - PCI_BRIDGE_RESOURCES) {
				printk(KERN_ERR "PCI: too many memory ranges"
				       " for bridge %pOF\n", node);
				continue;
			}
			res = bus->resource[i];
			++i;
		}
		res->flags = flags;
		region.start = of_read_number(&ranges[1], 2);
		region.end = region.start + size - 1;
		pcibios_bus_to_resource(dev->bus, res, &region);
	}
	sprintf(bus->name, "PCI Bus %04x:%02x", pci_domain_nr(bus),
		bus->number);
	pr_debug("    bus name: %s\n", bus->name);

	phb = pci_bus_to_host(bus);

	mode = PCI_PROBE_NORMAL;
	if (phb->controller_ops.probe_mode)
		mode = phb->controller_ops.probe_mode(bus);
	pr_debug("    probe mode: %d\n", mode);

	if (mode == PCI_PROBE_DEVTREE)
		of_scan_bus(node, bus);
	else if (mode == PCI_PROBE_NORMAL)
		pci_scan_child_bus(bus);
}
EXPORT_SYMBOL(of_scan_pci_bridge);

static struct pci_dev *of_scan_pci_dev(struct pci_bus *bus,
			    struct device_node *dn)
{
	struct pci_dev *dev = NULL;
	const __be32 *reg;
	int reglen, devfn;
#ifdef CONFIG_EEH
	struct eeh_dev *edev = pdn_to_eeh_dev(PCI_DN(dn));
#endif

	pr_debug("  * %pOF\n", dn);
	if (!of_device_is_available(dn))
		return NULL;

	reg = of_get_property(dn, "reg", &reglen);
	if (reg == NULL || reglen < 20)
		return NULL;
	devfn = (of_read_number(reg, 1) >> 8) & 0xff;

	/* Check if the PCI device is already there */
	dev = pci_get_slot(bus, devfn);
	if (dev) {
		pci_dev_put(dev);
		return dev;
	}

	/* Device removed permanently ? */
#ifdef CONFIG_EEH
	if (edev && (edev->mode & EEH_DEV_REMOVED))
		return NULL;
#endif

	/* create a new pci_dev for this device */
	dev = of_create_pci_dev(dn, bus, devfn);
	if (!dev)
		return NULL;

	pr_debug("  dev header type: %x\n", dev->hdr_type);
	return dev;
}

/**
 * __of_scan_bus - given a PCI bus node, setup bus and scan for child devices
 * @node: device tree node for the PCI bus
 * @bus: pci_bus structure for the PCI bus
 * @rescan_existing: Flag indicating bus has already been set up
 */
static void __of_scan_bus(struct device_node *node, struct pci_bus *bus,
			  int rescan_existing)
{
	struct device_node *child;
	struct pci_dev *dev;

	pr_debug("of_scan_bus(%pOF) bus no %d...\n",
		 node, bus->number);

	/* Scan direct children */
	for_each_child_of_node(node, child) {
		dev = of_scan_pci_dev(bus, child);
		if (!dev)
			continue;
		pr_debug("    dev header type: %x\n", dev->hdr_type);
	}

	/* Apply all fixups necessary. We don't fixup the bus "self"
	 * for an existing bridge that is being rescanned
	 */
	if (!rescan_existing)
		pcibios_setup_bus_self(bus);

	/* Now scan child busses */
	for_each_pci_bridge(dev, bus)
		of_scan_pci_bridge(dev);
}

/**
 * of_scan_bus - given a PCI bus node, setup bus and scan for child devices
 * @node: device tree node for the PCI bus
 * @bus: pci_bus structure for the PCI bus
 */
void of_scan_bus(struct device_node *node, struct pci_bus *bus)
{
	__of_scan_bus(node, bus, 0);
}
EXPORT_SYMBOL_GPL(of_scan_bus);

/**
 * of_rescan_bus - given a PCI bus node, scan for child devices
 * @node: device tree node for the PCI bus
 * @bus: pci_bus structure for the PCI bus
 *
 * Same as of_scan_bus, but for a pci_bus structure that has already been
 * setup.
 */
void of_rescan_bus(struct device_node *node, struct pci_bus *bus)
{
	__of_scan_bus(node, bus, 1);
}
EXPORT_SYMBOL_GPL(of_rescan_bus);

