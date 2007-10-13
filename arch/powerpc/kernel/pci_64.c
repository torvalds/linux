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
#include <asm/firmware.h>

#ifdef DEBUG
#include <asm/udbg.h>
#define DBG(fmt...) printk(fmt)
#else
#define DBG(fmt...)
#endif

unsigned long pci_probe_only = 1;
int pci_assign_all_buses = 0;

static void fixup_resource(struct resource *res, struct pci_dev *dev);
static void do_bus_setup(struct pci_bus *bus);

/* pci_io_base -- the base address from which io bars are offsets.
 * This is the lowest I/O base address (so bar values are always positive),
 * and it *must* be the start of ISA space if an ISA bus exists because
 * ISA drivers use hard coded offsets.  If no ISA bus exists nothing
 * is mapped on the first 64K of IO space
 */
unsigned long pci_io_base = ISA_IO_BASE;
EXPORT_SYMBOL(pci_io_base);

LIST_HEAD(hose_list);

static struct dma_mapping_ops *pci_dma_ops;

void set_pci_dma_ops(struct dma_mapping_ops *dma_ops)
{
	pci_dma_ops = dma_ops;
}

struct dma_mapping_ops *get_pci_dma_ops(void)
{
	return pci_dma_ops;
}
EXPORT_SYMBOL(get_pci_dma_ops);

