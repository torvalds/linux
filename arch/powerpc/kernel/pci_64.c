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

unsigned long pci_probe_only = 1;

/* pci_io_base -- the base address from which io bars are offsets.
 * This is the lowest I/O base address (so bar values are always positive),
 * and it *must* be the start of ISA space if an ISA bus exists because
 * ISA drivers use hard coded offsets.  If no ISA bus exists nothing
 * is mapped on the first 64K of IO space
 */
unsigned long pci_io_base = ISA_IO_BASE;
EXPORT_SYMBOL(pci_io_base);

static void fixup_broken_pcnet32(struct pci_dev* dev)
{
	if ((dev->class>>8 == PCI_CLASS_NETWORK_ETHERNET)) {
		dev->vendor = PCI_VENDOR_ID_AMD;
		pci_write_config_word(dev, PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_TRIDENT, PCI_ANY_ID, fixup_broken_pcnet32);


static u32 get_int_prop(struct device_node *np, const char *name, u32 def)
{
	const u32 *prop;
	int len;

	prop = of_get_property(np, name, &len);
	if (prop && len >= 4)
		return *prop;
	return def;
}

static unsigned int pci_parse_of_flags(u32 addr0)
{
	unsigned int flags = 0;

	if (addr0 & 0x02000000) {
		flags = IORESOURCE_MEM | PCI_BASE_ADDRESS_SPACE_MEMORY;
		flags |= (addr0 >> 22) & PCI_BASE_ADDRESS_MEM_TYPE_64;
		flags |= (addr0 >> 28) & PCI_BASE_ADDRESS_MEM_TYPE_1M;
		if (addr0 & 0x40000000)
			flags |= IORESOURCE_PREFETCH
				 | PCI_BASE_ADDRESS_MEM_PREFETCH;
	} else if (addr0 & 0x01000000)
		flags = IORESOURCE_IO | PCI_BASE_ADDRESS_SPACE_IO;
	return flags;
}


static void pci_parse_of_addrs(struct device_node *node, struct pci_dev *dev)
{
	u64 base, size;
	unsigned int flags;
	struct resource *res;
	const u32 *addrs;
	u32 i;
	int proplen;

	addrs = of_get_property(node, "assigned-addresses", &proplen);
	if (!addrs)
		return;
	pr_debug("    parse addresses (%d bytes) @ %p\n", proplen, addrs);
	for (; proplen >= 20; proplen -= 20, addrs += 5) {
		flags = pci_parse_of_flags(addrs[0]);
		if (!flags)
			continue;
		base = of_read_number(&addrs[1], 2);
		size = of_read_number(&addrs[3], 2);
		if (!size)
			continue;
		i = addrs[0] & 0xff;
		pr_debug("  base: %llx, size: %llx, i: %x\n",
			 (unsigned long long)base,
			 (unsigned long long)size, i);

		if (PCI_BASE_ADDRESS_0 <= i && i <= PCI_BASE_ADDRESS_5) {
			res = &dev->resource[(i - PCI_BASE_ADDRESS_0) >> 2];
		} else if (i == dev->rom_base_reg) {
			res = &dev->resource[PCI_ROM_RESOURCE];
			flags |= IORESOURCE_READONLY | IORESOURCE_CACHEABLE;
		} else {
			printk(KERN_ERR "PCI: bad cfg reg num 0x%x\n", i);
			continue;
		}
		res->start = base;
		res->end = base + size - 1;
		res->flags = flags;
		res->name = pci_name(dev);
	}
}

struct pci_dev *of_create_pci_dev(struct device_node *node,
				 struct pci_bus *bus, int devfn)
{
	struct pci_dev *dev;
	const char *type;

	dev = alloc_pci_dev();
	if (!dev)
		return NULL;
	type = of_get_property(node, "device_type", NULL);
	if (type == NULL)
		type = "";

	pr_debug("    create device, devfn: %x, type: %s\n", devfn, type);

	dev->bus = bus;
	dev->sysdata = node;
	dev->dev.parent = bus->bridge;
	dev->dev.bus = &pci_bus_type;
	dev->devfn = devfn;
	dev->multifunction = 0;		/* maybe a lie? */

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

	dev->current_state = 4;		/* unknown power state */
	dev->error_state = pci_channel_io_normal;
	dev->dma_mask = 0xffffffff;

	if (!strcmp(type, "pci") || !strcmp(type, "pciex")) {
		/* a PCI-PCI bridge */
		dev->hdr_type = PCI_HEADER_TYPE_BRIDGE;
		dev->rom_base_reg = PCI_ROM_ADDRESS1;
	} else if (!strcmp(type, "cardbus")) {
		dev->hdr_type = PCI_HEADER_TYPE_CARDBUS;
	} else {
		dev->hdr_type = PCI_HEADER_TYPE_NORMAL;
		dev->rom_base_reg = PCI_ROM_ADDRESS;
		/* Maybe do a default OF mapping here */
		dev->irq = NO_IRQ;
	}

	pci_parse_of_addrs(node, dev);

	pr_debug("    adding to system ...\n");

	pci_device_add(dev, bus);

	return dev;
}
EXPORT_SYMBOL(of_create_pci_dev);

static void __devinit __of_scan_bus(struct device_node *node,
				    struct pci_bus *bus, int rescan_existing)
{
	struct device_node *child;
	const u32 *reg;
	int reglen, devfn;
	struct pci_dev *dev;

	pr_debug("of_scan_bus(%s) bus no %d... \n",
		 node->full_name, bus->number);

	/* Scan direct children */
	for_each_child_of_node(node, child) {
		pr_debug("  * %s\n", child->full_name);
		reg = of_get_property(child, "reg", &reglen);
		if (reg == NULL || reglen < 20)
			continue;
		devfn = (reg[0] >> 8) & 0xff;

		/* create a new pci_dev for this device */
		dev = of_create_pci_dev(child, bus, devfn);
		if (!dev)
			continue;
		pr_debug("    dev header type: %x\n", dev->hdr_type);
	}

	/* Apply all fixups necessary. We don't fixup the bus "self"
	 * for an existing bridge that is being rescanned
	 */
	if (!rescan_existing)
		pcibios_setup_bus_self(bus);
	pcibios_setup_bus_devices(bus);

	/* Now scan child busses */
	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
		    dev->hdr_type == PCI_HEADER_TYPE_CARDBUS) {
			struct device_node *child = pci_device_to_OF_node(dev);
			if (dev)
				of_scan_pci_bridge(child, dev);
		}
	}
}

