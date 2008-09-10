/* pci.c: UltraSparc PCI controller support.
 *
 * Copyright (C) 1997, 1998, 1999 David S. Miller (davem@redhat.com)
 * Copyright (C) 1998, 1999 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek   (jj@ultra.linux.cz)
 *
 * OF tree based PCI bus probing taken from the PowerPC port
 * with minor modifications, see there for credits.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/apb.h>

#include "pci_impl.h"

/* List of all PCI controllers found in the system. */
struct pci_pbm_info *pci_pbm_root = NULL;

/* Each PBM found gets a unique index. */
int pci_num_pbms = 0;

volatile int pci_poke_in_progress;
volatile int pci_poke_cpu = -1;
volatile int pci_poke_faulted;

static DEFINE_SPINLOCK(pci_poke_lock);

void pci_config_read8(u8 *addr, u8 *ret)
{
	unsigned long flags;
	u8 byte;

	spin_lock_irqsave(&pci_poke_lock, flags);
	pci_poke_cpu = smp_processor_id();
	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduba [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (byte)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	pci_poke_in_progress = 0;
	pci_poke_cpu = -1;
	if (!pci_poke_faulted)
		*ret = byte;
	spin_unlock_irqrestore(&pci_poke_lock, flags);
}

void pci_config_read16(u16 *addr, u16 *ret)
{
	unsigned long flags;
	u16 word;

	spin_lock_irqsave(&pci_poke_lock, flags);
	pci_poke_cpu = smp_processor_id();
	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduha [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (word)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	pci_poke_in_progress = 0;
	pci_poke_cpu = -1;
	if (!pci_poke_faulted)
		*ret = word;
	spin_unlock_irqrestore(&pci_poke_lock, flags);
}

void pci_config_read32(u32 *addr, u32 *ret)
{
	unsigned long flags;
	u32 dword;

	spin_lock_irqsave(&pci_poke_lock, flags);
	pci_poke_cpu = smp_processor_id();
	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "lduwa [%1] %2, %0\n\t"
			     "membar #Sync"
			     : "=r" (dword)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	pci_poke_in_progress = 0;
	pci_poke_cpu = -1;
	if (!pci_poke_faulted)
		*ret = dword;
	spin_unlock_irqrestore(&pci_poke_lock, flags);
}

void pci_config_write8(u8 *addr, u8 val)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_poke_lock, flags);
	pci_poke_cpu = smp_processor_id();
	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stba %0, [%1] %2\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (val), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	pci_poke_in_progress = 0;
	pci_poke_cpu = -1;
	spin_unlock_irqrestore(&pci_poke_lock, flags);
}

void pci_config_write16(u16 *addr, u16 val)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_poke_lock, flags);
	pci_poke_cpu = smp_processor_id();
	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stha %0, [%1] %2\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (val), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	pci_poke_in_progress = 0;
	pci_poke_cpu = -1;
	spin_unlock_irqrestore(&pci_poke_lock, flags);
}

void pci_config_write32(u32 *addr, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&pci_poke_lock, flags);
	pci_poke_cpu = smp_processor_id();
	pci_poke_in_progress = 1;
	pci_poke_faulted = 0;
	__asm__ __volatile__("membar #Sync\n\t"
			     "stwa %0, [%1] %2\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (val), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E_L)
			     : "memory");
	pci_poke_in_progress = 0;
	pci_poke_cpu = -1;
	spin_unlock_irqrestore(&pci_poke_lock, flags);
}

static int ofpci_verbose;

static int __init ofpci_debug(char *str)
{
	int val = 0;

	get_option(&str, &val);
	if (val)
		ofpci_verbose = 1;
	return 1;
}

__setup("ofpci_debug=", ofpci_debug);

