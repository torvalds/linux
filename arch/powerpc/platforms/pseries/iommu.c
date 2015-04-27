/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup:
 *
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 * Copyright (C) 2006 Olof Johansson <olof@lixom.net>
 *
 * Dynamic DMA mapping support, pSeries-specific parts, both SMP and LPAR.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/memblock.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/crash_dump.h>
#include <linux/memory.h>
#include <linux/of.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/tce.h>
#include <asm/ppc-pci.h>
#include <asm/udbg.h>
#include <asm/mmzone.h>
#include <asm/plpar_wrappers.h>

#include "pseries.h"

static void tce_invalidate_pSeries_sw(struct iommu_table *tbl,
				      __be64 *startp, __be64 *endp)
{
	u64 __iomem *invalidate = (u64 __iomem *)tbl->it_index;
	unsigned long start, end, inc;

	start = __pa(startp);
	end = __pa(endp);
	inc = L1_CACHE_BYTES; /* invalidate a cacheline of TCEs at a time */

	/* If this is non-zero, change the format.  We shift the
	 * address and or in the magic from the device tree. */
	if (tbl->it_busno) {
		start <<= 12;
		end <<= 12;
		inc <<= 12;
		start |= tbl->it_busno;
		end |= tbl->it_busno;
	}

	end |= inc - 1; /* round up end to be different than start */

	mb(); /* Make sure TCEs in memory are written */
	while (start <= end) {
		out_be64(invalidate, start);
		start += inc;
	}
}

static int tce_build_pSeries(struct iommu_table *tbl, long index,
			      long npages, unsigned long uaddr,
			      enum dma_data_direction direction,
			      struct dma_attrs *attrs)
{
	u64 proto_tce;
	__be64 *tcep, *tces;
	u64 rpn;

	proto_tce = TCE_PCI_READ; // Read allowed

	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	tces = tcep = ((__be64 *)tbl->it_base) + index;

	while (npages--) {
		/* can't move this out since we might cross MEMBLOCK boundary */
		rpn = __pa(uaddr) >> TCE_SHIFT;
		*tcep = cpu_to_be64(proto_tce | (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT);

		uaddr += TCE_PAGE_SIZE;
		tcep++;
	}

	if (tbl->it_type & TCE_PCI_SWINV_CREATE)
		tce_invalidate_pSeries_sw(tbl, tces, tcep - 1);
	return 0;
}


static void tce_free_pSeries(struct iommu_table *tbl, long index, long npages)
{
	__be64 *tcep, *tces;

	tces = tcep = ((__be64 *)tbl->it_base) + index;

	while (npages--)
		*(tcep++) = 0;

	if (tbl->it_type & TCE_PCI_SWINV_FREE)
		tce_invalidate_pSeries_sw(tbl, tces, tcep - 1);
}

static unsigned long tce_get_pseries(struct iommu_table *tbl, long index)
{
	__be64 *tcep;

	tcep = ((__be64 *)tbl->it_base) + index;

	return be64_to_cpu(*tcep);
}

static void tce_free_pSeriesLP(struct iommu_table*, long, long);
static void tce_freemulti_pSeriesLP(struct iommu_table*, long, long);

static int tce_build_pSeriesLP(struct iommu_table *tbl, long tcenum,
				long npages, unsigned long uaddr,
				enum dma_data_direction direction,
				struct dma_attrs *attrs)
{
	u64 rc = 0;
	u64 proto_tce, tce;
	u64 rpn;
	int ret = 0;
	long tcenum_start = tcenum, npages_start = npages;

	rpn = __pa(uaddr) >> TCE_SHIFT;
	proto_tce = TCE_PCI_READ;
	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	while (npages--) {
		tce = proto_tce | (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT;
		rc = plpar_tce_put((u64)tbl->it_index, (u64)tcenum << 12, tce);

		if (unlikely(rc == H_NOT_ENOUGH_RESOURCES)) {
			ret = (int)rc;
			tce_free_pSeriesLP(tbl, tcenum_start,
			                   (npages_start - (npages + 1)));
			break;
		}

		if (rc && printk_ratelimit()) {
			printk("tce_build_pSeriesLP: plpar_tce_put failed. rc=%lld\n", rc);
			printk("\tindex   = 0x%llx\n", (u64)tbl->it_index);
			printk("\ttcenum  = 0x%llx\n", (u64)tcenum);
			printk("\ttce val = 0x%llx\n", tce );
			dump_stack();
		}

		tcenum++;
		rpn++;
	}
	return ret;
}

static DEFINE_PER_CPU(__be64 *, tce_page);

static int tce_buildmulti_pSeriesLP(struct iommu_table *tbl, long tcenum,
				     long npages, unsigned long uaddr,
				     enum dma_data_direction direction,
				     struct dma_attrs *attrs)
{
	u64 rc = 0;
	u64 proto_tce;
	__be64 *tcep;
	u64 rpn;
	long l, limit;
	long tcenum_start = tcenum, npages_start = npages;
	int ret = 0;
	unsigned long flags;

	if (npages == 1) {
		return tce_build_pSeriesLP(tbl, tcenum, npages, uaddr,
		                           direction, attrs);
	}

	local_irq_save(flags);	/* to protect tcep and the page behind it */

	tcep = __this_cpu_read(tce_page);

	/* This is safe to do since interrupts are off when we're called
	 * from iommu_alloc{,_sg}()
	 */
	if (!tcep) {
		tcep = (__be64 *)__get_free_page(GFP_ATOMIC);
		/* If allocation fails, fall back to the loop implementation */
		if (!tcep) {
			local_irq_restore(flags);
			return tce_build_pSeriesLP(tbl, tcenum, npages, uaddr,
					    direction, attrs);
		}
		__this_cpu_write(tce_page, tcep);
	}

