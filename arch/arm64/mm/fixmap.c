// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fixmap manipulation code
 */

#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/libfdt.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/sizes.h>

#include <asm/fixmap.h>
#include <asm/kernel-pgtable.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

#define NR_BM_PTE_TABLES \
	SPAN_NR_ENTRIES(FIXADDR_TOT_START, FIXADDR_TOP, PMD_SHIFT)
#define NR_BM_PMD_TABLES \
	SPAN_NR_ENTRIES(FIXADDR_TOT_START, FIXADDR_TOP, PUD_SHIFT)

static_assert(NR_BM_PMD_TABLES == 1);

#define __BM_TABLE_IDX(addr, shift) \
	(((addr) >> (shift)) - (FIXADDR_TOT_START >> (shift)))

#define BM_PTE_TABLE_IDX(addr)	__BM_TABLE_IDX(addr, PMD_SHIFT)

static pte_t bm_pte[NR_BM_PTE_TABLES][PTRS_PER_PTE] __page_aligned_bss;
static pmd_t bm_pmd[PTRS_PER_PMD] __page_aligned_bss __maybe_unused;
static pud_t bm_pud[PTRS_PER_PUD] __page_aligned_bss __maybe_unused;

static inline pte_t *fixmap_pte(unsigned long addr)
{
	return &bm_pte[BM_PTE_TABLE_IDX(addr)][pte_index(addr)];
}

static void __init early_fixmap_init_pte(pmd_t *pmdp, unsigned long addr)
{
	pmd_t pmd = READ_ONCE(*pmdp);
	pte_t *ptep;

	if (pmd_none(pmd)) {
		ptep = bm_pte[BM_PTE_TABLE_IDX(addr)];
		__pmd_populate(pmdp, __pa_symbol(ptep), PMD_TYPE_TABLE);
	}
}

static void __init early_fixmap_init_pmd(pud_t *pudp, unsigned long addr,
					 unsigned long end)
{
	unsigned long next;
	pud_t pud = READ_ONCE(*pudp);
	pmd_t *pmdp;

	if (pud_none(pud))
		__pud_populate(pudp, __pa_symbol(bm_pmd), PUD_TYPE_TABLE);

	pmdp = pmd_offset_kimg(pudp, addr);
	do {
		next = pmd_addr_end(addr, end);
		early_fixmap_init_pte(pmdp, addr);
	} while (pmdp++, addr = next, addr != end);
}


static void __init early_fixmap_init_pud(p4d_t *p4dp, unsigned long addr,
					 unsigned long end)
{
	p4d_t p4d = READ_ONCE(*p4dp);
	pud_t *pudp;

	if (CONFIG_PGTABLE_LEVELS > 3 && !p4d_none(p4d) &&
	    p4d_page_paddr(p4d) != __pa_symbol(bm_pud)) {
		/*
		 * We only end up here if the kernel mapping and the fixmap
		 * share the top level pgd entry, which should only happen on
		 * 16k/4 levels configurations.
		 */
		BUG_ON(!IS_ENABLED(CONFIG_ARM64_16K_PAGES));
	}

	if (p4d_none(p4d))
		__p4d_populate(p4dp, __pa_symbol(bm_pud), P4D_TYPE_TABLE);

	pudp = pud_offset_kimg(p4dp, addr);
	early_fixmap_init_pmd(pudp, addr, end);
}

/*
 * The p*d_populate functions call virt_to_phys implicitly so they can't be used
 * directly on kernel symbols (bm_p*d). This function is called too early to use
 * lm_alias so __p*d_populate functions must be used to populate with the
 * physical address from __pa_symbol.
 */
void __init early_fixmap_init(void)
{
	unsigned long addr = FIXADDR_TOT_START;
	unsigned long end = FIXADDR_TOP;

	pgd_t *pgdp = pgd_offset_k(addr);
	p4d_t *p4dp = p4d_offset(pgdp, addr);

	early_fixmap_init_pud(p4dp, addr, end);
}