static unsigned long pci_parse_of_flags(u32 addr0)
{
	unsigned long flags = 0;

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

/* The of_device layer has translated all of the assigned-address properties
 * into physical address resources, we only have to figure out the register
 * mapping.
 */
static void pci_parse_of_addrs(struct of_device *op,
			       struct device_node *node,
			       struct pci_dev *dev)
{
	struct resource *op_res;
	const u32 *addrs;
	int proplen;

	addrs = of_get_property(node, "assigned-addresses", &proplen);
	if (!addrs)
		return;
	if (ofpci_verbose)
		printk("    parse addresses (%d bytes) @ %p\n",
		       proplen, addrs);
	op_res = &op->resource[0];
	for (; proplen >= 20; proplen -= 20, addrs += 5, op_res++) {
		struct resource *res;
		unsigned long flags;
		int i;

		flags = pci_parse_of_flags(addrs[0]);
		if (!flags)
			continue;
		i = addrs[0] & 0xff;
		if (ofpci_verbose)
			printk("  start: %lx, end: %lx, i: %x\n",
			       op_res->start, op_res->end, i);

		if (PCI_BASE_ADDRESS_0 <= i && i <= PCI_BASE_ADDRESS_5) {
			res = &dev->resource[(i - PCI_BASE_ADDRESS_0) >> 2];
		} else if (i == dev->rom_base_reg) {
			res = &dev->resource[PCI_ROM_RESOURCE];
			flags |= IORESOURCE_READONLY | IORESOURCE_CACHEABLE;
		} else {
			printk(KERN_ERR "PCI: bad cfg reg num 0x%x\n", i);
			continue;
		}
		res->start = op_res->start;
		res->end = op_res->end;
		res->flags = flags;
		res->name = pci_name(dev);
	}
}

struct pci_dev *of_create_pci_dev(struct pci_pbm_info *pbm,
				  struct device_node *node,
				  struct pci_bus *bus, int devfn)
{
	struct dev_archdata *sd;
	struct of_device *op;
	struct pci_dev *dev;
	const char *type;
	u32 class;

	dev = alloc_pci_dev();
	if (!dev)
		return NULL;

	sd = &dev->dev.archdata;
	sd->iommu = pbm->iommu;
	sd->stc = &pbm->stc;
	sd->host_controller = pbm;
	sd->prom_node = node;
	sd->op = op = of_find_device_by_node(node);
	sd->numa_node = pbm->numa_node;

	sd = &op->dev.archdata;
	sd->iommu = pbm->iommu;
	sd->stc = &pbm->stc;
	sd->numa_node = pbm->numa_node;

	if (!strcmp(node->name, "ebus"))
		of_propagate_archdata(op);

	type = of_get_property(node, "device_type", NULL);
	if (type == NULL)
		type = "";

	if (ofpci_verbose)
		printk("    create device, devfn: %x, type: %s\n",
		       devfn, type);

	dev->bus = bus;
	dev->sysdata = node;
	dev->dev.parent = bus->bridge;
	dev->dev.bus = &pci_bus_type;
	dev->devfn = devfn;
	dev->multifunction = 0;		/* maybe a lie? */

	dev->vendor = of_getintprop_default(node, "vendor-id", 0xffff);
	dev->device = of_getintprop_default(node, "device-id", 0xffff);
	dev->subsystem_vendor =
		of_getintprop_default(node, "subsystem-vendor-id", 0);
	dev->subsystem_device =
		of_getintprop_default(node, "subsystem-id", 0);

	dev->cfg_size = pci_cfg_space_size(dev);

	/* We can't actually use the firmware value, we have
	 * to read what is in the register right now.  One
	 * reason is that in the case of IDE interfaces the
	 * firmware can sample the value before the the IDE
	 * interface is programmed into native mode.
	 */
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class);
	dev->class = class >> 8;
	dev->revision = class & 0xff;

	dev_set_name(&dev->dev, "%04x:%02x:%02x.%d", pci_domain_nr(bus),
		dev->bus->number, PCI_SLOT(devfn), PCI_FUNC(devfn));

	if (ofpci_verbose)
		printk("    class: 0x%x device name: %s\n",
		       dev->class, pci_name(dev));

	/* I have seen IDE devices which will not respond to
	 * the bmdma simplex check reads if bus mastering is
	 * disabled.
	 */
	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE)
		pci_set_master(dev);

	dev->current_state = 4;		/* unknown power state */
	dev->error_state = pci_channel_io_normal;

	if (!strcmp(type, "pci") || !strcmp(type, "pciex")) {
		/* a PCI-PCI bridge */
		dev->hdr_type = PCI_HEADER_TYPE_BRIDGE;
		dev->rom_base_reg = PCI_ROM_ADDRESS1;
	} else if (!strcmp(type, "cardbus")) {
		dev->hdr_type = PCI_HEADER_TYPE_CARDBUS;
	} else {
		dev->hdr_type = PCI_HEADER_TYPE_NORMAL;
		dev->rom_base_reg = PCI_ROM_ADDRESS;

		dev->irq = sd->op->irqs[0];
		if (dev->irq == 0xffffffff)
			dev->irq = PCI_IRQ_NONE;
	}

	pci_parse_of_addrs(sd->op, node, dev);

	if (ofpci_verbose)
		printk("    adding to system ...\n");

	pci_device_add(dev, bus);

	return dev;
}

