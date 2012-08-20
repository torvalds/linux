/*
 * Support PCI/PCIe on PowerNV platforms
 *
 * Copyright 2011 Benjamin Herrenschmidt, IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/msi.h>

#include <asm/sections.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/ppc-pci.h>
#include <asm/opal.h>
#include <asm/iommu.h>
#include <asm/tce.h>

#include "powernv.h"
#include "pci.h"

struct resource_wrap {
	struct list_head	link;
	resource_size_t		size;
	resource_size_t		align;
	struct pci_dev		*dev;	/* Set if it's a device */
	struct pci_bus		*bus;	/* Set if it's a bridge */
};

static int __pe_printk(const char *level, const struct pnv_ioda_pe *pe,
		       struct va_format *vaf)
{
	char pfix[32];

	if (pe->pdev)
		strlcpy(pfix, dev_name(&pe->pdev->dev), sizeof(pfix));
	else
		sprintf(pfix, "%04x:%02x     ",
			pci_domain_nr(pe->pbus), pe->pbus->number);
	return printk("pci %s%s: [PE# %.3d] %pV", level, pfix, pe->pe_number, vaf);
}

#define define_pe_printk_level(func, kern_level)		\
static int func(const struct pnv_ioda_pe *pe, const char *fmt, ...)	\
{								\
	struct va_format vaf;					\
	va_list args;						\
	int r;							\
								\
	va_start(args, fmt);					\
								\
	vaf.fmt = fmt;						\
	vaf.va = &args;						\
								\
	r = __pe_printk(kern_level, pe, &vaf);			\
	va_end(args);						\
								\
	return r;						\
}								\

define_pe_printk_level(pe_err, KERN_ERR);
define_pe_printk_level(pe_warn, KERN_WARNING);
define_pe_printk_level(pe_info, KERN_INFO);


/* Calculate resource usage & alignment requirement of a single
 * device. This will also assign all resources within the device
 * for a given type starting at 0 for the biggest one and then
 * assigning in decreasing order of size.
 */
static void __devinit pnv_ioda_calc_dev(struct pci_dev *dev, unsigned int flags,
					resource_size_t *size,
					resource_size_t *align)
{
	resource_size_t start;
	struct resource *r;
	int i;

	pr_devel("  -> CDR %s\n", pci_name(dev));

	*size = *align = 0;

	/* Clear the resources out and mark them all unset */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		r = &dev->resource[i];
		if (!(r->flags & flags))
		    continue;
		if (r->start) {
			r->end -= r->start;
			r->start = 0;
		}
		r->flags |= IORESOURCE_UNSET;
	}

	/* We currently keep all memory resources together, we
	 * will handle prefetch & 64-bit separately in the future
	 * but for now we stick everybody in M32
	 */
	start = 0;
	for (;;) {
		resource_size_t max_size = 0;
		int max_no = -1;

		/* Find next biggest resource */
		for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
			r = &dev->resource[i];
			if (!(r->flags & IORESOURCE_UNSET) ||
			    !(r->flags & flags))
				continue;
			if (resource_size(r) > max_size) {
				max_size = resource_size(r);
				max_no = i;
			}
		}
		if (max_no < 0)
			break;
		r = &dev->resource[max_no];
		if (max_size > *align)
			*align = max_size;
		*size += max_size;
		r->start = start;
		start += max_size;
		r->end = r->start + max_size - 1;
		r->flags &= ~IORESOURCE_UNSET;
		pr_devel("  ->     R%d %016llx..%016llx\n",
			 max_no, r->start, r->end);
	}
	pr_devel("  <- CDR %s size=%llx align=%llx\n",
		 pci_name(dev), *size, *align);
}

/* Allocate a resource "wrap" for a given device or bridge and
 * insert it at the right position in the sorted list
 */
static void __devinit pnv_ioda_add_wrap(struct list_head *list,
					struct pci_bus *bus,
					struct pci_dev *dev,
					resource_size_t size,
					resource_size_t align)
{
	struct resource_wrap *w1, *w = kzalloc(sizeof(*w), GFP_KERNEL);

	w->size = size;
	w->align = align;
	w->dev = dev;
	w->bus = bus;

	list_for_each_entry(w1, list, link) {
		if (w1->align < align) {
			list_add_tail(&w->link, &w1->link);
			return;
		}
	}
	list_add_tail(&w->link, list);
}

/* Offset device resources of a given type */
static void __devinit pnv_ioda_offset_dev(struct pci_dev *dev,
					  unsigned int flags,
					  resource_size_t offset)
{
	struct resource *r;
	int i;

	pr_devel("  -> ODR %s [%x] +%016llx\n", pci_name(dev), flags, offset);

	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		r = &dev->resource[i];
		if (r->flags & flags) {
			dev->resource[i].start += offset;
			dev->resource[i].end += offset;
		}
	}

	pr_devel("  <- ODR %s [%x] +%016llx\n", pci_name(dev), flags, offset);
}

/* Offset bus resources (& all children) of a given type */
static void __devinit pnv_ioda_offset_bus(struct pci_bus *bus,
					  unsigned int flags,
					  resource_size_t offset)
{
	struct resource *r;
	struct pci_dev *dev;
	struct pci_bus *cbus;
	int i;

	pr_devel("  -> OBR %s [%x] +%016llx\n",
		 bus->self ? pci_name(bus->self) : "root", flags, offset);

	pci_bus_for_each_resource(bus, r, i) {
		if (r && (r->flags & flags)) {
			r->start += offset;
			r->end += offset;
		}
	}
	list_for_each_entry(dev, &bus->devices, bus_list)
		pnv_ioda_offset_dev(dev, flags, offset);
	list_for_each_entry(cbus, &bus->children, node)
		pnv_ioda_offset_bus(cbus, flags, offset);

	pr_devel("  <- OBR %s [%x]\n",
		 bus->self ? pci_name(bus->self) : "root", flags);
}

/* This is the guts of our IODA resource allocation. This is called
 * recursively for each bus in the system. It calculates all the
 * necessary size and requirements for children and assign them
 * resources such that:
 *
 *   - Each function fits in it's own contiguous set of IO/M32
 *     segment
 *
 *   - All segments behind a P2P bridge are contiguous and obey
 *     alignment constraints of those bridges
 */
