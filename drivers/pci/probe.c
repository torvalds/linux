/*
 * probe.c - PCI detection and setup code
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/cpumask.h>
#include "pci.h"

#define CARDBUS_LATENCY_TIMER	176	/* secondary latency timer */
#define CARDBUS_RESERVE_BUSNR	3
#define PCI_CFG_SPACE_SIZE	256
#define PCI_CFG_SPACE_EXP_SIZE	4096

/* Ugh.  Need to stop exporting this to modules. */
LIST_HEAD(pci_root_buses);
EXPORT_SYMBOL(pci_root_buses);

LIST_HEAD(pci_devices);

#ifdef HAVE_PCI_LEGACY
/**
 * pci_create_legacy_files - create legacy I/O port and memory files
 * @b: bus to create files under
 *
 * Some platforms allow access to legacy I/O port and ISA memory space on
 * a per-bus basis.  This routine creates the files and ties them into
 * their associated read, write and mmap files from pci-sysfs.c
 */
static void pci_create_legacy_files(struct pci_bus *b)
{
	b->legacy_io = kzalloc(sizeof(struct bin_attribute) * 2,
			       GFP_ATOMIC);
	if (b->legacy_io) {
		b->legacy_io->attr.name = "legacy_io";
		b->legacy_io->size = 0xffff;
		b->legacy_io->attr.mode = S_IRUSR | S_IWUSR;
		b->legacy_io->attr.owner = THIS_MODULE;
		b->legacy_io->read = pci_read_legacy_io;
		b->legacy_io->write = pci_write_legacy_io;
		class_device_create_bin_file(&b->class_dev, b->legacy_io);

		/* Allocated above after the legacy_io struct */
		b->legacy_mem = b->legacy_io + 1;
		b->legacy_mem->attr.name = "legacy_mem";
		b->legacy_mem->size = 1024*1024;
		b->legacy_mem->attr.mode = S_IRUSR | S_IWUSR;
		b->legacy_mem->attr.owner = THIS_MODULE;
		b->legacy_mem->mmap = pci_mmap_legacy_mem;
		class_device_create_bin_file(&b->class_dev, b->legacy_mem);
	}
}

void pci_remove_legacy_files(struct pci_bus *b)
{
	if (b->legacy_io) {
		class_device_remove_bin_file(&b->class_dev, b->legacy_io);
		class_device_remove_bin_file(&b->class_dev, b->legacy_mem);
		kfree(b->legacy_io); /* both are allocated here */
	}
}
#else /* !HAVE_PCI_LEGACY */
static inline void pci_create_legacy_files(struct pci_bus *bus) { return; }
void pci_remove_legacy_files(struct pci_bus *bus) { return; }
#endif /* HAVE_PCI_LEGACY */

/*
 * PCI Bus Class Devices
 */
static ssize_t pci_bus_show_cpuaffinity(struct class_device *class_dev,
					char *buf)
{
	int ret;
	cpumask_t cpumask;

	cpumask = pcibus_to_cpumask(to_pci_bus(class_dev));
	ret = cpumask_scnprintf(buf, PAGE_SIZE, cpumask);
	if (ret < PAGE_SIZE)
		buf[ret++] = '\n';
	return ret;
}
CLASS_DEVICE_ATTR(cpuaffinity, S_IRUGO, pci_bus_show_cpuaffinity, NULL);

/*
 * PCI Bus Class
 */
static void release_pcibus_dev(struct class_device *class_dev)
{
	struct pci_bus *pci_bus = to_pci_bus(class_dev);

	if (pci_bus->bridge)
		put_device(pci_bus->bridge);
	kfree(pci_bus);
}

static struct class pcibus_class = {
	.name		= "pci_bus",
	.release	= &release_pcibus_dev,
};

static int __init pcibus_class_init(void)
{
	return class_register(&pcibus_class);
}
postcore_initcall(pcibus_class_init);

/*
 * Translate the low bits of the PCI base
 * to the resource type
 */
static inline unsigned int pci_calc_resource_flags(unsigned int flags)
{
	if (flags & PCI_BASE_ADDRESS_SPACE_IO)
		return IORESOURCE_IO;

	if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
		return IORESOURCE_MEM | IORESOURCE_PREFETCH;

	return IORESOURCE_MEM;
}

/*
 * Find the extent of a PCI decode..
 */
static u32 pci_size(u32 base, u32 maxbase, u32 mask)
{
	u32 size = mask & maxbase;	/* Find the significant bits */
	if (!size)
		return 0;

	/* Get the lowest of them to find the decode size, and
	   from that the extent.  */
	size = (size & ~(size-1)) - 1;

	/* base == maxbase can be valid only if the BAR has
	   already been programmed with all 1s.  */
	if (base == maxbase && ((base | size) & mask) != mask)
		return 0;

	return size;
}

static u64 pci_size64(u64 base, u64 maxbase, u64 mask)
{
	u64 size = mask & maxbase;	/* Find the significant bits */
	if (!size)
		return 0;

	/* Get the lowest of them to find the decode size, and
	   from that the extent.  */
	size = (size & ~(size-1)) - 1;

	/* base == maxbase can be valid only if the BAR has
	   already been programmed with all 1s.  */
	if (base == maxbase && ((base | size) & mask) != mask)
		return 0;

	return size;
}

