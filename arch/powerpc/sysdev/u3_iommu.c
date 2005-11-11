/*
 * arch/powerpc/sysdev/u3_iommu.c
 *
 * Copyright (C) 2004 Olof Johansson <olof@austin.ibm.com>, IBM Corporation
 *
 * Based on pSeries_iommu.c:
 * Copyright (C) 2001 Mike Corrigan & Dave Engebretsen, IBM Corporation
 * Copyright (C) 2004 Olof Johansson <olof@austin.ibm.com>, IBM Corporation
 *
 * Dynamic DMA mapping support, Apple U3 & IBM CPC925 "DART" iommu.
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

#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/iommu.h>
#include <asm/pci-bridge.h>
#include <asm/machdep.h>
#include <asm/abs_addr.h>
#include <asm/cacheflush.h>
#include <asm/lmb.h>
#include <asm/ppc-pci.h>

#include "dart.h"

extern int iommu_force_on;

/* Physical base address and size of the DART table */
unsigned long dart_tablebase; /* exported to htab_initialize */
static unsigned long dart_tablesize;

/* Virtual base address of the DART table */
static u32 *dart_vbase;

/* Mapped base address for the dart */
static unsigned int *dart; 

/* Dummy val that entries are set to when unused */
static unsigned int dart_emptyval;

static struct iommu_table iommu_table_u3;
static int iommu_table_u3_inited;
static int dart_dirty;

#define DBG(...)

static inline void dart_tlb_invalidate_all(void)
{
	unsigned long l = 0;
	unsigned int reg;
	unsigned long limit;

	DBG("dart: flush\n");

	/* To invalidate the DART, set the DARTCNTL_FLUSHTLB bit in the
	 * control register and wait for it to clear.
	 *
	 * Gotcha: Sometimes, the DART won't detect that the bit gets
	 * set. If so, clear it and set it again.
	 */ 

	limit = 0;

retry:
	reg = in_be32((unsigned int *)dart+DARTCNTL);
	reg |= DARTCNTL_FLUSHTLB;
	out_be32((unsigned int *)dart+DARTCNTL, reg);

	l = 0;
	while ((in_be32((unsigned int *)dart+DARTCNTL) & DARTCNTL_FLUSHTLB) &&
		l < (1L<<limit)) {
		l++;
	}
	if (l == (1L<<limit)) {
		if (limit < 4) {
			limit++;
		        reg = in_be32((unsigned int *)dart+DARTCNTL);
		        reg &= ~DARTCNTL_FLUSHTLB;
		        out_be32((unsigned int *)dart+DARTCNTL, reg);
			goto retry;
		} else
			panic("U3-DART: TLB did not flush after waiting a long "
			      "time. Buggy U3 ?");
	}
}

static void dart_flush(struct iommu_table *tbl)
{
	if (dart_dirty)
		dart_tlb_invalidate_all();
	dart_dirty = 0;
}

static void dart_build(struct iommu_table *tbl, long index, 
		       long npages, unsigned long uaddr,
		       enum dma_data_direction direction)
{
	unsigned int *dp;
	unsigned int rpn;

	DBG("dart: build at: %lx, %lx, addr: %x\n", index, npages, uaddr);

	index <<= DART_PAGE_FACTOR;
	npages <<= DART_PAGE_FACTOR;

	dp = ((unsigned int*)tbl->it_base) + index;
	
	/* On U3, all memory is contigous, so we can move this
	 * out of the loop.
	 */
	while (npages--) {
		rpn = virt_to_abs(uaddr) >> DART_PAGE_SHIFT;

		*(dp++) = DARTMAP_VALID | (rpn & DARTMAP_RPNMASK);

		rpn++;
		uaddr += DART_PAGE_SIZE;
	}

	dart_dirty = 1;
}


static void dart_free(struct iommu_table *tbl, long index, long npages)
{
	unsigned int *dp;
	
	/* We don't worry about flushing the TLB cache. The only drawback of
	 * not doing it is that we won't catch buggy device drivers doing
	 * bad DMAs, but then no 32-bit architecture ever does either.
	 */

	DBG("dart: free at: %lx, %lx\n", index, npages);

	index <<= DART_PAGE_FACTOR;
	npages <<= DART_PAGE_FACTOR;

	dp  = ((unsigned int *)tbl->it_base) + index;
		
	while (npages--)
		*(dp++) = dart_emptyval;
}