static void __devinit pnv_ioda_calc_bus(struct pci_bus *bus, unsigned int flags,
					resource_size_t *size,
					resource_size_t *align)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pnv_phb *phb = hose->private_data;
	resource_size_t dev_size, dev_align, start;
	resource_size_t min_align, min_balign;
	struct pci_dev *cdev;
	struct pci_bus *cbus;
	struct list_head head;
	struct resource_wrap *w;
	unsigned int bres;

	*size = *align = 0;

	pr_devel("-> CBR %s [%x]\n",
		 bus->self ? pci_name(bus->self) : "root", flags);

	/* Calculate alignment requirements based on the type
	 * of resource we are working on
	 */
	if (flags & IORESOURCE_IO) {
		bres = 0;
		min_align = phb->ioda.io_segsize;
		min_balign = 0x1000;
	} else {
		bres = 1;
		min_align = phb->ioda.m32_segsize;
		min_balign = 0x100000;
	}

	/* Gather all our children resources ordered by alignment */
	INIT_LIST_HEAD(&head);

	/*   - Busses */
	list_for_each_entry(cbus, &bus->children, node) {
		pnv_ioda_calc_bus(cbus, flags, &dev_size, &dev_align);
		pnv_ioda_add_wrap(&head, cbus, NULL, dev_size, dev_align);
	}

	/*   - Devices */
	list_for_each_entry(cdev, &bus->devices, bus_list) {
		pnv_ioda_calc_dev(cdev, flags, &dev_size, &dev_align);
		/* Align them to segment size */
		if (dev_align < min_align)
			dev_align = min_align;
		pnv_ioda_add_wrap(&head, NULL, cdev, dev_size, dev_align);
	}
	if (list_empty(&head))
		goto empty;

	/* Now we can do two things: assign offsets to them within that
	 * level and get our total alignment & size requirements. The
	 * assignment algorithm is going to be uber-trivial for now, we
	 * can try to be smarter later at filling out holes.
	 */
	if (bus->self) {
		/* No offset for downstream bridges */
		start = 0;
	} else {
		/* Offset from the root */
		if (flags & IORESOURCE_IO)
			/* Don't hand out IO 0 */
			start = hose->io_resource.start + 0x1000;
		else
			start = hose->mem_resources[0].start;
	}
	while(!list_empty(&head)) {
		w = list_first_entry(&head, struct resource_wrap, link);
		list_del(&w->link);
		if (w->size) {
			if (start) {
				start = ALIGN(start, w->align);
				if (w->dev)
					pnv_ioda_offset_dev(w->dev,flags,start);
				else if (w->bus)
					pnv_ioda_offset_bus(w->bus,flags,start);
			}
			if (w->align > *align)
				*align = w->align;
		}
		start += w->size;
		kfree(w);
	}
	*size = start;

	/* Align and setup bridge resources */
	*align = max_t(resource_size_t, *align,
		       max_t(resource_size_t, min_align, min_balign));
	*size = ALIGN(*size,
		      max_t(resource_size_t, min_align, min_balign));
 empty:
	/* Only setup P2P's, not the PHB itself */
	if (bus->self) {
		struct resource *res = bus->resource[bres];

		if (WARN_ON(res == NULL))
			return;

		/*
		 * FIXME: We should probably export and call
		 * pci_bridge_check_ranges() to properly re-initialize
		 * the PCI portion of the flags here, and to detect
		 * what the bridge actually supports.
		 */
		res->start = 0;
		res->flags = (*size) ? flags : 0;
		res->end = (*size) ? (*size - 1) : 0;
	}

	pr_devel("<- CBR %s [%x] *size=%016llx *align=%016llx\n",
		 bus->self ? pci_name(bus->self) : "root", flags,*size,*align);
}

static struct pci_dn *pnv_ioda_get_pdn(struct pci_dev *dev)
{
	struct device_node *np;

	np = pci_device_to_OF_node(dev);
	if (!np)
		return NULL;
	return PCI_DN(np);
}

static void __devinit pnv_ioda_setup_pe_segments(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dn *pdn = pnv_ioda_get_pdn(dev);
	unsigned int pe, i;
	resource_size_t pos;
	struct resource io_res;
	struct resource m32_res;
	struct pci_bus_region region;
	int rc;

	/* Anything not referenced in the device-tree gets PE#0 */
	pe = pdn ? pdn->pe_number : 0;

	/* Calculate the device min/max */
	io_res.start = m32_res.start = (resource_size_t)-1;
	io_res.end = m32_res.end = 0;
	io_res.flags = IORESOURCE_IO;
	m32_res.flags = IORESOURCE_MEM;

	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		struct resource *r = NULL;
		if (dev->resource[i].flags & IORESOURCE_IO)
			r = &io_res;
		if (dev->resource[i].flags & IORESOURCE_MEM)
			r = &m32_res;
		if (!r)
			continue;
		if (dev->resource[i].start < r->start)
			r->start = dev->resource[i].start;
		if (dev->resource[i].end > r->end)
			r->end = dev->resource[i].end;
	}

	/* Setup IO segments */
	if (io_res.start < io_res.end) {
		pcibios_resource_to_bus(dev, &region, &io_res);
		pos = region.start;
		i = pos / phb->ioda.io_segsize;
		while(i < phb->ioda.total_pe && pos <= region.end) {
			if (phb->ioda.io_segmap[i]) {
				pr_err("%s: Trying to use IO seg #%d which is"
				       " already used by PE# %d\n",
				       pci_name(dev), i,
				       phb->ioda.io_segmap[i]);
				/* XXX DO SOMETHING TO DISABLE DEVICE ? */
				break;
			}
			phb->ioda.io_segmap[i] = pe;
			rc = opal_pci_map_pe_mmio_window(phb->opal_id, pe,
							 OPAL_IO_WINDOW_TYPE,
							 0, i);
			if (rc != OPAL_SUCCESS) {
				pr_err("%s: OPAL error %d setting up mapping"
				       " for IO seg# %d\n",
				       pci_name(dev), rc, i);
				/* XXX DO SOMETHING TO DISABLE DEVICE ? */
				break;
			}
			pos += phb->ioda.io_segsize;
			i++;
		};
	}

	/* Setup M32 segments */
	if (m32_res.start < m32_res.end) {
		pcibios_resource_to_bus(dev, &region, &m32_res);
		pos = region.start;
		i = pos / phb->ioda.m32_segsize;
		while(i < phb->ioda.total_pe && pos <= region.end) {
			if (phb->ioda.m32_segmap[i]) {
				pr_err("%s: Trying to use M32 seg #%d which is"
				       " already used by PE# %d\n",
				       pci_name(dev), i,
				       phb->ioda.m32_segmap[i]);
				/* XXX DO SOMETHING TO DISABLE DEVICE ? */
				break;
			}
			phb->ioda.m32_segmap[i] = pe;
			rc = opal_pci_map_pe_mmio_window(phb->opal_id, pe,
							 OPAL_M32_WINDOW_TYPE,
							 0, i);
			if (rc != OPAL_SUCCESS) {
				pr_err("%s: OPAL error %d setting up mapping"
				       " for M32 seg# %d\n",
				       pci_name(dev), rc, i);
				/* XXX DO SOMETHING TO DISABLE DEVICE ? */
				break;
			}
			pos += phb->ioda.m32_segsize;
			i++;
		}
	}
}

