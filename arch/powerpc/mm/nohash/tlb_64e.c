// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2008,2009 Ben Herrenschmidt <benh@kernel.crashing.org>
 *                     IBM Corp.
 *
 *  Derived from arch/ppc/mm/init.c:
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/memblock.h>

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/tlb.h>
#include <asm/text-patching.h>
#include <asm/cputhreads.h>

#include <mm/mmu_decl.h>

/* The variables below are currently only used on 64-bit Book3E
 * though this will probably be made common with other nohash
 * implementations at some point
 */
static int mmu_pte_psize;	/* Page size used for PTE pages */
int mmu_vmemmap_psize;		/* Page size used for the virtual mem map */
int book3e_htw_mode;		/* HW tablewalk?  Value is PPC_HTW_* */
unsigned long linear_map_top;	/* Top of linear mapping */


/*
 * Number of bytes to add to SPRN_SPRG_TLB_EXFRAME on crit/mcheck/debug
 * exceptions.  This is used for bolted and e6500 TLB miss handlers which
 * do not modify this SPRG in the TLB miss code; for other TLB miss handlers,
 * this is set to zero.
 */
int extlb_level_exc;

/*
 * Handling of virtual linear page tables or indirect TLB entries
 * flushing when PTE pages are freed
 */
void tlb_flush_pgtable(struct mmu_gather *tlb, unsigned long address)
{
	int tsize = mmu_psize_defs[mmu_pte_psize].shift - 10;

	if (book3e_htw_mode != PPC_HTW_NONE) {
		unsigned long start = address & PMD_MASK;
		unsigned long end = address + PMD_SIZE;
		unsigned long size = 1UL << mmu_psize_defs[mmu_pte_psize].shift;

		/* This isn't the most optimal, ideally we would factor out the
		 * while preempt & CPU mask mucking around, or even the IPI but
		 * it will do for now
		 */
		while (start < end) {
			__flush_tlb_page(tlb->mm, start, tsize, 1);
			start += size;
		}
	} else {
		unsigned long rmask = 0xf000000000000000ul;
		unsigned long rid = (address & rmask) | 0x1000000000000000ul;
		unsigned long vpte = address & ~rmask;

		vpte = (vpte >> (PAGE_SHIFT - 3)) & ~0xffful;
		vpte |= rid;
		__flush_tlb_page(tlb->mm, vpte, tsize, 0);
	}
}

static void __init setup_page_sizes(void)
{
	unsigned int tlb0cfg;
	unsigned int eptcfg;
	int psize;

	unsigned int mmucfg = mfspr(SPRN_MMUCFG);

	if ((mmucfg & MMUCFG_MAVN) == MMUCFG_MAVN_V1) {
		unsigned int tlb1cfg = mfspr(SPRN_TLB1CFG);
		unsigned int min_pg, max_pg;

		min_pg = (tlb1cfg & TLBnCFG_MINSIZE) >> TLBnCFG_MINSIZE_SHIFT;
		max_pg = (tlb1cfg & TLBnCFG_MAXSIZE) >> TLBnCFG_MAXSIZE_SHIFT;

		for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
			struct mmu_psize_def *def;
			unsigned int shift;

			def = &mmu_psize_defs[psize];
			shift = def->shift;

			if (shift == 0 || shift & 1)
				continue;

			/* adjust to be in terms of 4^shift Kb */
			shift = (shift - 10) >> 1;

			if ((shift >= min_pg) && (shift <= max_pg))
				def->flags |= MMU_PAGE_SIZE_DIRECT;
		}

		goto out;
	}

	if ((mmucfg & MMUCFG_MAVN) == MMUCFG_MAVN_V2) {
		u32 tlb1cfg, tlb1ps;

		tlb0cfg = mfspr(SPRN_TLB0CFG);
		tlb1cfg = mfspr(SPRN_TLB1CFG);
		tlb1ps = mfspr(SPRN_TLB1PS);
		eptcfg = mfspr(SPRN_EPTCFG);

		if ((tlb1cfg & TLBnCFG_IND) && (tlb0cfg & TLBnCFG_PT))
			book3e_htw_mode = PPC_HTW_E6500;

		/*
		 * We expect 4K subpage size and unrestricted indirect size.
		 * The lack of a restriction on indirect size is a Freescale
		 * extension, indicated by PSn = 0 but SPSn != 0.
		 */
		if (eptcfg != 2)
			book3e_htw_mode = PPC_HTW_NONE;

		for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
			struct mmu_psize_def *def = &mmu_psize_defs[psize];

			if (!def->shift)
				continue;

			if (tlb1ps & (1U << (def->shift - 10))) {
				def->flags |= MMU_PAGE_SIZE_DIRECT;

				if (book3e_htw_mode && psize == MMU_PAGE_2M)
					def->flags |= MMU_PAGE_SIZE_INDIRECT;
			}
		}

		goto out;
	}