	rpn = __pa(uaddr) >> TCE_SHIFT;
	proto_tce = TCE_PCI_READ;
	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	/* We can map max one pageful of TCEs at a time */
	do {
		/*
		 * Set up the page with TCE data, looping through and setting
		 * the values.
		 */
		limit = min_t(long, npages, 4096/TCE_ENTRY_SIZE);

		for (l = 0; l < limit; l++) {
			tcep[l] = cpu_to_be64(proto_tce | (rpn & TCE_RPN_MASK) << TCE_RPN_SHIFT);
			rpn++;
		}

		rc = plpar_tce_put_indirect((u64)tbl->it_index,
					    (u64)tcenum << 12,
					    (u64)__pa(tcep),
					    limit);

		npages -= limit;
		tcenum += limit;
	} while (npages > 0 && !rc);

	local_irq_restore(flags);

	if (unlikely(rc == H_NOT_ENOUGH_RESOURCES)) {
		ret = (int)rc;
		tce_freemulti_pSeriesLP(tbl, tcenum_start,
		                        (npages_start - (npages + limit)));
		return ret;
	}

	if (rc && printk_ratelimit()) {
		printk("tce_buildmulti_pSeriesLP: plpar_tce_put failed. rc=%lld\n", rc);
		printk("\tindex   = 0x%llx\n", (u64)tbl->it_index);
		printk("\tnpages  = 0x%llx\n", (u64)npages);
		printk("\ttce[0] val = 0x%llx\n", tcep[0]);
		dump_stack();
	}
	return ret;
}

static void tce_free_pSeriesLP(struct iommu_table *tbl, long tcenum, long npages)
{
	u64 rc;

	while (npages--) {
		rc = plpar_tce_put((u64)tbl->it_index, (u64)tcenum << 12, 0);

		if (rc && printk_ratelimit()) {
			printk("tce_free_pSeriesLP: plpar_tce_put failed. rc=%lld\n", rc);
			printk("\tindex   = 0x%llx\n", (u64)tbl->it_index);
			printk("\ttcenum  = 0x%llx\n", (u64)tcenum);
			dump_stack();
		}

		tcenum++;
	}
}


static void tce_freemulti_pSeriesLP(struct iommu_table *tbl, long tcenum, long npages)
{
	u64 rc;

	rc = plpar_tce_stuff((u64)tbl->it_index, (u64)tcenum << 12, 0, npages);

	if (rc && printk_ratelimit()) {
		printk("tce_freemulti_pSeriesLP: plpar_tce_stuff failed\n");
		printk("\trc      = %lld\n", rc);
		printk("\tindex   = 0x%llx\n", (u64)tbl->it_index);
		printk("\tnpages  = 0x%llx\n", (u64)npages);
		dump_stack();
	}
}

static unsigned long tce_get_pSeriesLP(struct iommu_table *tbl, long tcenum)
{
	u64 rc;
	unsigned long tce_ret;

	rc = plpar_tce_get((u64)tbl->it_index, (u64)tcenum << 12, &tce_ret);

	if (rc && printk_ratelimit()) {
		printk("tce_get_pSeriesLP: plpar_tce_get failed. rc=%lld\n", rc);
		printk("\tindex   = 0x%llx\n", (u64)tbl->it_index);
		printk("\ttcenum  = 0x%llx\n", (u64)tcenum);
		dump_stack();
	}

	return tce_ret;
}

/* this is compatible with cells for the device tree property */
struct dynamic_dma_window_prop {
	__be32	liobn;		/* tce table number */
	__be64	dma_base;	/* address hi,lo */
	__be32	tce_shift;	/* ilog2(tce_page_size) */
	__be32	window_shift;	/* ilog2(tce_window_size) */
};

struct direct_window {
	struct device_node *device;
	const struct dynamic_dma_window_prop *prop;
	struct list_head list;
};

/* Dynamic DMA Window support */
struct ddw_query_response {
	u32 windows_available;
	u32 largest_available_block;
	u32 page_size;
	u32 migration_capable;
};

struct ddw_create_response {
	u32 liobn;
	u32 addr_hi;
	u32 addr_lo;
};

static LIST_HEAD(direct_window_list);
/* prevents races between memory on/offline and window creation */
static DEFINE_SPINLOCK(direct_window_list_lock);
/* protects initializing window twice for same device */
static DEFINE_MUTEX(direct_window_init_mutex);
#define DIRECT64_PROPNAME "linux,direct64-ddr-window-info"

static int tce_clearrange_multi_pSeriesLP(unsigned long start_pfn,
					unsigned long num_pfn, const void *arg)
{
	const struct dynamic_dma_window_prop *maprange = arg;
	int rc;
	u64 tce_size, num_tce, dma_offset, next;
	u32 tce_shift;
	long limit;

	tce_shift = be32_to_cpu(maprange->tce_shift);
	tce_size = 1ULL << tce_shift;
	next = start_pfn << PAGE_SHIFT;
	num_tce = num_pfn << PAGE_SHIFT;

	/* round back to the beginning of the tce page size */
	num_tce += next & (tce_size - 1);
	next &= ~(tce_size - 1);

	/* covert to number of tces */
	num_tce |= tce_size - 1;
	num_tce >>= tce_shift;

	do {
		/*
		 * Set up the page with TCE data, looping through and setting
		 * the values.
		 */
		limit = min_t(long, num_tce, 512);
		dma_offset = next + be64_to_cpu(maprange->dma_base);

		rc = plpar_tce_stuff((u64)be32_to_cpu(maprange->liobn),
					     dma_offset,
					     0, limit);
		next += limit * tce_size;
		num_tce -= limit;
	} while (num_tce > 0 && !rc);

	return rc;
}

static int tce_setrange_multi_pSeriesLP(unsigned long start_pfn,
					unsigned long num_pfn, const void *arg)
{
	const struct dynamic_dma_window_prop *maprange = arg;
	u64 tce_size, num_tce, dma_offset, next, proto_tce, liobn;
	__be64 *tcep;
	u32 tce_shift;
	u64 rc = 0;
	long l, limit;

	local_irq_disable();	/* to protect tcep and the page behind it */
	tcep = __this_cpu_read(tce_page);

	if (!tcep) {
		tcep = (__be64 *)__get_free_page(GFP_ATOMIC);
		if (!tcep) {
			local_irq_enable();
			return -ENOMEM;
		}
		__this_cpu_write(tce_page, tcep);
	}

	proto_tce = TCE_PCI_READ | TCE_PCI_WRITE;

	liobn = (u64)be32_to_cpu(maprange->liobn);
	tce_shift = be32_to_cpu(maprange->tce_shift);
	tce_size = 1ULL << tce_shift;
	next = start_pfn << PAGE_SHIFT;
	num_tce = num_pfn << PAGE_SHIFT;

	/* round back to the beginning of the tce page size */
	num_tce += next & (tce_size - 1);
	next &= ~(tce_size - 1);

	/* covert to number of tces */
	num_tce |= tce_size - 1;
	num_tce >>= tce_shift;

	/* We can map max one pageful of TCEs at a time */
	do {
		/*
		 * Set up the page with TCE data, looping through and setting
		 * the values.
		 */
		limit = min_t(long, num_tce, 4096/TCE_ENTRY_SIZE);
		dma_offset = next + be64_to_cpu(maprange->dma_base);

		for (l = 0; l < limit; l++) {
			tcep[l] = cpu_to_be64(proto_tce | next);
			next += tce_size;
		}

		rc = plpar_tce_put_indirect(liobn,
					    dma_offset,
					    (u64)__pa(tcep),
					    limit);

		num_tce -= limit;
	} while (num_tce > 0 && !rc);

	/* error cleanup: caller will clear whole range */

	local_irq_enable();
	return rc;
}

static int tce_setrange_multi_pSeriesLP_walk(unsigned long start_pfn,
		unsigned long num_pfn, void *arg)
{
	return tce_setrange_multi_pSeriesLP(start_pfn, num_pfn, arg);
}


#ifdef CONFIG_PCI
static void iommu_table_setparms(struct pci_controller *phb,
				 struct device_node *dn,
				 struct iommu_table *tbl)
{
	struct device_node *node;
	const unsigned long *basep, *sw_inval;
	const u32 *sizep;