/* Check if a resource still fits in the total IO or M32 range
 * for a given PHB
 */
static int __devinit pnv_ioda_resource_fit(struct pci_controller *hose,
					   struct resource *r)
{
	struct resource *bounds;

	if (r->flags & IORESOURCE_IO)
		bounds = &hose->io_resource;
	else if (r->flags & IORESOURCE_MEM)
		bounds = &hose->mem_resources[0];
	else
		return 1;

	if (r->start >= bounds->start && r->end <= bounds->end)
		return 1;
	r->flags = 0;
	return 0;
}

static void __devinit pnv_ioda_update_resources(struct pci_bus *bus)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pci_bus *cbus;
	struct pci_dev *cdev;
	unsigned int i;

	/* We used to clear all device enables here. However it looks like
	 * clearing MEM enable causes Obsidian (IPR SCS) to go bonkers,
	 * and shoot fatal errors to the PHB which in turns fences itself
	 * and we can't recover from that ... yet. So for now, let's leave
	 * the enables as-is and hope for the best.
	 */

	/* Check if bus resources fit in our IO or M32 range */
	for (i = 0; bus->self && (i < 2); i++) {
		struct resource *r = bus->resource[i];
		if (r && !pnv_ioda_resource_fit(hose, r))
			pr_err("%s: Bus %d resource %d disabled, no room\n",
			       pci_name(bus->self), bus->number, i);
	}

	/* Update self if it's not a PHB */
	if (bus->self)
		pci_setup_bridge(bus);

	/* Update child devices */
	list_for_each_entry(cdev, &bus->devices, bus_list) {
		/* Check if resource fits, if not, disabled it */
		for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
			struct resource *r = &cdev->resource[i];
			if (!pnv_ioda_resource_fit(hose, r))
				pr_err("%s: Resource %d disabled, no room\n",
				       pci_name(cdev), i);
		}

		/* Assign segments */
		pnv_ioda_setup_pe_segments(cdev);

		/* Update HW BARs */
		for (i = 0; i <= PCI_ROM_RESOURCE; i++)
			pci_update_resource(cdev, i);
	}

	/* Update child busses */
	list_for_each_entry(cbus, &bus->children, node)
		pnv_ioda_update_resources(cbus);
}

static int __devinit pnv_ioda_alloc_pe(struct pnv_phb *phb)
{
	unsigned long pe;

	do {
		pe = find_next_zero_bit(phb->ioda.pe_alloc,
					phb->ioda.total_pe, 0);
		if (pe >= phb->ioda.total_pe)
			return IODA_INVALID_PE;
	} while(test_and_set_bit(pe, phb->ioda.pe_alloc));

	phb->ioda.pe_array[pe].pe_number = pe;
	return pe;
}

static void __devinit pnv_ioda_free_pe(struct pnv_phb *phb, int pe)
{
	WARN_ON(phb->ioda.pe_array[pe].pdev);

	memset(&phb->ioda.pe_array[pe], 0, sizeof(struct pnv_ioda_pe));
	clear_bit(pe, phb->ioda.pe_alloc);
}

/* Currently those 2 are only used when MSIs are enabled, this will change
 * but in the meantime, we need to protect them to avoid warnings
 */
#ifdef CONFIG_PCI_MSI
static struct pnv_ioda_pe * __devinit pnv_ioda_get_pe(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dn *pdn = pnv_ioda_get_pdn(dev);

	if (!pdn)
		return NULL;
	if (pdn->pe_number == IODA_INVALID_PE)
		return NULL;
	return &phb->ioda.pe_array[pdn->pe_number];
}
#endif /* CONFIG_PCI_MSI */

static int __devinit pnv_ioda_configure_pe(struct pnv_phb *phb,
					   struct pnv_ioda_pe *pe)
{
	struct pci_dev *parent;
	uint8_t bcomp, dcomp, fcomp;
	long rc, rid_end, rid;

	/* Bus validation ? */
	if (pe->pbus) {
		int count;

		dcomp = OPAL_IGNORE_RID_DEVICE_NUMBER;
		fcomp = OPAL_IGNORE_RID_FUNCTION_NUMBER;
		parent = pe->pbus->self;
		if (pe->flags & PNV_IODA_PE_BUS_ALL)
			count = pe->pbus->busn_res.end - pe->pbus->busn_res.start + 1;
		else
			count = 1;

		switch(count) {
		case  1: bcomp = OpalPciBusAll;		break;
		case  2: bcomp = OpalPciBus7Bits;	break;
		case  4: bcomp = OpalPciBus6Bits;	break;
		case  8: bcomp = OpalPciBus5Bits;	break;
		case 16: bcomp = OpalPciBus4Bits;	break;
		case 32: bcomp = OpalPciBus3Bits;	break;
		default:
			pr_err("%s: Number of subordinate busses %d"
			       " unsupported\n",
			       pci_name(pe->pbus->self), count);
			/* Do an exact match only */
			bcomp = OpalPciBusAll;
		}
		rid_end = pe->rid + (count << 8);
	} else {
		parent = pe->pdev->bus->self;
		bcomp = OpalPciBusAll;
		dcomp = OPAL_COMPARE_RID_DEVICE_NUMBER;
		fcomp = OPAL_COMPARE_RID_FUNCTION_NUMBER;
		rid_end = pe->rid + 1;
	}

	/* Associate PE in PELT */
	rc = opal_pci_set_pe(phb->opal_id, pe->pe_number, pe->rid,
			     bcomp, dcomp, fcomp, OPAL_MAP_PE);
	if (rc) {
		pe_err(pe, "OPAL error %ld trying to setup PELT table\n", rc);
		return -ENXIO;
	}
	opal_pci_eeh_freeze_clear(phb->opal_id, pe->pe_number,
				  OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);

	/* Add to all parents PELT-V */
	while (parent) {
		struct pci_dn *pdn = pnv_ioda_get_pdn(parent);
		if (pdn && pdn->pe_number != IODA_INVALID_PE) {
			rc = opal_pci_set_peltv(phb->opal_id, pdn->pe_number,
						pe->pe_number, OPAL_ADD_PE_TO_DOMAIN);
			/* XXX What to do in case of error ? */
		}
		parent = parent->bus->self;
	}
	/* Setup reverse map */
	for (rid = pe->rid; rid < rid_end; rid++)
		phb->ioda.pe_rmap[rid] = pe->pe_number;