static inline int is_64bit_memory(u32 mask)
{
	if ((mask & (PCI_BASE_ADDRESS_SPACE|PCI_BASE_ADDRESS_MEM_TYPE_MASK)) ==
	    (PCI_BASE_ADDRESS_SPACE_MEMORY|PCI_BASE_ADDRESS_MEM_TYPE_64))
		return 1;
	return 0;
}

static void pci_read_bases(struct pci_dev *dev, unsigned int howmany, int rom)
{
	unsigned int pos, reg, next;
	u32 l, sz;
	struct resource *res;

	for(pos=0; pos<howmany; pos = next) {
		u64 l64;
		u64 sz64;
		u32 raw_sz;

		next = pos+1;
		res = &dev->resource[pos];
		res->name = pci_name(dev);
		reg = PCI_BASE_ADDRESS_0 + (pos << 2);
		pci_read_config_dword(dev, reg, &l);
		pci_write_config_dword(dev, reg, ~0);
		pci_read_config_dword(dev, reg, &sz);
		pci_write_config_dword(dev, reg, l);
		if (!sz || sz == 0xffffffff)
			continue;
		if (l == 0xffffffff)
			l = 0;
		raw_sz = sz;
		if ((l & PCI_BASE_ADDRESS_SPACE) ==
				PCI_BASE_ADDRESS_SPACE_MEMORY) {
			sz = pci_size(l, sz, (u32)PCI_BASE_ADDRESS_MEM_MASK);
			/*
			 * For 64bit prefetchable memory sz could be 0, if the
			 * real size is bigger than 4G, so we need to check
			 * szhi for that.
			 */
			if (!is_64bit_memory(l) && !sz)
				continue;
			res->start = l & PCI_BASE_ADDRESS_MEM_MASK;
			res->flags |= l & ~PCI_BASE_ADDRESS_MEM_MASK;
		} else {
			sz = pci_size(l, sz, PCI_BASE_ADDRESS_IO_MASK & 0xffff);
			if (!sz)
				continue;
			res->start = l & PCI_BASE_ADDRESS_IO_MASK;
			res->flags |= l & ~PCI_BASE_ADDRESS_IO_MASK;
		}
		res->end = res->start + (unsigned long) sz;
		res->flags |= pci_calc_resource_flags(l);
		if (is_64bit_memory(l)) {
			u32 szhi, lhi;

			pci_read_config_dword(dev, reg+4, &lhi);
			pci_write_config_dword(dev, reg+4, ~0);
			pci_read_config_dword(dev, reg+4, &szhi);
			pci_write_config_dword(dev, reg+4, lhi);
			sz64 = ((u64)szhi << 32) | raw_sz;
			l64 = ((u64)lhi << 32) | l;
			sz64 = pci_size64(l64, sz64, PCI_BASE_ADDRESS_MEM_MASK);
			next++;
#if BITS_PER_LONG == 64
			if (!sz64) {
				res->start = 0;
				res->end = 0;
				res->flags = 0;
				continue;
			}
			res->start = l64 & PCI_BASE_ADDRESS_MEM_MASK;
			res->end = res->start + sz64;
#else
			if (sz64 > 0x100000000ULL) {
				printk(KERN_ERR "PCI: Unable to handle 64-bit "
					"BAR for device %s\n", pci_name(dev));
				res->start = 0;
				res->flags = 0;
			} else if (lhi) {
				/* 64-bit wide address, treat as disabled */
				pci_write_config_dword(dev, reg,
					l & ~(u32)PCI_BASE_ADDRESS_MEM_MASK);
				pci_write_config_dword(dev, reg+4, 0);
				res->start = 0;
				res->end = sz;
			}
#endif
		}
	}
	if (rom) {
		dev->rom_base_reg = rom;
		res = &dev->resource[PCI_ROM_RESOURCE];
		res->name = pci_name(dev);
		pci_read_config_dword(dev, rom, &l);
		pci_write_config_dword(dev, rom, ~PCI_ROM_ADDRESS_ENABLE);
		pci_read_config_dword(dev, rom, &sz);
		pci_write_config_dword(dev, rom, l);
		if (l == 0xffffffff)
			l = 0;
		if (sz && sz != 0xffffffff) {
			sz = pci_size(l, sz, (u32)PCI_ROM_ADDRESS_MASK);
			if (sz) {
				res->flags = (l & IORESOURCE_ROM_ENABLE) |
				  IORESOURCE_MEM | IORESOURCE_PREFETCH |
				  IORESOURCE_READONLY | IORESOURCE_CACHEABLE;
				res->start = l & PCI_ROM_ADDRESS_MASK;
				res->end = res->start + (unsigned long) sz;
			}
		}
	}
}

