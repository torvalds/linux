// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 *
 * Rewrite, cleanup:
 *
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 * Copyright (C) 2006 Olof Johansson <olof@lixom.net>
 *
 * Dynamic DMA mapping support, pSeries-specific parts, both SMP and LPAR.
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
#include <linux/iommu.h>
#include <linux/rculist.h>
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

enum {
	DDW_QUERY_PE_DMA_WIN  = 0,
	DDW_CREATE_PE_DMA_WIN = 1,
	DDW_REMOVE_PE_DMA_WIN = 2,

	DDW_APPLICABLE_SIZE
};

enum {
	DDW_EXT_SIZE = 0,
	DDW_EXT_RESET_DMA_WIN = 1,
	DDW_EXT_QUERY_OUT_SIZE = 2
};

static struct iommu_table *iommu_pseries_alloc_table(int node)
{
	struct iommu_table *tbl;

	tbl = kzalloc_node(sizeof(struct iommu_table), GFP_KERNEL, node);
	if (!tbl)
		return NULL;

	INIT_LIST_HEAD_RCU(&tbl->it_group_list);
	kref_init(&tbl->it_kref);
	return tbl;
}

static struct iommu_table_group *iommu_pseries_alloc_group(int node)
{
	struct iommu_table_group *table_group;

	table_group = kzalloc_node(sizeof(*table_group), GFP_KERNEL, node);
	if (!table_group)
		return NULL;

	table_group->tables[0] = iommu_pseries_alloc_table(node);
	if (table_group->tables[0])
		return table_group;

	kfree(table_group);
	return NULL;
}

static void iommu_pseries_free_group(struct iommu_table_group *table_group,
		const char *node_name)
{
	struct iommu_table *tbl;

	if (!table_group)
		return;

	tbl = table_group->tables[0];
#ifdef CONFIG_IOMMU_API
	if (table_group->group) {
		iommu_group_put(table_group->group);
		BUG_ON(table_group->group);
	}
#endif
	iommu_tce_table_put(tbl);

	kfree(table_group);
}

static int tce_build_pSeries(struct iommu_table *tbl, long index,
			      long npages, unsigned long uaddr,
			      enum dma_data_direction direction,
			      unsigned long attrs)
{
	u64 proto_tce;
	__be64 *tcep;
	u64 rpn;
	const unsigned long tceshift = tbl->it_page_shift;
	const unsigned long pagesize = IOMMU_PAGE_SIZE(tbl);

	proto_tce = TCE_PCI_READ; // Read allowed

	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	tcep = ((__be64 *)tbl->it_base) + index;

	while (npages--) {
		/* can't move this out since we might cross MEMBLOCK boundary */
		rpn = __pa(uaddr) >> tceshift;
		*tcep = cpu_to_be64(proto_tce | rpn << tceshift);

		uaddr += pagesize;
		tcep++;
	}
	return 0;
}


static void tce_free_pSeries(struct iommu_table *tbl, long index, long npages)
{
	__be64 *tcep;

	tcep = ((__be64 *)tbl->it_base) + index;

	while (npages--)
		*(tcep++) = 0;
}

static unsigned long tce_get_pseries(struct iommu_table *tbl, long index)
{
	__be64 *tcep;

	tcep = ((__be64 *)tbl->it_base) + index;

	return be64_to_cpu(*tcep);
}

static void tce_free_pSeriesLP(unsigned long liobn, long, long, long);
static void tce_freemulti_pSeriesLP(struct iommu_table*, long, long);

