/*
 *	linux/arch/alpha/kernel/pci.c
 *
 * Extruded from code written by
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 */

/* 2.3.x PCI/resources, 1999 Andrea Arcangeli <andrea@suse.de> */

/*
 * Nov 2000, Ivan Kokshaysky <ink@jurassic.park.msu.ru>
 *	     PCI-PCI bridges cleanup
 */
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/cache.h>
#include <linux/slab.h>
#include <asm/machvec.h>

#include "proto.h"
#include "pci_impl.h"


/*
 * Some string constants used by the various core logics. 
 */

const char *const pci_io_names[] = {
  "PCI IO bus 0", "PCI IO bus 1", "PCI IO bus 2", "PCI IO bus 3",
  "PCI IO bus 4", "PCI IO bus 5", "PCI IO bus 6", "PCI IO bus 7"
};

const char *const pci_mem_names[] = {
  "PCI mem bus 0", "PCI mem bus 1", "PCI mem bus 2", "PCI mem bus 3",
  "PCI mem bus 4", "PCI mem bus 5", "PCI mem bus 6", "PCI mem bus 7"
};

const char pci_hae0_name[] = "HAE0";

/*
 * If PCI_PROBE_ONLY in pci_flags is set, we don't change any PCI resource
 * assignments.
 */

/*
 * The PCI controller list.
 */

struct pci_controller *hose_head, **hose_tail = &hose_head;
struct pci_controller *pci_isa_hose;

/*
 * Quirks.
 */

static void quirk_isa_bridge(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_ISA << 8;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82378, quirk_isa_bridge);

static void quirk_cypress(struct pci_dev *dev)
{
	/* The Notorious Cy82C693 chip.  */

	/* The generic legacy mode IDE fixup in drivers/pci/probe.c
	   doesn't work correctly with the Cypress IDE controller as
	   it has non-standard register layout.  Fix that.  */
	if (dev->class >> 8 == PCI_CLASS_STORAGE_IDE) {
		dev->resource[2].start = dev->resource[3].start = 0;
		dev->resource[2].end = dev->resource[3].end = 0;
		dev->resource[2].flags = dev->resource[3].flags = 0;
		if (PCI_FUNC(dev->devfn) == 2) {
			dev->resource[0].start = 0x170;
			dev->resource[0].end = 0x177;
			dev->resource[1].start = 0x376;
			dev->resource[1].end = 0x376;
		}
	}

	/* The Cypress bridge responds on the PCI bus in the address range
	   0xffff0000-0xffffffff (conventional x86 BIOS ROM).  There is no
	   way to turn this off.  The bridge also supports several extended
	   BIOS ranges (disabled after power-up), and some consoles do turn
	   them on.  So if we use a large direct-map window, or a large SG
	   window, we must avoid the entire 0xfff00000-0xffffffff region.  */
	if (dev->class >> 8 == PCI_CLASS_BRIDGE_ISA) {
		if (__direct_map_base + __direct_map_size >= 0xfff00000UL)
			__direct_map_size = 0xfff00000UL - __direct_map_base;
		else {
			struct pci_controller *hose = dev->sysdata;
			struct pci_iommu_arena *pci = hose->sg_pci;
			if (pci && pci->dma_base + pci->size >= 0xfff00000UL)
				pci->size = 0xfff00000UL - pci->dma_base;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_CONTAQ, PCI_DEVICE_ID_CONTAQ_82C693, quirk_cypress);

/* Called for each device after PCI setup is done. */
static void pcibios_fixup_final(struct pci_dev *dev)
{
	unsigned int class = dev->class >> 8;

	if (class == PCI_CLASS_BRIDGE_ISA || class == PCI_CLASS_BRIDGE_EISA) {
		dev->dma_mask = MAX_ISA_DMA_ADDRESS - 1;
		isa_bridge = dev;
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, pcibios_fixup_final);

/* Just declaring that the power-of-ten prefixes are actually the
   power-of-two ones doesn't make it true :) */
#define KB			1024
#define MB			(1024*KB)
#define GB			(1024*MB)

resource_size_t
pcibios_align_resource(void *data, const struct resource *res,
		       resource_size_t size, resource_size_t align)
{
	struct pci_dev *dev = data;
	struct pci_controller *hose = dev->sysdata;
	unsigned long alignto;
	resource_size_t start = res->start;

	if (res->flags & IORESOURCE_IO) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->io_space->start < PCIBIOS_MIN_IO)
			start = PCIBIOS_MIN_IO + hose->io_space->start;

		/*
		 * Put everything into 0x00-0xff region modulo 0x400
		 */
		if (start & 0x300)
			start = (start + 0x3ff) & ~0x3ff;
	}
	else if	(res->flags & IORESOURCE_MEM) {
		/* Make sure we start at our min on all hoses */
		if (start - hose->mem_space->start < PCIBIOS_MIN_MEM)
			start = PCIBIOS_MIN_MEM + hose->mem_space->start;

		/*
		 * The following holds at least for the Low Cost
		 * Alpha implementation of the PCI interface:
		 *
		 * In sparse memory address space, the first
		 * octant (16MB) of every 128MB segment is
		 * aliased to the very first 16 MB of the
		 * address space (i.e., it aliases the ISA
		 * memory address space).  Thus, we try to
		 * avoid allocating PCI devices in that range.
		 * Can be allocated in 2nd-7th octant only.
		 * Devices that need more than 112MB of
		 * address space must be accessed through
		 * dense memory space only!
		 */

		/* Align to multiple of size of minimum base.  */
		alignto = max_t(resource_size_t, 0x1000, align);
		start = ALIGN(start, alignto);
		if (hose->sparse_mem_base && size <= 7 * 16*MB) {
			if (((start / (16*MB)) & 0x7) == 0) {
				start &= ~(128*MB - 1);
				start += 16*MB;
				start  = ALIGN(start, alignto);
			}
			if (start/(128*MB) != (start + size - 1)/(128*MB)) {
				start &= ~(128*MB - 1);
				start += (128 + 16)*MB;
				start  = ALIGN(start, alignto);
			}
		}
	}

	return start;
}
#undef KB
#undef MB
#undef GB

static int __init
pcibios_init(void)
{
	if (alpha_mv.init_pci)
		alpha_mv.init_pci();
	return 0;
}

subsys_initcall(pcibios_init);

#ifdef ALPHA_RESTORE_SRM_SETUP
/* Store PCI device configuration left by SRM here. */
struct pdev_srm_saved_conf
{
	struct pdev_srm_saved_conf *next;
	struct pci_dev *dev;
};

static struct pdev_srm_saved_conf *srm_saved_configs;

static void pdev_save_srm_config(struct pci_dev *dev)
{
	struct pdev_srm_saved_conf *tmp;
	static int printed = 0;

	if (!alpha_using_srm || pci_has_flag(PCI_PROBE_ONLY))
		return;

	if (!printed) {
		printk(KERN_INFO "pci: enabling save/restore of SRM state\n");
		printed = 1;
	}

	tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp) {
		printk(KERN_ERR "%s: kmalloc() failed!\n", __func__);
		return;
	}
	tmp->next = srm_saved_configs;
	tmp->dev = dev;

	pci_save_state(dev);

	srm_saved_configs = tmp;
}