static void fixup_broken_pcnet32(struct pci_dev* dev)
{
	if ((dev->class>>8 == PCI_CLASS_NETWORK_ETHERNET)) {
		dev->vendor = PCI_VENDOR_ID_AMD;
		pci_write_config_word(dev, PCI_VENDOR_ID, PCI_VENDOR_ID_AMD);
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
	        offset = (unsigned long)hose->io_base_virt - _IO_BASE;

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
	        offset = (unsigned long)hose->io_base_virt - _IO_BASE;

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
			    resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	resource_size_t start = res->start;
	unsigned long alignto;

	if (res->flags & IORESOURCE_IO) {
	        unsigned long offset = (unsigned long)hose->io_base_virt -
					_IO_BASE;
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

void __devinit pcibios_claim_one_bus(struct pci_bus *b)
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
#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL_GPL(pcibios_claim_one_bus);
#endif

static void __init pcibios_claim_of_setup(void)
{
	struct pci_bus *b;

	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return;

	list_for_each_entry(b, &pci_root_buses, node)
		pcibios_claim_one_bus(b);
}

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
	DBG("    parse addresses (%d bytes) @ %p\n", proplen, addrs);
	for (; proplen >= 20; proplen -= 20, addrs += 5) {
		flags = pci_parse_of_flags(addrs[0]);
		if (!flags)
			continue;
		base = of_read_number(&addrs[1], 2);
		size = of_read_number(&addrs[3], 2);
		if (!size)
			continue;
		i = addrs[0] & 0xff;
		DBG("  base: %llx, size: %llx, i: %x\n",
		    (unsigned long long)base, (unsigned long long)size, i);

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
		fixup_resource(res, dev);
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

	DBG("    create device, devfn: %x, type: %s\n", devfn, type);

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

	sprintf(pci_name(dev), "%04x:%02x:%02x.%d", pci_domain_nr(bus),
		dev->bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));
	dev->class = get_int_prop(node, "class-code", 0);
	dev->revision = get_int_prop(node, "revision-id", 0);

	DBG("    class: 0x%x\n", dev->class);
	DBG("    revision: 0x%x\n", dev->revision);

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

	DBG("    adding to system ...\n");

	pci_device_add(dev, bus);

	return dev;
}
EXPORT_SYMBOL(of_create_pci_dev);

void __devinit of_scan_bus(struct device_node *node,
				  struct pci_bus *bus)
{
	struct device_node *child = NULL;
	const u32 *reg;
	int reglen, devfn;
	struct pci_dev *dev;

	DBG("of_scan_bus(%s) bus no %d... \n", node->full_name, bus->number);

	while ((child = of_get_next_child(node, child)) != NULL) {
		DBG("  * %s\n", child->full_name);
		reg = of_get_property(child, "reg", &reglen);
		if (reg == NULL || reglen < 20)
			continue;
		devfn = (reg[0] >> 8) & 0xff;

		/* create a new pci_dev for this device */
		dev = of_create_pci_dev(child, bus, devfn);
		if (!dev)
			continue;
		DBG("dev header type: %x\n", dev->hdr_type);

		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
		    dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
			of_scan_pci_bridge(child, dev);
	}

	do_bus_setup(bus);
}
EXPORT_SYMBOL(of_scan_bus);

void __devinit of_scan_pci_bridge(struct device_node *node,
			 	struct pci_dev *dev)
{
	struct pci_bus *bus;
	const u32 *busrange, *ranges;
	int len, i, mode;
	struct resource *res;
	unsigned int flags;
	u64 size;

	DBG("of_scan_pci_bridge(%s)\n", node->full_name);

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
		fixup_resource(res, dev);
	}
	sprintf(bus->name, "PCI Bus %04x:%02x", pci_domain_nr(bus),
		bus->number);
	DBG("    bus name: %s\n", bus->name);

	mode = PCI_PROBE_NORMAL;
	if (ppc_md.pci_probe_mode)
		mode = ppc_md.pci_probe_mode(bus);
	DBG("    probe mode: %d\n", mode);

	if (mode == PCI_PROBE_DEVTREE)
		of_scan_bus(node, bus);
	else if (mode == PCI_PROBE_NORMAL)
		pci_scan_child_bus(bus);
}
EXPORT_SYMBOL(of_scan_pci_bridge);

void __devinit scan_phb(struct pci_controller *hose)
{
	struct pci_bus *bus;
	struct device_node *node = hose->arch_data;
	int i, mode;
	struct resource *res;

	DBG("Scanning PHB %s\n", node ? node->full_name : "<NO NAME>");

	bus = pci_create_bus(hose->parent, hose->first_busno, hose->ops, node);
	if (bus == NULL) {
		printk(KERN_ERR "Failed to create bus for PCI domain %04x\n",
		       hose->global_number);
		return;
	}
	bus->secondary = hose->first_busno;
	hose->bus = bus;

	if (!firmware_has_feature(FW_FEATURE_ISERIES))
		pcibios_map_io_space(bus);

	bus->resource[0] = res = &hose->io_resource;
	if (res->flags && request_resource(&ioport_resource, res)) {
		printk(KERN_ERR "Failed to request PCI IO region "
		       "on PCI domain %04x\n", hose->global_number);
		DBG("res->start = 0x%016lx, res->end = 0x%016lx\n",
		    res->start, res->end);
	}

	for (i = 0; i < 3; ++i) {
		res = &hose->mem_resources[i];
		bus->resource[i+1] = res;
		if (res->flags && request_resource(&iomem_resource, res))
			printk(KERN_ERR "Failed to request PCI memory region "
			       "on PCI domain %04x\n", hose->global_number);
	}

	mode = PCI_PROBE_NORMAL;

	if (node && ppc_md.pci_probe_mode)
		mode = ppc_md.pci_probe_mode(bus);
	DBG("    probe mode: %d\n", mode);
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

	/* For now, override phys_mem_access_prot. If we need it,
	 * later, we may move that initialization to each ppc_md
	 */
	ppc_md.phys_mem_access_prot = pci_phys_mem_access_prot;

	if (firmware_has_feature(FW_FEATURE_ISERIES))
		iSeries_pcibios_init();

	printk(KERN_DEBUG "PCI: Probing PCI hardware\n");

	/* Scan all of the recorded PCI controllers.  */
	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		scan_phb(hose);
		pci_bus_add_devices(hose->bus);
	}

	if (!firmware_has_feature(FW_FEATURE_ISERIES)) {
		if (pci_probe_only)
			pcibios_claim_of_setup();
		else
			/* FIXME: `else' will be removed when
			   pci_assign_unassigned_resources() is able to work
			   correctly with [partially] allocated PCI tree. */
			pci_assign_unassigned_resources();
	}

	/* Call machine dependent final fixup */
	if (ppc_md.pcibios_fixup)
		ppc_md.pcibios_fixup();

	printk(KERN_DEBUG "PCI: Probing PCI hardware done\n");

	return 0;
}