	node = phb->dn;

	basep = of_get_property(node, "linux,tce-base", NULL);
	sizep = of_get_property(node, "linux,tce-size", NULL);
	if (basep == NULL || sizep == NULL) {
		printk(KERN_ERR "PCI_DMA: iommu_table_setparms: %s has "
				"missing tce entries !\n", dn->full_name);
		return;
	}

	tbl->it_base = (unsigned long)__va(*basep);

	if (!is_kdump_kernel())
		memset((void *)tbl->it_base, 0, *sizep);

	tbl->it_busno = phb->bus->number;
	tbl->it_page_shift = IOMMU_PAGE_SHIFT_4K;

	/* Units of tce entries */
	tbl->it_offset = phb->dma_window_base_cur >> tbl->it_page_shift;

	/* Test if we are going over 2GB of DMA space */
	if (phb->dma_window_base_cur + phb->dma_window_size > 0x80000000ul) {
		udbg_printf("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
		panic("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
	}

	phb->dma_window_base_cur += phb->dma_window_size;

	/* Set the tce table size - measured in entries */
	tbl->it_size = phb->dma_window_size >> tbl->it_page_shift;

	tbl->it_index = 0;
	tbl->it_blocksize = 16;
	tbl->it_type = TCE_PCI;

	sw_inval = of_get_property(node, "linux,tce-sw-invalidate-info", NULL);
	if (sw_inval) {
		/*
		 * This property contains information on how to
		 * invalidate the TCE entry.  The first property is
		 * the base MMIO address used to invalidate entries.
		 * The second property tells us the format of the TCE
		 * invalidate (whether it needs to be shifted) and
		 * some magic routing info to add to our invalidate
		 * command.
		 */
		tbl->it_index = (unsigned long) ioremap(sw_inval[0], 8);
		tbl->it_busno = sw_inval[1]; /* overload this with magic */
		tbl->it_type = TCE_PCI_SWINV_CREATE | TCE_PCI_SWINV_FREE;
	}
}

/*
 * iommu_table_setparms_lpar
 *
 * Function: On pSeries LPAR systems, return TCE table info, given a pci bus.
 */
static void iommu_table_setparms_lpar(struct pci_controller *phb,
				      struct device_node *dn,
				      struct iommu_table *tbl,
				      const __be32 *dma_window)
{
	unsigned long offset, size;

	of_parse_dma_window(dn, dma_window, &tbl->it_index, &offset, &size);

	tbl->it_busno = phb->bus->number;
	tbl->it_page_shift = IOMMU_PAGE_SHIFT_4K;
	tbl->it_base   = 0;
	tbl->it_blocksize  = 16;
	tbl->it_type = TCE_PCI;
	tbl->it_offset = offset >> tbl->it_page_shift;
	tbl->it_size = size >> tbl->it_page_shift;
}

static void pci_dma_bus_setup_pSeries(struct pci_bus *bus)
{
	struct device_node *dn;
	struct iommu_table *tbl;
	struct device_node *isa_dn, *isa_dn_orig;
	struct device_node *tmp;
	struct pci_dn *pci;
	int children;

	dn = pci_bus_to_OF_node(bus);

	pr_debug("pci_dma_bus_setup_pSeries: setting up bus %s\n", dn->full_name);

	if (bus->self) {
		/* This is not a root bus, any setup will be done for the
		 * device-side of the bridge in iommu_dev_setup_pSeries().
		 */
		return;
	}
	pci = PCI_DN(dn);

	/* Check if the ISA bus on the system is under
	 * this PHB.
	 */
	isa_dn = isa_dn_orig = of_find_node_by_type(NULL, "isa");

	while (isa_dn && isa_dn != dn)
		isa_dn = isa_dn->parent;

	of_node_put(isa_dn_orig);

	/* Count number of direct PCI children of the PHB. */
	for (children = 0, tmp = dn->child; tmp; tmp = tmp->sibling)
		children++;

	pr_debug("Children: %d\n", children);

	/* Calculate amount of DMA window per slot. Each window must be
	 * a power of two (due to pci_alloc_consistent requirements).
	 *
	 * Keep 256MB aside for PHBs with ISA.
	 */

	if (!isa_dn) {
		/* No ISA/IDE - just set window size and return */
		pci->phb->dma_window_size = 0x80000000ul; /* To be divided */

		while (pci->phb->dma_window_size * children > 0x80000000ul)
			pci->phb->dma_window_size >>= 1;
		pr_debug("No ISA/IDE, window size is 0x%llx\n",
			 pci->phb->dma_window_size);
		pci->phb->dma_window_base_cur = 0;

		return;
	}

	/* If we have ISA, then we probably have an IDE
	 * controller too. Allocate a 128MB table but
	 * skip the first 128MB to avoid stepping on ISA
	 * space.
	 */
	pci->phb->dma_window_size = 0x8000000ul;
	pci->phb->dma_window_base_cur = 0x8000000ul;

	tbl = kzalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
			   pci->phb->node);

	iommu_table_setparms(pci->phb, dn, tbl);
	pci->iommu_table = iommu_init_table(tbl, pci->phb->node);
	iommu_register_group(tbl, pci_domain_nr(bus), 0);

	/* Divide the rest (1.75GB) among the children */
	pci->phb->dma_window_size = 0x80000000ul;
	while (pci->phb->dma_window_size * children > 0x70000000ul)
		pci->phb->dma_window_size >>= 1;

	pr_debug("ISA/IDE, window size is 0x%llx\n", pci->phb->dma_window_size);
}


static void pci_dma_bus_setup_pSeriesLP(struct pci_bus *bus)
{
	struct iommu_table *tbl;
	struct device_node *dn, *pdn;
	struct pci_dn *ppci;
	const __be32 *dma_window = NULL;

	dn = pci_bus_to_OF_node(bus);

	pr_debug("pci_dma_bus_setup_pSeriesLP: setting up bus %s\n",
		 dn->full_name);

	/* Find nearest ibm,dma-window, walking up the device tree */
	for (pdn = dn; pdn != NULL; pdn = pdn->parent) {
		dma_window = of_get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window != NULL)
			break;
	}