void
pci_restore_srm_config(void)
{
	struct pdev_srm_saved_conf *tmp;

	/* No need to restore if probed only. */
	if (pci_has_flag(PCI_PROBE_ONLY))
		return;

	/* Restore SRM config. */
	for (tmp = srm_saved_configs; tmp; tmp = tmp->next) {
		pci_restore_state(tmp->dev);
	}
}
#else
#define pdev_save_srm_config(dev)	do {} while (0)
#endif

void pcibios_fixup_bus(struct pci_bus *bus)
{
	struct pci_dev *dev = bus->self;

	if (pci_has_flag(PCI_PROBE_ONLY) && dev &&
	    (dev->class >> 8) == PCI_CLASS_BRIDGE_PCI) {
		pci_read_bridge_bases(bus);
	}

	list_for_each_entry(dev, &bus->devices, bus_list) {
		pdev_save_srm_config(dev);
	}
}

/*
 *  If we set up a device for bus mastering, we need to check the latency
 *  timer as certain firmware forgets to set it properly, as seen
 *  on SX164 and LX164 with SRM.
 */
void
pcibios_set_master(struct pci_dev *dev)
{
	u8 lat;
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat >= 16) return;
	printk("PCI: Setting latency timer of device %s to 64\n",
							pci_name(dev));
	pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
}

void __init
pcibios_claim_one_bus(struct pci_bus *b)
{
	struct pci_dev *dev;
	struct pci_bus *child_bus;

	list_for_each_entry(dev, &b->devices, bus_list) {
		int i;

		for (i = 0; i < PCI_NUM_RESOURCES; i++) {
			struct resource *r = &dev->resource[i];

			if (r->parent || !r->start || !r->flags)
				continue;
			if (pci_has_flag(PCI_PROBE_ONLY) ||
			    (r->flags & IORESOURCE_PCI_FIXED)) {
				if (pci_claim_resource(dev, i) == 0)
					continue;

				pci_claim_bridge_resource(dev, i);
			}
		}
	}

	list_for_each_entry(child_bus, &b->children, node)
		pcibios_claim_one_bus(child_bus);
}

