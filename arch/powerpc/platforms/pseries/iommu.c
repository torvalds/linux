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
#include <linux/vmalloc.h>
#include <linux/of.h>
#include <linux/of_address.h>
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
	DDW_EXT_QUERY_OUT_SIZE = 2,
	DDW_EXT_LIMITED_ADDR_MODE = 3
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

#ifdef CONFIG_IOMMU_API
static struct iommu_table_group_ops spapr_tce_table_group_ops;
#endif

static struct iommu_table_group *iommu_pseries_alloc_group(int node)
{
	struct iommu_table_group *table_group;

	table_group = kzalloc_node(sizeof(*table_group), GFP_KERNEL, node);
	if (!table_group)
		return NULL;

#ifdef CONFIG_IOMMU_API
	table_group->ops = &spapr_tce_table_group_ops;
	table_group->pgsizes = SZ_4K;
#endif

	table_group->tables[0] = iommu_pseries_alloc_table(node);
	if (table_group->tables[0])
		return table_group;

	kfree(table_group);
	return NULL;
}

static void iommu_pseries_free_group(struct iommu_table_group *table_group,
		const char *node_name)
{
	if (!table_group)
		return;

#ifdef CONFIG_IOMMU_API
	if (table_group->group) {
		iommu_group_put(table_group->group);
		BUG_ON(table_group->group);
	}
#endif

	/* Default DMA window table is at index 0, while DDW at 1. SR-IOV
	 * adapters only have table on index 0(if not direct mapped).
	 */
	if (table_group->tables[0])
		iommu_tce_table_put(table_group->tables[0]);

	if (table_group->tables[1])
		iommu_tce_table_put(table_group->tables[1]);

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


static void tce_clear_pSeries(struct iommu_table *tbl, long index, long npages)
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

#ifdef CONFIG_IOMMU_API
static long pseries_tce_iommu_userspace_view_alloc(struct iommu_table *tbl)
{
	unsigned long cb = ALIGN(sizeof(tbl->it_userspace[0]) * tbl->it_size, PAGE_SIZE);
	unsigned long *uas;

	if (tbl->it_indirect_levels) /* Impossible */
		return -EPERM;

	WARN_ON(tbl->it_userspace);

	uas = vzalloc(cb);
	if (!uas)
		return -ENOMEM;

	tbl->it_userspace = (__be64 *) uas;

	return 0;
}
#endif

static void tce_iommu_userspace_view_free(struct iommu_table *tbl)
{
	vfree(tbl->it_userspace);
	tbl->it_userspace = NULL;
}

static void tce_free_pSeries(struct iommu_table *tbl)
{
	if (!tbl->it_userspace)
		tce_iommu_userspace_view_free(tbl);
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
		limit = min_t(long, npages, 4096 / TCE_ENTRY_SIZE);

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
	long rpages = npages;
	unsigned long limit;

	if (!firmware_has_feature(FW_FEATURE_STUFF_TCE))
		return tce_free_pSeriesLP(tbl->it_index, tcenum,
					  tbl->it_page_shift, npages);

	do {
		limit = min_t(unsigned long, rpages, 512);

		rc = plpar_tce_stuff((u64)tbl->it_index,
				     (u64)tcenum << tbl->it_page_shift, 0, limit);

		rpages -= limit;
		tcenum += limit;
	} while (rpages > 0 && !rc);

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

struct dma_win {
	struct device_node *device;
	const struct dynamic_dma_window_prop *prop;
	bool    direct;
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

static LIST_HEAD(dma_win_list);
/* prevents races between memory on/offline and window creation */
static DEFINE_SPINLOCK(dma_win_list_lock);
/* protects initializing window twice for same device */
static DEFINE_MUTEX(dma_win_init_mutex);

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
		limit = min_t(long, num_tce, 4096 / TCE_ENTRY_SIZE);
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

struct iommu_table_ops iommu_table_pseries_ops = {
	.set = tce_build_pSeries,
	.clear = tce_clear_pSeries,
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
				long *tce, enum dma_data_direction *direction)
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

static __be64 *tce_useraddr_pSeriesLP(struct iommu_table *tbl, long index,
				      bool __always_unused alloc)
{
	return tbl->it_userspace ? &tbl->it_userspace[index - tbl->it_offset] : NULL;
}
#endif

struct iommu_table_ops iommu_table_lpar_multi_ops = {
	.set = tce_buildmulti_pSeriesLP,
#ifdef CONFIG_IOMMU_API
	.xchg_no_kill = tce_exchange_pseries,
	.useraddrptr = tce_useraddr_pSeriesLP,
#endif
	.clear = tce_freemulti_pSeriesLP,
	.get = tce_get_pSeriesLP,
	.free = tce_free_pSeries
};

#ifdef CONFIG_IOMMU_API
/*
 * When the DMA window properties might have been removed,
 * the parent node has the table_group setup on it.
 */
static struct device_node *pci_dma_find_parent_node(struct pci_dev *dev,
					       struct iommu_table_group *table_group)
{
	struct device_node *dn = pci_device_to_OF_node(dev);
	struct pci_dn *rpdn;

	for (; dn && PCI_DN(dn); dn = dn->parent) {
		rpdn = PCI_DN(dn);

		if (table_group == rpdn->table_group)
			return dn;
	}

	return NULL;
}
#endif

/*
 * Find nearest ibm,dma-window (default DMA window) or direct DMA window or
 * dynamic 64bit DMA window, walking up the device tree.
 */
static struct device_node *pci_dma_find(struct device_node *dn,
					struct dynamic_dma_window_prop *prop)
{
	const __be32 *default_prop = NULL;
	const __be32 *ddw_prop = NULL;
	struct device_node *rdn = NULL;
	bool default_win = false, ddw_win = false;

	for ( ; dn && PCI_DN(dn); dn = dn->parent) {
		default_prop = of_get_property(dn, "ibm,dma-window", NULL);
		if (default_prop) {
			rdn = dn;
			default_win = true;
		}
		ddw_prop = of_get_property(dn, DIRECT64_PROPNAME, NULL);
		if (ddw_prop) {
			rdn = dn;
			ddw_win = true;
			break;
		}
		ddw_prop = of_get_property(dn, DMA64_PROPNAME, NULL);
		if (ddw_prop) {
			rdn = dn;
			ddw_win = true;
			break;
		}

		/* At least found default window, which is the case for normal boot */
		if (default_win)
			break;
	}

	/* For PCI devices there will always be a DMA window, either on the device
	 * or parent bus
	 */
	WARN_ON(!(default_win | ddw_win));

	/* caller doesn't want to get DMA window property */
	if (!prop)
		return rdn;

	/* parse DMA window property. During normal system boot, only default
	 * DMA window is passed in OF. But, for kdump, a dedicated adapter might
	 * have both default and DDW in FDT. In this scenario, DDW takes precedence
	 * over default window.
	 */
	if (ddw_win) {
		struct dynamic_dma_window_prop *p;

		p = (struct dynamic_dma_window_prop *)ddw_prop;
		prop->liobn = p->liobn;
		prop->dma_base = p->dma_base;
		prop->tce_shift = p->tce_shift;
		prop->window_shift = p->window_shift;
	} else if (default_win) {
		unsigned long offset, size, liobn;

		of_parse_dma_window(rdn, default_prop, &liobn, &offset, &size);

		prop->liobn = cpu_to_be32((u32)liobn);
		prop->dma_base = cpu_to_be64(offset);
		prop->tce_shift = cpu_to_be32(IOMMU_PAGE_SHIFT_4K);
		prop->window_shift = cpu_to_be32(order_base_2(size));
	}

	return rdn;
}

static void pci_dma_bus_setup_pSeriesLP(struct pci_bus *bus)
{
	struct iommu_table *tbl;
	struct device_node *dn, *pdn;
	struct pci_dn *ppci;
	struct dynamic_dma_window_prop prop;

	dn = pci_bus_to_OF_node(bus);

	pr_debug("pci_dma_bus_setup_pSeriesLP: setting up bus %pOF\n",
		 dn);

	pdn = pci_dma_find(dn, &prop);

	/* In PPC architecture, there will always be DMA window on bus or one of the
	 * parent bus. During reboot, there will be ibm,dma-window property to
	 * define DMA window. For kdump, there will at least be default window or DDW
	 * or both.
	 * There is an exception to the above. In case the PE goes into frozen
	 * state, firmware may not provide ibm,dma-window property at the time
	 * of LPAR boot up.
	 */

	if (!pdn) {
		pr_debug("  no ibm,dma-window property !\n");
		return;
	}

	ppci = PCI_DN(pdn);

	pr_debug("  parent is %pOF, iommu_table: 0x%p\n",
		 pdn, ppci->table_group);

	if (!ppci->table_group) {
		ppci->table_group = iommu_pseries_alloc_group(ppci->phb->node);
		tbl = ppci->table_group->tables[0];

		iommu_table_setparms_common(tbl, ppci->phb->bus->number,
				be32_to_cpu(prop.liobn),
				be64_to_cpu(prop.dma_base),
				1ULL << be32_to_cpu(prop.window_shift),
				be32_to_cpu(prop.tce_shift), NULL,
				&iommu_table_lpar_multi_ops);

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
			      struct property *win, bool cleanup)
{
	struct dynamic_dma_window_prop *dwp;
	u64 liobn;

	dwp = win->value;
	liobn = (u64)be32_to_cpu(dwp->liobn);

	if (cleanup)
		clean_dma_window(np, dwp);
	__remove_dma_window(np, ddw_avail, liobn);
}

static void copy_property(struct device_node *pdn, const char *from, const char *to)
{
	struct property *src, *dst;

	src = of_find_property(pdn, from, NULL);
	if (!src)
		return;

	dst = kzalloc(sizeof(*dst), GFP_KERNEL);
	if (!dst)
		return;

	dst->name = kstrdup(to, GFP_KERNEL);
	dst->value = kmemdup(src->value, src->length, GFP_KERNEL);
	dst->length = src->length;
	if (!dst->name || !dst->value)
		return;

	if (of_add_property(pdn, dst)) {
		pr_err("Unable to add DMA window property for %pOF", pdn);
		goto free_prop;
	}

	return;

free_prop:
	kfree(dst->name);
	kfree(dst->value);
	kfree(dst);
}

static int remove_dma_window_named(struct device_node *np, bool remove_prop, const char *win_name,
				   bool cleanup)
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
		remove_dma_window(np, ddw_avail, win, cleanup);

	if (!remove_prop)
		return 0;

	/* Default window property if removed is lost as reset-pe doesn't restore it.
	 * Though FDT has a copy of it, the DLPAR hotplugged devices will not have a
	 * node on FDT until next reboot. So, back it up.
	 */
	if ((strcmp(win_name, "ibm,dma-window") == 0) &&
	    !of_find_property(np, "ibm,dma-window-saved", NULL))
		copy_property(np, win_name, "ibm,dma-window-saved");

	ret = of_remove_property(np, win);
	if (ret)
		pr_warn("%pOF: failed to remove DMA window property: %d\n",
			np, ret);
	return 0;
}

static bool find_existing_ddw(struct device_node *pdn, u64 *dma_addr, int *window_shift,
			      bool *direct_mapping)
{
	struct dma_win *window;
	const struct dynamic_dma_window_prop *dma64;
	bool found = false;

	spin_lock(&dma_win_list_lock);
	/* check if we already created a window and dupe that config if so */
	list_for_each_entry(window, &dma_win_list, list) {
		if (window->device == pdn) {
			dma64 = window->prop;
			*dma_addr = be64_to_cpu(dma64->dma_base);
			*window_shift = be32_to_cpu(dma64->window_shift);
			*direct_mapping = window->direct;
			found = true;
			break;
		}
	}
	spin_unlock(&dma_win_list_lock);

	return found;
}

static struct dma_win *ddw_list_new_entry(struct device_node *pdn,
					  const struct dynamic_dma_window_prop *dma64)
{
	struct dma_win *window;

	window = kzalloc(sizeof(*window), GFP_KERNEL);
	if (!window)
		return NULL;

	window->device = pdn;
	window->prop = dma64;
	window->direct = false;

	return window;
}

static void find_existing_ddw_windows_named(const char *name)
{
	int len;
	struct device_node *pdn;
	struct dma_win *window;
	const struct dynamic_dma_window_prop *dma64;

	for_each_node_with_property(pdn, name) {
		dma64 = of_get_property(pdn, name, &len);
		if (!dma64 || len < sizeof(*dma64)) {
			remove_dma_window_named(pdn, true, name, true);
			continue;
		}

		/* If at the time of system initialization, there are DDWs in OF,
		 * it means this is during kexec. DDW could be direct or dynamic.
		 * We will just mark DDWs as "dynamic" since this is kdump path,
		 * no need to worry about perforance. ddw_list_new_entry() will
		 * set window->direct = false.
		 */
		window = ddw_list_new_entry(pdn, dma64);
		if (!window) {
			of_node_put(pdn);
			break;
		}

		spin_lock(&dma_win_list_lock);
		list_add(&window->list, &dma_win_list);
		spin_unlock(&dma_win_list_lock);
	}
}

static int find_existing_ddw_windows(void)
{
	if (!firmware_has_feature(FW_FEATURE_LPAR))
		return 0;

	find_existing_ddw_windows_named(DIRECT64_PROPNAME);
	find_existing_ddw_windows_named(DMA64_PROPNAME);

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

	dev_info(&dev->dev, "ibm,query-pe-dma-windows(%x) %x %x %x returned %d, lb=%llx ps=%x wn=%d\n",
		 ddw_avail[DDW_QUERY_PE_DMA_WIN], cfg_addr, BUID_HI(buid),
		 BUID_LO(buid), ret, query->largest_available_block,
		 query->page_size, query->windows_available);

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
	resource_size_t max_addr;

#if defined(CONFIG_NUMA) && defined(CONFIG_MEMORY_HOTPLUG)
	max_addr = hot_add_drconf_memory_max();
#else
	max_addr = memblock_end_of_DRAM();
#endif

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

/*
 * Platforms support placing PHB in limited address mode starting with LoPAR
 * level 2.13 implement. In this mode, the DMA address returned by DDW is over
 * 4GB but, less than 64-bits. This benefits IO adapters that don't support
 * 64-bits for DMA addresses.
 */
static int limited_dma_window(struct pci_dev *dev, struct device_node *par_dn)
{
	int ret;
	u32 cfg_addr, reset_dma_win, las_supported;
	u64 buid;
	struct device_node *dn;
	struct pci_dn *pdn;

	ret = ddw_read_ext(par_dn, DDW_EXT_RESET_DMA_WIN, &reset_dma_win);
	if (ret)
		goto out;

	ret = ddw_read_ext(par_dn, DDW_EXT_LIMITED_ADDR_MODE, &las_supported);

	/* Limited Address Space extension available on the platform but DDW in
	 * limited addressing mode not supported
	 */
	if (!ret && !las_supported)
		ret = -EPROTO;

	if (ret) {
		dev_info(&dev->dev, "Limited Address Space for DDW not Supported, err: %d", ret);
		goto out;
	}

	dn = pci_device_to_OF_node(dev);
	pdn = PCI_DN(dn);
	buid = pdn->phb->buid;
	cfg_addr = (pdn->busno << 16) | (pdn->devfn << 8);

	ret = rtas_call(reset_dma_win, 4, 1, NULL, cfg_addr, BUID_HI(buid),
			BUID_LO(buid), 1);
	if (ret)
		dev_info(&dev->dev,
			 "ibm,reset-pe-dma-windows(%x) for Limited Addr Support: %x %x %x returned %d ",
			 reset_dma_win, cfg_addr, BUID_HI(buid), BUID_LO(buid),
			 ret);

out:
	return ret;
}

/* Return largest page shift based on "IO Page Sizes" output of ibm,query-pe-dma-window. */
static int iommu_get_page_shift(u32 query_page_size)
{
	/* Supported IO page-sizes according to LoPAR, note that 2M is out of order */
	const int shift[] = {
		__builtin_ctzll(SZ_4K),   __builtin_ctzll(SZ_64K), __builtin_ctzll(SZ_16M),
		__builtin_ctzll(SZ_32M),  __builtin_ctzll(SZ_64M), __builtin_ctzll(SZ_128M),
		__builtin_ctzll(SZ_256M), __builtin_ctzll(SZ_16G), __builtin_ctzll(SZ_2M)
	};

	int i = ARRAY_SIZE(shift) - 1;
	int ret = 0;

	/*
	 * On LoPAR, ibm,query-pe-dma-window outputs "IO Page Sizes" using a bit field:
	 * - bit 31 means 4k pages are supported,
	 * - bit 30 means 64k pages are supported, and so on.
	 * Larger pagesizes map more memory with the same amount of TCEs, so start probing them.
	 */
	for (; i >= 0 ; i--) {
		if (query_page_size & (1 << i))
			ret = max(ret, shift[i]);
	}

	return ret;
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
static bool enable_ddw(struct pci_dev *dev, struct device_node *pdn, u64 dma_mask)
{
	int len = 0, ret;
	int max_ram_len = order_base_2(ddw_memory_hotplug_max());
	struct ddw_query_response query;
	struct ddw_create_response create;
	int page_shift;
	u64 win_addr, dynamic_offset = 0;
	const char *win_name;
	struct device_node *dn;
	u32 ddw_avail[DDW_APPLICABLE_SIZE];
	struct dma_win *window;
	struct property *win64;
	struct failed_ddw_pdn *fpdn;
	bool default_win_removed = false, direct_mapping = false;
	bool dynamic_mapping = false;
	bool pmem_present;
	struct pci_dn *pci = PCI_DN(pdn);
	struct property *default_win = NULL;
	bool limited_addr_req = false, limited_addr_enabled = false;
	int dev_max_ddw;
	int ddw_sz;

	dn = of_find_node_by_type(NULL, "ibm,pmemory");
	pmem_present = dn != NULL;
	of_node_put(dn);

	mutex_lock(&dma_win_init_mutex);

	if (find_existing_ddw(pdn, &dev->dev.archdata.dma_offset, &len, &direct_mapping))
		goto out_unlock;

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

	/* DMA Limited Addressing required? This is when the driver has
	 * requested to create DDW but supports mask which is less than 64-bits
	 */
	limited_addr_req = (dma_mask != DMA_BIT_MASK(64));

	/* place the PHB in Limited Addressing mode */
	if (limited_addr_req) {
		if (limited_dma_window(dev, pdn))
			goto out_failed;

		/* PHB is in Limited address mode */
		limited_addr_enabled = true;
	}

	/*
	 * If there is no window available, remove the default DMA window,
	 * if it's present. This will make all the resources available to the
	 * new DDW window.
	 * If anything fails after this, we need to restore it, so also check
	 * for extensions presence.
	 */
	if (query.windows_available == 0) {
		int reset_win_ext;

		/* DDW + IOMMU on single window may fail if there is any allocation */
		if (iommu_table_in_use(pci->table_group->tables[0])) {
			dev_warn(&dev->dev, "current IOMMU table in use, can't be replaced.\n");
			goto out_failed;
		}

		default_win = of_find_property(pdn, "ibm,dma-window", NULL);
		if (!default_win)
			goto out_failed;

		reset_win_ext = ddw_read_ext(pdn, DDW_EXT_RESET_DMA_WIN, NULL);
		if (reset_win_ext)
			goto out_failed;

		remove_dma_window(pdn, ddw_avail, default_win, true);
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
		dev_dbg(&dev->dev, "no supported page size in mask %x",
			query.page_size);
		goto out_failed;
	}

	/* Maximum DMA window size that the device can address (in log2) */
	dev_max_ddw = fls64(dma_mask);

	/* If the device DMA mask is less than 64-bits, make sure the DMA window
	 * size is not bigger than what the device can access
	 */
	ddw_sz = min(order_base_2(query.largest_available_block << page_shift),
			dev_max_ddw);

	/*
	 * The "ibm,pmemory" can appear anywhere in the address space.
	 * Assuming it is still backed by page structs, try MAX_PHYSMEM_BITS
	 * for the upper limit and fallback to max RAM otherwise but this
	 * disables device::dma_ops_bypass.
	 */
	len = max_ram_len;
	if (pmem_present) {
		if (ddw_sz >= MAX_PHYSMEM_BITS)
			len = MAX_PHYSMEM_BITS;
		else
			dev_info(&dev->dev, "Skipping ibm,pmemory");
	}

	/* check if the available block * number of ptes will map everything */
	if (ddw_sz < len) {
		dev_dbg(&dev->dev,
			"can't map partition max 0x%llx with %llu %llu-sized pages\n",
			1ULL << len,
			query.largest_available_block,
			1ULL << page_shift);

		len = ddw_sz;
		dynamic_mapping = true;
	} else {
		direct_mapping = !default_win_removed ||
			(len == MAX_PHYSMEM_BITS) ||
			(!pmem_present && (len == max_ram_len));

		/* DDW is big enough to direct map RAM. If there is vPMEM, check
		 * if enough space is left in DDW where we can dynamically
		 * allocate TCEs for vPMEM. For now, this Hybrid sharing of DDW
		 * is only for SR-IOV devices.
		 */
		if (default_win_removed && pmem_present && !direct_mapping) {
			/* DDW is big enough to be split */
			if ((1ULL << ddw_sz) >=
			    MIN_DDW_VPMEM_DMA_WINDOW + (1ULL << max_ram_len)) {

				direct_mapping = true;

				/* offset of the Dynamic part of DDW */
				dynamic_offset = 1ULL << max_ram_len;
			}

			/* DDW will at least have dynamic allocation */
			dynamic_mapping = true;

			/* create max size DDW possible */
			len = ddw_sz;
		}
	}

	/* Even if the DDW is split into both direct mapped RAM and dynamically
	 * mapped vPMEM, the DDW property in OF will be marked as Direct.
	 */
	win_name = direct_mapping ? DIRECT64_PROPNAME : DMA64_PROPNAME;

	ret = create_ddw(dev, ddw_avail, &create, page_shift, len);
	if (ret != 0)
		goto out_failed;

	dev_dbg(&dev->dev, "created tce table LIOBN 0x%x for %pOF\n",
		  create.liobn, dn);

	win_addr = ((u64)create.addr_hi << 32) | create.addr_lo;
	win64 = ddw_property_create(win_name, create.liobn, win_addr, page_shift, len);

	if (!win64) {
		dev_info(&dev->dev,
			 "couldn't allocate property, property name, or value\n");
		goto out_remove_win;
	}

	ret = of_add_property(pdn, win64);
	if (ret) {
		dev_err(&dev->dev, "unable to add DMA window property for %pOF: %d",
			pdn, ret);
		goto out_free_prop;
	}

	window = ddw_list_new_entry(pdn, win64->value);
	if (!window)
		goto out_del_prop;

	window->direct = direct_mapping;

	if (direct_mapping) {
		/* DDW maps the whole partition, so enable direct DMA mapping */
		ret = walk_system_ram_range(0, ddw_memory_hotplug_max() >> PAGE_SHIFT,
					    win64->value, tce_setrange_multi_pSeriesLP_walk);
		if (ret) {
			dev_info(&dev->dev, "failed to map DMA window for %pOF: %d\n",
				 dn, ret);

			/* Make sure to clean DDW if any TCE was set*/
			clean_dma_window(pdn, win64->value);
			goto out_del_list;
		}
		if (default_win_removed) {
			iommu_tce_table_put(pci->table_group->tables[0]);
			pci->table_group->tables[0] = NULL;
			set_iommu_table_base(&dev->dev, NULL);
		}
	}

	if (dynamic_mapping) {
		struct iommu_table *newtbl;
		int i;
		unsigned long start = 0, end = 0;
		u64 dynamic_addr, dynamic_len;

		for (i = 0; i < ARRAY_SIZE(pci->phb->mem_resources); i++) {
			const unsigned long mask = IORESOURCE_MEM_64 | IORESOURCE_MEM;

			/* Look for MMIO32 */
			if ((pci->phb->mem_resources[i].flags & mask) == IORESOURCE_MEM) {
				start = pci->phb->mem_resources[i].start;
				end = pci->phb->mem_resources[i].end;
				break;
			}
		}

		/* New table for using DDW instead of the default DMA window */
		newtbl = iommu_pseries_alloc_table(pci->phb->node);
		if (!newtbl) {
			dev_dbg(&dev->dev, "couldn't create new IOMMU table\n");
			goto out_del_list;
		}

		/* If the DDW is split between directly mapped RAM and Dynamic
		 * mapped for TCES, offset into the DDW where the dynamic part
		 * begins.
		 */
		dynamic_addr = win_addr + dynamic_offset;
		dynamic_len = (1UL << len) - dynamic_offset;
		iommu_table_setparms_common(newtbl, pci->phb->bus->number, create.liobn,
					    dynamic_addr, dynamic_len, page_shift, NULL,
					    &iommu_table_lpar_multi_ops);
		iommu_init_table(newtbl, pci->phb->node,
				 start >> page_shift, end >> page_shift);

		pci->table_group->tables[default_win_removed ? 0 : 1] = newtbl;

		set_iommu_table_base(&dev->dev, newtbl);
	}

	if (default_win_removed) {
		/* default_win is valid here because default_win_removed == true */
		if (!of_find_property(pdn, "ibm,dma-window-saved", NULL))
			copy_property(pdn, "ibm,dma-window", "ibm,dma-window-saved");
		of_remove_property(pdn, default_win);
		dev_info(&dev->dev, "Removed default DMA window for %pOF\n", pdn);
	}

	spin_lock(&dma_win_list_lock);
	list_add(&window->list, &dma_win_list);
	spin_unlock(&dma_win_list_lock);

	dev->dev.archdata.dma_offset = win_addr;
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
	if (default_win_removed || limited_addr_enabled)
		reset_dma_window(dev, pdn);

	fpdn = kzalloc(sizeof(*fpdn), GFP_KERNEL);
	if (!fpdn)
		goto out_unlock;
	fpdn->pdn = pdn;
	list_add(&fpdn->list, &failed_ddw_pdn_list);

out_unlock:
	mutex_unlock(&dma_win_init_mutex);

	/* If we have persistent memory and the window size is not big enough
	 * to directly map both RAM and vPMEM, then we need to set DMA limit.
	 */
	if (pmem_present && direct_mapping && len != MAX_PHYSMEM_BITS)
		dev->dev.bus_dma_limit = dev->dev.archdata.dma_offset +
						(1ULL << max_ram_len);

	dev_info(&dev->dev, "lsa_required: %x, lsa_enabled: %x, direct mapping: %x\n",
			limited_addr_req, limited_addr_enabled, direct_mapping);

	return direct_mapping;
}

static __u64 query_page_size_to_mask(u32 query_page_size)
{
	const long shift[] = {
		(SZ_4K),   (SZ_64K), (SZ_16M),
		(SZ_32M),  (SZ_64M), (SZ_128M),
		(SZ_256M), (SZ_16G), (SZ_2M)
	};
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(shift); i++) {
		if (query_page_size & (1 << i))
			ret |= shift[i];
	}

	return ret;
}

static void spapr_tce_init_table_group(struct pci_dev *pdev,
				       struct device_node *pdn,
				       struct dynamic_dma_window_prop prop)
{
	struct iommu_table_group  *table_group = PCI_DN(pdn)->table_group;
	u32 ddw_avail[DDW_APPLICABLE_SIZE];

	struct ddw_query_response query;
	int ret;

	/* Only for normal boot with default window. Doesn't matter during
	 * kdump, since these will not be used during kdump.
	 */
	if (is_kdump_kernel())
		return;

	if (table_group->max_dynamic_windows_supported != 0)
		return; /* already initialized */

	table_group->tce32_start = be64_to_cpu(prop.dma_base);
	table_group->tce32_size = 1 << be32_to_cpu(prop.window_shift);

	if (!of_find_property(pdn, "ibm,dma-window", NULL))
		dev_err(&pdev->dev, "default dma window missing!\n");

	ret = of_property_read_u32_array(pdn, "ibm,ddw-applicable",
			&ddw_avail[0], DDW_APPLICABLE_SIZE);
	if (ret) {
		table_group->max_dynamic_windows_supported = -1;
		return;
	}

	ret = query_ddw(pdev, ddw_avail, &query, pdn);
	if (ret) {
		dev_err(&pdev->dev, "%s: query_ddw failed\n", __func__);
		table_group->max_dynamic_windows_supported = -1;
		return;
	}

	if (query.windows_available == 0)
		table_group->max_dynamic_windows_supported = 1;
	else
		table_group->max_dynamic_windows_supported = IOMMU_TABLE_GROUP_MAX_TABLES;

	table_group->max_levels = 1;
	table_group->pgsizes |= query_page_size_to_mask(query.page_size);
}

static void pci_dma_dev_setup_pSeriesLP(struct pci_dev *dev)
{
	struct device_node *pdn, *dn;
	struct iommu_table *tbl;
	struct pci_dn *pci;
	struct dynamic_dma_window_prop prop;

	pr_debug("pci_dma_dev_setup_pSeriesLP: %s\n", pci_name(dev));

	/* dev setup for LPAR is a little tricky, since the device tree might
	 * contain the dma-window properties per-device and not necessarily
	 * for the bus. So we need to search upwards in the tree until we
	 * either hit a dma-window property, OR find a parent with a table
	 * already allocated.
	 */
	dn = pci_device_to_OF_node(dev);
	pr_debug("  node is %pOF\n", dn);

	pdn = pci_dma_find(dn, &prop);
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

		iommu_table_setparms_common(tbl, pci->phb->bus->number,
				be32_to_cpu(prop.liobn),
				be64_to_cpu(prop.dma_base),
				1ULL << be32_to_cpu(prop.window_shift),
				be32_to_cpu(prop.tce_shift), NULL,
				&iommu_table_lpar_multi_ops);

		iommu_init_table(tbl, pci->phb->node, 0, 0);
		iommu_register_group(pci->table_group,
				pci_domain_nr(pci->phb->bus), 0);
		pr_debug("  created table: %p\n", pci->table_group);
	} else {
		pr_debug("  found DMA window, table: %p\n", pci->table_group);
	}

	spapr_tce_init_table_group(dev, pdn, prop);

	set_iommu_table_base(&dev->dev, pci->table_group->tables[0]);
	iommu_add_device(pci->table_group, &dev->dev);
}

static bool iommu_bypass_supported_pSeriesLP(struct pci_dev *pdev, u64 dma_mask)
{
	struct device_node *dn = pci_device_to_OF_node(pdev), *pdn;

	/* For DDW, DMA mask should be more than 32-bits. For mask more then
	 * 32-bits but less then 64-bits, DMA addressing is supported in
	 * Limited Addressing mode.
	 */
	if (dma_mask <= DMA_BIT_MASK(32))
		return false;

	dev_dbg(&pdev->dev, "node is %pOF\n", dn);

	/*
	 * the device tree might contain the dma-window properties
	 * per-device and not necessarily for the bus. So we need to
	 * search upwards in the tree until we either hit a dma-window
	 * property, OR find a parent with a table already allocated.
	 */
	pdn = pci_dma_find(dn, NULL);
	if (pdn && PCI_DN(pdn))
		return enable_ddw(pdev, pdn, dma_mask);

	return false;
}

#ifdef CONFIG_IOMMU_API
/*
 * A simple iommu_table_group_ops which only allows reusing the existing
 * iommu_table. This handles VFIO for POWER7 or the nested KVM.
 * The ops does not allow creating windows and only allows reusing the existing
 * one if it matches table_group->tce32_start/tce32_size/page_shift.
 */
static unsigned long spapr_tce_get_table_size(__u32 page_shift,
					      __u64 window_size, __u32 levels)
{
	unsigned long size;

	if (levels > 1)
		return ~0U;
	size = window_size >> (page_shift - 3);
	return size;
}

static struct pci_dev *iommu_group_get_first_pci_dev(struct iommu_group *group)
{
	struct pci_dev *pdev = NULL;
	int ret;

	/* No IOMMU group ? */
	if (!group)
		return NULL;

	ret = iommu_group_for_each_dev(group, &pdev, dev_has_iommu_table);
	if (!ret || !pdev)
		return NULL;
	return pdev;
}

static void restore_default_dma_window(struct pci_dev *pdev, struct device_node *pdn)
{
	reset_dma_window(pdev, pdn);
	copy_property(pdn, "ibm,dma-window-saved", "ibm,dma-window");
}

static long remove_dynamic_dma_windows(struct pci_dev *pdev, struct device_node *pdn)
{
	struct pci_dn *pci = PCI_DN(pdn);
	struct dma_win *window;
	bool direct_mapping;
	int len;

	if (find_existing_ddw(pdn, &pdev->dev.archdata.dma_offset, &len, &direct_mapping)) {
		remove_dma_window_named(pdn, true, direct_mapping ?
						   DIRECT64_PROPNAME : DMA64_PROPNAME, true);
		if (!direct_mapping) {
			WARN_ON(!pci->table_group->tables[0] && !pci->table_group->tables[1]);

			if (pci->table_group->tables[1]) {
				iommu_tce_table_put(pci->table_group->tables[1]);
				pci->table_group->tables[1] = NULL;
			} else if (pci->table_group->tables[0]) {
				/* Default window was removed and only the DDW exists */
				iommu_tce_table_put(pci->table_group->tables[0]);
				pci->table_group->tables[0] = NULL;
			}
		}
		spin_lock(&dma_win_list_lock);
		list_for_each_entry(window, &dma_win_list, list) {
			if (window->device == pdn) {
				list_del(&window->list);
				kfree(window);
				break;
			}
		}
		spin_unlock(&dma_win_list_lock);
	}

	return 0;
}

static long pseries_setup_default_iommu_config(struct iommu_table_group *table_group,
					       struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	const __be32 *default_prop;
	long liobn, offset, size;
	struct device_node *pdn;
	struct iommu_table *tbl;
	struct pci_dn *pci;

	pdn = pci_dma_find_parent_node(pdev, table_group);
	if (!pdn || !PCI_DN(pdn)) {
		dev_warn(&pdev->dev, "No table_group configured for the node %pOF\n", pdn);
		return -1;
	}
	pci = PCI_DN(pdn);

	/* The default window is restored if not present already on removal of DDW.
	 * However, if used by VFIO SPAPR sub driver, the user's order of removal of
	 * windows might have been different to not leading to auto restoration,
	 * suppose the DDW was removed first followed by the default one.
	 * So, restore the default window with reset-pe-dma call explicitly.
	 */
	restore_default_dma_window(pdev, pdn);

	default_prop = of_get_property(pdn, "ibm,dma-window", NULL);
	of_parse_dma_window(pdn, default_prop, &liobn, &offset, &size);
	tbl = iommu_pseries_alloc_table(pci->phb->node);
	if (!tbl) {
		dev_err(&pdev->dev, "couldn't create new IOMMU table\n");
		return -1;
	}

	iommu_table_setparms_common(tbl, pci->phb->bus->number, liobn, offset,
				    size, IOMMU_PAGE_SHIFT_4K, NULL,
				    &iommu_table_lpar_multi_ops);
	iommu_init_table(tbl, pci->phb->node, 0, 0);

	pci->table_group->tables[0] = tbl;
	set_iommu_table_base(&pdev->dev, tbl);

	return 0;
}

static bool is_default_window_request(struct iommu_table_group *table_group, __u32 page_shift,
				      __u64 window_size)
{
	if ((window_size <= table_group->tce32_size) &&
	    (page_shift == IOMMU_PAGE_SHIFT_4K))
		return true;

	return false;
}

static long spapr_tce_create_table(struct iommu_table_group *table_group, int num,
				   __u32 page_shift, __u64 window_size, __u32 levels,
				   struct iommu_table **ptbl)
{
	struct pci_dev *pdev = iommu_group_get_first_pci_dev(table_group->group);
	u32 ddw_avail[DDW_APPLICABLE_SIZE];
	struct ddw_create_response create;
	unsigned long liobn, offset, size;
	unsigned long start = 0, end = 0;
	struct ddw_query_response query;
	const __be32 *default_prop;
	struct failed_ddw_pdn *fpdn;
	unsigned int window_shift;
	struct device_node *pdn;
	struct iommu_table *tbl;
	struct dma_win *window;
	struct property *win64;
	struct pci_dn *pci;
	u64 win_addr;
	int len, i;
	long ret;

	if (!is_power_of_2(window_size) || levels > 1)
		return -EINVAL;

	window_shift = order_base_2(window_size);

	mutex_lock(&dma_win_init_mutex);

	ret = -ENODEV;

	pdn = pci_dma_find_parent_node(pdev, table_group);
	if (!pdn || !PCI_DN(pdn)) { /* Niether of 32s|64-bit exist! */
		dev_warn(&pdev->dev, "No dma-windows exist for the node %pOF\n", pdn);
		goto out_failed;
	}
	pci = PCI_DN(pdn);

	/* If the enable DDW failed for the pdn, dont retry! */
	list_for_each_entry(fpdn, &failed_ddw_pdn_list, list) {
		if (fpdn->pdn == pdn) {
			dev_info(&pdev->dev, "%pOF in failed DDW device list\n", pdn);
			goto out_unlock;
		}
	}

	tbl = iommu_pseries_alloc_table(pci->phb->node);
	if (!tbl) {
		dev_dbg(&pdev->dev, "couldn't create new IOMMU table\n");
		goto out_unlock;
	}

	if (num == 0) {
		bool direct_mapping;
		/* The request is not for default window? Ensure there is no DDW window already */
		if (!is_default_window_request(table_group, page_shift, window_size)) {
			if (find_existing_ddw(pdn, &pdev->dev.archdata.dma_offset, &len,
					      &direct_mapping)) {
				dev_warn(&pdev->dev, "%pOF: 64-bit window already present.", pdn);
				ret = -EPERM;
				goto out_unlock;
			}
		} else {
			/* Request is for Default window, ensure there is no DDW if there is a
			 * need to reset. reset-pe otherwise removes the DDW also
			 */
			default_prop = of_get_property(pdn, "ibm,dma-window", NULL);
			if (!default_prop) {
				if (find_existing_ddw(pdn, &pdev->dev.archdata.dma_offset, &len,
						      &direct_mapping)) {
					dev_warn(&pdev->dev, "%pOF: Attempt to create window#0 when 64-bit window is present. Preventing the attempt as that would destroy the 64-bit window",
						 pdn);
					ret = -EPERM;
					goto out_unlock;
				}

				restore_default_dma_window(pdev, pdn);

				default_prop = of_get_property(pdn, "ibm,dma-window", NULL);
				of_parse_dma_window(pdn, default_prop, &liobn, &offset, &size);
				/* Limit the default window size to window_size */
				iommu_table_setparms_common(tbl, pci->phb->bus->number, liobn,
							    offset, 1UL << window_shift,
							    IOMMU_PAGE_SHIFT_4K, NULL,
							    &iommu_table_lpar_multi_ops);
				iommu_init_table(tbl, pci->phb->node,
						 start >> IOMMU_PAGE_SHIFT_4K,
						 end >> IOMMU_PAGE_SHIFT_4K);

				table_group->tables[0] = tbl;

				mutex_unlock(&dma_win_init_mutex);

				goto exit;
			}
		}
	}

	ret = of_property_read_u32_array(pdn, "ibm,ddw-applicable",
				&ddw_avail[0], DDW_APPLICABLE_SIZE);
	if (ret) {
		dev_info(&pdev->dev, "ibm,ddw-applicable not found\n");
		goto out_failed;
	}
	ret = -ENODEV;

	pr_err("%s: Calling query %pOF\n", __func__, pdn);
	ret = query_ddw(pdev, ddw_avail, &query, pdn);
	if (ret)
		goto out_failed;
	ret = -ENODEV;

	len = window_shift;
	if (query.largest_available_block < (1ULL << (len - page_shift))) {
		dev_dbg(&pdev->dev, "can't map window 0x%llx with %llu %llu-sized pages\n",
				1ULL << len, query.largest_available_block,
				1ULL << page_shift);
		ret = -EINVAL; /* Retry with smaller window size */
		goto out_unlock;
	}

	if (create_ddw(pdev, ddw_avail, &create, page_shift, len)) {
		pr_err("%s: Create ddw failed %pOF\n", __func__, pdn);
		goto out_failed;
	}

	win_addr = ((u64)create.addr_hi << 32) | create.addr_lo;
	win64 = ddw_property_create(DMA64_PROPNAME, create.liobn, win_addr, page_shift, len);
	if (!win64)
		goto remove_window;

	ret = of_add_property(pdn, win64);
	if (ret) {
		dev_err(&pdev->dev, "unable to add DMA window property for %pOF: %ld", pdn, ret);
		goto free_property;
	}
	ret = -ENODEV;

	window = ddw_list_new_entry(pdn, win64->value);
	if (!window)
		goto remove_property;

	window->direct = false;

	for (i = 0; i < ARRAY_SIZE(pci->phb->mem_resources); i++) {
		const unsigned long mask = IORESOURCE_MEM_64 | IORESOURCE_MEM;

		/* Look for MMIO32 */
		if ((pci->phb->mem_resources[i].flags & mask) == IORESOURCE_MEM) {
			start = pci->phb->mem_resources[i].start;
			end = pci->phb->mem_resources[i].end;
				break;
		}
	}

	/* New table for using DDW instead of the default DMA window */
	iommu_table_setparms_common(tbl, pci->phb->bus->number, create.liobn, win_addr,
				    1UL << len, page_shift, NULL, &iommu_table_lpar_multi_ops);
	iommu_init_table(tbl, pci->phb->node, start >> page_shift, end >> page_shift);

	pci->table_group->tables[num] = tbl;
	set_iommu_table_base(&pdev->dev, tbl);
	pdev->dev.archdata.dma_offset = win_addr;

	spin_lock(&dma_win_list_lock);
	list_add(&window->list, &dma_win_list);
	spin_unlock(&dma_win_list_lock);

	mutex_unlock(&dma_win_init_mutex);

	goto exit;

remove_property:
	of_remove_property(pdn, win64);
free_property:
	kfree(win64->name);
	kfree(win64->value);
	kfree(win64);
remove_window:
	__remove_dma_window(pdn, ddw_avail, create.liobn);

out_failed:
	fpdn = kzalloc(sizeof(*fpdn), GFP_KERNEL);
	if (!fpdn)
		goto out_unlock;
	fpdn->pdn = pdn;
	list_add(&fpdn->list, &failed_ddw_pdn_list);

out_unlock:
	mutex_unlock(&dma_win_init_mutex);

	return ret;
exit:
	/* Allocate the userspace view */
	pseries_tce_iommu_userspace_view_alloc(tbl);
	tbl->it_allocated_size = spapr_tce_get_table_size(page_shift, window_size, levels);

	*ptbl = iommu_tce_table_get(tbl);

	return 0;
}

static bool is_default_window_table(struct iommu_table_group *table_group, struct iommu_table *tbl)
{
	if (((tbl->it_size << tbl->it_page_shift)  <= table_group->tce32_size) &&
	    (tbl->it_page_shift == IOMMU_PAGE_SHIFT_4K))
		return true;

	return false;
}

static long spapr_tce_set_window(struct iommu_table_group *table_group,
				 int num, struct iommu_table *tbl)
{
	return tbl == table_group->tables[num] ? 0 : -EPERM;
}

static long spapr_tce_unset_window(struct iommu_table_group *table_group, int num)
{
	struct pci_dev *pdev = iommu_group_get_first_pci_dev(table_group->group);
	struct device_node *dn = pci_device_to_OF_node(pdev), *pdn;
	struct iommu_table *tbl = table_group->tables[num];
	struct failed_ddw_pdn *fpdn;
	struct dma_win *window;
	const char *win_name;
	int ret = -ENODEV;

	if (!tbl) /* The table was never created OR window was never opened */
		return 0;

	mutex_lock(&dma_win_init_mutex);

	if ((num == 0) && is_default_window_table(table_group, tbl))
		win_name = "ibm,dma-window";
	else
		win_name = DMA64_PROPNAME;

	pdn = pci_dma_find(dn, NULL);
	if (!pdn || !PCI_DN(pdn)) { /* Niether of 32s|64-bit exist! */
		dev_warn(&pdev->dev, "No dma-windows exist for the node %pOF\n", pdn);
		goto out_failed;
	}

	/* Dont clear the TCEs, User should have done it */
	if (remove_dma_window_named(pdn, true, win_name, false)) {
		pr_err("%s: The existing DDW removal failed for node %pOF\n", __func__, pdn);
		goto out_failed; /* Could not remove it either! */
	}

	if (strcmp(win_name, DMA64_PROPNAME) == 0) {
		spin_lock(&dma_win_list_lock);
		list_for_each_entry(window, &dma_win_list, list) {
			if (window->device == pdn) {
				list_del(&window->list);
				kfree(window);
				break;
			}
		}
		spin_unlock(&dma_win_list_lock);
	}

	iommu_tce_table_put(table_group->tables[num]);
	table_group->tables[num] = NULL;

	ret = 0;

	goto out_unlock;

out_failed:
	fpdn = kzalloc(sizeof(*fpdn), GFP_KERNEL);
	if (!fpdn)
		goto out_unlock;
	fpdn->pdn = pdn;
	list_add(&fpdn->list, &failed_ddw_pdn_list);

out_unlock:
	mutex_unlock(&dma_win_init_mutex);

	return ret;
}

static long spapr_tce_take_ownership(struct iommu_table_group *table_group, struct device *dev)
{
	struct iommu_table *tbl = table_group->tables[0];
	struct pci_dev *pdev = to_pci_dev(dev);
	struct device_node *dn = pci_device_to_OF_node(pdev);
	struct device_node *pdn;

	/* SRIOV VFs using direct map by the host driver OR multifunction devices
	 * where the ownership was taken on the attempt by the first function
	 */
	if (!tbl && (table_group->max_dynamic_windows_supported != 1))
		return 0;

	mutex_lock(&dma_win_init_mutex);

	pdn = pci_dma_find(dn, NULL);
	if (!pdn || !PCI_DN(pdn)) { /* Niether of 32s|64-bit exist! */
		dev_warn(&pdev->dev, "No dma-windows exist for the node %pOF\n", pdn);
		mutex_unlock(&dma_win_init_mutex);
		return -1;
	}

	/*
	 * Though rtas call reset-pe removes the DDW, it doesn't clear the entries on the table
	 * if there are any. In case of direct map, the entries will be left over, which
	 * is fine for PEs with 2 DMA windows where the second window is created with create-pe
	 * at which point the table is cleared. However, on VFs having only one DMA window, the
	 * default window would end up seeing the entries left over from the direct map done
	 * on the second window. So, remove the ddw explicitly so that clean_dma_window()
	 * cleans up the entries if any.
	 */
	if (remove_dynamic_dma_windows(pdev, pdn)) {
		dev_warn(&pdev->dev, "The existing DDW removal failed for node %pOF\n", pdn);
		mutex_unlock(&dma_win_init_mutex);
		return -1;
	}

	/* The table_group->tables[0] is not null now, it must be the default window
	 * Remove it, let the userspace create it as it needs.
	 */
	if (table_group->tables[0]) {
		remove_dma_window_named(pdn, true, "ibm,dma-window", true);
		iommu_tce_table_put(tbl);
		table_group->tables[0] = NULL;
	}
	set_iommu_table_base(dev, NULL);

	mutex_unlock(&dma_win_init_mutex);

	return 0;
}

static void spapr_tce_release_ownership(struct iommu_table_group *table_group, struct device *dev)
{
	struct iommu_table *tbl = table_group->tables[0];

	if (tbl) { /* Default window already restored */
		return;
	}

	mutex_lock(&dma_win_init_mutex);

	/* Restore the default window */
	pseries_setup_default_iommu_config(table_group, dev);

	mutex_unlock(&dma_win_init_mutex);

	return;
}

static struct iommu_table_group_ops spapr_tce_table_group_ops = {
	.get_table_size = spapr_tce_get_table_size,
	.create_table = spapr_tce_create_table,
	.set_window = spapr_tce_set_window,
	.unset_window = spapr_tce_unset_window,
	.take_ownership = spapr_tce_take_ownership,
	.release_ownership = spapr_tce_release_ownership,
};
#endif

static int iommu_mem_notifier(struct notifier_block *nb, unsigned long action,
		void *data)
{
	struct dma_win *window;
	struct memory_notify *arg = data;
	int ret = 0;

	/* This notifier can get called when onlining persistent memory as well.
	 * TCEs are not pre-mapped for persistent memory. Persistent memory will
	 * always be above ddw_memory_hotplug_max()
	 */

	switch (action) {
	case MEM_GOING_ONLINE:
		spin_lock(&dma_win_list_lock);
		list_for_each_entry(window, &dma_win_list, list) {
			if (window->direct && (arg->start_pfn << PAGE_SHIFT) <
				ddw_memory_hotplug_max()) {
				ret |= tce_setrange_multi_pSeriesLP(arg->start_pfn,
						arg->nr_pages, window->prop);
			}
			/* XXX log error */
		}
		spin_unlock(&dma_win_list_lock);
		break;
	case MEM_CANCEL_ONLINE:
	case MEM_OFFLINE:
		spin_lock(&dma_win_list_lock);
		list_for_each_entry(window, &dma_win_list, list) {
			if (window->direct && (arg->start_pfn << PAGE_SHIFT) <
				ddw_memory_hotplug_max()) {
				ret |= tce_clearrange_multi_pSeriesLP(arg->start_pfn,
						arg->nr_pages, window->prop);
			}
			/* XXX log error */
		}
		spin_unlock(&dma_win_list_lock);
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
	struct dma_win *window;

	switch (action) {
	case OF_RECONFIG_DETACH_NODE:
		/*
		 * Removing the property will invoke the reconfig
		 * notifier again, which causes dead-lock on the
		 * read-write semaphore of the notifier chain. So
		 * we have to remove the property when releasing
		 * the device node.
		 */
		if (remove_dma_window_named(np, false, DIRECT64_PROPNAME, true))
			remove_dma_window_named(np, false, DMA64_PROPNAME, true);

		if (pci && pci->table_group)
			iommu_pseries_free_group(pci->table_group,
					np->full_name);

		spin_lock(&dma_win_list_lock);
		list_for_each_entry(window, &dma_win_list, list) {
			if (window->device == np) {
				list_del(&window->list);
				kfree(window);
				break;
			}
		}
		spin_unlock(&dma_win_list_lock);
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
void __init iommu_init_early_pSeries(void)
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

#ifdef CONFIG_SPAPR_TCE_IOMMU
struct iommu_group *pSeries_pci_device_group(struct pci_controller *hose,
					     struct pci_dev *pdev)
{
	struct device_node *pdn, *dn = pdev->dev.of_node;
	struct iommu_group *grp;
	struct pci_dn *pci;

	pdn = pci_dma_find(dn, NULL);
	if (!pdn || !PCI_DN(pdn))
		return ERR_PTR(-ENODEV);

	pci = PCI_DN(pdn);
	if (!pci->table_group)
		return ERR_PTR(-ENODEV);

	grp = pci->table_group->group;
	if (!grp)
		return ERR_PTR(-ENODEV);

	return iommu_group_ref_get(grp);
}
#endif