	/* Setup one MVTs on IODA1 */
	if (phb->type == PNV_PHB_IODA1) {
		pe->mve_number = pe->pe_number;
		rc = opal_pci_set_mve(phb->opal_id, pe->mve_number,
				      pe->pe_number);
		if (rc) {
			pe_err(pe, "OPAL error %ld setting up MVE %d\n",
			       rc, pe->mve_number);
			pe->mve_number = -1;
		} else {
			rc = opal_pci_set_mve_enable(phb->opal_id,
						     pe->mve_number, OPAL_ENABLE_MVE);
			if (rc) {
				pe_err(pe, "OPAL error %ld enabling MVE %d\n",
				       rc, pe->mve_number);
				pe->mve_number = -1;
			}
		}
	} else if (phb->type == PNV_PHB_IODA2)
		pe->mve_number = 0;

	return 0;
}

static void __devinit pnv_ioda_link_pe_by_weight(struct pnv_phb *phb,
						 struct pnv_ioda_pe *pe)
{
	struct pnv_ioda_pe *lpe;

	list_for_each_entry(lpe, &phb->ioda.pe_dma_list, dma_link) {
		if (lpe->dma_weight < pe->dma_weight) {
			list_add_tail(&pe->dma_link, &lpe->dma_link);
			return;
		}
	}
	list_add_tail(&pe->dma_link, &phb->ioda.pe_dma_list);
}

static unsigned int pnv_ioda_dma_weight(struct pci_dev *dev)
{
	/* This is quite simplistic. The "base" weight of a device
	 * is 10. 0 means no DMA is to be accounted for it.
	 */

	/* If it's a bridge, no DMA */
	if (dev->hdr_type != PCI_HEADER_TYPE_NORMAL)
		return 0;

	/* Reduce the weight of slow USB controllers */
	if (dev->class == PCI_CLASS_SERIAL_USB_UHCI ||
	    dev->class == PCI_CLASS_SERIAL_USB_OHCI ||
	    dev->class == PCI_CLASS_SERIAL_USB_EHCI)
		return 3;

	/* Increase the weight of RAID (includes Obsidian) */
	if ((dev->class >> 8) == PCI_CLASS_STORAGE_RAID)
		return 15;

	/* Default */
	return 10;
}

#if 0
static struct pnv_ioda_pe * __devinit pnv_ioda_setup_dev_PE(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dn *pdn = pnv_ioda_get_pdn(dev);
	struct pnv_ioda_pe *pe;
	int pe_num;

	if (!pdn) {
		pr_err("%s: Device tree node not associated properly\n",
			   pci_name(dev));
		return NULL;
	}
	if (pdn->pe_number != IODA_INVALID_PE)
		return NULL;

	/* PE#0 has been pre-set */
	if (dev->bus->number == 0)
		pe_num = 0;
	else
		pe_num = pnv_ioda_alloc_pe(phb);
	if (pe_num == IODA_INVALID_PE) {
		pr_warning("%s: Not enough PE# available, disabling device\n",
			   pci_name(dev));
		return NULL;
	}

	/* NOTE: We get only one ref to the pci_dev for the pdn, not for the
	 * pointer in the PE data structure, both should be destroyed at the
	 * same time. However, this needs to be looked at more closely again
	 * once we actually start removing things (Hotplug, SR-IOV, ...)
	 *
	 * At some point we want to remove the PDN completely anyways
	 */
	pe = &phb->ioda.pe_array[pe_num];
	pci_dev_get(dev);
	pdn->pcidev = dev;
	pdn->pe_number = pe_num;
	pe->pdev = dev;
	pe->pbus = NULL;
	pe->tce32_seg = -1;
	pe->mve_number = -1;
	pe->rid = dev->bus->number << 8 | pdn->devfn;

	pe_info(pe, "Associated device to PE\n");

	if (pnv_ioda_configure_pe(phb, pe)) {
		/* XXX What do we do here ? */
		if (pe_num)
			pnv_ioda_free_pe(phb, pe_num);
		pdn->pe_number = IODA_INVALID_PE;
		pe->pdev = NULL;
		pci_dev_put(dev);
		return NULL;
	}

	/* Assign a DMA weight to the device */
	pe->dma_weight = pnv_ioda_dma_weight(dev);
	if (pe->dma_weight != 0) {
		phb->ioda.dma_weight += pe->dma_weight;
		phb->ioda.dma_pe_count++;
	}

	/* Link the PE */
	pnv_ioda_link_pe_by_weight(phb, pe);

	return pe;
}
#endif /* Useful for SRIOV case */

static void pnv_ioda_setup_same_PE(struct pci_bus *bus, struct pnv_ioda_pe *pe)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		struct pci_dn *pdn = pnv_ioda_get_pdn(dev);

		if (pdn == NULL) {
			pr_warn("%s: No device node associated with device !\n",
				pci_name(dev));
			continue;
		}
		pci_dev_get(dev);
		pdn->pcidev = dev;
		pdn->pe_number = pe->pe_number;
		pe->dma_weight += pnv_ioda_dma_weight(dev);
		if ((pe->flags & PNV_IODA_PE_BUS_ALL) && dev->subordinate)
			pnv_ioda_setup_same_PE(dev->subordinate, pe);
	}
}

/*
 * There're 2 types of PCI bus sensitive PEs: One that is compromised of
 * single PCI bus. Another one that contains the primary PCI bus and its
 * subordinate PCI devices and buses. The second type of PE is normally
 * orgiriated by PCIe-to-PCI bridge or PLX switch downstream ports.
 */