void __devinit pci_read_bridge_bases(struct pci_bus *child)
{
	struct pci_dev *dev = child->self;
	u8 io_base_lo, io_limit_lo;
	u16 mem_base_lo, mem_limit_lo;
	unsigned long base, limit;
	struct resource *res;
	int i;

	if (!dev)		/* It's a host bus, nothing to read */
		return;

	if (dev->transparent) {
		printk(KERN_INFO "PCI: Transparent bridge - %s\n", pci_name(dev));
		for(i = 3; i < PCI_BUS_NUM_RESOURCES; i++)
			child->resource[i] = child->parent->resource[i - 3];
	}

	for(i=0; i<3; i++)
		child->resource[i] = &dev->resource[PCI_BRIDGE_RESOURCES+i];

	res = child->resource[0];
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

	if (base <= limit) {
		res->flags = (io_base_lo & PCI_IO_RANGE_TYPE_MASK) | IORESOURCE_IO;
		if (!res->start)
			res->start = base;
		if (!res->end)
			res->end = limit + 0xfff;
	}

	res = child->resource[1];
	pci_read_config_word(dev, PCI_MEMORY_BASE, &mem_base_lo);
	pci_read_config_word(dev, PCI_MEMORY_LIMIT, &mem_limit_lo);
	base = (mem_base_lo & PCI_MEMORY_RANGE_MASK) << 16;
	limit = (mem_limit_lo & PCI_MEMORY_RANGE_MASK) << 16;
	if (base <= limit) {
		res->flags = (mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) | IORESOURCE_MEM;
		res->start = base;
		res->end = limit + 0xfffff;
	}

	res = child->resource[2];
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
#if BITS_PER_LONG == 64
			base |= ((long) mem_base_hi) << 32;
			limit |= ((long) mem_limit_hi) << 32;
#else
			if (mem_base_hi || mem_limit_hi) {
				printk(KERN_ERR "PCI: Unable to handle 64-bit address space for bridge %s\n", pci_name(dev));
				return;
			}
#endif
		}
	}
	if (base <= limit) {
		res->flags = (mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) | IORESOURCE_MEM | IORESOURCE_PREFETCH;
		res->start = base;
		res->end = limit + 0xfffff;
	}
}

static struct pci_bus * pci_alloc_bus(void)
{
	struct pci_bus *b;

	b = kzalloc(sizeof(*b), GFP_KERNEL);
	if (b) {
		INIT_LIST_HEAD(&b->node);
		INIT_LIST_HEAD(&b->children);
		INIT_LIST_HEAD(&b->devices);
	}
	return b;
}

static struct pci_bus * __devinit
pci_alloc_child_bus(struct pci_bus *parent, struct pci_dev *bridge, int busnr)
{
	struct pci_bus *child;
	int i;
	int retval;

	/*
	 * Allocate a new bus, and inherit stuff from the parent..
	 */
	child = pci_alloc_bus();
	if (!child)
		return NULL;

	child->self = bridge;
	child->parent = parent;
	child->ops = parent->ops;
	child->sysdata = parent->sysdata;
	child->bus_flags = parent->bus_flags;
	child->bridge = get_device(&bridge->dev);

	child->class_dev.class = &pcibus_class;
	sprintf(child->class_dev.class_id, "%04x:%02x", pci_domain_nr(child), busnr);
	retval = class_device_register(&child->class_dev);
	if (retval)
		goto error_register;
	retval = class_device_create_file(&child->class_dev,
					  &class_device_attr_cpuaffinity);
	if (retval)
		goto error_file_create;

	/*
	 * Set up the primary, secondary and subordinate
	 * bus numbers.
	 */
	child->number = child->secondary = busnr;
	child->primary = parent->secondary;
	child->subordinate = 0xff;

	/* Set up default resource pointers and names.. */
	for (i = 0; i < 4; i++) {
		child->resource[i] = &bridge->resource[PCI_BRIDGE_RESOURCES+i];
		child->resource[i]->name = child->name;
	}
	bridge->subordinate = child;

	return child;

error_file_create:
	class_device_unregister(&child->class_dev);
error_register:
	kfree(child);
	return NULL;
}

struct pci_bus *pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev, int busnr)
{
	struct pci_bus *child;

	child = pci_alloc_child_bus(parent, dev, busnr);
	if (child) {
		down_write(&pci_bus_sem);
		list_add_tail(&child->node, &parent->children);
		up_write(&pci_bus_sem);
	}
	return child;
}

static void pci_enable_crs(struct pci_dev *dev)
{
	u16 cap, rpctl;
	int rpcap = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!rpcap)
		return;

	pci_read_config_word(dev, rpcap + PCI_CAP_FLAGS, &cap);
	if (((cap & PCI_EXP_FLAGS_TYPE) >> 4) != PCI_EXP_TYPE_ROOT_PORT)
		return;

	pci_read_config_word(dev, rpcap + PCI_EXP_RTCTL, &rpctl);
	rpctl |= PCI_EXP_RTCTL_CRSSVE;
	pci_write_config_word(dev, rpcap + PCI_EXP_RTCTL, rpctl);
}

static void pci_fixup_parent_subordinate_busnr(struct pci_bus *child, int max)
{
	struct pci_bus *parent = child->parent;

	/* Attempts to fix that up are really dangerous unless
	   we're going to re-assign all bus numbers. */
	if (!pcibios_assign_all_busses())
		return;

	while (parent->parent && parent->subordinate < max) {
		parent->subordinate = max;
		pci_write_config_byte(parent->self, PCI_SUBORDINATE_BUS, max);
		parent = parent->parent;
	}
}