static void __init
pcibios_claim_console_setup(void)
{
	struct pci_bus *b;

	list_for_each_entry(b, &pci_root_buses, node)
		pcibios_claim_one_bus(b);
}

void __init
common_init_pci(void)
{
	struct pci_controller *hose;
	struct list_head resources;
	struct pci_host_bridge *bridge;
	struct pci_bus *bus;
	int ret, next_busno;
	int need_domain_info = 0;
	u32 pci_mem_end;
	u32 sg_base;
	unsigned long end;

	/* Scan all of the recorded PCI controllers.  */
	for (next_busno = 0, hose = hose_head; hose; hose = hose->next) {
		sg_base = hose->sg_pci ? hose->sg_pci->dma_base : ~0;

		/* Adjust hose mem_space limit to prevent PCI allocations
		   in the iommu windows. */
		pci_mem_end = min((u32)__direct_map_base, sg_base) - 1;
		end = hose->mem_space->start + pci_mem_end;
		if (hose->mem_space->end > end)
			hose->mem_space->end = end;

		INIT_LIST_HEAD(&resources);
		pci_add_resource_offset(&resources, hose->io_space,
					hose->io_space->start);
		pci_add_resource_offset(&resources, hose->mem_space,
					hose->mem_space->start);

		bridge = pci_alloc_host_bridge(0);
		if (!bridge)
			continue;

		list_splice_init(&resources, &bridge->windows);
		bridge->dev.parent = NULL;
		bridge->sysdata = hose;
		bridge->busnr = next_busno;
		bridge->ops = alpha_mv.pci_ops;
		bridge->swizzle_irq = alpha_mv.pci_swizzle;
		bridge->map_irq = alpha_mv.pci_map_irq;

		ret = pci_scan_root_bus_bridge(bridge);
		if (ret) {
			pci_free_host_bridge(bridge);
			continue;
		}

		bus = hose->bus = bridge->bus;
		hose->need_domain_info = need_domain_info;
		next_busno = bus->busn_res.end + 1;
		/* Don't allow 8-bit bus number overflow inside the hose -
		   reserve some space for bridges. */ 
		if (next_busno > 224) {
			next_busno = 0;
			need_domain_info = 1;
		}
	}

	pcibios_claim_console_setup();

	pci_assign_unassigned_resources();
	for (hose = hose_head; hose; hose = hose->next) {
		bus = hose->bus;
		if (bus)
			pci_bus_add_devices(bus);
	}
}

struct pci_controller * __init
alloc_pci_controller(void)
{
	struct pci_controller *hose;

	hose = alloc_bootmem(sizeof(*hose));

	*hose_tail = hose;
	hose_tail = &hose->next;

	return hose;
}

struct resource * __init
alloc_resource(void)
{
	return alloc_bootmem(sizeof(struct resource));
}


/* Provide information on locations of various I/O regions in physical
   memory.  Do this on a per-card basis so that we choose the right hose.  */

asmlinkage long
sys_pciconfig_iobase(long which, unsigned long bus, unsigned long dfn)
{
	struct pci_controller *hose;
	struct pci_dev *dev;

	/* from hose or from bus.devfn */
	if (which & IOBASE_FROM_HOSE) {
		for(hose = hose_head; hose; hose = hose->next) 
			if (hose->index == bus) break;
		if (!hose) return -ENODEV;
	} else {
		/* Special hook for ISA access.  */
		if (bus == 0 && dfn == 0) {
			hose = pci_isa_hose;
		} else {
			dev = pci_get_bus_and_slot(bus, dfn);
			if (!dev)
				return -ENODEV;
			hose = dev->sysdata;
			pci_dev_put(dev);
		}
	}

	switch (which & ~IOBASE_FROM_HOSE) {
	case IOBASE_HOSE:
		return hose->index;
	case IOBASE_SPARSE_MEM:
		return hose->sparse_mem_base;
	case IOBASE_DENSE_MEM:
		return hose->dense_mem_base;
	case IOBASE_SPARSE_IO:
		return hose->sparse_io_base;
	case IOBASE_DENSE_IO:
		return hose->dense_io_base;
	case IOBASE_ROOT_BUS:
		return hose->bus->number;
	}

	return -EOPNOTSUPP;
}

/* Destroy an __iomem token.  Not copied from lib/iomap.c.  */

void pci_iounmap(struct pci_dev *dev, void __iomem * addr)
{
	if (__is_mmio(addr))
		iounmap(addr);
}

EXPORT_SYMBOL(pci_iounmap);

/* FIXME: Some boxes have multiple ISA bridges! */
struct pci_dev *isa_bridge;
EXPORT_SYMBOL(isa_bridge);
