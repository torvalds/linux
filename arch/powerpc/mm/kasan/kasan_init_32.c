// SPDX-License-Identifier: GPL-2.0

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/printk.h>
#include <linux/memblock.h>
#include <linux/moduleloader.h>
#include <linux/sched/task.h>
#include <linux/vmalloc.h>
#include <asm/pgalloc.h>
#include <asm/code-patching.h>
#include <mm/mmu_decl.h>

static pgprot_t __init kasan_prot_ro(void)
{
	if (early_mmu_has_feature(MMU_FTR_HPTE_TABLE))
		return PAGE_READONLY;

	return PAGE_KERNEL_RO;
}

static void __init kasan_populate_pte(pte_t *ptep, pgprot_t prot)
{
	unsigned long va = (unsigned long)kasan_early_shadow_page;
	phys_addr_t pa = __pa(kasan_early_shadow_page);
	int i;

	for (i = 0; i < PTRS_PER_PTE; i++, ptep++)
		__set_pte_at(&init_mm, va, ptep, pfn_pte(PHYS_PFN(pa), prot), 0);
}

static int __init kasan_init_shadow_page_tables(unsigned long k_start, unsigned long k_end)
{
	pmd_t *pmd;
	unsigned long k_cur, k_next;
	pte_t *new = NULL;

	pmd = pmd_offset(pud_offset(pgd_offset_k(k_start), k_start), k_start);

	for (k_cur = k_start; k_cur != k_end; k_cur = k_next, pmd++) {
		k_next = pgd_addr_end(k_cur, k_end);
		if ((void *)pmd_page_vaddr(*pmd) != kasan_early_shadow_pte)
			continue;

		if (!new)
			new = memblock_alloc(PTE_FRAG_SIZE, PTE_FRAG_SIZE);

		if (!new)
			return -ENOMEM;
		kasan_populate_pte(new, PAGE_KERNEL);

		smp_wmb(); /* See comment in __pte_alloc */

		spin_lock(&init_mm.page_table_lock);
			/* Has another populated it ? */
		if (likely((void *)pmd_page_vaddr(*pmd) == kasan_early_shadow_pte)) {
			pmd_populate_kernel(&init_mm, pmd, new);
			new = NULL;
		}
		spin_unlock(&init_mm.page_table_lock);
	}
	return 0;
}

static int __init kasan_init_region(void *start, size_t size)
{
	unsigned long k_start = (unsigned long)kasan_mem_to_shadow(start);
	unsigned long k_end = (unsigned long)kasan_mem_to_shadow(start + size);
	unsigned long k_cur;
	int ret;
	void *block;

	ret = kasan_init_shadow_page_tables(k_start, k_end);
	if (ret)
		return ret;

	block = memblock_alloc(k_end - k_start, PAGE_SIZE);

	for (k_cur = k_start & PAGE_MASK; k_cur < k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_offset(pud_offset(pgd_offset_k(k_cur), k_cur), k_cur);
		void *va = block + k_cur - k_start;
		pte_t pte = pfn_pte(PHYS_PFN(__pa(va)), PAGE_KERNEL);

		if (!va)
			return -ENOMEM;

		__set_pte_at(&init_mm, k_cur, pte_offset_kernel(pmd, k_cur), pte, 0);
	}
	flush_tlb_kernel_range(k_start, k_end);
	return 0;
}

static void __init kasan_remap_early_shadow_ro(void)
{
	pgprot_t prot = kasan_prot_ro();
	unsigned long k_start = KASAN_SHADOW_START;
	unsigned long k_end = KASAN_SHADOW_END;
	unsigned long k_cur;
	phys_addr_t pa = __pa(kasan_early_shadow_page);

	kasan_populate_pte(kasan_early_shadow_pte, prot);

	for (k_cur = k_start & PAGE_MASK; k_cur < k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_offset(pud_offset(pgd_offset_k(k_cur), k_cur), k_cur);
		pte_t *ptep = pte_offset_kernel(pmd, k_cur);

		if ((pte_val(*ptep) & PTE_RPN_MASK) != pa)
			continue;

		__set_pte_at(&init_mm, k_cur, ptep, pfn_pte(PHYS_PFN(pa), prot), 0);
	}
	flush_tlb_kernel_range(KASAN_SHADOW_START, KASAN_SHADOW_END);
}