void __devinit of_scan_bus(struct device_node *node,
			   struct pci_bus *bus)
{
	__of_scan_bus(node, bus, 0);
}
EXPORT_SYMBOL_GPL(of_scan_bus);

void __devinit of_rescan_bus(struct device_node *node,
			     struct pci_bus *bus)
{
	__of_scan_bus(node, bus, 1);
}
EXPORT_SYMBOL_GPL(of_rescan_bus);

void __devinit of_scan_pci_bridge(struct device_node *node,
				  struct pci_dev *dev)
{
	struct pci_bus *bus;
	const u32 *busrange, *ranges;
	int len, i, mode;
	struct resource *res;
	unsigned int flags;
	u64 size;

	pr_debug("of_scan_pci_bridge(%s)\n", node->full_name);

	/* parse bus-range property */
	busrange = of_get_property(node, "bus-range", &len);
	if (busrange == NULL || len != 8) {
		printk(KERN_DEBUG "Can't get bus-range for PCI-PCI bridge %s\n",
		       node->full_name);
		return;
	}
	ranges = of_get_property(node, "ranges", &len);
	if (ranges == NULL) {
		printk(KERN_DEBUG "Can't get ranges for PCI-PCI bridge %s\n",
		       node->full_name);
		return;
	}

	bus = pci_add_new_bus(dev->bus, dev, busrange[0]);
	if (!bus) {
		printk(KERN_ERR "Failed to create pci bus for %s\n",
		       node->full_name);
		return;
	}

	bus->primary = dev->bus->number;
	bus->subordinate = busrange[1];
	bus->bridge_ctl = 0;
	bus->sysdata = node;

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
		flags = pci_parse_of_flags(ranges[0]);
		size = of_read_number(&ranges[6], 2);
		if (flags == 0 || size == 0)
			continue;
		if (flags & IORESOURCE_IO) {
			res = bus->resource[0];
			if (res->flags) {
				printk(KERN_ERR "PCI: ignoring extra I/O range"
				       " for bridge %s\n", node->full_name);
				continue;
			}
		} else {
			if (i >= PCI_NUM_RESOURCES - PCI_BRIDGE_RESOURCES) {
				printk(KERN_ERR "PCI: too many memory ranges"
				       " for bridge %s\n", node->full_name);
				continue;
			}
			res = bus->resource[i];
			++i;
		}
		res->start = of_read_number(&ranges[1], 2);
		res->end = res->start + size - 1;
		res->flags = flags;
	}
	sprintf(bus->name, "PCI Bus %04x:%02x", pci_domain_nr(bus),
		bus->number);
	pr_debug("    bus name: %s\n", bus->name);

	mode = PCI_PROBE_NORMAL;
	if (ppc_md.pci_probe_mode)
		mode = ppc_md.pci_probe_mode(bus);
	pr_debug("    probe mode: %d\n", mode);

	if (mode == PCI_PROBE_DEVTREE)
		of_scan_bus(node, bus);
	else if (mode == PCI_PROBE_NORMAL)
		pci_scan_child_bus(bus);
}
EXPORT_SYMBOL(of_scan_pci_bridge);

