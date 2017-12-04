// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008,2009,2010,2011 Imagination Technologies Ltd.
 *
 * Meta 2 enhanced mode MMU handling code.
 *
 */

#include <linux/mm.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/bootmem.h>
#include <linux/syscore_ops.h>

#include <asm/mmu.h>
#include <asm/mmu_context.h>

unsigned long mmu_read_first_level_page(unsigned long vaddr)
{
	unsigned int cpu = hard_processor_id();
	unsigned long offset, linear_base, linear_limit;
	unsigned int phys0;
	pgd_t *pgd, entry;

	if (is_global_space(vaddr))
		vaddr &= ~0x80000000;

	offset = vaddr >> PGDIR_SHIFT;

	phys0 = metag_in32(mmu_phys0_addr(cpu));

	/* Top bit of linear base is always zero. */
	linear_base = (phys0 >> PGDIR_SHIFT) & 0x1ff;

	/* Limit in the range 0 (4MB) to 9 (2GB). */
	linear_limit = 1 << ((phys0 >> 8) & 0xf);
	linear_limit += linear_base;

	/*
	 * If offset is below linear base or above the limit then no
	 * mapping exists.
	 */
	if (offset < linear_base || offset > linear_limit)
		return 0;

	offset -= linear_base;
	pgd = (pgd_t *)mmu_get_base();
	entry = pgd[offset];

	return pgd_val(entry);
}

unsigned long mmu_read_second_level_page(unsigned long vaddr)
{
	return __builtin_meta2_cacherd((void *)(vaddr & PAGE_MASK));
}

unsigned long mmu_get_base(void)
{
	unsigned int cpu = hard_processor_id();
	unsigned long stride;

	stride = cpu * LINSYSMEMTnX_STRIDE;

	/*
	 * Bits 18:2 of the MMCU_TnLocal_TABLE_PHYS1 register should be
	 * used as an offset to the start of the top-level pgd table.
	 */
	stride += (metag_in32(mmu_phys1_addr(cpu)) & 0x7fffc);

	if (is_global_space(PAGE_OFFSET))
		stride += LINSYSMEMTXG_OFFSET;

	return LINSYSMEMT0L_BASE + stride;
}

#define FIRST_LEVEL_MASK	0xffffffc0
#define SECOND_LEVEL_MASK	0xfffff000
#define SECOND_LEVEL_ALIGN	64

static void repriv_mmu_tables(void)
{
	unsigned long phys0_addr;
	unsigned int g;

	/*
	 * Check that all the mmu table regions are priv protected, and if not
	 * fix them and emit a warning. If we left them without priv protection
	 * then userland processes would have access to a 2M window into
	 * physical memory near where the page tables are.
	 */
	phys0_addr = MMCU_T0LOCAL_TABLE_PHYS0;
	for (g = 0; g < 2; ++g) {
		unsigned int t, phys0;
		unsigned long flags;
		for (t = 0; t < 4; ++t) {
			__global_lock2(flags);
			phys0 = metag_in32(phys0_addr);
			if ((phys0 & _PAGE_PRESENT) && !(phys0 & _PAGE_PRIV)) {
				pr_warn("Fixing priv protection on T%d %s MMU table region\n",
					t,
					g ? "global" : "local");
				phys0 |= _PAGE_PRIV;
				metag_out32(phys0, phys0_addr);
			}
			__global_unlock2(flags);

			phys0_addr += MMCU_TnX_TABLE_PHYSX_STRIDE;
		}

		phys0_addr += MMCU_TXG_TABLE_PHYSX_OFFSET
			    - 4*MMCU_TnX_TABLE_PHYSX_STRIDE;
	}
}

#ifdef CONFIG_METAG_SUSPEND_MEM
static void mmu_resume(void)
{
	/*
	 * If a full suspend to RAM has happened then the original bad MMU table
	 * priv may have been restored, so repriv them again.
	 */
	repriv_mmu_tables();
}
#else
#define mmu_resume NULL
#endif	/* CONFIG_METAG_SUSPEND_MEM */

static struct syscore_ops mmu_syscore_ops = {
	.resume  = mmu_resume,
};

void __init mmu_init(unsigned long mem_end)
{
	unsigned long entry, addr;
	pgd_t *p_swapper_pg_dir;
#ifdef CONFIG_KERNEL_4M_PAGES
	unsigned long mem_size = mem_end - PAGE_OFFSET;
	unsigned int pages = DIV_ROUND_UP(mem_size, 1 << 22);
	unsigned int second_level_entry = 0;
	unsigned long *second_level_table;
#endif

	/*
	 * Now copy over any MMU pgd entries already in the mmu page tables
	 * over to our root init process (swapper_pg_dir) map.  This map is
	 * then inherited by all other processes, which means all processes
	 * inherit a map of the kernel space.
	 */
	addr = META_MEMORY_BASE;
	entry = pgd_index(META_MEMORY_BASE);
	p_swapper_pg_dir = pgd_offset_k(0) + entry;

	while (entry < (PTRS_PER_PGD - pgd_index(META_MEMORY_BASE))) {
		unsigned long pgd_entry;
		/* copy over the current MMU value */
		pgd_entry = mmu_read_first_level_page(addr);
		pgd_val(*p_swapper_pg_dir) = pgd_entry;

		p_swapper_pg_dir++;
		addr += PGDIR_SIZE;
		entry++;
	}

#ifdef CONFIG_KERNEL_4M_PAGES
	/*
	 * At this point we can also map the kernel with 4MB pages to
	 * reduce TLB pressure.
	 */
	second_level_table = alloc_bootmem_pages(SECOND_LEVEL_ALIGN * pages);

	addr = PAGE_OFFSET;
	entry = pgd_index(PAGE_OFFSET);
	p_swapper_pg_dir = pgd_offset_k(0) + entry;

	while (pages > 0) {
		unsigned long phys_addr, second_level_phys;
		pte_t *pte = (pte_t *)&second_level_table[second_level_entry];

		phys_addr = __pa(addr);

		second_level_phys = __pa(pte);

		pgd_val(*p_swapper_pg_dir) = ((second_level_phys &
					       FIRST_LEVEL_MASK) |
					      _PAGE_SZ_4M |
					      _PAGE_PRESENT);

		pte_val(*pte) = ((phys_addr & SECOND_LEVEL_MASK) |
				 _PAGE_PRESENT | _PAGE_DIRTY |
				 _PAGE_ACCESSED | _PAGE_WRITE |
				 _PAGE_CACHEABLE | _PAGE_KERNEL);

		p_swapper_pg_dir++;
		addr += PGDIR_SIZE;
		/* Second level pages must be 64byte aligned. */
		second_level_entry += (SECOND_LEVEL_ALIGN /
				       sizeof(unsigned long));
		pages--;
	}
	load_pgd(swapper_pg_dir, hard_processor_id());
	flush_tlb_all();
#endif

	repriv_mmu_tables();
	register_syscore_ops(&mmu_syscore_ops);
}