static void __init kasan_unmap_early_shadow_vmalloc(void)
{
	unsigned long k_start = (unsigned long)kasan_mem_to_shadow((void *)VMALLOC_START);
	unsigned long k_end = (unsigned long)kasan_mem_to_shadow((void *)VMALLOC_END);
	unsigned long k_cur;
	phys_addr_t pa = __pa(kasan_early_shadow_page);

	if (!early_mmu_has_feature(MMU_FTR_HPTE_TABLE)) {
		int ret = kasan_init_shadow_page_tables(k_start, k_end);

		if (ret)
			panic("kasan: kasan_init_shadow_page_tables() failed");
	}
	for (k_cur = k_start & PAGE_MASK; k_cur < k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_offset(pud_offset(pgd_offset_k(k_cur), k_cur), k_cur);
		pte_t *ptep = pte_offset_kernel(pmd, k_cur);

		if ((pte_val(*ptep) & PTE_RPN_MASK) != pa)
			continue;

		__set_pte_at(&init_mm, k_cur, ptep, __pte(0), 0);
	}
	flush_tlb_kernel_range(k_start, k_end);
}

void __init kasan_mmu_init(void)
{
	int ret;
	struct memblock_region *reg;

	if (early_mmu_has_feature(MMU_FTR_HPTE_TABLE)) {
		ret = kasan_init_shadow_page_tables(KASAN_SHADOW_START, KASAN_SHADOW_END);

		if (ret)
			panic("kasan: kasan_init_shadow_page_tables() failed");
	}

	for_each_memblock(memory, reg) {
		phys_addr_t base = reg->base;
		phys_addr_t top = min(base + reg->size, total_lowmem);

		if (base >= top)
			continue;

		ret = kasan_init_region(__va(base), top - base);
		if (ret)
			panic("kasan: kasan_init_region() failed");
	}
}

void __init kasan_init(void)
{
	kasan_remap_early_shadow_ro();

	clear_page(kasan_early_shadow_page);

	/* At this point kasan is fully initialized. Enable error messages */
	init_task.kasan_depth = 0;
	pr_info("KASAN init done\n");
}

void __init kasan_late_init(void)
{
	if (IS_ENABLED(CONFIG_KASAN_VMALLOC))
		kasan_unmap_early_shadow_vmalloc();
}

#ifdef CONFIG_PPC_BOOK3S_32
u8 __initdata early_hash[256 << 10] __aligned(256 << 10) = {0};

static void __init kasan_early_hash_table(void)
{
	unsigned int hash = IS_ENABLED(CONFIG_VMAP_STACK) ? (unsigned int)early_hash :
							    __pa(early_hash);

	modify_instruction_site(&patch__hash_page_A0, 0xffff, hash >> 16);
	modify_instruction_site(&patch__flush_hash_A0, 0xffff, hash >> 16);

	Hash = (struct hash_pte *)early_hash;
}
#else
static void __init kasan_early_hash_table(void) {}
#endif

void __init kasan_early_init(void)
{
	unsigned long addr = KASAN_SHADOW_START;
	unsigned long end = KASAN_SHADOW_END;
	unsigned long next;
	pmd_t *pmd = pmd_offset(pud_offset(pgd_offset_k(addr), addr), addr);

	BUILD_BUG_ON(KASAN_SHADOW_START & ~PGDIR_MASK);

	kasan_populate_pte(kasan_early_shadow_pte, PAGE_KERNEL);

	do {
		next = pgd_addr_end(addr, end);
		pmd_populate_kernel(&init_mm, pmd, kasan_early_shadow_pte);
	} while (pmd++, addr = next, addr != end);

	if (early_mmu_has_feature(MMU_FTR_HPTE_TABLE))
		kasan_early_hash_table();
}