out:
	/* Cleanup array and print summary */
	pr_info("MMU: Supported page sizes\n");
	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		struct mmu_psize_def *def = &mmu_psize_defs[psize];
		const char *__page_type_names[] = {
			"unsupported",
			"direct",
			"indirect",
			"direct & indirect"
		};
		if (def->flags == 0) {
			def->shift = 0;
			continue;
		}
		pr_info("  %8ld KB as %s\n", 1ul << (def->shift - 10),
			__page_type_names[def->flags & 0x3]);
	}
}

/*
 * Early initialization of the MMU TLB code
 */
static void early_init_this_mmu(void)
{
	unsigned int mas4;

	/* Set MAS4 based on page table setting */

	mas4 = 0x4 << MAS4_WIMGED_SHIFT;
	switch (book3e_htw_mode) {
	case PPC_HTW_E6500:
		mas4 |= MAS4_INDD;
		mas4 |= BOOK3E_PAGESZ_2M << MAS4_TSIZED_SHIFT;
		mas4 |= MAS4_TLBSELD(1);
		mmu_pte_psize = MMU_PAGE_2M;
		break;

	case PPC_HTW_NONE:
		mas4 |=	BOOK3E_PAGESZ_4K << MAS4_TSIZED_SHIFT;
		mmu_pte_psize = mmu_virtual_psize;
		break;
	}
	mtspr(SPRN_MAS4, mas4);

	unsigned int num_cams;
	bool map = true;

	/* use a quarter of the TLBCAM for bolted linear map */
	num_cams = (mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY) / 4;

	/*
	 * Only do the mapping once per core, or else the
	 * transient mapping would cause problems.
	 */
#ifdef CONFIG_SMP
	if (hweight32(get_tensr()) > 1)
		map = false;
#endif

	if (map)
		linear_map_top = map_mem_in_cams(linear_map_top,
						 num_cams, false, true);

	/* A sync won't hurt us after mucking around with
	 * the MMU configuration
	 */
	mb();
}

static void __init early_init_mmu_global(void)
{
	/*
	 * Freescale booke only supports 4K pages in TLB0, so use that.
	 */
	mmu_vmemmap_psize = MMU_PAGE_4K;

	/* XXX This code only checks for TLB 0 capabilities and doesn't
	 *     check what page size combos are supported by the HW. It
	 *     also doesn't handle the case where a separate array holds
	 *     the IND entries from the array loaded by the PT.
	 */
	/* Look for supported page sizes */
	setup_page_sizes();

	/*
	 * If we want to use HW tablewalk, enable it by patching the TLB miss
	 * handlers to branch to the one dedicated to it.
	 */
	extlb_level_exc = EX_TLB_SIZE;
	switch (book3e_htw_mode) {
	case PPC_HTW_E6500:
		patch_exception(0x1c0, exc_data_tlb_miss_e6500_book3e);
		patch_exception(0x1e0, exc_instruction_tlb_miss_e6500_book3e);
		break;
	}

	pr_info("MMU: Book3E HW tablewalk %s\n",
		book3e_htw_mode != PPC_HTW_NONE ? "enabled" : "not supported");

	/* Set the global containing the top of the linear mapping
	 * for use by the TLB miss code
	 */
	linear_map_top = memblock_end_of_DRAM();

	ioremap_bot = IOREMAP_BASE;
}

static void __init early_mmu_set_memory_limit(void)
{
	/*
	 * Limit memory so we dont have linear faults.
	 * Unlike memblock_set_current_limit, which limits
	 * memory available during early boot, this permanently
	 * reduces the memory available to Linux.  We need to
	 * do this because highmem is not supported on 64-bit.
	 */
	memblock_enforce_memory_limit(linear_map_top);

	memblock_set_current_limit(linear_map_top);
}

/* boot cpu only */
void __init early_init_mmu(void)
{
	early_init_mmu_global();
	early_init_this_mmu();
	early_mmu_set_memory_limit();
}

void early_init_mmu_secondary(void)
{
	early_init_this_mmu();
}

void setup_initial_memory_limit(phys_addr_t first_memblock_base,
				phys_addr_t first_memblock_size)
{
	/*
	 * On FSL Embedded 64-bit, usually all RAM is bolted, but with
	 * unusual memory sizes it's possible for some RAM to not be mapped
	 * (such RAM is not used at all by Linux, since we don't support
	 * highmem on 64-bit).  We limit ppc64_rma_size to what would be
	 * mappable if this memblock is the only one.  Additional memblocks
	 * can only increase, not decrease, the amount that ends up getting
	 * mapped.  We still limit max to 1G even if we'll eventually map
	 * more.  This is due to what the early init code is set up to do.
	 *
	 * We crop it to the size of the first MEMBLOCK to
	 * avoid going over total available memory just in case...
	 */
	unsigned long linear_sz;
	unsigned int num_cams;

	/* use a quarter of the TLBCAM for bolted linear map */
	num_cams = (mfspr(SPRN_TLB1CFG) & TLBnCFG_N_ENTRY) / 4;

	linear_sz = map_mem_in_cams(first_memblock_size, num_cams, true, true);
	ppc64_rma_size = min_t(u64, linear_sz, 0x40000000);

	/* Finally limit subsequent allocations */
	memblock_set_current_limit(first_memblock_base + ppc64_rma_size);
}