static void __devinit apb_calc_first_last(u8 map, u32 *first_p, u32 *last_p)
{
	u32 idx, first, last;

	first = 8;
	last = 0;
	for (idx = 0; idx < 8; idx++) {
		if ((map & (1 << idx)) != 0) {
			if (first > idx)
				first = idx;
			if (last < idx)
				last = idx;
		}
	}

	*first_p = first;
	*last_p = last;
}

static void pci_resource_adjust(struct resource *res,
				struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

/* For PCI bus devices which lack a 'ranges' property we interrogate
 * the config space values to set the resources, just like the generic
 * Linux PCI probing code does.
 */
static void __devinit pci_cfg_fake_ranges(struct pci_dev *dev,
					  struct pci_bus *bus,
					  struct pci_pbm_info *pbm)
{
	struct resource *res;
	u8 io_base_lo, io_limit_lo;
	u16 mem_base_lo, mem_limit_lo;
	unsigned long base, limit;

	pci_read_config_byte(dev, PCI_IO_BASE, &io_base_lo);
	pci_read_config_byte(dev, PCI_IO_LIMIT, &io_limit_lo);
	base = (io_base_lo & PCI_IO_RANGE_MASK) << 8;
	limit = (io_limit_lo & PCI_IO_RANGE_MASK) << 8;

	if ((io_base_lo & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32) {
		u16 io_base_hi, io_limit_hi;

		pci_read_config_word(dev, PCI_IO_BASE_UPPER16, &io_base_hi);
		pci_read_config_word(dev, PCI_IO_LIMIT_UPPER16, &io_limit_hi);
		base |= (io_base_hi << 16);
		limit |= (io_limit_hi << 16);
	}

	res = bus->resource[0];
	if (base <= limit) {
		res->flags = (io_base_lo & PCI_IO_RANGE_TYPE_MASK) | IORESOURCE_IO;
		if (!res->start)
			res->start = base;
		if (!res->end)
			res->end = limit + 0xfff;
		pci_resource_adjust(res, &pbm->io_space);
	}

	pci_read_config_word(dev, PCI_MEMORY_BASE, &mem_base_lo);
	pci_read_config_word(dev, PCI_MEMORY_LIMIT, &mem_limit_lo);
	base = (mem_base_lo & PCI_MEMORY_RANGE_MASK) << 16;
	limit = (mem_limit_lo & PCI_MEMORY_RANGE_MASK) << 16;

	res = bus->resource[1];
	if (base <= limit) {
		res->flags = ((mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) |
			      IORESOURCE_MEM);
		res->start = base;
		res->end = limit + 0xfffff;
		pci_resource_adjust(res, &pbm->mem_space);
	}

	pci_read_config_word(dev, PCI_PREF_MEMORY_BASE, &mem_base_lo);
	pci_read_config_word(dev, PCI_PREF_MEMORY_LIMIT, &mem_limit_lo);
	base = (mem_base_lo & PCI_PREF_RANGE_MASK) << 16;
	limit = (mem_limit_lo & PCI_PREF_RANGE_MASK) << 16;

	if ((mem_base_lo & PCI_PREF_RANGE_TYPE_MASK) == PCI_PREF_RANGE_TYPE_64) {
		u32 mem_base_hi, mem_limit_hi;

		pci_read_config_dword(dev, PCI_PREF_BASE_UPPER32, &mem_base_hi);
		pci_read_config_dword(dev, PCI_PREF_LIMIT_UPPER32, &mem_limit_hi);

		/*
		 * Some bridges set the base > limit by default, and some
		 * (broken) BIOSes do not initialize them.  If we find
		 * this, just assume they are not being used.
		 */
		if (mem_base_hi <= mem_limit_hi) {
			base |= ((long) mem_base_hi) << 32;
			limit |= ((long) mem_limit_hi) << 32;
		}
	}

	res = bus->resource[2];
	if (base <= limit) {
		res->flags = ((mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) |
			      IORESOURCE_MEM | IORESOURCE_PREFETCH);
		res->start = base;
		res->end = limit + 0xfffff;
		pci_resource_adjust(res, &pbm->mem_space);
	}
}

/* Cook up fake bus resources for SUNW,simba PCI bridges which lack
 * a proper 'ranges' property.
 */
static void __devinit apb_fake_ranges(struct pci_dev *dev,
				      struct pci_bus *bus,
				      struct pci_pbm_info *pbm)
{
	struct resource *res;
	u32 first, last;
	u8 map;

	pci_read_config_byte(dev, APB_IO_ADDRESS_MAP, &map);
	apb_calc_first_last(map, &first, &last);
	res = bus->resource[0];
	res->start = (first << 21);
	res->end = (last << 21) + ((1 << 21) - 1);
	res->flags = IORESOURCE_IO;
	pci_resource_adjust(res, &pbm->io_space);

	pci_read_config_byte(dev, APB_MEM_ADDRESS_MAP, &map);
	apb_calc_first_last(map, &first, &last);
	res = bus->resource[1];
	res->start = (first << 21);
	res->end = (last << 21) + ((1 << 21) - 1);
	res->flags = IORESOURCE_MEM;
	pci_resource_adjust(res, &pbm->mem_space);
}

static void __devinit pci_of_scan_bus(struct pci_pbm_info *pbm,
				      struct device_node *node,
				      struct pci_bus *bus);

#define GET_64BIT(prop, i)	((((u64) (prop)[(i)]) << 32) | (prop)[(i)+1])

static void __devinit of_scan_pci_bridge(struct pci_pbm_info *pbm,
					 struct device_node *node,
					 struct pci_dev *dev)
{
	struct pci_bus *bus;
	const u32 *busrange, *ranges;
	int len, i, simba;
	struct resource *res;
	unsigned int flags;
	u64 size;

	if (ofpci_verbose)
		printk("of_scan_pci_bridge(%s)\n", node->full_name);

	/* parse bus-range property */
	busrange = of_get_property(node, "bus-range", &len);
	if (busrange == NULL || len != 8) {
		printk(KERN_DEBUG "Can't get bus-range for PCI-PCI bridge %s\n",
		       node->full_name);
		return;
	}
	ranges = of_get_property(node, "ranges", &len);
	simba = 0;
	if (ranges == NULL) {
		const char *model = of_get_property(node, "model", NULL);
		if (model && !strcmp(model, "SUNW,simba"))
			simba = 1;
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

	/* parse ranges property, or cook one up by hand for Simba */
	/* PCI #address-cells == 3 and #size-cells == 2 always */
	res = &dev->resource[PCI_BRIDGE_RESOURCES];
	for (i = 0; i < PCI_NUM_RESOURCES - PCI_BRIDGE_RESOURCES; ++i) {
		res->flags = 0;
		bus->resource[i] = res;
		++res;
	}
	if (simba) {
		apb_fake_ranges(dev, bus, pbm);
		goto after_ranges;
	} else if (ranges == NULL) {
		pci_cfg_fake_ranges(dev, bus, pbm);
		goto after_ranges;
	}
	i = 1;
	for (; len >= 32; len -= 32, ranges += 8) {
		struct resource *root;

		flags = pci_parse_of_flags(ranges[0]);
		size = GET_64BIT(ranges, 6);
		if (flags == 0 || size == 0)
			continue;
		if (flags & IORESOURCE_IO) {
			res = bus->resource[0];
			if (res->flags) {
				printk(KERN_ERR "PCI: ignoring extra I/O range"
				       " for bridge %s\n", node->full_name);
				continue;
			}
			root = &pbm->io_space;
		} else {
			if (i >= PCI_NUM_RESOURCES - PCI_BRIDGE_RESOURCES) {
				printk(KERN_ERR "PCI: too many memory ranges"
				       " for bridge %s\n", node->full_name);
				continue;
			}
			res = bus->resource[i];
			++i;
			root = &pbm->mem_space;
		}

		res->start = GET_64BIT(ranges, 1);
		res->end = res->start + size - 1;
		res->flags = flags;

		/* Another way to implement this would be to add an of_device
		 * layer routine that can calculate a resource for a given
		 * range property value in a PCI device.
		 */
		pci_resource_adjust(res, root);
	}
after_ranges:
	sprintf(bus->name, "PCI Bus %04x:%02x", pci_domain_nr(bus),
		bus->number);
	if (ofpci_verbose)
		printk("    bus name: %s\n", bus->name);

	pci_of_scan_bus(pbm, node, bus);
}

static void __devinit pci_of_scan_bus(struct pci_pbm_info *pbm,
				      struct device_node *node,
				      struct pci_bus *bus)
{
	struct device_node *child;
	const u32 *reg;
	int reglen, devfn, prev_devfn;
	struct pci_dev *dev;

	if (ofpci_verbose)
		printk("PCI: scan_bus[%s] bus no %d\n",
		       node->full_name, bus->number);

	child = NULL;
	prev_devfn = -1;
	while ((child = of_get_next_child(node, child)) != NULL) {
		if (ofpci_verbose)
			printk("  * %s\n", child->full_name);
		reg = of_get_property(child, "reg", &reglen);
		if (reg == NULL || reglen < 20)
			continue;

		devfn = (reg[0] >> 8) & 0xff;

		/* This is a workaround for some device trees
		 * which list PCI devices twice.  On the V100
		 * for example, device number 3 is listed twice.
		 * Once as "pm" and once again as "lomp".
		 */
		if (devfn == prev_devfn)
			continue;
		prev_devfn = devfn;

		/* create a new pci_dev for this device */
		dev = of_create_pci_dev(pbm, child, bus, devfn);
		if (!dev)
			continue;
		if (ofpci_verbose)
			printk("PCI: dev header type: %x\n",
			       dev->hdr_type);

		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
		    dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
			of_scan_pci_bridge(pbm, child, dev);
	}
}

static ssize_t
show_pciobppath_attr(struct device * dev, struct device_attribute * attr, char * buf)
{
	struct pci_dev *pdev;
	struct device_node *dp;

	pdev = to_pci_dev(dev);
	dp = pdev->dev.archdata.prom_node;

	return snprintf (buf, PAGE_SIZE, "%s\n", dp->full_name);
}

static DEVICE_ATTR(obppath, S_IRUSR | S_IRGRP | S_IROTH, show_pciobppath_attr, NULL);

static void __devinit pci_bus_register_of_sysfs(struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child_bus;
	int err;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/* we don't really care if we can create this file or
		 * not, but we need to assign the result of the call
		 * or the world will fall under alien invasion and
		 * everybody will be frozen on a spaceship ready to be
		 * eaten on alpha centauri by some green and jelly
		 * humanoid.
		 */
		err = sysfs_create_file(&dev->dev.kobj, &dev_attr_obppath.attr);
	}
	list_for_each_entry(child_bus, &bus->children, node)
		pci_bus_register_of_sysfs(child_bus);
}

struct pci_bus * __devinit pci_scan_one_pbm(struct pci_pbm_info *pbm,
					    struct device *parent)
{
	struct device_node *node = pbm->prom_node;
	struct pci_bus *bus;

	printk("PCI: Scanning PBM %s\n", node->full_name);

	bus = pci_create_bus(parent, pbm->pci_first_busno, pbm->pci_ops, pbm);
	if (!bus) {
		printk(KERN_ERR "Failed to create bus for %s\n",
		       node->full_name);
		return NULL;
	}
	bus->secondary = pbm->pci_first_busno;
	bus->subordinate = pbm->pci_last_busno;

	bus->resource[0] = &pbm->io_space;
	bus->resource[1] = &pbm->mem_space;

	pci_of_scan_bus(pbm, node, bus);
	pci_bus_add_devices(bus);
	pci_bus_register_of_sysfs(bus);

	return bus;
}

void __devinit pcibios_fixup_bus(struct pci_bus *pbus)
{
	struct pci_pbm_info *pbm = pbus->sysdata;

	/* Generic PCI bus probing sets these to point at
	 * &io{port,mem}_resouce which is wrong for us.
	 */
	pbus->resource[0] = &pbm->io_space;
	pbus->resource[1] = &pbm->mem_space;
}

struct resource *pcibios_select_root(struct pci_dev *pdev, struct resource *r)
{
	struct pci_pbm_info *pbm = pdev->bus->sysdata;
	struct resource *root = NULL;

	if (r->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	if (r->flags & IORESOURCE_MEM)
		root = &pbm->mem_space;

	return root;
}

void pcibios_update_irq(struct pci_dev *pdev, int irq)
{
}

void pcibios_align_resource(void *data, struct resource *res,
			    resource_size_t size, resource_size_t align)
{
}

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

void pcibios_resource_to_bus(struct pci_dev *pdev, struct pci_bus_region *region,
			     struct resource *res)
{
	struct pci_pbm_info *pbm = pdev->bus->sysdata;
	struct resource zero_res, *root;

	zero_res.start = 0;
	zero_res.end = 0;
	zero_res.flags = res->flags;

	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else
		root = &pbm->mem_space;

	pci_resource_adjust(&zero_res, root);

	region->start = res->start - zero_res.start;
	region->end = res->end - zero_res.start;
}
EXPORT_SYMBOL(pcibios_resource_to_bus);

void pcibios_bus_to_resource(struct pci_dev *pdev, struct resource *res,
			     struct pci_bus_region *region)
{
	struct pci_pbm_info *pbm = pdev->bus->sysdata;
	struct resource *root;

	res->start = region->start;
	res->end = region->end;

	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else
		root = &pbm->mem_space;

	pci_resource_adjust(res, root);
}
EXPORT_SYMBOL(pcibios_bus_to_resource);

char * __devinit pcibios_setup(char *str)
{
	return str;
}

/* Platform support for /proc/bus/pci/X/Y mmap()s. */

/* If the user uses a host-bridge as the PCI device, he may use
 * this to perform a raw mmap() of the I/O or MEM space behind
 * that controller.
 *
 * This can be useful for execution of x86 PCI bios initialization code
 * on a PCI card, like the xfree86 int10 stuff does.
 */
static int __pci_mmap_make_offset_bus(struct pci_dev *pdev, struct vm_area_struct *vma,
				      enum pci_mmap_state mmap_state)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	unsigned long space_size, user_offset, user_size;

	if (mmap_state == pci_mmap_io) {
		space_size = (pbm->io_space.end -
			      pbm->io_space.start) + 1;
	} else {
		space_size = (pbm->mem_space.end -
			      pbm->mem_space.start) + 1;
	}

	/* Make sure the request is in range. */
	user_offset = vma->vm_pgoff << PAGE_SHIFT;
	user_size = vma->vm_end - vma->vm_start;

	if (user_offset >= space_size ||
	    (user_offset + user_size) > space_size)
		return -EINVAL;

	if (mmap_state == pci_mmap_io) {
		vma->vm_pgoff = (pbm->io_space.start +
				 user_offset) >> PAGE_SHIFT;
	} else {
		vma->vm_pgoff = (pbm->mem_space.start +
				 user_offset) >> PAGE_SHIFT;
	}

	return 0;
}

/* Adjust vm_pgoff of VMA such that it is the physical page offset
 * corresponding to the 32-bit pci bus offset for DEV requested by the user.
 *
 * Basically, the user finds the base address for his device which he wishes
 * to mmap.  They read the 32-bit value from the config space base register,
 * add whatever PAGE_SIZE multiple offset they wish, and feed this into the
 * offset parameter of mmap on /proc/bus/pci/XXX for that device.
 *
 * Returns negative error code on failure, zero on success.
 */
static int __pci_mmap_make_offset(struct pci_dev *pdev,
				  struct vm_area_struct *vma,
				  enum pci_mmap_state mmap_state)
{
	unsigned long user_paddr, user_size;
	int i, err;

	/* First compute the physical address in vma->vm_pgoff,
	 * making sure the user offset is within range in the
	 * appropriate PCI space.
	 */
	err = __pci_mmap_make_offset_bus(pdev, vma, mmap_state);
	if (err)
		return err;

	/* If this is a mapping on a host bridge, any address
	 * is OK.
	 */
	if ((pdev->class >> 8) == PCI_CLASS_BRIDGE_HOST)
		return err;

	/* Otherwise make sure it's in the range for one of the
	 * device's resources.
	 */
	user_paddr = vma->vm_pgoff << PAGE_SHIFT;
	user_size = vma->vm_end - vma->vm_start;

	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		struct resource *rp = &pdev->resource[i];

		/* Active? */
		if (!rp->flags)
			continue;

		/* Same type? */
		if (i == PCI_ROM_RESOURCE) {
			if (mmap_state != pci_mmap_mem)
				continue;
		} else {
			if ((mmap_state == pci_mmap_io &&
			     (rp->flags & IORESOURCE_IO) == 0) ||
			    (mmap_state == pci_mmap_mem &&
			     (rp->flags & IORESOURCE_MEM) == 0))
				continue;
		}

		if ((rp->start <= user_paddr) &&
		    (user_paddr + user_size) <= (rp->end + 1UL))
			break;
	}

	if (i > PCI_ROM_RESOURCE)
		return -EINVAL;

	return 0;
}

/* Set vm_flags of VMA, as appropriate for this architecture, for a pci device
 * mapping.
 */
static void __pci_mmap_set_flags(struct pci_dev *dev, struct vm_area_struct *vma,
					    enum pci_mmap_state mmap_state)
{
	vma->vm_flags |= (VM_IO | VM_RESERVED);
}

/* Set vm_page_prot of VMA, as appropriate for this architecture, for a pci
 * device mapping.
 */
static void __pci_mmap_set_pgprot(struct pci_dev *dev, struct vm_area_struct *vma,
					     enum pci_mmap_state mmap_state)
{
	/* Our io_remap_pfn_range takes care of this, do nothing.  */
}

/* Perform the actual remap of the pages for a PCI device mapping, as appropriate
 * for this architecture.  The region in the process to map is described by vm_start
 * and vm_end members of VMA, the base physical address is found in vm_pgoff.
 * The pci device structure is provided so that architectures may make mapping
 * decisions on a per-device or per-bus basis.
 *
 * Returns a negative error code on failure, zero on success.
 */
int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			enum pci_mmap_state mmap_state,
			int write_combine)
{
	int ret;

	ret = __pci_mmap_make_offset(dev, vma, mmap_state);
	if (ret < 0)
		return ret;

	__pci_mmap_set_flags(dev, vma, mmap_state);
	__pci_mmap_set_pgprot(dev, vma, mmap_state);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	ret = io_remap_pfn_range(vma, vma->vm_start,
				 vma->vm_pgoff,
				 vma->vm_end - vma->vm_start,
				 vma->vm_page_prot);
	if (ret)
		return ret;

	return 0;
}