static void __devinit pnv_ioda_setup_bus_PE(struct pci_bus *bus, int all)
{
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pnv_phb *phb = hose->private_data;
	struct pnv_ioda_pe *pe;
	int pe_num;

	pe_num = pnv_ioda_alloc_pe(phb);
	if (pe_num == IODA_INVALID_PE) {
		pr_warning("%s: Not enough PE# available for PCI bus %04x:%02x\n",
			__func__, pci_domain_nr(bus), bus->number);
		return;
	}

	pe = &phb->ioda.pe_array[pe_num];
	pe->flags = (all ? PNV_IODA_PE_BUS_ALL : PNV_IODA_PE_BUS);
	pe->pbus = bus;
	pe->pdev = NULL;
	pe->tce32_seg = -1;
	pe->mve_number = -1;
	pe->rid = bus->busn_res.start << 8;
	pe->dma_weight = 0;

	if (all)
		pe_info(pe, "Secondary bus %d..%d associated with PE#%d\n",
			bus->busn_res.start, bus->busn_res.end, pe_num);
	else
		pe_info(pe, "Secondary bus %d associated with PE#%d\n",
			bus->busn_res.start, pe_num);

	if (pnv_ioda_configure_pe(phb, pe)) {
		/* XXX What do we do here ? */
		if (pe_num)
			pnv_ioda_free_pe(phb, pe_num);
		pe->pbus = NULL;
		return;
	}

	/* Associate it with all child devices */
	pnv_ioda_setup_same_PE(bus, pe);

	/* Put PE to the list */
	list_add_tail(&pe->list, &phb->ioda.pe_list);

	/* Account for one DMA PE if at least one DMA capable device exist
	 * below the bridge
	 */
	if (pe->dma_weight != 0) {
		phb->ioda.dma_weight += pe->dma_weight;
		phb->ioda.dma_pe_count++;
	}

	/* Link the PE */
	pnv_ioda_link_pe_by_weight(phb, pe);
}

static void __devinit pnv_ioda_setup_PEs(struct pci_bus *bus)
{
	struct pci_dev *dev;

	pnv_ioda_setup_bus_PE(bus, 0);

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->subordinate) {
			if (pci_pcie_type(dev) == PCI_EXP_TYPE_PCI_BRIDGE)
				pnv_ioda_setup_bus_PE(dev->subordinate, 1);
			else
				pnv_ioda_setup_PEs(dev->subordinate);
		}
	}
}

/*
 * Configure PEs so that the downstream PCI buses and devices
 * could have their associated PE#. Unfortunately, we didn't
 * figure out the way to identify the PLX bridge yet. So we
 * simply put the PCI bus and the subordinate behind the root
 * port to PE# here. The game rule here is expected to be changed
 * as soon as we can detected PLX bridge correctly.
 */
static void __devinit pnv_pci_ioda_setup_PEs(void)
{
	struct pci_controller *hose, *tmp;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		pnv_ioda_setup_PEs(hose->bus);
	}
}

static void __devinit pnv_pci_ioda_dma_dev_setup(struct pnv_phb *phb,
						 struct pci_dev *dev)
{
	/* We delay DMA setup after we have assigned all PE# */
}

static void __devinit pnv_ioda_setup_bus_dma(struct pnv_ioda_pe *pe,
					     struct pci_bus *bus)
{
	struct pci_dev *dev;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		set_iommu_table_base(&dev->dev, &pe->tce32_table);
		if (dev->subordinate)
			pnv_ioda_setup_bus_dma(pe, dev->subordinate);
	}
}

static void __devinit pnv_pci_ioda_setup_dma_pe(struct pnv_phb *phb,
						struct pnv_ioda_pe *pe,
						unsigned int base,
						unsigned int segs)
{

	struct page *tce_mem = NULL;
	const __be64 *swinvp;
	struct iommu_table *tbl;
	unsigned int i;
	int64_t rc;
	void *addr;

	/* 256M DMA window, 4K TCE pages, 8 bytes TCE */
#define TCE32_TABLE_SIZE	((0x10000000 / 0x1000) * 8)

	/* XXX FIXME: Handle 64-bit only DMA devices */
	/* XXX FIXME: Provide 64-bit DMA facilities & non-4K TCE tables etc.. */
	/* XXX FIXME: Allocate multi-level tables on PHB3 */

	/* We shouldn't already have a 32-bit DMA associated */
	if (WARN_ON(pe->tce32_seg >= 0))
		return;

	/* Grab a 32-bit TCE table */
	pe->tce32_seg = base;
	pe_info(pe, " Setting up 32-bit TCE table at %08x..%08x\n",
		(base << 28), ((base + segs) << 28) - 1);

	/* XXX Currently, we allocate one big contiguous table for the
	 * TCEs. We only really need one chunk per 256M of TCE space
	 * (ie per segment) but that's an optimization for later, it
	 * requires some added smarts with our get/put_tce implementation
	 */
	tce_mem = alloc_pages_node(phb->hose->node, GFP_KERNEL,
				   get_order(TCE32_TABLE_SIZE * segs));
	if (!tce_mem) {
		pe_err(pe, " Failed to allocate a 32-bit TCE memory\n");
		goto fail;
	}
	addr = page_address(tce_mem);
	memset(addr, 0, TCE32_TABLE_SIZE * segs);

	/* Configure HW */
	for (i = 0; i < segs; i++) {
		rc = opal_pci_map_pe_dma_window(phb->opal_id,
					      pe->pe_number,
					      base + i, 1,
					      __pa(addr) + TCE32_TABLE_SIZE * i,
					      TCE32_TABLE_SIZE, 0x1000);
		if (rc) {
			pe_err(pe, " Failed to configure 32-bit TCE table,"
			       " err %ld\n", rc);
			goto fail;
		}
	}

	/* Setup linux iommu table */
	tbl = &pe->tce32_table;
	pnv_pci_setup_iommu_table(tbl, addr, TCE32_TABLE_SIZE * segs,
				  base << 28);

	/* OPAL variant of P7IOC SW invalidated TCEs */
	swinvp = of_get_property(phb->hose->dn, "ibm,opal-tce-kill", NULL);
	if (swinvp) {
		/* We need a couple more fields -- an address and a data
		 * to or.  Since the bus is only printed out on table free
		 * errors, and on the first pass the data will be a relative
		 * bus number, print that out instead.
		 */
		tbl->it_busno = 0;
		tbl->it_index = (unsigned long)ioremap(be64_to_cpup(swinvp), 8);
		tbl->it_type = TCE_PCI_SWINV_CREATE | TCE_PCI_SWINV_FREE
			| TCE_PCI_SWINV_PAIR;
	}
	iommu_init_table(tbl, phb->hose->node);

	if (pe->pdev)
		set_iommu_table_base(&pe->pdev->dev, tbl);
	else
		pnv_ioda_setup_bus_dma(pe, pe->pbus);

	return;
 fail:
	/* XXX Failure: Try to fallback to 64-bit only ? */
	if (pe->tce32_seg >= 0)
		pe->tce32_seg = -1;
	if (tce_mem)
		__free_pages(tce_mem, get_order(TCE32_TABLE_SIZE * segs));
}