unsigned int pci_scan_child_bus(struct pci_bus *bus);

/*
 * If it's a bridge, configure it and scan the bus behind it.
 * For CardBus bridges, we don't scan behind as the devices will
 * be handled by the bridge driver itself.
 *
 * We need to process bridges in two passes -- first we scan those
 * already configured by the BIOS and after we are done with all of
 * them, we proceed to assigning numbers to the remaining buses in
 * order to avoid overlaps between old and new bus numbers.
 */
int pci_scan_bridge(struct pci_bus *bus, struct pci_dev * dev, int max, int pass)
{
	struct pci_bus *child;
	int is_cardbus = (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS);
	u32 buses, i, j = 0;
	u16 bctl;

	pci_read_config_dword(dev, PCI_PRIMARY_BUS, &buses);

	pr_debug("PCI: Scanning behind PCI bridge %s, config %06x, pass %d\n",
		 pci_name(dev), buses & 0xffffff, pass);

	/* Disable MasterAbortMode during probing to avoid reporting
	   of bus errors (in some architectures) */ 
	pci_read_config_word(dev, PCI_BRIDGE_CONTROL, &bctl);
	pci_write_config_word(dev, PCI_BRIDGE_CONTROL,
			      bctl & ~PCI_BRIDGE_CTL_MASTER_ABORT);

	pci_enable_crs(dev);

	if ((buses & 0xffff00) && !pcibios_assign_all_busses() && !is_cardbus) {
		unsigned int cmax, busnr;
		/*
		 * Bus already configured by firmware, process it in the first
		 * pass and just note the configuration.
		 */
		if (pass)
			goto out;
		busnr = (buses >> 8) & 0xFF;

		/*
		 * If we already got to this bus through a different bridge,
		 * ignore it.  This can happen with the i450NX chipset.
		 */
		if (pci_find_bus(pci_domain_nr(bus), busnr)) {
			printk(KERN_INFO "PCI: Bus %04x:%02x already known\n",
					pci_domain_nr(bus), busnr);
			goto out;
		}

		child = pci_add_new_bus(bus, dev, busnr);
		if (!child)
			goto out;
		child->primary = buses & 0xFF;
		child->subordinate = (buses >> 16) & 0xFF;
		child->bridge_ctl = bctl;

		cmax = pci_scan_child_bus(child);
		if (cmax > max)
			max = cmax;
		if (child->subordinate > max)
			max = child->subordinate;
	} else {
		/*
		 * We need to assign a number to this bus which we always
		 * do in the second pass.
		 */
		if (!pass) {
			if (pcibios_assign_all_busses())
				/* Temporarily disable forwarding of the
				   configuration cycles on all bridges in
				   this bus segment to avoid possible
				   conflicts in the second pass between two
				   bridges programmed with overlapping
				   bus ranges. */
				pci_write_config_dword(dev, PCI_PRIMARY_BUS,
						       buses & ~0xffffff);
			goto out;
		}

		/* Clear errors */
		pci_write_config_word(dev, PCI_STATUS, 0xffff);

		/* Prevent assigning a bus number that already exists.
		 * This can happen when a bridge is hot-plugged */
		if (pci_find_bus(pci_domain_nr(bus), max+1))
			goto out;
		child = pci_add_new_bus(bus, dev, ++max);
		buses = (buses & 0xff000000)
		      | ((unsigned int)(child->primary)     <<  0)
		      | ((unsigned int)(child->secondary)   <<  8)
		      | ((unsigned int)(child->subordinate) << 16);

		/*
		 * yenta.c forces a secondary latency timer of 176.
		 * Copy that behaviour here.
		 */
		if (is_cardbus) {
			buses &= ~0xff000000;
			buses |= CARDBUS_LATENCY_TIMER << 24;
		}
			
		/*
		 * We need to blast all three values with a single write.
		 */
		pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);

		if (!is_cardbus) {
			child->bridge_ctl = bctl | PCI_BRIDGE_CTL_NO_ISA;
			/*
			 * Adjust subordinate busnr in parent buses.
			 * We do this before scanning for children because
			 * some devices may not be detected if the bios
			 * was lazy.
			 */
			pci_fixup_parent_subordinate_busnr(child, max);
			/* Now we can scan all subordinate buses... */
			max = pci_scan_child_bus(child);
			/*
			 * now fix it up again since we have found
			 * the real value of max.
			 */
			pci_fixup_parent_subordinate_busnr(child, max);
		} else {
			/*
			 * For CardBus bridges, we leave 4 bus numbers
			 * as cards with a PCI-to-PCI bridge can be
			 * inserted later.
			 */
			for (i=0; i<CARDBUS_RESERVE_BUSNR; i++) {
				struct pci_bus *parent = bus;
				if (pci_find_bus(pci_domain_nr(bus),
							max+i+1))
					break;
				while (parent->parent) {
					if ((!pcibios_assign_all_busses()) &&
					    (parent->subordinate > max) &&
					    (parent->subordinate <= max+i)) {
						j = 1;
					}
					parent = parent->parent;
				}
				if (j) {
					/*
					 * Often, there are two cardbus bridges
					 * -- try to leave one valid bus number
					 * for each one.
					 */
					i /= 2;
					break;
				}
			}
			max += i;
			pci_fixup_parent_subordinate_busnr(child, max);
		}
		/*
		 * Set the subordinate bus number to its real value.
		 */
		child->subordinate = max;
		pci_write_config_byte(dev, PCI_SUBORDINATE_BUS, max);
	}

	sprintf(child->name, (is_cardbus ? "PCI CardBus #%02x" : "PCI Bus #%02x"), child->number);

	while (bus->parent) {
		if ((child->subordinate > bus->subordinate) ||
		    (child->number > bus->subordinate) ||
		    (child->number < bus->number) ||
		    (child->subordinate < bus->number)) {
			printk(KERN_WARNING "PCI: Bus #%02x (-#%02x) is "
			       "hidden behind%s bridge #%02x (-#%02x)%s\n",
			       child->number, child->subordinate,
			       bus->self->transparent ? " transparent" : " ",
			       bus->number, bus->subordinate,
			       pcibios_assign_all_busses() ? " " :
			       " (try 'pci=assign-busses')");
			printk(KERN_WARNING "Please report the result to "
			       "linux-kernel to fix this permanently\n");
		}
		bus = bus->parent;
	}

