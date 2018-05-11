// SPDX-License-Identifier: GPL-2.0
#define DISABLE_BRANCH_PROFILING
#define pr_fmt(fmt) "kasan: " fmt

#ifdef CONFIG_X86_5LEVEL
/* Too early to use cpu_feature_enabled() */
#define pgtable_l5_enabled __pgtable_l5_enabled
#endif

#include <linux/bootmem.h>
#include <linux/kasan.h>
#include <linux/kdebug.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/vmalloc.h>

#include <asm/e820/types.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/pgtable.h>
#include <asm/cpu_entry_area.h>

extern struct range pfn_mapped[E820_MAX_ENTRIES];

static p4d_t tmp_p4d_table[MAX_PTRS_PER_P4D] __initdata __aligned(PAGE_SIZE);

static __init void *early_alloc(size_t size, int nid, bool panic)
{
	if (panic)
		return memblock_virt_alloc_try_nid(size, size,
			__pa(MAX_DMA_ADDRESS), BOOTMEM_ALLOC_ACCESSIBLE, nid);
	else
		return memblock_virt_alloc_try_nid_nopanic(size, size,
			__pa(MAX_DMA_ADDRESS), BOOTMEM_ALLOC_ACCESSIBLE, nid);
}

static void __init kasan_populate_pmd(pmd_t *pmd, unsigned long addr,
				      unsigned long end, int nid)
{
	pte_t *pte;

	if (pmd_none(*pmd)) {
		void *p;

		if (boot_cpu_has(X86_FEATURE_PSE) &&
		    ((end - addr) == PMD_SIZE) &&
		    IS_ALIGNED(addr, PMD_SIZE)) {
			p = early_alloc(PMD_SIZE, nid, false);
			if (p && pmd_set_huge(pmd, __pa(p), PAGE_KERNEL))
				return;
			else if (p)
				memblock_free(__pa(p), PMD_SIZE);
		}

		p = early_alloc(PAGE_SIZE, nid, true);
		pmd_populate_kernel(&init_mm, pmd, p);
	}

	pte = pte_offset_kernel(pmd, addr);
	do {
		pte_t entry;
		void *p;

		if (!pte_none(*pte))
			continue;

		p = early_alloc(PAGE_SIZE, nid, true);
		entry = pfn_pte(PFN_DOWN(__pa(p)), PAGE_KERNEL);
		set_pte_at(&init_mm, addr, pte, entry);
	} while (pte++, addr += PAGE_SIZE, addr != end);
}

static void __init kasan_populate_pud(pud_t *pud, unsigned long addr,
				      unsigned long end, int nid)
{
	pmd_t *pmd;
	unsigned long next;

	if (pud_none(*pud)) {
		void *p;

		if (boot_cpu_has(X86_FEATURE_GBPAGES) &&
		    ((end - addr) == PUD_SIZE) &&
		    IS_ALIGNED(addr, PUD_SIZE)) {
			p = early_alloc(PUD_SIZE, nid, false);
			if (p && pud_set_huge(pud, __pa(p), PAGE_KERNEL))
				return;
			else if (p)
				memblock_free(__pa(p), PUD_SIZE);
		}

		p = early_alloc(PAGE_SIZE, nid, true);
		pud_populate(&init_mm, pud, p);
	}

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (!pmd_large(*pmd))
			kasan_populate_pmd(pmd, addr, next, nid);
	} while (pmd++, addr = next, addr != end);
}

static void __init kasan_populate_p4d(p4d_t *p4d, unsigned long addr,
				      unsigned long end, int nid)
{
	pud_t *pud;
	unsigned long next;

	if (p4d_none(*p4d)) {
		void *p = early_alloc(PAGE_SIZE, nid, true);

		p4d_populate(&init_mm, p4d, p);
	}

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (!pud_large(*pud))
			kasan_populate_pud(pud, addr, next, nid);
	} while (pud++, addr = next, addr != end);
}

static void __init kasan_populate_pgd(pgd_t *pgd, unsigned long addr,
				      unsigned long end, int nid)
{
	void *p;
	p4d_t *p4d;
	unsigned long next;

	if (pgd_none(*pgd)) {
		p = early_alloc(PAGE_SIZE, nid, true);
		pgd_populate(&init_mm, pgd, p);
	}

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		kasan_populate_p4d(p4d, addr, next, nid);
	} while (p4d++, addr = next, addr != end);
}