static void __devinit pnv_ioda_setup_dma(struct pnv_phb *phb)
{
	struct pci_controller *hose = phb->hose;
	unsigned int residual, remaining, segs, tw, base;
	struct pnv_ioda_pe *pe;

	/* If we have more PE# than segments available, hand out one
	 * per PE until we run out and let the rest fail. If not,
	 * then we assign at least one segment per PE, plus more based
	 * on the amount of devices under that PE
	 */
	if (phb->ioda.dma_pe_count > phb->ioda.tce32_count)
		residual = 0;
	else
		residual = phb->ioda.tce32_count -
			phb->ioda.dma_pe_count;

	pr_info("PCI: Domain %04x has %ld available 32-bit DMA segments\n",
		hose->global_number, phb->ioda.tce32_count);
	pr_info("PCI: %d PE# for a total weight of %d\n",
		phb->ioda.dma_pe_count, phb->ioda.dma_weight);

	/* Walk our PE list and configure their DMA segments, hand them
	 * out one base segment plus any residual segments based on
	 * weight
	 */
	remaining = phb->ioda.tce32_count;
	tw = phb->ioda.dma_weight;
	base = 0;
	list_for_each_entry(pe, &phb->ioda.pe_dma_list, dma_link) {
		if (!pe->dma_weight)
			continue;
		if (!remaining) {
			pe_warn(pe, "No DMA32 resources available\n");
			continue;
		}
		segs = 1;
		if (residual) {
			segs += ((pe->dma_weight * residual)  + (tw / 2)) / tw;
			if (segs > remaining)
				segs = remaining;
		}
		pe_info(pe, "DMA weight %d, assigned %d DMA32 segments\n",
			pe->dma_weight, segs);
		pnv_pci_ioda_setup_dma_pe(phb, pe, base, segs);
		remaining -= segs;
		base += segs;
	}
}

#ifdef CONFIG_PCI_MSI
static int pnv_pci_ioda_msi_setup(struct pnv_phb *phb, struct pci_dev *dev,
				  unsigned int hwirq, unsigned int is_64,
				  struct msi_msg *msg)
{
	struct pnv_ioda_pe *pe = pnv_ioda_get_pe(dev);
	unsigned int xive_num = hwirq - phb->msi_base;
	uint64_t addr64;
	uint32_t addr32, data;
	int rc;

	/* No PE assigned ? bail out ... no MSI for you ! */
	if (pe == NULL)
		return -ENXIO;

	/* Check if we have an MVE */
	if (pe->mve_number < 0)
		return -ENXIO;

	/* Assign XIVE to PE */
	rc = opal_pci_set_xive_pe(phb->opal_id, pe->pe_number, xive_num);
	if (rc) {
		pr_warn("%s: OPAL error %d setting XIVE %d PE\n",
			pci_name(dev), rc, xive_num);
		return -EIO;
	}

	if (is_64) {
		rc = opal_get_msi_64(phb->opal_id, pe->mve_number, xive_num, 1,
				     &addr64, &data);
		if (rc) {
			pr_warn("%s: OPAL error %d getting 64-bit MSI data\n",
				pci_name(dev), rc);
			return -EIO;
		}
		msg->address_hi = addr64 >> 32;
		msg->address_lo = addr64 & 0xfffffffful;
	} else {
		rc = opal_get_msi_32(phb->opal_id, pe->mve_number, xive_num, 1,
				     &addr32, &data);
		if (rc) {
			pr_warn("%s: OPAL error %d getting 32-bit MSI data\n",
				pci_name(dev), rc);
			return -EIO;
		}
		msg->address_hi = 0;
		msg->address_lo = addr32;
	}
	msg->data = data;

	pr_devel("%s: %s-bit MSI on hwirq %x (xive #%d),"
		 " address=%x_%08x data=%x PE# %d\n",
		 pci_name(dev), is_64 ? "64" : "32", hwirq, xive_num,
		 msg->address_hi, msg->address_lo, data, pe->pe_number);

	return 0;
}

static void pnv_pci_init_ioda_msis(struct pnv_phb *phb)
{
	unsigned int bmap_size;
	const __be32 *prop = of_get_property(phb->hose->dn,
					     "ibm,opal-msi-ranges", NULL);
	if (!prop) {
		/* BML Fallback */
		prop = of_get_property(phb->hose->dn, "msi-ranges", NULL);
	}
	if (!prop)
		return;

	phb->msi_base = be32_to_cpup(prop);
	phb->msi_count = be32_to_cpup(prop + 1);
	bmap_size = BITS_TO_LONGS(phb->msi_count) * sizeof(unsigned long);
	phb->msi_map = zalloc_maybe_bootmem(bmap_size, GFP_KERNEL);
	if (!phb->msi_map) {
		pr_err("PCI %d: Failed to allocate MSI bitmap !\n",
		       phb->hose->global_number);
		return;
	}
	phb->msi_setup = pnv_pci_ioda_msi_setup;
	phb->msi32_support = 1;
	pr_info("  Allocated bitmap for %d MSIs (base IRQ 0x%x)\n",
		phb->msi_count, phb->msi_base);
}
#else
static void pnv_pci_init_ioda_msis(struct pnv_phb *phb) { }
#endif /* CONFIG_PCI_MSI */

/* This is the starting point of our IODA specific resource
 * allocation process
 */
static void __devinit pnv_pci_ioda_fixup_phb(struct pci_controller *hose)
{
	resource_size_t size, align;
	struct pci_bus *child;

	/* Associate PEs per functions */
	pnv_ioda_setup_PEs(hose->bus);

	/* Calculate all resources */
	pnv_ioda_calc_bus(hose->bus, IORESOURCE_IO, &size, &align);
	pnv_ioda_calc_bus(hose->bus, IORESOURCE_MEM, &size, &align);

	/* Apply then to HW */
	pnv_ioda_update_resources(hose->bus);

	/* Setup DMA */
	pnv_ioda_setup_dma(hose->private_data);

	/* Configure PCI Express settings */
	list_for_each_entry(child, &hose->bus->children, node) {
		struct pci_dev *self = child->self;
		if (!self)
			continue;
		pcie_bus_configure_settings(child, self->pcie_mpss);
	}
}

/*
 * This function is supposed to be called on basis of PE from top
 * to bottom style. So the the I/O or MMIO segment assigned to
 * parent PE could be overrided by its child PEs if necessary.
 */
static void __devinit pnv_ioda_setup_pe_seg(struct pci_controller *hose,
				struct pnv_ioda_pe *pe)
{
	struct pnv_phb *phb = hose->private_data;
	struct pci_bus_region region;
	struct resource *res;
	int i, index;
	int rc;

	/*
	 * NOTE: We only care PCI bus based PE for now. For PCI
	 * device based PE, for example SRIOV sensitive VF should
	 * be figured out later.
	 */
	BUG_ON(!(pe->flags & (PNV_IODA_PE_BUS | PNV_IODA_PE_BUS_ALL)));