	if (dma_window == NULL) {
		pr_debug("  no ibm,dma-window property !\n");
		return;
	}

	ppci = PCI_DN(pdn);

	pr_debug("  parent is %s, iommu_table: 0x%p\n",
		 pdn->full_name, ppci->iommu_table);

	if (!ppci->iommu_table) {
		tbl = kzalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   ppci->phb->node);
		iommu_table_setparms_lpar(ppci->phb, pdn, tbl, dma_window);
		ppci->iommu_table = iommu_init_table(tbl, ppci->phb->node);
		iommu_register_group(tbl, pci_domain_nr(bus), 0);
		pr_debug("  created table: %p\n", ppci->iommu_table);
	}
}


static void pci_dma_dev_setup_pSeries(struct pci_dev *dev)
{
	struct device_node *dn;
	struct iommu_table *tbl;

	pr_debug("pci_dma_dev_setup_pSeries: %s\n", pci_name(dev));

	dn = dev->dev.of_node;

	/* If we're the direct child of a root bus, then we need to allocate
	 * an iommu table ourselves. The bus setup code should have setup
	 * the window sizes already.
	 */
	if (!dev->bus->self) {
		struct pci_controller *phb = PCI_DN(dn)->phb;

		pr_debug(" --> first child, no bridge. Allocating iommu table.\n");
		tbl = kzalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   phb->node);
		iommu_table_setparms(phb, dn, tbl);
		PCI_DN(dn)->iommu_table = iommu_init_table(tbl, phb->node);
		iommu_register_group(tbl, pci_domain_nr(phb->bus), 0);
		set_iommu_table_base_and_group(&dev->dev,
					       PCI_DN(dn)->iommu_table);
		return;
	}

	/* If this device is further down the bus tree, search upwards until
	 * an already allocated iommu table is found and use that.
	 */

	while (dn && PCI_DN(dn) && PCI_DN(dn)->iommu_table == NULL)
		dn = dn->parent;

	if (dn && PCI_DN(dn))
		set_iommu_table_base_and_group(&dev->dev,
					       PCI_DN(dn)->iommu_table);
	else
		printk(KERN_WARNING "iommu: Device %s has no iommu table\n",
		       pci_name(dev));
}