static int dart_init(struct device_node *dart_node)
{
	unsigned int regword;
	unsigned int i;
	unsigned long tmp;

	if (dart_tablebase == 0 || dart_tablesize == 0) {
		printk(KERN_INFO "U3-DART: table not allocated, using direct DMA\n");
		return -ENODEV;
	}

	/* Make sure nothing from the DART range remains in the CPU cache
	 * from a previous mapping that existed before the kernel took
	 * over
	 */
	flush_dcache_phys_range(dart_tablebase, dart_tablebase + dart_tablesize);

	/* Allocate a spare page to map all invalid DART pages. We need to do
	 * that to work around what looks like a problem with the HT bridge
	 * prefetching into invalid pages and corrupting data
	 */
	tmp = lmb_alloc(DART_PAGE_SIZE, DART_PAGE_SIZE);
	if (!tmp)
		panic("U3-DART: Cannot allocate spare page!");
	dart_emptyval = DARTMAP_VALID | ((tmp >> DART_PAGE_SHIFT) & DARTMAP_RPNMASK);

	/* Map in DART registers. FIXME: Use device node to get base address */
	dart = ioremap(DART_BASE, 0x7000);
	if (dart == NULL)
		panic("U3-DART: Cannot map registers!");

	/* Set initial control register contents: table base, 
	 * table size and enable bit
	 */
	regword = DARTCNTL_ENABLE | 
		((dart_tablebase >> DART_PAGE_SHIFT) << DARTCNTL_BASE_SHIFT) |
		(((dart_tablesize >> DART_PAGE_SHIFT) & DARTCNTL_SIZE_MASK)
				 << DARTCNTL_SIZE_SHIFT);
	dart_vbase = ioremap(virt_to_abs(dart_tablebase), dart_tablesize);

	/* Fill initial table */
	for (i = 0; i < dart_tablesize/4; i++)
		dart_vbase[i] = dart_emptyval;

	/* Initialize DART with table base and enable it. */
	out_be32((unsigned int *)dart, regword);

	/* Invalidate DART to get rid of possible stale TLBs */
	dart_tlb_invalidate_all();

	printk(KERN_INFO "U3/CPC925 DART IOMMU initialized\n");

	return 0;
}

static void iommu_table_u3_setup(void)
{
	iommu_table_u3.it_busno = 0;
	iommu_table_u3.it_offset = 0;
	/* it_size is in number of entries */
	iommu_table_u3.it_size = (dart_tablesize / sizeof(u32)) >> DART_PAGE_FACTOR;

	/* Initialize the common IOMMU code */
	iommu_table_u3.it_base = (unsigned long)dart_vbase;
	iommu_table_u3.it_index = 0;
	iommu_table_u3.it_blocksize = 1;
	iommu_init_table(&iommu_table_u3);

	/* Reserve the last page of the DART to avoid possible prefetch
	 * past the DART mapped area
	 */
	set_bit(iommu_table_u3.it_size - 1, iommu_table_u3.it_map);
}

static void iommu_dev_setup_u3(struct pci_dev *dev)
{
	struct device_node *dn;

	/* We only have one iommu table on the mac for now, which makes
	 * things simple. Setup all PCI devices to point to this table
	 *
	 * We must use pci_device_to_OF_node() to make sure that
	 * we get the real "final" pointer to the device in the
	 * pci_dev sysdata and not the temporary PHB one
	 */
	dn = pci_device_to_OF_node(dev);

	if (dn)
		PCI_DN(dn)->iommu_table = &iommu_table_u3;
}

static void iommu_bus_setup_u3(struct pci_bus *bus)
{
	struct device_node *dn;

	if (!iommu_table_u3_inited) {
		iommu_table_u3_inited = 1;
		iommu_table_u3_setup();
	}

	dn = pci_bus_to_OF_node(bus);

	if (dn)
		PCI_DN(dn)->iommu_table = &iommu_table_u3;
}

static void iommu_dev_setup_null(struct pci_dev *dev) { }
static void iommu_bus_setup_null(struct pci_bus *bus) { }

void iommu_init_early_u3(void)
{
	struct device_node *dn;

	/* Find the DART in the device-tree */
	dn = of_find_compatible_node(NULL, "dart", "u3-dart");
	if (dn == NULL)
		return;

	/* Setup low level TCE operations for the core IOMMU code */
	ppc_md.tce_build = dart_build;
	ppc_md.tce_free  = dart_free;
	ppc_md.tce_flush = dart_flush;

	/* Initialize the DART HW */
	if (dart_init(dn)) {
		/* If init failed, use direct iommu and null setup functions */
		ppc_md.iommu_dev_setup = iommu_dev_setup_null;
		ppc_md.iommu_bus_setup = iommu_bus_setup_null;

		/* Setup pci_dma ops */
		pci_direct_iommu_init();
	} else {
		ppc_md.iommu_dev_setup = iommu_dev_setup_u3;
		ppc_md.iommu_bus_setup = iommu_bus_setup_u3;

		/* Setup pci_dma ops */
		pci_iommu_init();
	}
}


void __init alloc_u3_dart_table(void)
{
	/* Only reserve DART space if machine has more than 2GB of RAM
	 * or if requested with iommu=on on cmdline.
	 */
	if (lmb_end_of_DRAM() <= 0x80000000ull && !iommu_force_on)
		return;

	/* 512 pages (2MB) is max DART tablesize. */
	dart_tablesize = 1UL << 21;
	/* 16MB (1 << 24) alignment. We allocate a full 16Mb chuck since we
	 * will blow up an entire large page anyway in the kernel mapping
	 */
	dart_tablebase = (unsigned long)
		abs_to_virt(lmb_alloc_base(1UL<<24, 1UL<<24, 0x80000000L));

	printk(KERN_INFO "U3-DART allocated at: %lx\n", dart_tablebase);
}
