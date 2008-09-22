/*
 * Set up paging and the MMU.
 *
 * Copyright (C) 2000-2003, Axis Communications AB.
 *
 * Authors:   Bjorn Wesen <bjornw@axis.com>
 *            Tobias Anderberg <tobiasa@axis.com>, CRISv32 port.
 */
#include <linux/mmzone.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/types.h>
#include <asm/mmu.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/arch/hwregs/asm/mmu_defs_asm.h>
#include <asm/arch/hwregs/supp_reg.h>

extern void tlb_init(void);

/*
 * The kernel is already mapped with linear mapping at kseg_c so there's no
 * need to map it with a page table. However, head.S also temporarily mapped it
 * at kseg_4 thus the ksegs are set up again. Also clear the TLB and do various
 * other paging stuff.
 */
void __init
cris_mmu_init(void)
{
	unsigned long mmu_config;
	unsigned long mmu_kbase_hi;
	unsigned long mmu_kbase_lo;
	unsigned short mmu_page_id;

	/*
	 * Make sure the current pgd table points to something sane, even if it
	 * is most probably not used until the next switch_mm.
	 */
	per_cpu(current_pgd, smp_processor_id()) = init_mm.pgd;

#ifdef CONFIG_SMP
	{
		pgd_t **pgd;
		pgd = (pgd_t**)&per_cpu(current_pgd, smp_processor_id());
		SUPP_BANK_SEL(1);
		SUPP_REG_WR(RW_MM_TLB_PGD, pgd);
		SUPP_BANK_SEL(2);
		SUPP_REG_WR(RW_MM_TLB_PGD, pgd);
	}
#endif

	/* Initialise the TLB. Function found in tlb.c. */
	tlb_init();

	/* Enable exceptions and initialize the kernel segments. */
	mmu_config = ( REG_STATE(mmu, rw_mm_cfg, we, on)        |
		       REG_STATE(mmu, rw_mm_cfg, acc, on)       |
		       REG_STATE(mmu, rw_mm_cfg, ex, on)        |
		       REG_STATE(mmu, rw_mm_cfg, inv, on)       |
		       REG_STATE(mmu, rw_mm_cfg, seg_f, linear) |
		       REG_STATE(mmu, rw_mm_cfg, seg_e, linear) |
		       REG_STATE(mmu, rw_mm_cfg, seg_d, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_c, linear) |
		       REG_STATE(mmu, rw_mm_cfg, seg_b, linear) |
#ifndef CONFIG_ETRAX_VCS_SIM
                       REG_STATE(mmu, rw_mm_cfg, seg_a, page)   |
#else
		       REG_STATE(mmu, rw_mm_cfg, seg_a, linear) |
#endif
		       REG_STATE(mmu, rw_mm_cfg, seg_9, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_8, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_7, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_6, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_5, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_4, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_3, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_2, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_1, page)   |
		       REG_STATE(mmu, rw_mm_cfg, seg_0, page));

	mmu_kbase_hi = ( REG_FIELD(mmu, rw_mm_kbase_hi, base_f, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_hi, base_e, 0x8) |
			 REG_FIELD(mmu, rw_mm_kbase_hi, base_d, 0x0) |
                         REG_FIELD(mmu, rw_mm_kbase_hi, base_c, 0x4) |
			 REG_FIELD(mmu, rw_mm_kbase_hi, base_b, 0xb) |
#ifndef CONFIG_ETRAX_VCS_SIM
			 REG_FIELD(mmu, rw_mm_kbase_hi, base_a, 0x0) |
#else
                         REG_FIELD(mmu, rw_mm_kbase_hi, base_a, 0xa) |
#endif
			 REG_FIELD(mmu, rw_mm_kbase_hi, base_9, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_hi, base_8, 0x0));

	mmu_kbase_lo = ( REG_FIELD(mmu, rw_mm_kbase_lo, base_7, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_lo, base_6, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_lo, base_5, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_lo, base_4, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_lo, base_3, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_lo, base_2, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_lo, base_1, 0x0) |
			 REG_FIELD(mmu, rw_mm_kbase_lo, base_0, 0x0));

	mmu_page_id = REG_FIELD(mmu, rw_mm_tlb_hi, pid, 0);

	/* Update the instruction MMU. */
	SUPP_BANK_SEL(BANK_IM);
	SUPP_REG_WR(RW_MM_CFG, mmu_config);
	SUPP_REG_WR(RW_MM_KBASE_HI, mmu_kbase_hi);
	SUPP_REG_WR(RW_MM_KBASE_LO, mmu_kbase_lo);
	SUPP_REG_WR(RW_MM_TLB_HI, mmu_page_id);

	/* Update the data MMU. */
	SUPP_BANK_SEL(BANK_DM);
	SUPP_REG_WR(RW_MM_CFG, mmu_config);
	SUPP_REG_WR(RW_MM_KBASE_HI, mmu_kbase_hi);
	SUPP_REG_WR(RW_MM_KBASE_LO, mmu_kbase_lo);
	SUPP_REG_WR(RW_MM_TLB_HI, mmu_page_id);

	SPEC_REG_WR(SPEC_REG_PID, 0);

	/*
	 * The MMU has been enabled ever since head.S but just to make it
	 * totally obvious enable it here as well.
	 */
	SUPP_BANK_SEL(BANK_GC);
	SUPP_REG_WR(RW_GC_CFG, 0xf); /* IMMU, DMMU, ICache, DCache on */
}

void __init
paging_init(void)
{
	int i;
	unsigned long zones_size[MAX_NR_ZONES];

	printk("Setting up paging and the MMU.\n");

	/* Clear out the init_mm.pgd that will contain the kernel's mappings. */
	for(i = 0; i < PTRS_PER_PGD; i++)
		swapper_pg_dir[i] = __pgd(0);

	cris_mmu_init();

	/*
	 * Initialize the bad page table and bad page to point to a couple of
	 * allocated pages.
	 */
	empty_zero_page = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
	memset((void *) empty_zero_page, 0, PAGE_SIZE);

	/* All pages are DMA'able in Etrax, so put all in the DMA'able zone. */
	zones_size[0] = ((unsigned long) high_memory - PAGE_OFFSET) >> PAGE_SHIFT;

	for (i = 1; i < MAX_NR_ZONES; i++)
		zones_size[i] = 0;

	/*
	 * Use free_area_init_node instead of free_area_init, because it is
	 * designed for systems where the DRAM starts at an address
	 * substantially higher than 0, like us (we start at PAGE_OFFSET). This
	 * saves space in the mem_map page array.
	 */
	free_area_init_node(0, zones_size, PAGE_OFFSET >> PAGE_SHIFT, 0);

	mem_map = contig_page_data.node_mem_map;
}