	pci_bus_for_each_resource(pe->pbus, res, i) {
		if (!res || !res->flags ||
		    res->start > res->end)
			continue;

		if (res->flags & IORESOURCE_IO) {
			region.start = res->start - phb->ioda.io_pci_base;
			region.end   = res->end - phb->ioda.io_pci_base;
			index = region.start / phb->ioda.io_segsize;

			while (index < phb->ioda.total_pe &&
			       region.start <= region.end) {
				phb->ioda.io_segmap[index] = pe->pe_number;
				rc = opal_pci_map_pe_mmio_window(phb->opal_id,
					pe->pe_number, OPAL_IO_WINDOW_TYPE, 0, index);
				if (rc != OPAL_SUCCESS) {
					pr_err("%s: OPAL error %d when mapping IO "
					       "segment #%d to PE#%d\n",
					       __func__, rc, index, pe->pe_number);
					break;
				}

				region.start += phb->ioda.io_segsize;
				index++;
			}
		} else if (res->flags & IORESOURCE_MEM) {
			region.start = res->start -
				       hose->pci_mem_offset -
				       phb->ioda.m32_pci_base;
			region.end   = res->end -
				       hose->pci_mem_offset -
				       phb->ioda.m32_pci_base;
			index = region.start / phb->ioda.m32_segsize;

			while (index < phb->ioda.total_pe &&
			       region.start <= region.end) {
				phb->ioda.m32_segmap[index] = pe->pe_number;
				rc = opal_pci_map_pe_mmio_window(phb->opal_id,
					pe->pe_number, OPAL_M32_WINDOW_TYPE, 0, index);
				if (rc != OPAL_SUCCESS) {
					pr_err("%s: OPAL error %d when mapping M32 "
					       "segment#%d to PE#%d",
					       __func__, rc, index, pe->pe_number);
					break;
				}

				region.start += phb->ioda.m32_segsize;
				index++;
			}
		}
	}
}

static void __devinit pnv_pci_ioda_setup_seg(void)
{
	struct pci_controller *tmp, *hose;
	struct pnv_phb *phb;
	struct pnv_ioda_pe *pe;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		phb = hose->private_data;
		list_for_each_entry(pe, &phb->ioda.pe_list, list) {
			pnv_ioda_setup_pe_seg(hose, pe);
		}
	}
}

static void __devinit pnv_pci_ioda_setup_DMA(void)
{
	struct pci_controller *hose, *tmp;
	struct pnv_phb *phb;

	list_for_each_entry_safe(hose, tmp, &hose_list, list_node) {
		pnv_ioda_setup_dma(hose->private_data);

		/* Mark the PHB initialization done */
		phb = hose->private_data;
		phb->initialized = 1;
	}
}

static void __devinit pnv_pci_ioda_fixup(void)
{
	pnv_pci_ioda_setup_PEs();
	pnv_pci_ioda_setup_seg();
	pnv_pci_ioda_setup_DMA();
}

/*
 * Returns the alignment for I/O or memory windows for P2P
 * bridges. That actually depends on how PEs are segmented.
 * For now, we return I/O or M32 segment size for PE sensitive
 * P2P bridges. Otherwise, the default values (4KiB for I/O,
 * 1MiB for memory) will be returned.
 *
 * The current PCI bus might be put into one PE, which was
 * create against the parent PCI bridge. For that case, we
 * needn't enlarge the alignment so that we can save some
 * resources.
 */
static resource_size_t pnv_pci_window_alignment(struct pci_bus *bus,
						unsigned long type)
{
	struct pci_dev *bridge;
	struct pci_controller *hose = pci_bus_to_host(bus);
	struct pnv_phb *phb = hose->private_data;
	int num_pci_bridges = 0;

	bridge = bus->self;
	while (bridge) {
		if (pci_pcie_type(bridge) == PCI_EXP_TYPE_PCI_BRIDGE) {
			num_pci_bridges++;
			if (num_pci_bridges >= 2)
				return 1;
		}

		bridge = bridge->bus->self;
	}

	/* We need support prefetchable memory window later */
	if (type & IORESOURCE_MEM)
		return phb->ioda.m32_segsize;

	return phb->ioda.io_segsize;
}

/* Prevent enabling devices for which we couldn't properly
 * assign a PE
 */
static int __devinit pnv_pci_enable_device_hook(struct pci_dev *dev)
{
	struct pci_controller *hose = pci_bus_to_host(dev->bus);
	struct pnv_phb *phb = hose->private_data;
	struct pci_dn *pdn;

	/* The function is probably called while the PEs have
	 * not be created yet. For example, resource reassignment
	 * during PCI probe period. We just skip the check if
	 * PEs isn't ready.
	 */
	if (!phb->initialized)
		return 0;

	pdn = pnv_ioda_get_pdn(dev);
	if (!pdn || pdn->pe_number == IODA_INVALID_PE)
		return -EINVAL;

	return 0;
}

static u32 pnv_ioda_bdfn_to_pe(struct pnv_phb *phb, struct pci_bus *bus,
			       u32 devfn)
{
	return phb->ioda.pe_rmap[(bus->number << 8) | devfn];
}