subsys_initcall(pcibios_init);

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

/* Decide whether to display the domain number in /proc */
int pci_proc_domain(struct pci_bus *bus)
{
	if (firmware_has_feature(FW_FEATURE_ISERIES))
		return 0;
	else {
		struct pci_controller *hose = pci_bus_to_host(bus);
		return hose->buid != 0;
	}
}

void __devinit pci_process_bridge_OF_ranges(struct pci_controller *hose,
					    struct device_node *dev, int prim)
{
	const unsigned int *ranges;
	unsigned int pci_space;
	unsigned long size;
	int rlen = 0;
	int memno = 0;
	struct resource *res;
	int np, na = of_n_addr_cells(dev);
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
	ranges = of_get_property(dev, "ranges", &rlen);
	if (ranges == NULL)
		return;
	hose->io_base_phys = 0;
	while ((rlen -= np * sizeof(unsigned int)) >= 0) {
		res = NULL;
		pci_space = ranges[0];
		pci_addr = ((unsigned long)ranges[1] << 32) | ranges[2];
		cpu_phys_addr = of_translate_address(dev, &ranges[3]);
		size = ((unsigned long)ranges[na+3] << 32) | ranges[na+4];
		ranges += np;
		if (size == 0)
			continue;

		/* Now consume following elements while they are contiguous */
		while (rlen >= np * sizeof(unsigned int)) {
			unsigned long addr, phys;

			if (ranges[0] != pci_space)
				break;
			addr = ((unsigned long)ranges[1] << 32) | ranges[2];
			phys = ranges[3];
			if (na >= 2)
				phys = (phys << 32) | ranges[4];
			if (addr != pci_addr + size ||
			    phys != cpu_phys_addr + size)
				break;

			size += ((unsigned long)ranges[na+3] << 32)
				| ranges[na+4];
			ranges += np;
			rlen -= np * sizeof(unsigned int);
		}

		switch ((pci_space >> 24) & 0x3) {
		case 1:		/* I/O space */
			hose->io_base_phys = cpu_phys_addr - pci_addr;
			/* handle from 0 to top of I/O window */
			hose->pci_io_size = pci_addr + size;

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
	}
}

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

		DBG("IO unmapping for PCI-PCI bridge %s\n",
		    pci_name(bus->self));

		__flush_hash_table_range(&init_mm, res->start + _IO_BASE,
					 res->end - res->start + 1);
		return 0;
	}

	/* Get the host bridge */
	hose = pci_bus_to_host(bus);

	/* Check if we have IOs allocated */
	if (hose->io_base_alloc == 0)
		return 0;

	DBG("IO unmapping for PHB %s\n",
	    ((struct device_node *)hose->arch_data)->full_name);
	DBG("  alloc=0x%p\n", hose->io_base_alloc);

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
		DBG("IO mapping for PCI-PCI bridge %s\n",
		    pci_name(bus->self));
		DBG("  virt=0x%016lx...0x%016lx\n",
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

	DBG("IO mapping for PHB %s\n",
	    ((struct device_node *)hose->arch_data)->full_name);
	DBG("  phys=0x%016lx, virt=0x%p (alloc=0x%p)\n",
	    hose->io_base_phys, hose->io_base_virt, hose->io_base_alloc);
	DBG("  size=0x%016lx (alloc=0x%016lx)\n",
	    hose->pci_io_size, size_page);

	/* Establish the mapping */
	if (__ioremap_at(phys_page, area->addr, size_page,
			 _PAGE_NO_CACHE | _PAGE_GUARDED) == NULL)
		return -ENOMEM;

	/* Fixup hose IO resource */
	io_virt_offset = (unsigned long)hose->io_base_virt - _IO_BASE;
	hose->io_resource.start += io_virt_offset;
	hose->io_resource.end += io_virt_offset;

	DBG("  hose->io_resource=0x%016lx...0x%016lx\n",
	    hose->io_resource.start, hose->io_resource.end);

	return 0;
}
EXPORT_SYMBOL_GPL(pcibios_map_io_space);

