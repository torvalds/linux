// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * arch/powerpc/sysdev/dart_iommu.c
 *
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 * Copyright (C) 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>,
 *                    IBM Corporation
 *
 * Based on pSeries_iommu.c:
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * Copyright (C) 2004 Olof Johansson <olof@lixom.net>, IBM Corporation
 *
 * Dynamic DMA mapping support, Apple U3, U4 & IBM CPC925 "DART" iommu.
 */

#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <linux/suspend.h>
#include <linux/memblock.h>
#include <linux/gfp.h>
#include <linux/of_address.h>
#include <asm/io.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/cacheflush.h>
#include <asm/ppc-pci.h>

#include "dart.h"

/* DART table address and size */
static u32 *dart_tablebase;
static unsigned long dart_tablesize;

/* Mapped base address for the dart */
static unsigned int __iomem *dart;

/* Dummy val that entries are set to when unused */
static unsigned int dart_emptyval;

static struct iommu_table iommu_table_dart;
static int iommu_table_dart_inited;
static int dart_dirty;
static int dart_is_u4;

#define DART_U4_BYPASS_BASE	0x8000000000ull

#define DBG(...)

static DEFINE_SPINLOCK(invalidate_lock);

static inline void dart_tlb_invalidate_all(void)
{
	unsigned long l = 0;
	unsigned int reg, inv_bit;
	unsigned long limit;
	unsigned long flags;

	spin_lock_irqsave(&invalidate_lock, flags);

	DBG("dart: flush\n");

	/* To invalidate the DART, set the DARTCNTL_FLUSHTLB bit in the
	 * control register and wait for it to clear.
	 *
	 * Gotcha: Sometimes, the DART won't detect that the bit gets
	 * set. If so, clear it and set it again.
	 */

	limit = 0;

	inv_bit = dart_is_u4 ? DART_CNTL_U4_FLUSHTLB : DART_CNTL_U3_FLUSHTLB;
retry:
	l = 0;
	reg = DART_IN(DART_CNTL);
	reg |= inv_bit;
	DART_OUT(DART_CNTL, reg);

	while ((DART_IN(DART_CNTL) & inv_bit) && l < (1L << limit))
		l++;
	if (l == (1L << limit)) {
		if (limit < 4) {
			limit++;
			reg = DART_IN(DART_CNTL);
			reg &= ~inv_bit;
			DART_OUT(DART_CNTL, reg);
			goto retry;
		} else
			panic("DART: TLB did not flush after waiting a long "
			      "time. Buggy U3 ?");
	}

	spin_unlock_irqrestore(&invalidate_lock, flags);
}

static inline void dart_tlb_invalidate_one(unsigned long bus_rpn)
{
	unsigned int reg;
	unsigned int l, limit;
	unsigned long flags;

	spin_lock_irqsave(&invalidate_lock, flags);

	reg = DART_CNTL_U4_ENABLE | DART_CNTL_U4_IONE |
		(bus_rpn & DART_CNTL_U4_IONE_MASK);
	DART_OUT(DART_CNTL, reg);

	limit = 0;
wait_more:
	l = 0;
	while ((DART_IN(DART_CNTL) & DART_CNTL_U4_IONE) && l < (1L << limit)) {
		rmb();
		l++;
	}

	if (l == (1L << limit)) {
		if (limit < 4) {
			limit++;
			goto wait_more;
		} else
			panic("DART: TLB did not flush after waiting a long "
			      "time. Buggy U4 ?");
	}

	spin_unlock_irqrestore(&invalidate_lock, flags);
}

static void dart_cache_sync(unsigned int *base, unsigned int count)
{
	/*
	 * We add 1 to the number of entries to flush, following a
	 * comment in Darwin indicating that the memory controller
	 * can prefetch unmapped memory under some circumstances.
	 */
	unsigned long start = (unsigned long)base;
	unsigned long end = start + (count + 1) * sizeof(unsigned int);
	unsigned int tmp;

	/* Perform a standard cache flush */
	flush_dcache_range(start, end);

	/*
	 * Perform the sequence described in the CPC925 manual to
	 * ensure all the data gets to a point the cache incoherent
	 * DART hardware will see.
	 */
	asm volatile(" sync;"
		     " isync;"
		     " dcbf 0,%1;"
		     " sync;"
		     " isync;"
		     " lwz %0,0(%1);"
		     " isync" : "=r" (tmp) : "r" (end) : "memory");
}

static void dart_flush(struct iommu_table *tbl)
{
	mb();
	if (dart_dirty) {
		dart_tlb_invalidate_all();
		dart_dirty = 0;
	}
}