#ifdef CONFIG_NUMA
int pcibus_to_node(struct pci_bus *pbus)
{
	struct pci_pbm_info *pbm = pbus->sysdata;

	return pbm->numa_node;
}
EXPORT_SYMBOL(pcibus_to_node);
#endif

/* Return the domain number for this pci bus */

int pci_domain_nr(struct pci_bus *pbus)
{
	struct pci_pbm_info *pbm = pbus->sysdata;
	int ret;

	if (!pbm) {
		ret = -ENXIO;
	} else {
		ret = pbm->index;
	}

	return ret;
}
EXPORT_SYMBOL(pci_domain_nr);

#ifdef CONFIG_PCI_MSI
int arch_setup_msi_irq(struct pci_dev *pdev, struct msi_desc *desc)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	int virt_irq;

	if (!pbm->setup_msi_irq)
		return -EINVAL;

	return pbm->setup_msi_irq(&virt_irq, pdev, desc);
}

void arch_teardown_msi_irq(unsigned int virt_irq)
{
	struct msi_desc *entry = get_irq_msi(virt_irq);
	struct pci_dev *pdev = entry->dev;
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;

	if (!pbm->teardown_msi_irq)
		return;

	return pbm->teardown_msi_irq(virt_irq, pdev);
}
#endif /* !(CONFIG_PCI_MSI) */