static int __read_mostly disable_ddw;

static int __init disable_ddw_setup(char *str)
{
	disable_ddw = 1;
	printk(KERN_INFO "ppc iommu: disabling ddw.\n");

	return 0;
}

early_param("disable_ddw", disable_ddw_setup);

static void remove_ddw(struct device_node *np, bool remove_prop)
{
	struct dynamic_dma_window_prop *dwp;
	struct property *win64;
	u32 ddw_avail[3];
	u64 liobn;
	int ret = 0;

	ret = of_property_read_u32_array(np, "ibm,ddw-applicable",
					 &ddw_avail[0], 3);

	win64 = of_find_property(np, DIRECT64_PROPNAME, NULL);
	if (!win64)
		return;

	if (ret || win64->length < sizeof(*dwp))
		goto delprop;

	dwp = win64->value;
	liobn = (u64)be32_to_cpu(dwp->liobn);

	/* clear the whole window, note the arg is in kernel pages */
	ret = tce_clearrange_multi_pSeriesLP(0,
		1ULL << (be32_to_cpu(dwp->window_shift) - PAGE_SHIFT), dwp);
	if (ret)
		pr_warning("%s failed to clear tces in window.\n",
			 np->full_name);
	else
		pr_debug("%s successfully cleared tces in window.\n",
			 np->full_name);

	ret = rtas_call(ddw_avail[2], 1, 1, NULL, liobn);
	if (ret)
		pr_warning("%s: failed to remove direct window: rtas returned "
			"%d to ibm,remove-pe-dma-window(%x) %llx\n",
			np->full_name, ret, ddw_avail[2], liobn);
	else
		pr_debug("%s: successfully removed direct window: rtas returned "
			"%d to ibm,remove-pe-dma-window(%x) %llx\n",
			np->full_name, ret, ddw_avail[2], liobn);

delprop:
	if (remove_prop)
		ret = of_remove_property(np, win64);
	if (ret)
		pr_warning("%s: failed to remove direct window property: %d\n",
			np->full_name, ret);
}

static u64 find_existing_ddw(struct device_node *pdn)
{
	struct direct_window *window;
	const struct dynamic_dma_window_prop *direct64;
	u64 dma_addr = 0;

	spin_lock(&direct_window_list_lock);
	/* check if we already created a window and dupe that config if so */
	list_for_each_entry(window, &direct_window_list, list) {
		if (window->device == pdn) {
			direct64 = window->prop;
			dma_addr = be64_to_cpu(direct64->dma_base);
			break;
		}
	}
	spin_unlock(&direct_window_list_lock);

	return dma_addr;
}

static int find_existing_ddw_windows(void)
{
	int len;
	struct device_node *pdn;
	struct direct_window *window;
	const struct dynamic_dma_window_prop *direct64;

	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	for_each_node_with_property(pdn, DIRECT64_PROPNAME) {
		direct64 = of_get_property(pdn, DIRECT64_PROPNAME, &len);
		if (!direct64)
			continue;

		window = kzalloc(sizeof(*window), GFP_KERNEL);
		if (!window || len < sizeof(struct dynamic_dma_window_prop)) {
			kfree(window);
			remove_ddw(pdn, true);
			continue;
		}

		window->device = pdn;
		window->prop = direct64;
		spin_lock(&direct_window_list_lock);
		list_add(&window->list, &direct_window_list);
		spin_unlock(&direct_window_list_lock);
	}

	return 0;
}
machine_arch_initcall(pseries, find_existing_ddw_windows);

static int query_ddw(struct pci_dev *dev, const u32 *ddw_avail,
			struct ddw_query_response *query)
{
	struct eeh_dev *edev;
	u32 cfg_addr;
	u64 buid;
	int ret;

	/*
	 * Get the config address and phb buid of the PE window.
	 * Rely on eeh to retrieve this for us.
	 * Retrieve them from the pci device, not the node with the
	 * dma-window property
	 */
	edev = pci_dev_to_eeh_dev(dev);
	cfg_addr = edev->config_addr;
	if (edev->pe_config_addr)
		cfg_addr = edev->pe_config_addr;
	buid = edev->phb->buid;

	ret = rtas_call(ddw_avail[0], 3, 5, (u32 *)query,
		  cfg_addr, BUID_HI(buid), BUID_LO(buid));
	dev_info(&dev->dev, "ibm,query-pe-dma-windows(%x) %x %x %x"
		" returned %d\n", ddw_avail[0], cfg_addr, BUID_HI(buid),
		BUID_LO(buid), ret);
	return ret;
}

static int create_ddw(struct pci_dev *dev, const u32 *ddw_avail,
			struct ddw_create_response *create, int page_shift,
			int window_shift)
{
	struct eeh_dev *edev;
	u32 cfg_addr;
	u64 buid;
	int ret;

	/*
	 * Get the config address and phb buid of the PE window.
	 * Rely on eeh to retrieve this for us.
	 * Retrieve them from the pci device, not the node with the
	 * dma-window property
	 */
	edev = pci_dev_to_eeh_dev(dev);
	cfg_addr = edev->config_addr;
	if (edev->pe_config_addr)
		cfg_addr = edev->pe_config_addr;
	buid = edev->phb->buid;

	do {
		/* extra outputs are LIOBN and dma-addr (hi, lo) */
		ret = rtas_call(ddw_avail[1], 5, 4, (u32 *)create,
				cfg_addr, BUID_HI(buid), BUID_LO(buid),
				page_shift, window_shift);
	} while (rtas_busy_delay(ret));
	dev_info(&dev->dev,
		"ibm,create-pe-dma-window(%x) %x %x %x %x %x returned %d "
		"(liobn = 0x%x starting addr = %x %x)\n", ddw_avail[1],
		 cfg_addr, BUID_HI(buid), BUID_LO(buid), page_shift,
		 window_shift, ret, create->liobn, create->addr_hi, create->addr_lo);

	return ret;
}