out:
	pci_write_config_word(dev, PCI_BRIDGE_CONTROL, bctl);

	return max;
}

/*
 * Read interrupt line and base address registers.
 * The architecture-dependent code can tweak these, of course.
 */
static void pci_read_irq(struct pci_dev *dev)
{
	unsigned char irq;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq);
	dev->pin = irq;
	if (irq)
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	dev->irq = irq;
}

#define LEGACY_IO_RESOURCE	(IORESOURCE_IO | IORESOURCE_PCI_FIXED)

/**
 * pci_setup_device - fill in class and map information of a device
 * @dev: the device structure to fill
 *
 * Initialize the device structure with information about the device's 
 * vendor,class,memory and IO-space addresses,IRQ lines etc.
 * Called at initialisation of the PCI subsystem and by CardBus services.
 * Returns 0 on success and -1 if unknown type of device (not normal, bridge
 * or CardBus).
 */
static int pci_setup_device(struct pci_dev * dev)
{
	u32 class;

	sprintf(pci_name(dev), "%04x:%02x:%02x.%d", pci_domain_nr(dev->bus),
		dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class);
	class >>= 8;				    /* upper 3 bytes */
	dev->class = class;
	class >>= 8;

	pr_debug("PCI: Found %s [%04x/%04x] %06x %02x\n", pci_name(dev),
		 dev->vendor, dev->device, class, dev->hdr_type);

	/* "Unknown power state" */
	dev->current_state = PCI_UNKNOWN;

	/* Early fixups, before probing the BARs */
	pci_fixup_device(pci_fixup_early, dev);
	class = dev->class >> 8;

	switch (dev->hdr_type) {		    /* header type */
	case PCI_HEADER_TYPE_NORMAL:		    /* standard header */
		if (class == PCI_CLASS_BRIDGE_PCI)
			goto bad;
		pci_read_irq(dev);
		pci_read_bases(dev, 6, PCI_ROM_ADDRESS);
		pci_read_config_word(dev, PCI_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
		pci_read_config_word(dev, PCI_SUBSYSTEM_ID, &dev->subsystem_device);

		/*
		 *	Do the ugly legacy mode stuff here rather than broken chip
		 *	quirk code. Legacy mode ATA controllers have fixed
		 *	addresses. These are not always echoed in BAR0-3, and
		 *	BAR0-3 in a few cases contain junk!
		 */
		if (class == PCI_CLASS_STORAGE_IDE) {
			u8 progif;
			pci_read_config_byte(dev, PCI_CLASS_PROG, &progif);
			if ((progif & 1) == 0) {
				dev->resource[0].start = 0x1F0;
				dev->resource[0].end = 0x1F7;
				dev->resource[0].flags = LEGACY_IO_RESOURCE;
				dev->resource[1].start = 0x3F6;
				dev->resource[1].end = 0x3F6;
				dev->resource[1].flags = LEGACY_IO_RESOURCE;
			}
			if ((progif & 4) == 0) {
				dev->resource[2].start = 0x170;
				dev->resource[2].end = 0x177;
				dev->resource[2].flags = LEGACY_IO_RESOURCE;
				dev->resource[3].start = 0x376;
				dev->resource[3].end = 0x376;
				dev->resource[3].flags = LEGACY_IO_RESOURCE;
			}
		}
		break;

	case PCI_HEADER_TYPE_BRIDGE:		    /* bridge header */
		if (class != PCI_CLASS_BRIDGE_PCI)
			goto bad;
		/* The PCI-to-PCI bridge spec requires that subtractive
		   decoding (i.e. transparent) bridge must have programming
		   interface code of 0x01. */ 
		pci_read_irq(dev);
		dev->transparent = ((dev->class & 0xff) == 1);
		pci_read_bases(dev, 2, PCI_ROM_ADDRESS1);
		break;

	case PCI_HEADER_TYPE_CARDBUS:		    /* CardBus bridge header */
		if (class != PCI_CLASS_BRIDGE_CARDBUS)
			goto bad;
		pci_read_irq(dev);
		pci_read_bases(dev, 1, 0);
		pci_read_config_word(dev, PCI_CB_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
		pci_read_config_word(dev, PCI_CB_SUBSYSTEM_ID, &dev->subsystem_device);
		break;

	default:				    /* unknown header */
		printk(KERN_ERR "PCI: device %s has unknown header type %02x, ignoring.\n",
			pci_name(dev), dev->hdr_type);
		return -1;

	bad:
		printk(KERN_ERR "PCI: %s: class %x doesn't match header type %02x. Ignoring class.\n",
		       pci_name(dev), class, dev->hdr_type);
		dev->class = PCI_CLASS_NOT_DEFINED;
	}

	/* We found a fine healthy device, go go go... */
	return 0;
}

/**
 * pci_release_dev - free a pci device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this pci device are
 * done.
 */
static void pci_release_dev(struct device *dev)
{
	struct pci_dev *pci_dev;

	pci_dev = to_pci_dev(dev);
	kfree(pci_dev);
}

/**
 * pci_cfg_space_size - get the configuration space size of the PCI device.
 * @dev: PCI device
 *
 * Regular PCI devices have 256 bytes, but PCI-X 2 and PCI Express devices
 * have 4096 bytes.  Even if the device is capable, that doesn't mean we can
 * access it.  Maybe we don't have a way to generate extended config space
 * accesses, or the device is behind a reverse Express bridge.  So we try
 * reading the dword at 0x100 which must either be 0 or a valid extended
 * capability header.
 */
int pci_cfg_space_size(struct pci_dev *dev)
{
	int pos;
	u32 status;

	pos = pci_find_capability(dev, PCI_CAP_ID_EXP);
	if (!pos) {
		pos = pci_find_capability(dev, PCI_CAP_ID_PCIX);
		if (!pos)
			goto fail;

		pci_read_config_dword(dev, pos + PCI_X_STATUS, &status);
		if (!(status & (PCI_X_STATUS_266MHZ | PCI_X_STATUS_533MHZ)))
			goto fail;
	}

	if (pci_read_config_dword(dev, 256, &status) != PCIBIOS_SUCCESSFUL)
		goto fail;
	if (status == 0xffffffff)
		goto fail;

	return PCI_CFG_SPACE_EXP_SIZE;

 fail:
	return PCI_CFG_SPACE_SIZE;
}

static void pci_release_bus_bridge_dev(struct device *dev)
{
	kfree(dev);
}

struct pci_dev *alloc_pci_dev(void)
{
	struct pci_dev *dev;

	dev = kzalloc(sizeof(struct pci_dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	INIT_LIST_HEAD(&dev->global_list);
	INIT_LIST_HEAD(&dev->bus_list);

	pci_msi_init_pci_dev(dev);

	return dev;
}
EXPORT_SYMBOL(alloc_pci_dev);

/*
 * Read the config data for a PCI device, sanity-check it
 * and fill in the dev structure...
 */
static struct pci_dev * __devinit
pci_scan_device(struct pci_bus *bus, int devfn)
{
	struct pci_dev *dev;
	u32 l;
	u8 hdr_type;
	int delay = 1;

	if (pci_bus_read_config_dword(bus, devfn, PCI_VENDOR_ID, &l))
		return NULL;

	/* some broken boards return 0 or ~0 if a slot is empty: */
	if (l == 0xffffffff || l == 0x00000000 ||
	    l == 0x0000ffff || l == 0xffff0000)
		return NULL;

	/* Configuration request Retry Status */
	while (l == 0xffff0001) {
		msleep(delay);
		delay *= 2;
		if (pci_bus_read_config_dword(bus, devfn, PCI_VENDOR_ID, &l))
			return NULL;
		/* Card hasn't responded in 60 seconds?  Must be stuck. */
		if (delay > 60 * 1000) {
			printk(KERN_WARNING "Device %04x:%02x:%02x.%d not "
					"responding\n", pci_domain_nr(bus),
					bus->number, PCI_SLOT(devfn),
					PCI_FUNC(devfn));
			return NULL;
		}
	}

	if (pci_bus_read_config_byte(bus, devfn, PCI_HEADER_TYPE, &hdr_type))
		return NULL;

	dev = alloc_pci_dev();
	if (!dev)
		return NULL;

	dev->bus = bus;
	dev->sysdata = bus->sysdata;
	dev->dev.parent = bus->bridge;
	dev->dev.bus = &pci_bus_type;
	dev->devfn = devfn;
	dev->hdr_type = hdr_type & 0x7f;
	dev->multifunction = !!(hdr_type & 0x80);
	dev->vendor = l & 0xffff;
	dev->device = (l >> 16) & 0xffff;
	dev->cfg_size = pci_cfg_space_size(dev);
	dev->error_state = pci_channel_io_normal;

	/* Assume 32-bit PCI; let 64-bit PCI cards (which are far rarer)
	   set this higher, assuming the system even supports it.  */
	dev->dma_mask = 0xffffffff;
	if (pci_setup_device(dev) < 0) {
		kfree(dev);
		return NULL;
	}

	return dev;
}

void pci_device_add(struct pci_dev *dev, struct pci_bus *bus)
{
	device_initialize(&dev->dev);
	dev->dev.release = pci_release_dev;
	pci_dev_get(dev);

	set_dev_node(&dev->dev, pcibus_to_node(bus));
	dev->dev.dma_mask = &dev->dma_mask;
	dev->dev.coherent_dma_mask = 0xffffffffull;

	/* Fix up broken headers */
	pci_fixup_device(pci_fixup_header, dev);

	/*
	 * Add the device to our list of discovered devices
	 * and the bus list for fixup functions, etc.
	 */
	INIT_LIST_HEAD(&dev->global_list);
	down_write(&pci_bus_sem);
	list_add_tail(&dev->bus_list, &bus->devices);
	up_write(&pci_bus_sem);
}

struct pci_dev *pci_scan_single_device(struct pci_bus *bus, int devfn)
{
	struct pci_dev *dev;

	dev = pci_scan_device(bus, devfn);
	if (!dev)
		return NULL;

	pci_device_add(dev, bus);

	return dev;
}

/**
 * pci_scan_slot - scan a PCI slot on a bus for devices.
 * @bus: PCI bus to scan
 * @devfn: slot number to scan (must have zero function.)
 *
 * Scan a PCI slot on the specified PCI bus for devices, adding
 * discovered devices to the @bus->devices list.  New devices
 * will have an empty dev->global_list head.
 */
int pci_scan_slot(struct pci_bus *bus, int devfn)
{
	int func, nr = 0;
	int scan_all_fns;

	scan_all_fns = pcibios_scan_all_fns(bus, devfn);

	for (func = 0; func < 8; func++, devfn++) {
		struct pci_dev *dev;

		dev = pci_scan_single_device(bus, devfn);
		if (dev) {
			nr++;

			/*
		 	 * If this is a single function device,
		 	 * don't scan past the first function.
		 	 */
			if (!dev->multifunction) {
				if (func > 0) {
					dev->multifunction = 1;
				} else {
 					break;
				}
			}
		} else {
			if (func == 0 && !scan_all_fns)
				break;
		}
	}
	return nr;
}

unsigned int pci_scan_child_bus(struct pci_bus *bus)
{
	unsigned int devfn, pass, max = bus->secondary;
	struct pci_dev *dev;

	pr_debug("PCI: Scanning bus %04x:%02x\n", pci_domain_nr(bus), bus->number);

	/* Go find them, Rover! */
	for (devfn = 0; devfn < 0x100; devfn += 8)
		pci_scan_slot(bus, devfn);

	/*
	 * After performing arch-dependent fixup of the bus, look behind
	 * all PCI-to-PCI bridges on this bus.
	 */
	pr_debug("PCI: Fixups for bus %04x:%02x\n", pci_domain_nr(bus), bus->number);
	pcibios_fixup_bus(bus);
	for (pass=0; pass < 2; pass++)
		list_for_each_entry(dev, &bus->devices, bus_list) {
			if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE ||
			    dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
				max = pci_scan_bridge(bus, dev, max, pass);
		}

	/*
	 * We've scanned the bus and so we know all about what's on
	 * the other side of any bridges that may be on this bus plus
	 * any devices.
	 *
	 * Return how far we've got finding sub-buses.
	 */
	pr_debug("PCI: Bus scan for %04x:%02x returning with max=%02x\n",
		pci_domain_nr(bus), bus->number, max);
	return max;
}

unsigned int __devinit pci_do_scan_bus(struct pci_bus *bus)
{
	unsigned int max;

	max = pci_scan_child_bus(bus);

	/*
	 * Make the discovered devices available.
	 */
	pci_bus_add_devices(bus);

	return max;
}

struct pci_bus * pci_create_bus(struct device *parent,
		int bus, struct pci_ops *ops, void *sysdata)
{
	int error;
	struct pci_bus *b;
	struct device *dev;

	b = pci_alloc_bus();
	if (!b)
		return NULL;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev){
		kfree(b);
		return NULL;
	}

	b->sysdata = sysdata;
	b->ops = ops;

	if (pci_find_bus(pci_domain_nr(b), bus)) {
		/* If we already got to this bus through a different bridge, ignore it */
		pr_debug("PCI: Bus %04x:%02x already known\n", pci_domain_nr(b), bus);
		goto err_out;
	}

	down_write(&pci_bus_sem);
	list_add_tail(&b->node, &pci_root_buses);
	up_write(&pci_bus_sem);

	memset(dev, 0, sizeof(*dev));
	dev->parent = parent;
	dev->release = pci_release_bus_bridge_dev;
	sprintf(dev->bus_id, "pci%04x:%02x", pci_domain_nr(b), bus);
	error = device_register(dev);
	if (error)
		goto dev_reg_err;
	b->bridge = get_device(dev);

	b->class_dev.class = &pcibus_class;
	sprintf(b->class_dev.class_id, "%04x:%02x", pci_domain_nr(b), bus);
	error = class_device_register(&b->class_dev);
	if (error)
		goto class_dev_reg_err;
	error = class_device_create_file(&b->class_dev, &class_device_attr_cpuaffinity);
	if (error)
		goto class_dev_create_file_err;

	/* Create legacy_io and legacy_mem files for this bus */
	pci_create_legacy_files(b);

	error = sysfs_create_link(&b->class_dev.kobj, &b->bridge->kobj, "bridge");
	if (error)
		goto sys_create_link_err;

	b->number = b->secondary = bus;
	b->resource[0] = &ioport_resource;
	b->resource[1] = &iomem_resource;

	return b;

sys_create_link_err:
	class_device_remove_file(&b->class_dev, &class_device_attr_cpuaffinity);
class_dev_create_file_err:
	class_device_unregister(&b->class_dev);
class_dev_reg_err:
	device_unregister(dev);
dev_reg_err:
	down_write(&pci_bus_sem);
	list_del(&b->node);
	up_write(&pci_bus_sem);
err_out:
	kfree(dev);
	kfree(b);
	return NULL;
}
EXPORT_SYMBOL_GPL(pci_create_bus);

struct pci_bus *pci_scan_bus_parented(struct device *parent,
		int bus, struct pci_ops *ops, void *sysdata)
{
	struct pci_bus *b;

	b = pci_create_bus(parent, bus, ops, sysdata);
	if (b)
		b->subordinate = pci_scan_child_bus(b);
	return b;
}
EXPORT_SYMBOL(pci_scan_bus_parented);

#ifdef CONFIG_HOTPLUG
EXPORT_SYMBOL(pci_add_new_bus);
EXPORT_SYMBOL(pci_do_scan_bus);
EXPORT_SYMBOL(pci_scan_slot);
EXPORT_SYMBOL(pci_scan_bridge);
EXPORT_SYMBOL(pci_scan_single_device);
EXPORT_SYMBOL_GPL(pci_scan_child_bus);
#endif

static int __init pci_sort_bf_cmp(const struct pci_dev *a, const struct pci_dev *b)
{
	if      (pci_domain_nr(a->bus) < pci_domain_nr(b->bus)) return -1;
	else if (pci_domain_nr(a->bus) > pci_domain_nr(b->bus)) return  1;

	if      (a->bus->number < b->bus->number) return -1;
	else if (a->bus->number > b->bus->number) return  1;

	if      (a->devfn < b->devfn) return -1;
	else if (a->devfn > b->devfn) return  1;

	return 0;
}

/*
 * Yes, this forcably breaks the klist abstraction temporarily.  It
 * just wants to sort the klist, not change reference counts and
 * take/drop locks rapidly in the process.  It does all this while
 * holding the lock for the list, so objects can't otherwise be
 * added/removed while we're swizzling.
 */
static void __init pci_insertion_sort_klist(struct pci_dev *a, struct list_head *list)
{
	struct list_head *pos;
	struct klist_node *n;
	struct device *dev;
	struct pci_dev *b;

	list_for_each(pos, list) {
		n = container_of(pos, struct klist_node, n_node);
		dev = container_of(n, struct device, knode_bus);
		b = to_pci_dev(dev);
		if (pci_sort_bf_cmp(a, b) <= 0) {
			list_move_tail(&a->dev.knode_bus.n_node, &b->dev.knode_bus.n_node);
			return;
		}
	}
	list_move_tail(&a->dev.knode_bus.n_node, list);
}

static void __init pci_sort_breadthfirst_klist(void)
{
	LIST_HEAD(sorted_devices);
	struct list_head *pos, *tmp;
	struct klist_node *n;
	struct device *dev;
	struct pci_dev *pdev;

	spin_lock(&pci_bus_type.klist_devices.k_lock);
	list_for_each_safe(pos, tmp, &pci_bus_type.klist_devices.k_list) {
		n = container_of(pos, struct klist_node, n_node);
		dev = container_of(n, struct device, knode_bus);
		pdev = to_pci_dev(dev);
		pci_insertion_sort_klist(pdev, &sorted_devices);
	}
	list_splice(&sorted_devices, &pci_bus_type.klist_devices.k_list);
	spin_unlock(&pci_bus_type.klist_devices.k_lock);
}

static void __init pci_insertion_sort_devices(struct pci_dev *a, struct list_head *list)
{
	struct pci_dev *b;

	list_for_each_entry(b, list, global_list) {
		if (pci_sort_bf_cmp(a, b) <= 0) {
			list_move_tail(&a->global_list, &b->global_list);
			return;
		}
	}
	list_move_tail(&a->global_list, list);
}

static void __init pci_sort_breadthfirst_devices(void)
{
	LIST_HEAD(sorted_devices);
	struct pci_dev *dev, *tmp;

	down_write(&pci_bus_sem);
	list_for_each_entry_safe(dev, tmp, &pci_devices, global_list) {
		pci_insertion_sort_devices(dev, &sorted_devices);
	}
	list_splice(&sorted_devices, &pci_devices);
	up_write(&pci_bus_sem);
}

void __init pci_sort_breadthfirst(void)
{
	pci_sort_breadthfirst_devices();
	pci_sort_breadthfirst_klist();
}