struct device_node *pci_device_to_OF_node(struct pci_dev *pdev)
{
	return pdev->dev.archdata.prom_node;
}
EXPORT_SYMBOL(pci_device_to_OF_node);

static void ali_sound_dma_hack(struct pci_dev *pdev, int set_bit)
{
	struct pci_dev *ali_isa_bridge;
	u8 val;

	/* ALI sound chips generate 31-bits of DMA, a special register
	 * determines what bit 31 is emitted as.
	 */
	ali_isa_bridge = pci_get_device(PCI_VENDOR_ID_AL,
					 PCI_DEVICE_ID_AL_M1533,
					 NULL);

	pci_read_config_byte(ali_isa_bridge, 0x7e, &val);
	if (set_bit)
		val |= 0x01;
	else
		val &= ~0x01;
	pci_write_config_byte(ali_isa_bridge, 0x7e, val);
	pci_dev_put(ali_isa_bridge);
}

int pci_dma_supported(struct pci_dev *pdev, u64 device_mask)
{
	u64 dma_addr_mask;

	if (pdev == NULL) {
		dma_addr_mask = 0xffffffff;
	} else {
		struct iommu *iommu = pdev->dev.archdata.iommu;

		dma_addr_mask = iommu->dma_addr_mask;

		if (pdev->vendor == PCI_VENDOR_ID_AL &&
		    pdev->device == PCI_DEVICE_ID_AL_M5451 &&
		    device_mask == 0x7fffffff) {
			ali_sound_dma_hack(pdev,
					   (dma_addr_mask & 0x80000000) != 0);
			return 1;
		}
	}

	if (device_mask >= (1UL << 32UL))
		return 0;

	return (device_mask & dma_addr_mask) == dma_addr_mask;
}

void pci_resource_to_user(const struct pci_dev *pdev, int bar,
			  const struct resource *rp, resource_size_t *start,
			  resource_size_t *end)
{
	struct pci_pbm_info *pbm = pdev->dev.archdata.host_controller;
	unsigned long offset;

	if (rp->flags & IORESOURCE_IO)
		offset = pbm->io_space.start;
	else
		offset = pbm->mem_space.start;

	*start = rp->start - offset;
	*end = rp->end - offset;
}