void __devinit scan_phb(struct pci_controller *hose)
{
	struct pci_bus *bus;
	struct device_node *node = hose->dn;
	int mode;

	pr_debug("PCI: Scanning PHB %s\n",
		 node ? node->full_name : "<NO NAME>");

	/* Create an empty bus for the toplevel */
	bus = pci_create_bus(hose->parent, hose->first_busno, hose->ops, node);
	if (bus == NULL) {
		printk(KERN_ERR "Failed to create bus for PCI domain %04x\n",
		       hose->global_number);
		return;
	}
	bus->secondary = hose->first_busno;
	hose->bus = bus;

	/* Get some IO space for the new PHB */
	pcibios_map_io_space(bus);

	/* Wire up PHB bus resources */
	pcibios_setup_phb_resources(hose);

	/* Get probe mode and perform scan */
	mode = PCI_PROBE_NORMAL;
	if (node && ppc_md.pci_probe_mode)
		mode = ppc_md.pci_probe_mode(bus);
	pr_debug("    probe mode: %d\n", mode);
	if (mode == PCI_PROBE_DEVTREE) {
		bus->subordinate = hose->last_busno;
		of_scan_bus(node, bus);
	}

	if (mode == PCI_PROBE_NORMAL)
		hose->last_busno = bus->subordinate = pci_scan_child_bus(bus);
}

static int __init pcibios_init(void)
{
	struct pci_controller *hose, *tmp;

	printk(KERN_INFO "PCI: Probing PCI hardware\n");

	/* For now, override phys_mem_access_prot. If we need it,g
	 * later, we may move that initialization to each ppc_md
	 */
	ppc_md.phys_mem_access_prot = pci_phys_mem_access_prot;

	if (pci_probe_only)
		ppc_pci_flags |= PPC_PCI_PROBE_ONLY;

	/* On ppc64, we always enable PCI domains and we keep domain 0
	 * backward compatible in /proc for video cards
	 */
	ppc_pci_flags |= PPC_PCI_ENABLE_PROC_DOMAINS | PPC_PCI_COMPAT_DOMAIN_0;

	/* Scan all of the recorded PCI controllers.  */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		scan_phb(hose);
		pci_bus_add_devices(hose->bus);
	}

	/* Call common code to handle resource allocation */
	pcibios_resource_survey();

	printk(KERN_DEBUG "PCI: Probing PCI hardware done\n");

	return 0;
}

subsys_initcall(pcibios_init);

#ifdef CONFIG_HOTPLUG

int pcibios_unmap_io_space(struct pci_bus *bus)
{
	struct pci_controller *hose;

	WARN_ON(bus == NULL);

	/* If this is not a PHB, we only flush the hash table over
	 * the area mapped by this bridge. We don't play with the PTE
	 * mappings since we might have to deal with sub-page alignemnts
	 * so flushing the hash table is the only sane way to make sure
	 * that no hash entries are covering that removed bridge area
	 * while still allowing other busses overlapping those pages
	 */
	if (bus->self) {
		struct resource *res = bus->resource[0];

		pr_debug("IO unmapping for PCI-PCI bridge %s\n",
			 pci_name(bus->self));

		__flush_hash_table_range(&init_mm, res->start + _IO_BASE,
					 res->end + _IO_BASE + 1);
		return 0;
	}

	/* Get the host bridge */
	hose = pci_bus_to_host(bus);

	/* Check if we have IOs allocated */
	if (hose->io_base_alloc == 0)
		return 0;

	pr_debug("IO unmapping for PHB %s\n", hose->dn->full_name);
	pr_debug("  alloc=0x%p\n", hose->io_base_alloc);

	/* This is a PHB, we fully unmap the IO area */
	vunmap(hose->io_base_alloc);

	return 0;
}
EXPORT_SYMBOL_GPL(pcibios_unmap_io_space);

#endif /* CONFIG_HOTPLUG */

