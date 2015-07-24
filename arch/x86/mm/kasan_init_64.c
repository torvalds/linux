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

static pud_t kasan_zero_pud[PTRS_PER_PUD] __page_aligned_bss;
static pmd_t kasan_zero_pmd[PTRS_PER_PMD] __page_aligned_bss;
static pte_t kasan_zero_pte[PTRS_PER_PTE] __page_aligned_bss;

/*
 * This page used as early shadow. We don't use empty_zero_page
 * at early stages, stack instrumentation could write some garbage
 * to this page.
 * Latter we reuse it as zero shadow for large ranges of memory
 * that allowed to access, but not instrumented by kasan
 * (vmalloc/vmemmap ...).
 */
static unsigned char kasan_zero_page[PAGE_SIZE] __page_aligned_bss;

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

static int __init zero_pte_populate(pmd_t *pmd, unsigned long addr,
				unsigned long end)
{
	pte_t *pte = pte_offset_kernel(pmd, addr);

	while (addr + PAGE_SIZE <= end) {
		WARN_ON(!pte_none(*pte));
		set_pte(pte, __pte(__pa_nodebug(kasan_zero_page)
					| __PAGE_KERNEL_RO));
		addr += PAGE_SIZE;
		pte = pte_offset_kernel(pmd, addr);
	}
	return 0;
}

static int __init zero_pmd_populate(pud_t *pud, unsigned long addr,
				unsigned long end)
{
	int ret = 0;
	pmd_t *pmd = pmd_offset(pud, addr);

	while (IS_ALIGNED(addr, PMD_SIZE) && addr + PMD_SIZE <= end) {
		WARN_ON(!pmd_none(*pmd));
		set_pmd(pmd, __pmd(__pa_nodebug(kasan_zero_pte)
					| _KERNPG_TABLE));
		addr += PMD_SIZE;
		pmd = pmd_offset(pud, addr);
	}
	if (addr < end) {
		if (pmd_none(*pmd)) {
			void *p = vmemmap_alloc_block(PAGE_SIZE, NUMA_NO_NODE);
			if (!p)
				return -ENOMEM;
			set_pmd(pmd, __pmd(__pa_nodebug(p) | _KERNPG_TABLE));
		}
		ret = zero_pte_populate(pmd, addr, end);
	}
	return ret;
}


static int __init zero_pud_populate(pgd_t *pgd, unsigned long addr,
				unsigned long end)
{
	int ret = 0;
	pud_t *pud = pud_offset(pgd, addr);

	while (IS_ALIGNED(addr, PUD_SIZE) && addr + PUD_SIZE <= end) {
		WARN_ON(!pud_none(*pud));
		set_pud(pud, __pud(__pa_nodebug(kasan_zero_pmd)
					| _KERNPG_TABLE));
		addr += PUD_SIZE;
		pud = pud_offset(pgd, addr);
	}

	if (addr < end) {
		if (pud_none(*pud)) {
			void *p = vmemmap_alloc_block(PAGE_SIZE, NUMA_NO_NODE);
			if (!p)
				return -ENOMEM;
			set_pud(pud, __pud(__pa_nodebug(p) | _KERNPG_TABLE));
		}
		ret = zero_pmd_populate(pud, addr, end);
	}
	return ret;
}

static int __init zero_pgd_populate(unsigned long addr, unsigned long end)
{
	int ret = 0;
	pgd_t *pgd = pgd_offset_k(addr);

	while (IS_ALIGNED(addr, PGDIR_SIZE) && addr + PGDIR_SIZE <= end) {
		WARN_ON(!pgd_none(*pgd));
		set_pgd(pgd, __pgd(__pa_nodebug(kasan_zero_pud)
					| _KERNPG_TABLE));
		addr += PGDIR_SIZE;
		pgd = pgd_offset_k(addr);
	}

	if (addr < end) {
		if (pgd_none(*pgd)) {
			void *p = vmemmap_alloc_block(PAGE_SIZE, NUMA_NO_NODE);
			if (!p)
				return -ENOMEM;
			set_pgd(pgd, __pgd(__pa_nodebug(p) | _KERNPG_TABLE));
		}
		ret = zero_pud_populate(pgd, addr, end);
	}
	return ret;
}


static void __init populate_zero_shadow(const void *start, const void *end)
{
	if (zero_pgd_populate((unsigned long)start, (unsigned long)end))
		panic("kasan: unable to map zero shadow!");
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

	populate_zero_shadow((void *)KASAN_SHADOW_START,
			kasan_mem_to_shadow((void *)PAGE_OFFSET));

	for (i = 0; i < E820_X_MAX; i++) {
		if (pfn_mapped[i].end == 0)
			break;

		if (map_range(&pfn_mapped[i]))
			panic("kasan: unable to allocate shadow!");
	}
	populate_zero_shadow(kasan_mem_to_shadow((void *)PAGE_OFFSET + MAXMEM),
			kasan_mem_to_shadow((void *)__START_KERNEL_map));

	vmemmap_populate((unsigned long)kasan_mem_to_shadow(_stext),
			(unsigned long)kasan_mem_to_shadow(_end),
			NUMA_NO_NODE);

	populate_zero_shadow(kasan_mem_to_shadow((void *)MODULES_END),
			(void *)KASAN_SHADOW_END);

	memset(kasan_zero_page, 0, PAGE_SIZE);

	load_cr3(init_level4_pgt);
	__flush_tlb_all();
	init_task.kasan_depth = 0;

	pr_info("Kernel address sanitizer initialized\n");
}
