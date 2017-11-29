// SPDX-License-Identifier: GPL-2.0
#define DISABLE_BRANCH_PROFILING
#define pr_fmt(fmt) "kasan: " fmt
#include <linux/bootmem.h>
#include <linux/kasan.h>
#include <linux/kdebug.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/vmalloc.h>

#include <asm/e820/types.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/pgtable.h>

extern struct range pfn_mapped[E820_MAX_ENTRIES];

static p4d_t tmp_p4d_table[PTRS_PER_P4D] __initdata __aligned(PAGE_SIZE);

static int __init map_range(struct range *range)
{
	unsigned long start;
	unsigned long end;

	start = (unsigned long)kasan_mem_to_shadow(pfn_to_kaddr(range->start));
	end = (unsigned long)kasan_mem_to_shadow(pfn_to_kaddr(range->end));

	return vmemmap_populate(start, end, NUMA_NO_NODE);
}

static void __init clear_pgds(unsigned long start,
			unsigned long end)
{
	pgd_t *pgd;
	/* See comment in kasan_init() */
	unsigned long pgd_end = end & PGDIR_MASK;

	for (; start < pgd_end; start += PGDIR_SIZE) {
		pgd = pgd_offset_k(start);
		/*
		 * With folded p4d, pgd_clear() is nop, use p4d_clear()
		 * instead.
		 */
		if (CONFIG_PGTABLE_LEVELS < 5)
			p4d_clear(p4d_offset(pgd, start));
		else
			pgd_clear(pgd);
	}

	pgd = pgd_offset_k(start);
	for (; start < end; start += P4D_SIZE)
		p4d_clear(p4d_offset(pgd, start));
}

static inline p4d_t *early_p4d_offset(pgd_t *pgd, unsigned long addr)
{
	unsigned long p4d;

	if (!IS_ENABLED(CONFIG_X86_5LEVEL))
		return (p4d_t *)pgd;

	p4d = __pa_nodebug(pgd_val(*pgd)) & PTE_PFN_MASK;
	p4d += __START_KERNEL_map - phys_base;
	return (p4d_t *)p4d + p4d_index(addr);
}

static void __init kasan_early_p4d_populate(pgd_t *pgd,
		unsigned long addr,
		unsigned long end)
{
	pgd_t pgd_entry;
	p4d_t *p4d, p4d_entry;
	unsigned long next;

	if (pgd_none(*pgd)) {
		pgd_entry = __pgd(_KERNPG_TABLE | __pa_nodebug(kasan_zero_p4d));
		set_pgd(pgd, pgd_entry);
	}

	p4d = early_p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);

		if (!p4d_none(*p4d))
			continue;

		p4d_entry = __p4d(_KERNPG_TABLE | __pa_nodebug(kasan_zero_pud));
		set_p4d(p4d, p4d_entry);
	} while (p4d++, addr = next, addr != end && p4d_none(*p4d));
}

static void __init kasan_map_early_shadow(pgd_t *pgd)
{
	/* See comment in kasan_init() */
	unsigned long addr = KASAN_SHADOW_START & PGDIR_MASK;
	unsigned long end = KASAN_SHADOW_END;
	unsigned long next;

	pgd += pgd_index(addr);
	do {
		next = pgd_addr_end(addr, end);
		kasan_early_p4d_populate(pgd, addr, next);
	} while (pgd++, addr = next, addr != end);
}

#ifdef CONFIG_KASAN_INLINE
static int kasan_die_handler(struct notifier_block *self,
			     unsigned long val,
			     void *data)
{
	if (val == DIE_GPF) {
		pr_emerg("CONFIG_KASAN_INLINE enabled\n");
		pr_emerg("GPF could be caused by NULL-ptr deref or user memory access\n");
	}
	return NOTIFY_OK;
}

static struct notifier_block kasan_die_notifier = {
	.notifier_call = kasan_die_handler,
};
#endif