static void __devinit fixup_resource(struct resource *res, struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	unsigned long offset;

	if (res->flags & IORESOURCE_IO) {
		offset = (unsigned long)hose->io_base_virt - _IO_BASE;
		res->start += offset;
		res->end += offset;
	} else if (res->flags & IORESOURCE_MEM) {
		res->start += hose->pci_mem_offset;
		res->end += hose->pci_mem_offset;
	}
}

void __devinit pcibios_fixup_device_resources(struct pci_dev *dev,
					      struct pci_bus *bus)
{
	/* Update device resources.  */
	int i;

	DBG("%s: Fixup resources:\n", pci_name(dev));
	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = &dev->resource[i];
		if (!res->flags)
			continue;

		DBG("  0x%02x < %08lx:0x%016lx...0x%016lx\n",
		    i, res->flags, res->start, res->end);

		fixup_resource(res, dev);

		DBG("       > %08lx:0x%016lx...0x%016lx\n",
		    res->flags, res->start, res->end);
	}
}
EXPORT_SYMBOL(pcibios_fixup_device_resources);

void __devinit pcibios_setup_new_device(struct pci_dev *dev)
{
	struct dev_archdata *sd = &dev->dev.archdata;

	sd->of_node = pci_device_to_OF_node(dev);

	DBG("PCI device %s OF node: %s\n", pci_name(dev),
	    sd->of_node ? sd->of_node->full_name : "<none>");

	sd->dma_ops = pci_dma_ops;
#ifdef CONFIG_NUMA
	sd->numa_node = pcibus_to_node(dev->bus);
#else
	sd->numa_node = -1;
#endif
	if (ppc_md.pci_dma_dev_setup)
		ppc_md.pci_dma_dev_setup(dev);
}
EXPORT_SYMBOL(pcibios_setup_new_device);

static void __devinit do_bus_setup(struct pci_bus *bus)
{
	struct pci_dev *dev;

	if (ppc_md.pci_dma_bus_setup)
		ppc_md.pci_dma_bus_setup(bus);

	list_for_each_entry(dev, &bus->devices, bus_list)
		pcibios_setup_new_device(dev);

	/* Read default IRQs and fixup if necessary */
	list_for_each_entry(dev, &bus->devices, bus_list) {
		pci_read_irq_line(dev);
		if (ppc_md.pci_irq_fixup)
			ppc_md.pci_irq_fixup(dev);
	}
}

void __devinit pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;
	struct device_node *np;

	np = pci_bus_to_OF_node(bus);

	DBG("pcibios_fixup_bus(%s)\n", np ? np->full_name : "<???>");

	if (dev && pci_probe_only &&
	    (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		/* This is a subordinate bridge */

		pci_read_bridge_bases(bus);
		pcibios_fixup_device_resources(dev, bus);
	}

	do_bus_setup(bus);

	if (!pci_probe_only)
		return;

	list_for_each_entry(dev, &bus->devices, bus_list)
		if ((dev->class >> 8) != PCI_CLASS_BRIDGE_PCI)
			pcibios_fixup_device_resources(dev, bus);
}
EXPORT_SYMBOL(pcibios_fixup_bus);

unsigned long pci_address_to_pio(phys_addr_t address)
{
	struct pci_controller *hose, *tmp;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		if (address >= hose->io_base_phys &&
		    address < (hose->io_base_phys + hose->pci_io_size)) {
			unsigned long base =
				(unsigned long)hose->io_base_virt - _IO_BASE;
			return base + (address - hose->io_base_phys);
		}
	}
	return (unsigned int)-1;
}
EXPORT_SYMBOL_GPL(pci_address_to_pio);


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
	if (machine_is_compatible("MacRISC4"))
		if (in_bus == 0)
			in_bus = 0xf0;

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