static void __init kasan_populate_shadow(unsigned long addr, unsigned long end,
					 int nid)
{
	pgd_t *pgd;
	unsigned long next;

	addr = addr & PAGE_MASK;
	end = round_up(end, PAGE_SIZE);
	pgd = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		kasan_populate_pgd(pgd, addr, next, nid);
	} while (pgd++, addr = next, addr != end);
}

static void __init map_range(struct range *range)
{
	unsigned long start;
	unsigned long end;

	start = (unsigned long)kasan_mem_to_shadow(pfn_to_kaddr(range->start));
	end = (unsigned long)kasan_mem_to_shadow(pfn_to_kaddr(range->end));

	kasan_populate_shadow(start, end, early_pfn_to_nid(range->start));
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
		if (pgtable_l5_enabled)
			pgd_clear(pgd);
		else
			p4d_clear(p4d_offset(pgd, start));
	}

	pgd = pgd_offset_k(start);
	for (; start < end; start += P4D_SIZE)
		p4d_clear(p4d_offset(pgd, start));
}

static inline p4d_t *early_p4d_offset(pgd_t *pgd, unsigned long addr)
{
	unsigned long p4d;

	if (!pgtable_l5_enabled)
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

	/* Mask out unsupported __PAGE_KERNEL bits: */
	pte_val &= __default_kernel_pte_mask;
	pmd_val &= __default_kernel_pte_mask;
	pud_val &= __default_kernel_pte_mask;
	p4d_val &= __default_kernel_pte_mask;

	for (i = 0; i < PTRS_PER_PTE; i++)
		kasan_zero_pte[i] = __pte(pte_val);

	for (i = 0; i < PTRS_PER_PMD; i++)
		kasan_zero_pmd[i] = __pmd(pmd_val);

	for (i = 0; i < PTRS_PER_PUD; i++)
		kasan_zero_pud[i] = __pud(pud_val);

	for (i = 0; pgtable_l5_enabled && i < PTRS_PER_P4D; i++)
		kasan_zero_p4d[i] = __p4d(p4d_val);

	kasan_map_early_shadow(early_top_pgt);
	kasan_map_early_shadow(init_top_pgt);
}

void __init kasan_init(void)
{
	int i;
	void *shadow_cpu_entry_begin, *shadow_cpu_entry_end;

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
	if (pgtable_l5_enabled) {
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

		map_range(&pfn_mapped[i]);
	}

	shadow_cpu_entry_begin = (void *)CPU_ENTRY_AREA_BASE;
	shadow_cpu_entry_begin = kasan_mem_to_shadow(shadow_cpu_entry_begin);
	shadow_cpu_entry_begin = (void *)round_down((unsigned long)shadow_cpu_entry_begin,
						PAGE_SIZE);

	shadow_cpu_entry_end = (void *)(CPU_ENTRY_AREA_BASE +
					CPU_ENTRY_AREA_MAP_SIZE);
	shadow_cpu_entry_end = kasan_mem_to_shadow(shadow_cpu_entry_end);
	shadow_cpu_entry_end = (void *)round_up((unsigned long)shadow_cpu_entry_end,
					PAGE_SIZE);

	kasan_populate_zero_shadow(
		kasan_mem_to_shadow((void *)PAGE_OFFSET + MAXMEM),
		shadow_cpu_entry_begin);

	kasan_populate_shadow((unsigned long)shadow_cpu_entry_begin,
			      (unsigned long)shadow_cpu_entry_end, 0);

	kasan_populate_zero_shadow(shadow_cpu_entry_end,
				kasan_mem_to_shadow((void *)__START_KERNEL_map));

	kasan_populate_shadow((unsigned long)kasan_mem_to_shadow(_stext),
			      (unsigned long)kasan_mem_to_shadow(_end),
			      early_pfn_to_nid(__pa(_stext)));

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
		pte_t pte;
		pgprot_t prot;

		prot = __pgprot(__PAGE_KERNEL_RO | _PAGE_ENC);
		pgprot_val(prot) &= __default_kernel_pte_mask;

		pte = __pte(__pa(kasan_zero_page) | pgprot_val(prot));
		set_pte(&kasan_zero_pte[i], pte);
	}
	/* Flush TLBs again to be sure that write protection applied. */
	__flush_tlb_all();

	init_task.kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized\n");
}