struct failed_ddw_pdn {
	struct device_node *pdn;
	struct list_head list;
};

static LIST_HEAD(failed_ddw_pdn_list);

/*
 * If the PE supports dynamic dma windows, and there is space for a table
 * that can map all pages in a linear offset, then setup such a table,
 * and record the dma-offset in the struct device.
 *
 * dev: the pci device we are checking
 * pdn: the parent pe node with the ibm,dma_window property
 * Future: also check if we can remap the base window for our base page size
 *
 * returns the dma offset for use by dma_set_mask
 */
static u64 enable_ddw(struct pci_dev *dev, struct device_node *pdn)
{
	int len, ret;
	struct ddw_query_response query;
	struct ddw_create_response create;
	int page_shift;
	u64 dma_addr, max_addr;
	struct device_node *dn;
	u32 ddw_avail[3];
	struct direct_window *window;
	struct property *win64;
	struct dynamic_dma_window_prop *ddwprop;
	struct failed_ddw_pdn *fpdn;

	mutex_lock(&direct_window_init_mutex);

	dma_addr = find_existing_ddw(pdn);
	if (dma_addr != 0)
		goto out_unlock;

	/*
	 * If we already went through this for a previous function of
	 * the same device and failed, we don't want to muck with the
	 * DMA window again, as it will race with in-flight operations
	 * and can lead to EEHs. The above mutex protects access to the
	 * list.
	 */
	list_for_each_entry(fpdn, &failed_ddw_pdn_list, list) {
		if (!strcmp(fpdn->pdn->full_name, pdn->full_name))
			goto out_unlock;
	}

	/*
	 * the ibm,ddw-applicable property holds the tokens for:
	 * ibm,query-pe-dma-window
	 * ibm,create-pe-dma-window
	 * ibm,remove-pe-dma-window
	 * for the given node in that order.
	 * the property is actually in the parent, not the PE
	 */
	ret = of_property_read_u32_array(pdn, "ibm,ddw-applicable",
					 &ddw_avail[0], 3);
	if (ret)
		goto out_failed;

       /*
	 * Query if there is a second window of size to map the
	 * whole partition.  Query returns number of windows, largest
	 * block assigned to PE (partition endpoint), and two bitmasks
	 * of page sizes: supported and supported for migrate-dma.
	 */
	dn = pci_device_to_OF_node(dev);
	ret = query_ddw(dev, ddw_avail, &query);
	if (ret != 0)
		goto out_failed;

	if (query.windows_available == 0) {
		/*
		 * no additional windows are available for this device.
		 * We might be able to reallocate the existing window,
		 * trading in for a larger page size.
		 */
		dev_dbg(&dev->dev, "no free dynamic windows");
		goto out_failed;
	}
	if (query.page_size & 4) {
		page_shift = 24; /* 16MB */
	} else if (query.page_size & 2) {
		page_shift = 16; /* 64kB */
	} else if (query.page_size & 1) {
		page_shift = 12; /* 4kB */
	} else {
		dev_dbg(&dev->dev, "no supported direct page size in mask %x",
			  query.page_size);
		goto out_failed;
	}
	/* verify the window * number of ptes will map the partition */
	/* check largest block * page size > max memory hotplug addr */
	max_addr = memory_hotplug_max();
	if (query.largest_available_block < (max_addr >> page_shift)) {
		dev_dbg(&dev->dev, "can't map partiton max 0x%llx with %u "
			  "%llu-sized pages\n", max_addr,  query.largest_available_block,
			  1ULL << page_shift);
		goto out_failed;
	}
	len = order_base_2(max_addr);
	win64 = kzalloc(sizeof(struct property), GFP_KERNEL);
	if (!win64) {
		dev_info(&dev->dev,
			"couldn't allocate property for 64bit dma window\n");
		goto out_failed;
	}
	win64->name = kstrdup(DIRECT64_PROPNAME, GFP_KERNEL);
	win64->value = ddwprop = kmalloc(sizeof(*ddwprop), GFP_KERNEL);
	win64->length = sizeof(*ddwprop);
	if (!win64->name || !win64->value) {
		dev_info(&dev->dev,
			"couldn't allocate property name and value\n");
		goto out_free_prop;
	}

	ret = create_ddw(dev, ddw_avail, &create, page_shift, len);
	if (ret != 0)
		goto out_free_prop;

	ddwprop->liobn = cpu_to_be32(create.liobn);
	ddwprop->dma_base = cpu_to_be64(((u64)create.addr_hi << 32) |
			create.addr_lo);
	ddwprop->tce_shift = cpu_to_be32(page_shift);
	ddwprop->window_shift = cpu_to_be32(len);

	dev_dbg(&dev->dev, "created tce table LIOBN 0x%x for %s\n",
		  create.liobn, dn->full_name);

	window = kzalloc(sizeof(*window), GFP_KERNEL);
	if (!window)
		goto out_clear_window;

	ret = walk_system_ram_range(0, memblock_end_of_DRAM() >> PAGE_SHIFT,
			win64->value, tce_setrange_multi_pSeriesLP_walk);
	if (ret) {
		dev_info(&dev->dev, "failed to map direct window for %s: %d\n",
			 dn->full_name, ret);
		goto out_free_window;
	}

	ret = of_add_property(pdn, win64);
	if (ret) {
		dev_err(&dev->dev, "unable to add dma window property for %s: %d",
			 pdn->full_name, ret);
		goto out_free_window;
	}

	window->device = pdn;
	window->prop = ddwprop;
	spin_lock(&direct_window_list_lock);
	list_add(&window->list, &direct_window_list);
	spin_unlock(&direct_window_list_lock);

	dma_addr = be64_to_cpu(ddwprop->dma_base);
	goto out_unlock;

out_free_window:
	kfree(window);

out_clear_window:
	remove_ddw(pdn, true);

out_free_prop:
	kfree(win64->name);
	kfree(win64->value);
	kfree(win64);

out_failed:

	fpdn = kzalloc(sizeof(*fpdn), GFP_KERNEL);
	if (!fpdn)
		goto out_unlock;
	fpdn->pdn = pdn;
	list_add(&fpdn->list, &failed_ddw_pdn_list);

out_unlock:
	mutex_unlock(&direct_window_init_mutex);
	return dma_addr;
}