void __init pnv_pci_init_ioda1_phb(struct device_node *np)
{
	struct pci_controller *hose;
	static int primary = 1;
	struct pnv_phb *phb;
	unsigned long size, m32map_off, iomap_off, pemap_off;
	const u64 *prop64;
	u64 phb_id;
	void *aux;
	long rc;

	pr_info(" Initializing IODA OPAL PHB %s\n", np->full_name);

	prop64 = of_get_property(np, "ibm,opal-phbid", NULL);
	if (!prop64) {
		pr_err("  Missing \"ibm,opal-phbid\" property !\n");
		return;
	}
	phb_id = be64_to_cpup(prop64);
	pr_debug("  PHB-ID  : 0x%016llx\n", phb_id);

	phb = alloc_bootmem(sizeof(struct pnv_phb));
	if (phb) {
		memset(phb, 0, sizeof(struct pnv_phb));
		phb->hose = hose = pcibios_alloc_controller(np);
	}
	if (!phb || !phb->hose) {
		pr_err("PCI: Failed to allocate PCI controller for %s\n",
		       np->full_name);
		return;
	}

	spin_lock_init(&phb->lock);
	/* XXX Use device-tree */
	hose->first_busno = 0;
	hose->last_busno = 0xff;
	hose->private_data = phb;
	phb->opal_id = phb_id;
	phb->type = PNV_PHB_IODA1;

	/* Detect specific models for error handling */
	if (of_device_is_compatible(np, "ibm,p7ioc-pciex"))
		phb->model = PNV_PHB_MODEL_P7IOC;
	else
		phb->model = PNV_PHB_MODEL_UNKNOWN;

	/* We parse "ranges" now since we need to deduce the register base
	 * from the IO base
	 */
	pci_process_bridge_OF_ranges(phb->hose, np, primary);
	primary = 0;

	/* Magic formula from Milton */
	phb->regs = of_iomap(np, 0);
	if (phb->regs == NULL)
		pr_err("  Failed to map registers !\n");


	/* XXX This is hack-a-thon. This needs to be changed so that:
	 *  - we obtain stuff like PE# etc... from device-tree
	 *  - we properly re-allocate M32 ourselves
	 *    (the OFW one isn't very good)
	 */

	/* Initialize more IODA stuff */
	phb->ioda.total_pe = 128;

	phb->ioda.m32_size = resource_size(&hose->mem_resources[0]);
	/* OFW Has already off top 64k of M32 space (MSI space) */
	phb->ioda.m32_size += 0x10000;

	phb->ioda.m32_segsize = phb->ioda.m32_size / phb->ioda.total_pe;
	phb->ioda.m32_pci_base = hose->mem_resources[0].start -
		hose->pci_mem_offset;
	phb->ioda.io_size = hose->pci_io_size;
	phb->ioda.io_segsize = phb->ioda.io_size / phb->ioda.total_pe;
	phb->ioda.io_pci_base = 0; /* XXX calculate this ? */

	/* Allocate aux data & arrays */
	size = _ALIGN_UP(phb->ioda.total_pe / 8, sizeof(unsigned long));
	m32map_off = size;
	size += phb->ioda.total_pe * sizeof(phb->ioda.m32_segmap[0]);
	iomap_off = size;
	size += phb->ioda.total_pe * sizeof(phb->ioda.io_segmap[0]);
	pemap_off = size;
	size += phb->ioda.total_pe * sizeof(struct pnv_ioda_pe);
	aux = alloc_bootmem(size);
	memset(aux, 0, size);
	phb->ioda.pe_alloc = aux;
	phb->ioda.m32_segmap = aux + m32map_off;
	phb->ioda.io_segmap = aux + iomap_off;
	phb->ioda.pe_array = aux + pemap_off;
	set_bit(0, phb->ioda.pe_alloc);

	INIT_LIST_HEAD(&phb->ioda.pe_dma_list);
	INIT_LIST_HEAD(&phb->ioda.pe_list);

	/* Calculate how many 32-bit TCE segments we have */
	phb->ioda.tce32_count = phb->ioda.m32_pci_base >> 28;

	/* Clear unusable m64 */
	hose->mem_resources[1].flags = 0;
	hose->mem_resources[1].start = 0;
	hose->mem_resources[1].end = 0;
	hose->mem_resources[2].flags = 0;
	hose->mem_resources[2].start = 0;
	hose->mem_resources[2].end = 0;

#if 0
	rc = opal_pci_set_phb_mem_window(opal->phb_id,
					 window_type,
					 window_num,
					 starting_real_address,
					 starting_pci_address,
					 segment_size);
#endif

	pr_info("  %d PE's M32: 0x%x [segment=0x%x] IO: 0x%x [segment=0x%x]\n",
		phb->ioda.total_pe,
		phb->ioda.m32_size, phb->ioda.m32_segsize,
		phb->ioda.io_size, phb->ioda.io_segsize);

	if (phb->regs)  {
		pr_devel(" BUID     = 0x%016llx\n", in_be64(phb->regs + 0x100));
		pr_devel(" PHB2_CR  = 0x%016llx\n", in_be64(phb->regs + 0x160));
		pr_devel(" IO_BAR   = 0x%016llx\n", in_be64(phb->regs + 0x170));
		pr_devel(" IO_BAMR  = 0x%016llx\n", in_be64(phb->regs + 0x178));
		pr_devel(" IO_SAR   = 0x%016llx\n", in_be64(phb->regs + 0x180));
		pr_devel(" M32_BAR  = 0x%016llx\n", in_be64(phb->regs + 0x190));
		pr_devel(" M32_BAMR = 0x%016llx\n", in_be64(phb->regs + 0x198));
		pr_devel(" M32_SAR  = 0x%016llx\n", in_be64(phb->regs + 0x1a0));
	}
	phb->hose->ops = &pnv_pci_ops;

	/* Setup RID -> PE mapping function */
	phb->bdfn_to_pe = pnv_ioda_bdfn_to_pe;

	/* Setup TCEs */
	phb->dma_dev_setup = pnv_pci_ioda_dma_dev_setup;

	/* Setup MSI support */
	pnv_pci_init_ioda_msis(phb);

	/* We set both PCI_PROBE_ONLY and PCI_REASSIGN_ALL_RSRC. This is an
	 * odd combination which essentially means that we skip all resource
	 * fixups and assignments in the generic code, and do it all
	 * ourselves here
	 */
	ppc_md.pcibios_fixup_phb = pnv_pci_ioda_fixup_phb;
	ppc_md.pcibios_fixup = pnv_pci_ioda_fixup;
	ppc_md.pcibios_enable_device_hook = pnv_pci_enable_device_hook;
	ppc_md.pcibios_window_alignment = pnv_pci_window_alignment;
	pci_add_flags(PCI_PROBE_ONLY | PCI_REASSIGN_ALL_RSRC);

	/* Reset IODA tables to a clean state */
	rc = opal_pci_reset(phb_id, OPAL_PCI_IODA_TABLE_RESET, OPAL_ASSERT_RESET);
	if (rc)
		pr_warning("  OPAL Error %ld performing IODA table reset !\n", rc);
	opal_pci_set_pe(phb_id, 0, 0, 7, 1, 1 , OPAL_MAP_PE);
}

void __init pnv_pci_init_ioda_hub(struct device_node *np)
{
	struct device_node *phbn;
	const u64 *prop64;
	u64 hub_id;

	pr_info("Probing IODA IO-Hub %s\n", np->full_name);

	prop64 = of_get_property(np, "ibm,opal-hubid", NULL);
	if (!prop64) {
		pr_err(" Missing \"ibm,opal-hubid\" property !\n");
		return;
	}
	hub_id = be64_to_cpup(prop64);
	pr_devel(" HUB-ID : 0x%016llx\n", hub_id);

	/* Count child PHBs */
	for_each_child_of_node(np, phbn) {
		/* Look for IODA1 PHBs */
		if (of_device_is_compatible(phbn, "ibm,ioda-phb"))
			pnv_pci_init_ioda1_phb(phbn);
	}
}