int __devinit pcibios_map_io_space(struct pci_bus *bus)
{
	struct vm_struct *area;
	unsigned long phys_page;
	unsigned long size_page;
	unsigned long io_virt_offset;
	struct pci_controller *hose;

	WARN_ON(bus == NULL);

	/* If this not a PHB, nothing to do, page tables still exist and
	 * thus HPTEs will be faulted in when needed
	 */
	if (bus->self) {
		pr_debug("IO mapping for PCI-PCI bridge %s\n",
			 pci_name(bus->self));
		pr_debug("  virt=0x%016llx...0x%016llx\n",
			 bus->resource[0]->start + _IO_BASE,
			 bus->resource[0]->end + _IO_BASE);
		return 0;
	}

	/* Get the host bridge */
	hose = pci_bus_to_host(bus);
	phys_page = _ALIGN_DOWN(hose->io_base_phys, PAGE_SIZE);
	size_page = _ALIGN_UP(hose->pci_io_size, PAGE_SIZE);

	/* Make sure IO area address is clear */
	hose->io_base_alloc = NULL;

	/* If there's no IO to map on that bus, get away too */
	if (hose->pci_io_size == 0 || hose->io_base_phys == 0)
		return 0;

	/* Let's allocate some IO space for that guy. We don't pass
	 * VM_IOREMAP because we don't care about alignment tricks that
	 * the core does in that case. Maybe we should due to stupid card
	 * with incomplete address decoding but I'd rather not deal with
	 * those outside of the reserved 64K legacy region.
	 */
	area = __get_vm_area(size_page, 0, PHB_IO_BASE, PHB_IO_END);
	if (area == NULL)
		return -ENOMEM;
	hose->io_base_alloc = area->addr;
	hose->io_base_virt = (void __iomem *)(area->addr +
					      hose->io_base_phys - phys_page);

	pr_debug("IO mapping for PHB %s\n", hose->dn->full_name);
	pr_debug("  phys=0x%016llx, virt=0x%p (alloc=0x%p)\n",
		 hose->io_base_phys, hose->io_base_virt, hose->io_base_alloc);
	pr_debug("  size=0x%016lx (alloc=0x%016lx)\n",
		 hose->pci_io_size, size_page);

	/* Establish the mapping */
	if (__ioremap_at(phys_page, area->addr, size_page,
			 _PAGE_NO_CACHE | _PAGE_GUARDED) == NULL)
		return -ENOMEM;

	/* Fixup hose IO resource */
	io_virt_offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	hose->io_resource.start += io_virt_offset;
	hose->io_resource.end += io_virt_offset;

	pr_debug("  hose->io_resource=0x%016llx...0x%016llx\n",
		 hose->io_resource.start, hose->io_resource.end);

	return 0;
}
EXPORT_SYMBOL_GPL(pcibios_map_io_space);

#define IOBASE_BRIDGE_NUMBER	0
#define IOBASE_MEMORY		1
#define IOBASE_IO		2
#define IOBASE_ISA_IO		3
#define IOBASE_ISA_MEM		4

long sys_pciconfig_iobase(long which, unsigned long in_bus,
			  unsigned long in_devfn)
{
	struct pci_controller* hose;
	struct list_head *ln;
	struct pci_bus *bus = NULL;
	struct device_node *hose_node;

	/* Argh ! Please forgive me for that hack, but that's the
	 * simplest way to get existing XFree to not lockup on some
	 * G5 machines... So when something asks for bus 0 io base
	 * (bus 0 is HT root), we return the AGP one instead.
	 */
	if (in_bus == 0 && machine_is_compatible("MacRISC4")) {
		struct device_node *agp;

		agp = of_find_compatible_node(NULL, NULL, "u3-agp");
		if (agp)
			in_bus = 0xf0;
		of_node_put(agp);
	}

	/* That syscall isn't quite compatible with PCI domains, but it's
	 * used on pre-domains setup. We return the first match
	 */

	for (ln = pci_root_buses.next; ln != &pci_root_buses; ln = ln->next) {
		bus = pci_bus_b(ln);
		if (in_bus >= bus->number && in_bus <= bus->subordinate)
			break;
		bus = NULL;
	}
	if (bus == NULL || bus->sysdata == NULL)
		return -ENODEV;

	hose_node = (struct device_node *)bus->sysdata;
	hose = PCI_DN(hose_node)->phb;

	switch (which) {
	case IOBASE_BRIDGE_NUMBER:
		return (long)hose->first_busno;
	case IOBASE_MEMORY:
		return (long)hose->pci_mem_offset;
	case IOBASE_IO:
		return (long)hose->io_base_phys;
	case IOBASE_ISA_IO:
		return (long)isa_io_base;
	case IOBASE_ISA_MEM:
		return -EINVAL;
	}

	return -EOPNOTSUPP;
}

#ifdef CONFIG_NUMA
int pcibus_to_node(struct pci_bus *bus)
{
	struct pci_controller *phb = pci_bus_to_host(bus);
	return phb->node;
}
EXPORT_SYMBOL(pcibus_to_node);
#endif