static void pci_dma_dev_setup_pSeriesLP(struct pci_dev *dev)
{
	struct device_node *pdn, *dn;
	struct iommu_table *tbl;
	const __be32 *dma_window = NULL;
	struct pci_dn *pci;

	pr_debug("pci_dma_dev_setup_pSeriesLP: %s\n", pci_name(dev));

	/* dev setup for LPAR is a little tricky, since the device tree might
	 * contain the dma-window properties per-device and not necessarily
	 * for the bus. So we need to search upwards in the tree until we
	 * either hit a dma-window property, OR find a parent with a table
	 * already allocated.
	 */
	dn = pci_device_to_OF_node(dev);
	pr_debug("  node is %s\n", dn->full_name);

	for (pdn = dn; pdn && PCI_DN(pdn) && !PCI_DN(pdn)->iommu_table;
	     pdn = pdn->parent) {
		dma_window = of_get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window)
			break;
	}

	if (!pdn || !PCI_DN(pdn)) {
		printk(KERN_WARNING "pci_dma_dev_setup_pSeriesLP: "
		       "no DMA window found for pci dev=%s dn=%s\n",
				 pci_name(dev), of_node_full_name(dn));
		return;
	}
	pr_debug("  parent is %s\n", pdn->full_name);

	pci = PCI_DN(pdn);
	if (!pci->iommu_table) {
		tbl = kzalloc_node(sizeof(struct iommu_table), GFP_KERNEL,
				   pci->phb->node);
		iommu_table_setparms_lpar(pci->phb, pdn, tbl, dma_window);
		pci->iommu_table = iommu_init_table(tbl, pci->phb->node);
		iommu_register_group(tbl, pci_domain_nr(pci->phb->bus), 0);
		pr_debug("  created table: %p\n", pci->iommu_table);
	} else {
		pr_debug("  found DMA window, table: %p\n", pci->iommu_table);
	}

	set_iommu_table_base_and_group(&dev->dev, pci->iommu_table);
}

static int dma_set_mask_pSeriesLP(struct device *dev, u64 dma_mask)
{
	bool ddw_enabled = false;
	struct device_node *pdn, *dn;
	struct pci_dev *pdev;
	const __be32 *dma_window = NULL;
	u64 dma_offset;

	if (!dev->dma_mask)
		return -EIO;

	if (!dev_is_pci(dev))
		goto check_mask;

	pdev = to_pci_dev(dev);

	/* only attempt to use a new window if 64-bit DMA is requested */
	if (!disable_ddw && dma_mask == DMA_BIT_MASK(64)) {
		dn = pci_device_to_OF_node(pdev);
		dev_dbg(dev, "node is %s\n", dn->full_name);

		/*
		 * the device tree might contain the dma-window properties
		 * per-device and not necessarily for the bus. So we need to
		 * search upwards in the tree until we either hit a dma-window
		 * property, OR find a parent with a table already allocated.
		 */
		for (pdn = dn; pdn && PCI_DN(pdn) && !PCI_DN(pdn)->iommu_table;
				pdn = pdn->parent) {
			dma_window = of_get_property(pdn, "ibm,dma-window", NULL);
			if (dma_window)
				break;
		}
		if (pdn && PCI_DN(pdn)) {
			dma_offset = enable_ddw(pdev, pdn);
			if (dma_offset != 0) {
				dev_info(dev, "Using 64-bit direct DMA at offset %llx\n", dma_offset);
				set_dma_offset(dev, dma_offset);
				set_dma_ops(dev, &dma_direct_ops);
				ddw_enabled = true;
			}
		}
	}

	/* fall back on iommu ops, restore table pointer with ops */
	if (!ddw_enabled && get_dma_ops(dev) != &dma_iommu_ops) {
		dev_info(dev, "Restoring 32-bit DMA via iommu\n");
		set_dma_ops(dev, &dma_iommu_ops);
		pci_dma_dev_setup_pSeriesLP(pdev);
	}

check_mask:
	if (!dma_supported(dev, dma_mask))
		return -EIO;

	*dev->dma_mask = dma_mask;
	return 0;
}