static int dart_build(struct iommu_table *tbl, long index,
		       long npages, unsigned long uaddr,
		       enum dma_data_direction direction,
		       unsigned long attrs)
{
	unsigned int *dp, *orig_dp;
	unsigned int rpn;
	long l;

	DBG("dart: build at: %lx, %lx, addr: %x\n", index, npages, uaddr);

	orig_dp = dp = ((unsigned int*)tbl->it_base) + index;

	/* On U3, all memory is contiguous, so we can move this
	 * out of the loop.
	 */
	l = npages;
	while (l--) {
		rpn = __pa(uaddr) >> DART_PAGE_SHIFT;

		*(dp++) = DARTMAP_VALID | (rpn & DARTMAP_RPNMASK);

		uaddr += DART_PAGE_SIZE;
	}
	dart_cache_sync(orig_dp, npages);

	if (dart_is_u4) {
		rpn = index;
		while (npages--)
			dart_tlb_invalidate_one(rpn++);
	} else {
		dart_dirty = 1;
	}
	return 0;
}


static void dart_free(struct iommu_table *tbl, long index, long npages)
{
	unsigned int *dp, *orig_dp;
	long orig_npages = npages;

	/* We don't worry about flushing the TLB cache. The only drawback of
	 * not doing it is that we won't catch buggy device drivers doing
	 * bad DMAs, but then no 32-bit architecture ever does either.
	 */

	DBG("dart: free at: %lx, %lx\n", index, npages);

	orig_dp = dp  = ((unsigned int *)tbl->it_base) + index;

	while (npages--)
		*(dp++) = dart_emptyval;

	dart_cache_sync(orig_dp, orig_npages);
}

static void __init allocate_dart(void)
{
	unsigned long tmp;

	/* 512 pages (2MB) is max DART tablesize. */
	dart_tablesize = 1UL << 21;

	/*
	 * 16MB (1 << 24) alignment. We allocate a full 16Mb chuck since we
	 * will blow up an entire large page anyway in the kernel mapping.
	 */
	dart_tablebase = memblock_alloc_try_nid_raw(SZ_16M, SZ_16M,
					MEMBLOCK_LOW_LIMIT, SZ_2G,
					NUMA_NO_NODE);
	if (!dart_tablebase)
		panic("Failed to allocate 16MB below 2GB for DART table\n");

	/* Allocate a spare page to map all invalid DART pages. We need to do
	 * that to work around what looks like a problem with the HT bridge
	 * prefetching into invalid pages and corrupting data
	 */
	tmp = memblock_phys_alloc(DART_PAGE_SIZE, DART_PAGE_SIZE);
	if (!tmp)
		panic("DART: table allocation failed\n");

	dart_emptyval = DARTMAP_VALID | ((tmp >> DART_PAGE_SHIFT) &
					 DARTMAP_RPNMASK);

	printk(KERN_INFO "DART table allocated at: %p\n", dart_tablebase);
}

static int __init dart_init(struct device_node *dart_node)
{
	unsigned int i;
	unsigned long base, size;
	struct resource r;

	/* IOMMU disabled by the user ? bail out */
	if (iommu_is_off)
		return -ENODEV;

	/*
	 * Only use the DART if the machine has more than 1GB of RAM
	 * or if requested with iommu=on on cmdline.
	 *
	 * 1GB of RAM is picked as limit because some default devices
	 * (i.e. Airport Extreme) have 30 bit address range limits.
	 */

	if (!iommu_force_on && memblock_end_of_DRAM() <= 0x40000000ull)
		return -ENODEV;

	/* Get DART registers */
	if (of_address_to_resource(dart_node, 0, &r))
		panic("DART: can't get register base ! ");

	/* Map in DART registers */
	dart = ioremap(r.start, resource_size(&r));
	if (dart == NULL)
		panic("DART: Cannot map registers!");

	/* Allocate the DART and dummy page */
	allocate_dart();

	/* Fill initial table */
	for (i = 0; i < dart_tablesize/4; i++)
		dart_tablebase[i] = dart_emptyval;

	/* Push to memory */
	dart_cache_sync(dart_tablebase, dart_tablesize / sizeof(u32));

	/* Initialize DART with table base and enable it. */
	base = ((unsigned long)dart_tablebase) >> DART_PAGE_SHIFT;
	size = dart_tablesize >> DART_PAGE_SHIFT;
	if (dart_is_u4) {
		size &= DART_SIZE_U4_SIZE_MASK;
		DART_OUT(DART_BASE_U4, base);
		DART_OUT(DART_SIZE_U4, size);
		DART_OUT(DART_CNTL, DART_CNTL_U4_ENABLE);
	} else {
		size &= DART_CNTL_U3_SIZE_MASK;
		DART_OUT(DART_CNTL,
			 DART_CNTL_U3_ENABLE |
			 (base << DART_CNTL_U3_BASE_SHIFT) |
			 (size << DART_CNTL_U3_SIZE_SHIFT));
	}

	/* Invalidate DART to get rid of possible stale TLBs */
	dart_tlb_invalidate_all();

	printk(KERN_INFO "DART IOMMU initialized for %s type chipset\n",
	       dart_is_u4 ? "U4" : "U3");

	return 0;
}