/*
 * Unusually, this is also called in IRQ context (ghes_iounmap_irq) so if we
 * ever need to use IPIs for TLB broadcasting, then we're in trouble here.
 */
void __set_fixmap(enum fixed_addresses idx,
			       phys_addr_t phys, pgprot_t flags)
{
	unsigned long addr = __fix_to_virt(idx);
	pte_t *ptep;

	BUG_ON(idx <= FIX_HOLE || idx >= __end_of_fixed_addresses);

	ptep = fixmap_pte(addr);

	if (pgprot_val(flags)) {
		set_pte(ptep, pfn_pte(phys >> PAGE_SHIFT, flags));
	} else {
		pte_clear(&init_mm, addr, ptep);
		flush_tlb_kernel_range(addr, addr+PAGE_SIZE);
	}
}

void *__init fixmap_remap_fdt(phys_addr_t dt_phys, int *size, pgprot_t prot)
{
	const u64 dt_virt_base = __fix_to_virt(FIX_FDT);
	phys_addr_t dt_phys_base;
	int offset;
	void *dt_virt;

	/*
	 * Check whether the physical FDT address is set and meets the minimum
	 * alignment requirement. Since we are relying on MIN_FDT_ALIGN to be
	 * at least 8 bytes so that we can always access the magic and size
	 * fields of the FDT header after mapping the first chunk, double check
	 * here if that is indeed the case.
	 */
	BUILD_BUG_ON(MIN_FDT_ALIGN < 8);
	if (!dt_phys || dt_phys % MIN_FDT_ALIGN)
		return NULL;

	dt_phys_base = round_down(dt_phys, PAGE_SIZE);
	offset = dt_phys % PAGE_SIZE;
	dt_virt = (void *)dt_virt_base + offset;

	/* map the first chunk so we can read the size from the header */
	create_mapping_noalloc(dt_phys_base, dt_virt_base, PAGE_SIZE, prot);

	if (fdt_magic(dt_virt) != FDT_MAGIC)
		return NULL;

	*size = fdt_totalsize(dt_virt);
	if (*size > MAX_FDT_SIZE)
		return NULL;

	if (offset + *size > PAGE_SIZE) {
		create_mapping_noalloc(dt_phys_base, dt_virt_base,
				       offset + *size, prot);
	}

	return dt_virt;
}

/*
 * Copy the fixmap region into a new pgdir.
 */
void __init fixmap_copy(pgd_t *pgdir)
{
	if (!READ_ONCE(pgd_val(*pgd_offset_pgd(pgdir, FIXADDR_TOT_START)))) {
		/*
		 * The fixmap falls in a separate pgd to the kernel, and doesn't
		 * live in the carveout for the swapper_pg_dir. We can simply
		 * re-use the existing dir for the fixmap.
		 */
		set_pgd(pgd_offset_pgd(pgdir, FIXADDR_TOT_START),
			READ_ONCE(*pgd_offset_k(FIXADDR_TOT_START)));
	} else if (CONFIG_PGTABLE_LEVELS > 3) {
		pgd_t *bm_pgdp;
		p4d_t *bm_p4dp;
		pud_t *bm_pudp;
		/*
		 * The fixmap shares its top level pgd entry with the kernel
		 * mapping. This can really only occur when we are running
		 * with 16k/4 levels, so we can simply reuse the pud level
		 * entry instead.
		 */
		BUG_ON(!IS_ENABLED(CONFIG_ARM64_16K_PAGES));
		bm_pgdp = pgd_offset_pgd(pgdir, FIXADDR_TOT_START);
		bm_p4dp = p4d_offset(bm_pgdp, FIXADDR_TOT_START);
		bm_pudp = pud_set_fixmap_offset(bm_p4dp, FIXADDR_TOT_START);
		pud_populate(&init_mm, bm_pudp, lm_alias(bm_pmd));
		pud_clear_fixmap();
	} else {
		BUG();
	}
}