static int tce_build_pSeriesLP(unsigned long liobn, long tcenum, long tceshift,
				long npages, unsigned long uaddr,
				enum dma_data_direction direction,
				unsigned long attrs)
{
	u64 rc = 0;
	u64 proto_tce, tce;
	u64 rpn;
	int ret = 0;
	long tcenum_start = tcenum, npages_start = npages;

	rpn = __pa(uaddr) >> tceshift;
	proto_tce = TCE_PCI_READ;
	if (direction != DMA_TO_DEVICE)
		proto_tce |= TCE_PCI_WRITE;

	while (npages--) {
		tce = proto_tce | rpn << tceshift;
		rc = plpar_tce_put((u64)liobn, (u64)tcenum << tceshift, tce);

		if (unlikely(rc == H_NOT_ENOUGH_RESOURCES)) {
			ret = (int)rc;
			tce_free_pSeriesLP(liobn, tcenum_start, tceshift,
			                   (npages_start - (npages + 1)));
			break;
		}

		if (rc && printk_ratelimit()) {
			printk("tce_build_pSeriesLP: plpar_tce_put failed. rc=%lld\n", rc);
			printk("\tindex   = 0x%llx\n", (u64)liobn);
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
				     unsigned long attrs)
{
	u64 rc = 0;
	u64 proto_tce;
	__be64 *tcep;
	u64 rpn;
	long l, limit;
	long tcenum_start = tcenum, npages_start = npages;
	int ret = 0;
	unsigned long flags;
	const unsigned long tceshift = tbl->it_page_shift;

	if ((npages == 1) || !firmware_has_feature(FW_FEATURE_PUT_TCE_IND)) {
		return tce_build_pSeriesLP(tbl->it_index, tcenum,
					   tceshift, npages, uaddr,
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
			return tce_build_pSeriesLP(tbl->it_index, tcenum,
					tceshift,
					npages, uaddr, direction, attrs);
		}
		__this_cpu_write(tce_page, tcep);
	}

	rpn = __pa(uaddr) >> tceshift;
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
			tcep[l] = cpu_to_be64(proto_tce | rpn << tceshift);
			rpn++;
		}

		rc = plpar_tce_put_indirect((u64)tbl->it_index,
					    (u64)tcenum << tceshift,
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

static void tce_free_pSeriesLP(unsigned long liobn, long tcenum, long tceshift,
			       long npages)
{
	u64 rc;

	while (npages--) {
		rc = plpar_tce_put((u64)liobn, (u64)tcenum << tceshift, 0);

		if (rc && printk_ratelimit()) {
			printk("tce_free_pSeriesLP: plpar_tce_put failed. rc=%lld\n", rc);
			printk("\tindex   = 0x%llx\n", (u64)liobn);
			printk("\ttcenum  = 0x%llx\n", (u64)tcenum);
			dump_stack();
		}

		tcenum++;
	}
}


static void tce_freemulti_pSeriesLP(struct iommu_table *tbl, long tcenum, long npages)
{
	u64 rc;

	if (!firmware_has_feature(FW_FEATURE_STUFF_TCE))
		return tce_free_pSeriesLP(tbl->it_index, tcenum,
					  tbl->it_page_shift, npages);

	rc = plpar_tce_stuff((u64)tbl->it_index,
			     (u64)tcenum << tbl->it_page_shift, 0, npages);

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

	rc = plpar_tce_get((u64)tbl->it_index,
			   (u64)tcenum << tbl->it_page_shift, &tce_ret);

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
	u64 largest_available_block;
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

	if (!firmware_has_feature(FW_FEATURE_PUT_TCE_IND)) {
		unsigned long tceshift = be32_to_cpu(maprange->tce_shift);
		unsigned long dmastart = (start_pfn << PAGE_SHIFT) +
				be64_to_cpu(maprange->dma_base);
		unsigned long tcenum = dmastart >> tceshift;
		unsigned long npages = num_pfn << PAGE_SHIFT >> tceshift;
		void *uaddr = __va(start_pfn << PAGE_SHIFT);

		return tce_build_pSeriesLP(be32_to_cpu(maprange->liobn),
				tcenum, tceshift, npages, (unsigned long) uaddr,
				DMA_BIDIRECTIONAL, 0);
	}

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

static void iommu_table_setparms_common(struct iommu_table *tbl, unsigned long busno,
					unsigned long liobn, unsigned long win_addr,
					unsigned long window_size, unsigned long page_shift,
					void *base, struct iommu_table_ops *table_ops)
{
	tbl->it_busno = busno;
	tbl->it_index = liobn;
	tbl->it_offset = win_addr >> page_shift;
	tbl->it_size = window_size >> page_shift;
	tbl->it_page_shift = page_shift;
	tbl->it_base = (unsigned long)base;
	tbl->it_blocksize = 16;
	tbl->it_type = TCE_PCI;
	tbl->it_ops = table_ops;
}

struct iommu_table_ops iommu_table_pseries_ops;

static void iommu_table_setparms(struct pci_controller *phb,
				 struct device_node *dn,
				 struct iommu_table *tbl)
{
	struct device_node *node;
	const unsigned long *basep;
	const u32 *sizep;

	/* Test if we are going over 2GB of DMA space */
	if (phb->dma_window_base_cur + phb->dma_window_size > SZ_2G) {
		udbg_printf("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
		panic("PCI_DMA: Unexpected number of IOAs under this PHB.\n");
	}

	node = phb->dn;
	basep = of_get_property(node, "linux,tce-base", NULL);
	sizep = of_get_property(node, "linux,tce-size", NULL);
	if (basep == NULL || sizep == NULL) {
		printk(KERN_ERR "PCI_DMA: iommu_table_setparms: %pOF has "
				"missing tce entries !\n", dn);
		return;
	}

	iommu_table_setparms_common(tbl, phb->bus->number, 0, phb->dma_window_base_cur,
				    phb->dma_window_size, IOMMU_PAGE_SHIFT_4K,
				    __va(*basep), &iommu_table_pseries_ops);

	if (!is_kdump_kernel())
		memset((void *)tbl->it_base, 0, *sizep);

	phb->dma_window_base_cur += phb->dma_window_size;
}

struct iommu_table_ops iommu_table_lpar_multi_ops;

/*
 * iommu_table_setparms_lpar
 *
 * Function: On pSeries LPAR systems, return TCE table info, given a pci bus.
 */
static void iommu_table_setparms_lpar(struct pci_controller *phb,
				      struct device_node *dn,
				      struct iommu_table *tbl,
				      struct iommu_table_group *table_group,
				      const __be32 *dma_window)
{
	unsigned long offset, size, liobn;

	of_parse_dma_window(dn, dma_window, &liobn, &offset, &size);

	iommu_table_setparms_common(tbl, phb->bus->number, liobn, offset, size, IOMMU_PAGE_SHIFT_4K, NULL,
				    &iommu_table_lpar_multi_ops);


	table_group->tce32_start = offset;
	table_group->tce32_size = size;
}

struct iommu_table_ops iommu_table_pseries_ops = {
	.set = tce_build_pSeries,
	.clear = tce_free_pSeries,
	.get = tce_get_pseries
};

static void pci_dma_bus_setup_pSeries(struct pci_bus *bus)
{
	struct device_node *dn;
	struct iommu_table *tbl;
	struct device_node *isa_dn, *isa_dn_orig;
	struct device_node *tmp;
	struct pci_dn *pci;
	int children;

	dn = pci_bus_to_OF_node(bus);

	pr_debug("pci_dma_bus_setup_pSeries: setting up bus %pOF\n", dn);

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

	pci->table_group = iommu_pseries_alloc_group(pci->phb->node);
	tbl = pci->table_group->tables[0];

	iommu_table_setparms(pci->phb, dn, tbl);

	if (!iommu_init_table(tbl, pci->phb->node, 0, 0))
		panic("Failed to initialize iommu table");

	/* Divide the rest (1.75GB) among the children */
	pci->phb->dma_window_size = 0x80000000ul;
	while (pci->phb->dma_window_size * children > 0x70000000ul)
		pci->phb->dma_window_size >>= 1;

	pr_debug("ISA/IDE, window size is 0x%llx\n", pci->phb->dma_window_size);
}

#ifdef CONFIG_IOMMU_API
static int tce_exchange_pseries(struct iommu_table *tbl, long index, unsigned
				long *tce, enum dma_data_direction *direction,
				bool realmode)
{
	long rc;
	unsigned long ioba = (unsigned long) index << tbl->it_page_shift;
	unsigned long flags, oldtce = 0;
	u64 proto_tce = iommu_direction_to_tce_perm(*direction);
	unsigned long newtce = *tce | proto_tce;

	spin_lock_irqsave(&tbl->large_pool.lock, flags);

	rc = plpar_tce_get((u64)tbl->it_index, ioba, &oldtce);
	if (!rc)
		rc = plpar_tce_put((u64)tbl->it_index, ioba, newtce);

	if (!rc) {
		*direction = iommu_tce_direction(oldtce);
		*tce = oldtce & ~(TCE_PCI_READ | TCE_PCI_WRITE);
	}

	spin_unlock_irqrestore(&tbl->large_pool.lock, flags);

	return rc;
}
#endif

struct iommu_table_ops iommu_table_lpar_multi_ops = {
	.set = tce_buildmulti_pSeriesLP,
#ifdef CONFIG_IOMMU_API
	.xchg_no_kill = tce_exchange_pseries,
#endif
	.clear = tce_freemulti_pSeriesLP,
	.get = tce_get_pSeriesLP
};

static void pci_dma_bus_setup_pSeriesLP(struct pci_bus *bus)
{
	struct iommu_table *tbl;
	struct device_node *dn, *pdn;
	struct pci_dn *ppci;
	const __be32 *dma_window = NULL;

	dn = pci_bus_to_OF_node(bus);

	pr_debug("pci_dma_bus_setup_pSeriesLP: setting up bus %pOF\n",
		 dn);

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

	pr_debug("  parent is %pOF, iommu_table: 0x%p\n",
		 pdn, ppci->table_group);

	if (!ppci->table_group) {
		ppci->table_group = iommu_pseries_alloc_group(ppci->phb->node);
		tbl = ppci->table_group->tables[0];
		iommu_table_setparms_lpar(ppci->phb, pdn, tbl,
				ppci->table_group, dma_window);

		if (!iommu_init_table(tbl, ppci->phb->node, 0, 0))
			panic("Failed to initialize iommu table");
		iommu_register_group(ppci->table_group,
				pci_domain_nr(bus), 0);
		pr_debug("  created table: %p\n", ppci->table_group);
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
		PCI_DN(dn)->table_group = iommu_pseries_alloc_group(phb->node);
		tbl = PCI_DN(dn)->table_group->tables[0];
		iommu_table_setparms(phb, dn, tbl);

		if (!iommu_init_table(tbl, phb->node, 0, 0))
			panic("Failed to initialize iommu table");

		set_iommu_table_base(&dev->dev, tbl);
		return;
	}

	/* If this device is further down the bus tree, search upwards until
	 * an already allocated iommu table is found and use that.
	 */

	while (dn && PCI_DN(dn) && PCI_DN(dn)->table_group == NULL)
		dn = dn->parent;

	if (dn && PCI_DN(dn))
		set_iommu_table_base(&dev->dev,
				PCI_DN(dn)->table_group->tables[0]);
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

static void clean_dma_window(struct device_node *np, struct dynamic_dma_window_prop *dwp)
{
	int ret;

	ret = tce_clearrange_multi_pSeriesLP(0,
		1ULL << (be32_to_cpu(dwp->window_shift) - PAGE_SHIFT), dwp);
	if (ret)
		pr_warn("%pOF failed to clear tces in window.\n",
			np);
	else
		pr_debug("%pOF successfully cleared tces in window.\n",
			 np);
}

/*
 * Call only if DMA window is clean.
 */
static void __remove_dma_window(struct device_node *np, u32 *ddw_avail, u64 liobn)
{
	int ret;

	ret = rtas_call(ddw_avail[DDW_REMOVE_PE_DMA_WIN], 1, 1, NULL, liobn);
	if (ret)
		pr_warn("%pOF: failed to remove DMA window: rtas returned "
			"%d to ibm,remove-pe-dma-window(%x) %llx\n",
			np, ret, ddw_avail[DDW_REMOVE_PE_DMA_WIN], liobn);
	else
		pr_debug("%pOF: successfully removed DMA window: rtas returned "
			"%d to ibm,remove-pe-dma-window(%x) %llx\n",
			np, ret, ddw_avail[DDW_REMOVE_PE_DMA_WIN], liobn);
}

static void remove_dma_window(struct device_node *np, u32 *ddw_avail,
			      struct property *win)
{
	struct dynamic_dma_window_prop *dwp;
	u64 liobn;

	dwp = win->value;
	liobn = (u64)be32_to_cpu(dwp->liobn);

	clean_dma_window(np, dwp);
	__remove_dma_window(np, ddw_avail, liobn);
}

static int remove_ddw(struct device_node *np, bool remove_prop, const char *win_name)
{
	struct property *win;
	u32 ddw_avail[DDW_APPLICABLE_SIZE];
	int ret = 0;

	win = of_find_property(np, win_name, NULL);
	if (!win)
		return -EINVAL;

	ret = of_property_read_u32_array(np, "ibm,ddw-applicable",
					 &ddw_avail[0], DDW_APPLICABLE_SIZE);
	if (ret)
		return 0;


	if (win->length >= sizeof(struct dynamic_dma_window_prop))
		remove_dma_window(np, ddw_avail, win);

	if (!remove_prop)
		return 0;

	ret = of_remove_property(np, win);
	if (ret)
		pr_warn("%pOF: failed to remove direct window property: %d\n",
			np, ret);
	return 0;
}

static bool find_existing_ddw(struct device_node *pdn, u64 *dma_addr, int *window_shift)
{
	struct direct_window *window;
	const struct dynamic_dma_window_prop *direct64;
	bool found = false;

	spin_lock(&direct_window_list_lock);
	/* check if we already created a window and dupe that config if so */
	list_for_each_entry(window, &direct_window_list, list) {
		if (window->device == pdn) {
			direct64 = window->prop;
			*dma_addr = be64_to_cpu(direct64->dma_base);
			*window_shift = be32_to_cpu(direct64->window_shift);
			found = true;
			break;
		}
	}
	spin_unlock(&direct_window_list_lock);

	return found;
}

static struct direct_window *ddw_list_new_entry(struct device_node *pdn,
						const struct dynamic_dma_window_prop *dma64)
{
	struct direct_window *window;

	window = kzalloc(sizeof(*window), GFP_KERNEL);
	if (!window)
		return NULL;

	window->device = pdn;
	window->prop = dma64;

	return window;
}

static void find_existing_ddw_windows_named(const char *name)
{
	int len;
	struct device_node *pdn;
	struct direct_window *window;
	const struct dynamic_dma_window_prop *dma64;

	for_each_node_with_property(pdn, name) {
		dma64 = of_get_property(pdn, name, &len);
		if (!dma64 || len < sizeof(*dma64)) {
			remove_ddw(pdn, true, name);
			continue;
		}

		window = ddw_list_new_entry(pdn, dma64);
		if (!window)
			break;

		spin_lock(&direct_window_list_lock);
		list_add(&window->list, &direct_window_list);
		spin_unlock(&direct_window_list_lock);
	}
}

static int find_existing_ddw_windows(void)
{
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	find_existing_ddw_windows_named(DIRECT64_PROPNAME);

	return 0;
}
machine_arch_initcall(pseries, find_existing_ddw_windows);

/**
 * ddw_read_ext - Get the value of an DDW extension
 * @np:		device node from which the extension value is to be read.
 * @extnum:	index number of the extension.
 * @value:	pointer to return value, modified when extension is available.
 *
 * Checks if "ibm,ddw-extensions" exists for this node, and get the value
 * on index 'extnum'.
 * It can be used only to check if a property exists, passing value == NULL.
 *
 * Returns:
 *	0 if extension successfully read
 *	-EINVAL if the "ibm,ddw-extensions" does not exist,
 *	-ENODATA if "ibm,ddw-extensions" does not have a value, and
 *	-EOVERFLOW if "ibm,ddw-extensions" does not contain this extension.
 */
static inline int ddw_read_ext(const struct device_node *np, int extnum,
			       u32 *value)
{
	static const char propname[] = "ibm,ddw-extensions";
	u32 count;
	int ret;

	ret = of_property_read_u32_index(np, propname, DDW_EXT_SIZE, &count);
	if (ret)
		return ret;

	if (count < extnum)
		return -EOVERFLOW;

	if (!value)
		value = &count;

	return of_property_read_u32_index(np, propname, extnum, value);
}

static int query_ddw(struct pci_dev *dev, const u32 *ddw_avail,
		     struct ddw_query_response *query,
		     struct device_node *parent)
{
	struct device_node *dn;
	struct pci_dn *pdn;
	u32 cfg_addr, ext_query, query_out[5];
	u64 buid;
	int ret, out_sz;

	/*
	 * From LoPAR level 2.8, "ibm,ddw-extensions" index 3 can rule how many
	 * output parameters ibm,query-pe-dma-windows will have, ranging from
	 * 5 to 6.
	 */
	ret = ddw_read_ext(parent, DDW_EXT_QUERY_OUT_SIZE, &ext_query);
	if (!ret && ext_query == 1)
		out_sz = 6;
	else
		out_sz = 5;

	/*
	 * Get the config address and phb buid of the PE window.
	 * Rely on eeh to retrieve this for us.
	 * Retrieve them from the pci device, not the node with the
	 * dma-window property
	 */
	dn = pci_device_to_OF_node(dev);
	pdn = PCI_DN(dn);
	buid = pdn->phb->buid;
	cfg_addr = ((pdn->busno << 16) | (pdn->devfn << 8));

	ret = rtas_call(ddw_avail[DDW_QUERY_PE_DMA_WIN], 3, out_sz, query_out,
			cfg_addr, BUID_HI(buid), BUID_LO(buid));
	dev_info(&dev->dev, "ibm,query-pe-dma-windows(%x) %x %x %x returned %d\n",
		 ddw_avail[DDW_QUERY_PE_DMA_WIN], cfg_addr, BUID_HI(buid),
		 BUID_LO(buid), ret);

	switch (out_sz) {
	case 5:
		query->windows_available = query_out[0];
		query->largest_available_block = query_out[1];
		query->page_size = query_out[2];
		query->migration_capable = query_out[3];
		break;
	case 6:
		query->windows_available = query_out[0];
		query->largest_available_block = ((u64)query_out[1] << 32) |
						 query_out[2];
		query->page_size = query_out[3];
		query->migration_capable = query_out[4];
		break;
	}

	return ret;
}

static int create_ddw(struct pci_dev *dev, const u32 *ddw_avail,
			struct ddw_create_response *create, int page_shift,
			int window_shift)
{
	struct device_node *dn;
	struct pci_dn *pdn;
	u32 cfg_addr;
	u64 buid;
	int ret;

	/*
	 * Get the config address and phb buid of the PE window.
	 * Rely on eeh to retrieve this for us.
	 * Retrieve them from the pci device, not the node with the
	 * dma-window property
	 */
	dn = pci_device_to_OF_node(dev);
	pdn = PCI_DN(dn);
	buid = pdn->phb->buid;
	cfg_addr = ((pdn->busno << 16) | (pdn->devfn << 8));

	do {
		/* extra outputs are LIOBN and dma-addr (hi, lo) */
		ret = rtas_call(ddw_avail[DDW_CREATE_PE_DMA_WIN], 5, 4,
				(u32 *)create, cfg_addr, BUID_HI(buid),
				BUID_LO(buid), page_shift, window_shift);
	} while (rtas_busy_delay(ret));
	dev_info(&dev->dev,
		"ibm,create-pe-dma-window(%x) %x %x %x %x %x returned %d "
		"(liobn = 0x%x starting addr = %x %x)\n",
		 ddw_avail[DDW_CREATE_PE_DMA_WIN], cfg_addr, BUID_HI(buid),
		 BUID_LO(buid), page_shift, window_shift, ret, create->liobn,
		 create->addr_hi, create->addr_lo);

	return ret;
}

struct failed_ddw_pdn {
	struct device_node *pdn;
	struct list_head list;
};

static LIST_HEAD(failed_ddw_pdn_list);

static phys_addr_t ddw_memory_hotplug_max(void)
{
	phys_addr_t max_addr = memory_hotplug_max();
	struct device_node *memory;

	/*
	 * The "ibm,pmemory" can appear anywhere in the address space.
	 * Assuming it is still backed by page structs, set the upper limit
	 * for the huge DMA window as MAX_PHYSMEM_BITS.
	 */
	if (of_find_node_by_type(NULL, "ibm,pmemory"))
		return (sizeof(phys_addr_t) * 8 <= MAX_PHYSMEM_BITS) ?
			(phys_addr_t) -1 : (1ULL << MAX_PHYSMEM_BITS);

	for_each_node_by_type(memory, "memory") {
		unsigned long start, size;
		int n_mem_addr_cells, n_mem_size_cells, len;
		const __be32 *memcell_buf;

		memcell_buf = of_get_property(memory, "reg", &len);
		if (!memcell_buf || len <= 0)
			continue;

		n_mem_addr_cells = of_n_addr_cells(memory);
		n_mem_size_cells = of_n_size_cells(memory);

		start = of_read_number(memcell_buf, n_mem_addr_cells);
		memcell_buf += n_mem_addr_cells;
		size = of_read_number(memcell_buf, n_mem_size_cells);
		memcell_buf += n_mem_size_cells;

		max_addr = max_t(phys_addr_t, max_addr, start + size);
	}

	return max_addr;
}

/*
 * Platforms supporting the DDW option starting with LoPAR level 2.7 implement
 * ibm,ddw-extensions, which carries the rtas token for
 * ibm,reset-pe-dma-windows.
 * That rtas-call can be used to restore the default DMA window for the device.
 */
static void reset_dma_window(struct pci_dev *dev, struct device_node *par_dn)
{
	int ret;
	u32 cfg_addr, reset_dma_win;
	u64 buid;
	struct device_node *dn;
	struct pci_dn *pdn;

	ret = ddw_read_ext(par_dn, DDW_EXT_RESET_DMA_WIN, &reset_dma_win);
	if (ret)
		return;

	dn = pci_device_to_OF_node(dev);
	pdn = PCI_DN(dn);
	buid = pdn->phb->buid;
	cfg_addr = (pdn->busno << 16) | (pdn->devfn << 8);

	ret = rtas_call(reset_dma_win, 3, 1, NULL, cfg_addr, BUID_HI(buid),
			BUID_LO(buid));
	if (ret)
		dev_info(&dev->dev,
			 "ibm,reset-pe-dma-windows(%x) %x %x %x returned %d ",
			 reset_dma_win, cfg_addr, BUID_HI(buid), BUID_LO(buid),
			 ret);
}

/* Return largest page shift based on "IO Page Sizes" output of ibm,query-pe-dma-window. */
static int iommu_get_page_shift(u32 query_page_size)
{
	/* Supported IO page-sizes according to LoPAR */
	const int shift[] = {
		__builtin_ctzll(SZ_4K),   __builtin_ctzll(SZ_64K), __builtin_ctzll(SZ_16M),
		__builtin_ctzll(SZ_32M),  __builtin_ctzll(SZ_64M), __builtin_ctzll(SZ_128M),
		__builtin_ctzll(SZ_256M), __builtin_ctzll(SZ_16G)
	};

	int i = ARRAY_SIZE(shift) - 1;

	/*
	 * On LoPAR, ibm,query-pe-dma-window outputs "IO Page Sizes" using a bit field:
	 * - bit 31 means 4k pages are supported,
	 * - bit 30 means 64k pages are supported, and so on.
	 * Larger pagesizes map more memory with the same amount of TCEs, so start probing them.
	 */
	for (; i >= 0 ; i--) {
		if (query_page_size & (1 << i))
			return shift[i];
	}

	/* No valid page size found. */
	return 0;
}

static struct property *ddw_property_create(const char *propname, u32 liobn, u64 dma_addr,
					    u32 page_shift, u32 window_shift)
{
	struct dynamic_dma_window_prop *ddwprop;
	struct property *win64;

	win64 = kzalloc(sizeof(*win64), GFP_KERNEL);
	if (!win64)
		return NULL;

	win64->name = kstrdup(propname, GFP_KERNEL);
	ddwprop = kzalloc(sizeof(*ddwprop), GFP_KERNEL);
	win64->value = ddwprop;
	win64->length = sizeof(*ddwprop);
	if (!win64->name || !win64->value) {
		kfree(win64->name);
		kfree(win64->value);
		kfree(win64);
		return NULL;
	}

	ddwprop->liobn = cpu_to_be32(liobn);
	ddwprop->dma_base = cpu_to_be64(dma_addr);
	ddwprop->tce_shift = cpu_to_be32(page_shift);
	ddwprop->window_shift = cpu_to_be32(window_shift);

	return win64;
}

/*
 * If the PE supports dynamic dma windows, and there is space for a table
 * that can map all pages in a linear offset, then setup such a table,
 * and record the dma-offset in the struct device.
 *
 * dev: the pci device we are checking
 * pdn: the parent pe node with the ibm,dma_window property
 * Future: also check if we can remap the base window for our base page size
 *
 * returns true if can map all pages (direct mapping), false otherwise..
 */
static bool enable_ddw(struct pci_dev *dev, struct device_node *pdn)
{
	int len = 0, ret;
	int max_ram_len = order_base_2(ddw_memory_hotplug_max());
	struct ddw_query_response query;
	struct ddw_create_response create;
	int page_shift;
	u64 win_addr;
	struct device_node *dn;
	u32 ddw_avail[DDW_APPLICABLE_SIZE];
	struct direct_window *window;
	struct property *win64;
	bool ddw_enabled = false;
	struct failed_ddw_pdn *fpdn;
	bool default_win_removed = false;
	bool pmem_present;

	dn = of_find_node_by_type(NULL, "ibm,pmemory");
	pmem_present = dn != NULL;
	of_node_put(dn);

	mutex_lock(&direct_window_init_mutex);

	if (find_existing_ddw(pdn, &dev->dev.archdata.dma_offset, &len)) {
		ddw_enabled = true;
		goto out_unlock;
	}

	/*
	 * If we already went through this for a previous function of
	 * the same device and failed, we don't want to muck with the
	 * DMA window again, as it will race with in-flight operations
	 * and can lead to EEHs. The above mutex protects access to the
	 * list.
	 */
	list_for_each_entry(fpdn, &failed_ddw_pdn_list, list) {
		if (fpdn->pdn == pdn)
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
					 &ddw_avail[0], DDW_APPLICABLE_SIZE);
	if (ret)
		goto out_failed;

       /*
	 * Query if there is a second window of size to map the
	 * whole partition.  Query returns number of windows, largest
	 * block assigned to PE (partition endpoint), and two bitmasks
	 * of page sizes: supported and supported for migrate-dma.
	 */
	dn = pci_device_to_OF_node(dev);
	ret = query_ddw(dev, ddw_avail, &query, pdn);
	if (ret != 0)
		goto out_failed;

	/*
	 * If there is no window available, remove the default DMA window,
	 * if it's present. This will make all the resources available to the
	 * new DDW window.
	 * If anything fails after this, we need to restore it, so also check
	 * for extensions presence.
	 */
	if (query.windows_available == 0) {
		struct property *default_win;
		int reset_win_ext;

		default_win = of_find_property(pdn, "ibm,dma-window", NULL);
		if (!default_win)
			goto out_failed;

		reset_win_ext = ddw_read_ext(pdn, DDW_EXT_RESET_DMA_WIN, NULL);
		if (reset_win_ext)
			goto out_failed;

		remove_dma_window(pdn, ddw_avail, default_win);
		default_win_removed = true;

		/* Query again, to check if the window is available */
		ret = query_ddw(dev, ddw_avail, &query, pdn);
		if (ret != 0)
			goto out_failed;

		if (query.windows_available == 0) {
			/* no windows are available for this device. */
			dev_dbg(&dev->dev, "no free dynamic windows");
			goto out_failed;
		}
	}

	page_shift = iommu_get_page_shift(query.page_size);
	if (!page_shift) {
		dev_dbg(&dev->dev, "no supported direct page size in mask %x",
			  query.page_size);
		goto out_failed;
	}
	/* verify the window * number of ptes will map the partition */
	/* check largest block * page size > max memory hotplug addr */
	/*
	 * The "ibm,pmemory" can appear anywhere in the address space.
	 * Assuming it is still backed by page structs, try MAX_PHYSMEM_BITS
	 * for the upper limit and fallback to max RAM otherwise but this
	 * disables device::dma_ops_bypass.
	 */
	len = max_ram_len;
	if (pmem_present) {
		if (query.largest_available_block >=
		    (1ULL << (MAX_PHYSMEM_BITS - page_shift)))
			len = MAX_PHYSMEM_BITS;
		else
			dev_info(&dev->dev, "Skipping ibm,pmemory");
	}

	if (query.largest_available_block < (1ULL << (len - page_shift))) {
		dev_dbg(&dev->dev,
			"can't map partition max 0x%llx with %llu %llu-sized pages\n",
			1ULL << len,
			query.largest_available_block,
			1ULL << page_shift);
		goto out_failed;
	}

	ret = create_ddw(dev, ddw_avail, &create, page_shift, len);
	if (ret != 0)
		goto out_failed;

	dev_dbg(&dev->dev, "created tce table LIOBN 0x%x for %pOF\n",
		  create.liobn, dn);

	win_addr = ((u64)create.addr_hi << 32) | create.addr_lo;
	win64 = ddw_property_create(DIRECT64_PROPNAME, create.liobn, win_addr,
				    page_shift, len);
	if (!win64) {
		dev_info(&dev->dev,
			 "couldn't allocate property, property name, or value\n");
		goto out_remove_win;
	}

	ret = of_add_property(pdn, win64);
	if (ret) {
		dev_err(&dev->dev, "unable to add dma window property for %pOF: %d",
			pdn, ret);
		goto out_free_prop;
	}

	window = ddw_list_new_entry(pdn, win64->value);
	if (!window)
		goto out_del_prop;

	ret = walk_system_ram_range(0, memblock_end_of_DRAM() >> PAGE_SHIFT,
			win64->value, tce_setrange_multi_pSeriesLP_walk);
	if (ret) {
		dev_info(&dev->dev, "failed to map direct window for %pOF: %d\n",
			 dn, ret);

		/* Make sure to clean DDW if any TCE was set*/
		clean_dma_window(pdn, win64->value);
		goto out_del_list;
	}

	spin_lock(&direct_window_list_lock);
	list_add(&window->list, &direct_window_list);
	spin_unlock(&direct_window_list_lock);

	dev->dev.archdata.dma_offset = win_addr;
	ddw_enabled = true;
	goto out_unlock;

out_del_list:
	kfree(window);

out_del_prop:
	of_remove_property(pdn, win64);

out_free_prop:
	kfree(win64->name);
	kfree(win64->value);
	kfree(win64);

out_remove_win:
	/* DDW is clean, so it's ok to call this directly. */
	__remove_dma_window(pdn, ddw_avail, create.liobn);

out_failed:
	if (default_win_removed)
		reset_dma_window(dev, pdn);

	fpdn = kzalloc(sizeof(*fpdn), GFP_KERNEL);
	if (!fpdn)
		goto out_unlock;
	fpdn->pdn = pdn;
	list_add(&fpdn->list, &failed_ddw_pdn_list);

out_unlock:
	mutex_unlock(&direct_window_init_mutex);

	/*
	 * If we have persistent memory and the window size is only as big
	 * as RAM, then we failed to create a window to cover persistent
	 * memory and need to set the DMA limit.
	 */
	if (pmem_present && ddw_enabled && (len == max_ram_len))
		dev->dev.bus_dma_limit = dev->dev.archdata.dma_offset + (1ULL << len);

	return ddw_enabled;
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
	pr_debug("  node is %pOF\n", dn);

	for (pdn = dn; pdn && PCI_DN(pdn) && !PCI_DN(pdn)->table_group;
	     pdn = pdn->parent) {
		dma_window = of_get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window)
			break;
	}

	if (!pdn || !PCI_DN(pdn)) {
		printk(KERN_WARNING "pci_dma_dev_setup_pSeriesLP: "
		       "no DMA window found for pci dev=%s dn=%pOF\n",
				 pci_name(dev), dn);
		return;
	}
	pr_debug("  parent is %pOF\n", pdn);

	pci = PCI_DN(pdn);
	if (!pci->table_group) {
		pci->table_group = iommu_pseries_alloc_group(pci->phb->node);
		tbl = pci->table_group->tables[0];
		iommu_table_setparms_lpar(pci->phb, pdn, tbl,
				pci->table_group, dma_window);

		iommu_init_table(tbl, pci->phb->node, 0, 0);
		iommu_register_group(pci->table_group,
				pci_domain_nr(pci->phb->bus), 0);
		pr_debug("  created table: %p\n", pci->table_group);
	} else {
		pr_debug("  found DMA window, table: %p\n", pci->table_group);
	}

	set_iommu_table_base(&dev->dev, pci->table_group->tables[0]);
	iommu_add_device(pci->table_group, &dev->dev);
}

static bool iommu_bypass_supported_pSeriesLP(struct pci_dev *pdev, u64 dma_mask)
{
	struct device_node *dn = pci_device_to_OF_node(pdev), *pdn;
	const __be32 *dma_window = NULL;

	/* only attempt to use a new window if 64-bit DMA is requested */
	if (dma_mask < DMA_BIT_MASK(64))
		return false;

	dev_dbg(&pdev->dev, "node is %pOF\n", dn);

	/*
	 * the device tree might contain the dma-window properties
	 * per-device and not necessarily for the bus. So we need to
	 * search upwards in the tree until we either hit a dma-window
	 * property, OR find a parent with a table already allocated.
	 */
	for (pdn = dn; pdn && PCI_DN(pdn) && !PCI_DN(pdn)->table_group;
			pdn = pdn->parent) {
		dma_window = of_get_property(pdn, "ibm,dma-window", NULL);
		if (dma_window)
			break;
	}

	if (pdn && PCI_DN(pdn))
		return enable_ddw(pdev, pdn);

	return false;
}

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
		remove_ddw(np, false, DIRECT64_PROPNAME);
		if (pci && pci->table_group)
			iommu_pseries_free_group(pci->table_group,
					np->full_name);

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
		pseries_pci_controller_ops.dma_bus_setup = pci_dma_bus_setup_pSeriesLP;
		pseries_pci_controller_ops.dma_dev_setup = pci_dma_dev_setup_pSeriesLP;
		if (!disable_ddw)
			pseries_pci_controller_ops.iommu_bypass_supported =
				iommu_bypass_supported_pSeriesLP;
	} else {
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
	    (firmware_has_feature(FW_FEATURE_PUT_TCE_IND) ||
	     firmware_has_feature(FW_FEATURE_STUFF_TCE))) {
		printk(KERN_INFO "Disabling MULTITCE firmware feature\n");
		powerpc_firmware_features &=
			~(FW_FEATURE_PUT_TCE_IND | FW_FEATURE_STUFF_TCE);
	}
	return 1;
}

__setup("multitce=", disable_multitce);

static int tce_iommu_bus_notifier(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct device *dev = data;

	switch (action) {
	case BUS_NOTIFY_DEL_DEVICE:
		iommu_del_device(dev);
		return 0;
	default:
		return 0;
	}
}

static struct notifier_block tce_iommu_bus_nb = {
	.notifier_call = tce_iommu_bus_notifier,
};

static int __init tce_iommu_bus_notifier_init(void)
{
	bus_register_notifier(&pci_bus_type, &tce_iommu_bus_nb);
	return 0;
}
machine_subsys_initcall_sync(pseries, tce_iommu_bus_notifier_init);