static struct iommu_table_ops iommu_dart_ops = {
	.set = dart_build,
	.clear = dart_free,
	.flush = dart_flush,
};

static void iommu_table_dart_setup(void)
{
	iommu_table_dart.it_busno = 0;
	iommu_table_dart.it_offset = 0;
	/* it_size is in number of entries */
	iommu_table_dart.it_size = dart_tablesize / sizeof(u32);
	iommu_table_dart.it_page_shift = IOMMU_PAGE_SHIFT_4K;

	/* Initialize the common IOMMU code */
	iommu_table_dart.it_base = (unsigned long)dart_tablebase;
	iommu_table_dart.it_index = 0;
	iommu_table_dart.it_blocksize = 1;
	iommu_table_dart.it_ops = &iommu_dart_ops;
	if (!iommu_init_table(&iommu_table_dart, -1, 0, 0))
		panic("Failed to initialize iommu table");

	/* Reserve the last page of the DART to avoid possible prefetch
	 * past the DART mapped area
	 */
	set_bit(iommu_table_dart.it_size - 1, iommu_table_dart.it_map);
}

static void pci_dma_bus_setup_dart(struct pci_bus *bus)
{
	if (!iommu_table_dart_inited) {
		iommu_table_dart_inited = 1;
		iommu_table_dart_setup();
	}
}

static bool dart_device_on_pcie(struct device *dev)
{
	struct device_node *np = of_node_get(dev->of_node);

	while(np) {
		if (of_device_is_compatible(np, "U4-pcie") ||
		    of_device_is_compatible(np, "u4-pcie")) {
			of_node_put(np);
			return true;
		}
		np = of_get_next_parent(np);
	}
	return false;
}

static void pci_dma_dev_setup_dart(struct pci_dev *dev)
{
	if (dart_is_u4 && dart_device_on_pcie(&dev->dev))
		dev->dev.archdata.dma_offset = DART_U4_BYPASS_BASE;
	set_iommu_table_base(&dev->dev, &iommu_table_dart);
}

static bool iommu_bypass_supported_dart(struct pci_dev *dev, u64 mask)
{
	return dart_is_u4 &&
		dart_device_on_pcie(&dev->dev) &&
		mask >= DMA_BIT_MASK(40);
}

void __init iommu_init_early_dart(struct pci_controller_ops *controller_ops)
{
	struct device_node *dn;

	/* Find the DART in the device-tree */
	dn = of_find_compatible_node(NULL, "dart", "u3-dart");
	if (dn == NULL) {
		dn = of_find_compatible_node(NULL, "dart", "u4-dart");
		if (dn == NULL)
			return;	/* use default direct_dma_ops */
		dart_is_u4 = 1;
	}

	/* Initialize the DART HW */
	if (dart_init(dn) != 0) {
		of_node_put(dn);
		return;
	}
	/*
	 * U4 supports a DART bypass, we use it for 64-bit capable devices to
	 * improve performance.  However, that only works for devices connected
	 * to the U4 own PCIe interface, not bridged through hypertransport.
	 * We need the device to support at least 40 bits of addresses.
	 */
	controller_ops->dma_dev_setup = pci_dma_dev_setup_dart;
	controller_ops->dma_bus_setup = pci_dma_bus_setup_dart;
	controller_ops->iommu_bypass_supported = iommu_bypass_supported_dart;

	/* Setup pci_dma ops */
	set_pci_dma_ops(&dma_iommu_ops);
	of_node_put(dn);
}

#ifdef CONFIG_PM
static void iommu_dart_restore(void)
{
	dart_cache_sync(dart_tablebase, dart_tablesize / sizeof(u32));
	dart_tlb_invalidate_all();
}

static int __init iommu_init_late_dart(void)
{
	if (!dart_tablebase)
		return 0;

	ppc_md.iommu_restore = iommu_dart_restore;

	return 0;
}

late_initcall(iommu_init_late_dart);
#endif /* CONFIG_PM */