static u64 dma_get_required_mask_pSeriesLP(struct device *dev)
{
	if (!dev->dma_mask)
		return 0;

	if (!disable_ddw && dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);
		struct device_node *dn;

		dn = pci_device_to_OF_node(pdev);

		/* search upwards for ibm,dma-window */
		for (; dn && PCI_DN(dn) && !PCI_DN(dn)->iommu_table;
				dn = dn->parent)
			if (of_get_property(dn, "ibm,dma-window", NULL))
				break;
		/* if there is a ibm,ddw-applicable property require 64 bits */
		if (dn && PCI_DN(dn) &&
				of_get_property(dn, "ibm,ddw-applicable", NULL))
			return DMA_BIT_MASK(64);
	}

	return dma_iommu_ops.get_required_mask(dev);
}

#else  /* CONFIG_PCI */
#define pci_dma_bus_setup_pSeries	NULL
#define pci_dma_dev_setup_pSeries	NULL
#define pci_dma_bus_setup_pSeriesLP	NULL
#define pci_dma_dev_setup_pSeriesLP	NULL
#define dma_set_mask_pSeriesLP		NULL
#define dma_get_required_mask_pSeriesLP	NULL
#endif /* !CONFIG_PCI */

static int iommu_mem_notifier(struct notifier_block *nb, unsigned long action,
		void *data)
{
	struct direct_window *window;
	struct memory_notify *arg = data;
	int ret = 0;

	switch (action) {
	case MEM_GOING_ONLINE:
		spin_lock(&direct_window_list_lock);
		list_for_each_entry(window, &direct_window_list, list) {
			ret |= tce_setrange_multi_pSeriesLP(arg->start_pfn,
					arg->nr_pages, window->prop);
			/* XXX log error */
		}
		spin_unlock(&direct_window_list_lock);
		break;
	case MEM_CANCEL_ONLINE:
	case MEM_OFFLINE:
		spin_lock(&direct_window_list_lock);
		list_for_each_entry(window, &direct_window_list, list) {
			ret |= tce_clearrange_multi_pSeriesLP(arg->start_pfn,
					arg->nr_pages, window->prop);
			/* XXX log error */
		}
		spin_unlock(&direct_window_list_lock);
		break;
	default:
		break;
	}
	if (ret && action != MEM_CANCEL_ONLINE)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static struct notifier_block iommu_mem_nb = {
	.notifier_call = iommu_mem_notifier,
};

static int iommu_reconfig_notifier(struct notifier_block *nb, unsigned long action, void *data)
{
	int err = NOTIFY_OK;
	struct of_reconfig_data *rd = data;
	struct device_node *np = rd->dn;
	struct pci_dn *pci = PCI_DN(np);
	struct direct_window *window;

	switch (action) {
	case OF_RECONFIG_DETACH_NODE:
		/*
		 * Removing the property will invoke the reconfig
		 * notifier again, which causes dead-lock on the
		 * read-write semaphore of the notifier chain. So
		 * we have to remove the property when releasing
		 * the device node.
		 */
		remove_ddw(np, false);
		if (pci && pci->iommu_table)
			iommu_free_table(pci->iommu_table, np->full_name);

		spin_lock(&direct_window_list_lock);
		list_for_each_entry(window, &direct_window_list, list) {
			if (window->device == np) {
				list_del(&window->list);
				kfree(window);
				break;
			}
		}
		spin_unlock(&direct_window_list_lock);
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block iommu_reconfig_nb = {
	.notifier_call = iommu_reconfig_notifier,
};

/* These are called very early. */
void iommu_init_early_pSeries(void)
{
	if (of_chosen && of_get_property(of_chosen, "linux,iommu-off", NULL))
		return;

	if (firmware_has_feature(FW_FEATURE_LPAR)) {
		if (firmware_has_feature(FW_FEATURE_MULTITCE)) {
			ppc_md.tce_build = tce_buildmulti_pSeriesLP;
			ppc_md.tce_free	 = tce_freemulti_pSeriesLP;
		} else {
			ppc_md.tce_build = tce_build_pSeriesLP;
			ppc_md.tce_free	 = tce_free_pSeriesLP;
		}
		ppc_md.tce_get   = tce_get_pSeriesLP;
		pseries_pci_controller_ops.dma_bus_setup = pci_dma_bus_setup_pSeriesLP;
		pseries_pci_controller_ops.dma_dev_setup = pci_dma_dev_setup_pSeriesLP;
		ppc_md.dma_set_mask = dma_set_mask_pSeriesLP;
		ppc_md.dma_get_required_mask = dma_get_required_mask_pSeriesLP;
	} else {
		ppc_md.tce_build = tce_build_pSeries;
		ppc_md.tce_free  = tce_free_pSeries;
		ppc_md.tce_get   = tce_get_pseries;
		pseries_pci_controller_ops.dma_bus_setup = pci_dma_bus_setup_pSeries;
		pseries_pci_controller_ops.dma_dev_setup = pci_dma_dev_setup_pSeries;
	}


	of_reconfig_notifier_register(&iommu_reconfig_nb);
	register_memory_notifier(&iommu_mem_nb);

	set_pci_dma_ops(&dma_iommu_ops);
}

static int __init disable_multitce(char *str)
{
	if (strcmp(str, "off") == 0 &&
	    firmware_has_feature(FW_FEATURE_LPAR) &&
	    firmware_has_feature(FW_FEATURE_MULTITCE)) {
		printk(KERN_INFO "Disabling MULTITCE firmware feature\n");
		ppc_md.tce_build = tce_build_pSeriesLP;
		ppc_md.tce_free	 = tce_free_pSeriesLP;
		powerpc_firmware_features &= ~FW_FEATURE_MULTITCE;
	}
	return 1;
}

__setup("multitce=", disable_multitce);

machine_subsys_initcall_sync(pseries, tce_iommu_bus_notifier_init);