void __init kasan_early_init(void)
{
	int i;
	pteval_t pte_val = __pa_nodebug(kasan_zero_page) | __PAGE_KERNEL | _PAGE_ENC;
	pmdval_t pmd_val = __pa_nodebug(kasan_zero_pte) | _KERNPG_TABLE;
	pudval_t pud_val = __pa_nodebug(kasan_zero_pmd) | _KERNPG_TABLE;
	p4dval_t p4d_val = __pa_nodebug(kasan_zero_pud) | _KERNPG_TABLE;

	for (i = 0; i < PTRS_PER_PTE; i++)
		kasan_zero_pte[i] = __pte(pte_val);

	for (i = 0; i < PTRS_PER_PMD; i++)
		kasan_zero_pmd[i] = __pmd(pmd_val);

	for (i = 0; i < PTRS_PER_PUD; i++)
		kasan_zero_pud[i] = __pud(pud_val);

	for (i = 0; IS_ENABLED(CONFIG_X86_5LEVEL) && i < PTRS_PER_P4D; i++)
		kasan_zero_p4d[i] = __p4d(p4d_val);

	kasan_map_early_shadow(early_top_pgt);
	kasan_map_early_shadow(init_top_pgt);
}

void __init kasan_init(void)
{
	int i;

#ifdef CONFIG_KASAN_INLINE
	register_die_notifier(&kasan_die_notifier);
#endif

	memcpy(early_top_pgt, init_top_pgt, sizeof(early_top_pgt));

	/*
	 * We use the same shadow offset for 4- and 5-level paging to
	 * facilitate boot-time switching between paging modes.
	 * As result in 5-level paging mode KASAN_SHADOW_START and
	 * KASAN_SHADOW_END are not aligned to PGD boundary.
	 *
	 * KASAN_SHADOW_START doesn't share PGD with anything else.
	 * We claim whole PGD entry to make things easier.
	 *
	 * KASAN_SHADOW_END lands in the last PGD entry and it collides with
	 * bunch of things like kernel code, modules, EFI mapping, etc.
	 * We need to take extra steps to not overwrite them.
	 */
	if (IS_ENABLED(CONFIG_X86_5LEVEL)) {
		void *ptr;

		ptr = (void *)pgd_page_vaddr(*pgd_offset_k(KASAN_SHADOW_END));
		memcpy(tmp_p4d_table, (void *)ptr, sizeof(tmp_p4d_table));
		set_pgd(&early_top_pgt[pgd_index(KASAN_SHADOW_END)],
				__pgd(__pa(tmp_p4d_table) | _KERNPG_TABLE));
	}

	load_cr3(early_top_pgt);
	__flush_tlb_all();

	clear_pgds(KASAN_SHADOW_START & PGDIR_MASK, KASAN_SHADOW_END);

	kasan_populate_zero_shadow((void *)(KASAN_SHADOW_START & PGDIR_MASK),
			kasan_mem_to_shadow((void *)PAGE_OFFSET));

	for (i = 0; i < E820_MAX_ENTRIES; i++) {
		if (pfn_mapped[i].end == 0)
			break;

		if (map_range(&pfn_mapped[i]))
			panic("kasan: unable to allocate shadow!");
	}
	kasan_populate_zero_shadow(
		kasan_mem_to_shadow((void *)PAGE_OFFSET + MAXMEM),
		kasan_mem_to_shadow((void *)__START_KERNEL_map));

	vmemmap_populate((unsigned long)kasan_mem_to_shadow(_stext),
			(unsigned long)kasan_mem_to_shadow(_end),
			NUMA_NO_NODE);

	kasan_populate_zero_shadow(kasan_mem_to_shadow((void *)MODULES_END),
			(void *)KASAN_SHADOW_END);

	load_cr3(init_top_pgt);
	__flush_tlb_all();

	/*
	 * kasan_zero_page has been used as early shadow memory, thus it may
	 * contain some garbage. Now we can clear and write protect it, since
	 * after the TLB flush no one should write to it.
	 */
	memset(kasan_zero_page, 0, PAGE_SIZE);
	for (i = 0; i < PTRS_PER_PTE; i++) {
		pte_t pte = __pte(__pa(kasan_zero_page) | __PAGE_KERNEL_RO | _PAGE_ENC);
		set_pte(&kasan_zero_pte[i], pte);
	}
	/* Flush TLBs again to be sure that write protection applied. */
	__flush_tlb_all();

	init_task.kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized\n");
}
