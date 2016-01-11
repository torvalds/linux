#define DISABLE_BRANCH_PROFILING
#define pr_fmt(fmt) "kasan: " fmt
#include <linux/bootmem.h>
#include <linux/kasan.h>
#include <linux/kdebug.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <asm/tlbflush.h>
#include <asm/sections.h>

extern pgd_t early_level4_pgt[PTRS_PER_PGD];
extern struct range pfn_mapped[E820_X_MAX];

static int __init map_range(struct range *range)
{
	unsigned long start;
	unsigned long end;

	start = (unsigned long)kasan_mem_to_shadow(pfn_to_kaddr(range->start));
	end = (unsigned long)kasan_mem_to_shadow(pfn_to_kaddr(range->end));

	/*
	 * end + 1 here is intentional. We check several shadow bytes in advance
	 * to slightly speed up fastpath. In some rare cases we could cross
	 * boundary of mapped shadow, so we just map some more here.
	 */
	return vmemmap_populate(start, end + 1, NUMA_NO_NODE);
}

static void __init clear_pgds(unsigned long start,
			unsigned long end)
{
	for (; start < end; start += PGDIR_SIZE)
		pgd_clear(pgd_offset_k(start));
}

static void __init kasan_map_early_shadow(pgd_t *pgd)
{
	int i;
	unsigned long start = KASAN_SHADOW_START;
	unsigned long end = KASAN_SHADOW_END;

	for (i = pgd_index(start); start < end; i++) {
		pgd[i] = __pgd(__pa_nodebug(kasan_zero_pud)
				| _KERNPG_TABLE);
		start += PGDIR_SIZE;
	}
}

#ifdef CONFIG_KASAN_INLINE
static int kasan_die_handler(struct notifier_block *self,
			     unsigned long val,
			     void *data)
{
	if (val == DIE_GPF) {
		pr_emerg("CONFIG_KASAN_INLINE enabled");
		pr_emerg("GPF could be caused by NULL-ptr deref or user memory access");
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
	pteval_t pte_val = __pa_nodebug(kasan_zero_page) | __PAGE_KERNEL;
	pmdval_t pmd_val = __pa_nodebug(kasan_zero_pte) | _KERNPG_TABLE;
	pudval_t pud_val = __pa_nodebug(kasan_zero_pmd) | _KERNPG_TABLE;

	for (i = 0; i < PTRS_PER_PTE; i++)
		kasan_zero_pte[i] = __pte(pte_val);

	for (i = 0; i < PTRS_PER_PMD; i++)
		kasan_zero_pmd[i] = __pmd(pmd_val);

	for (i = 0; i < PTRS_PER_PUD; i++)
		kasan_zero_pud[i] = __pud(pud_val);

	kasan_map_early_shadow(early_level4_pgt);
	kasan_map_early_shadow(init_level4_pgt);
}

void __init kasan_init(void)
{
	int i;

#ifdef CONFIG_KASAN_INLINE
	register_die_notifier(&kasan_die_notifier);
#endif

	memcpy(early_level4_pgt, init_level4_pgt, sizeof(early_level4_pgt));
	load_cr3(early_level4_pgt);
	__flush_tlb_all();

	clear_pgds(KASAN_SHADOW_START, KASAN_SHADOW_END);

	kasan_populate_zero_shadow((void *)KASAN_SHADOW_START,
			kasan_mem_to_shadow((void *)PAGE_OFFSET));

	for (i = 0; i < E820_X_MAX; i++) {
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

	load_cr3(init_level4_pgt);
	__flush_tlb_all();

	/*
	 * kasan_zero_page has been used as early shadow memory, thus it may
	 * contain some garbage. Now we can clear it, since after the TLB flush
	 * no one should write to it.
	 */
	memset(kasan_zero_page, 0, PAGE_SIZE);

	init_task.kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized\n");
}
