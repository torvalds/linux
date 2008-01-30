/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Thanks to Ben LaHaise for precious feedback.
 */

#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

pte_t *lookup_address(unsigned long address, int *level)
{
	pgd_t *pgd = pgd_offset_k(address);
	pud_t *pud;
	pmd_t *pmd;

	if (pgd_none(*pgd))
		return NULL;
	pud = pud_offset(pgd, address);
	if (pud_none(*pud))
		return NULL;
	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return NULL;
	*level = 3;
	if (pmd_large(*pmd))
		return (pte_t *)pmd;
	*level = 4;

	return pte_offset_kernel(pmd, address);
}

static void __set_pmd_pte(pte_t *kpte, unsigned long address, pte_t pte)
{
	struct page *page;

	/* change init_mm */
	set_pte_atomic(kpte, pte);
	if (SHARED_KERNEL_PMD)
		return;

	for (page = pgd_list; page; page = (struct page *)page->index) {
		pgd_t *pgd;
		pud_t *pud;
		pmd_t *pmd;

		pgd = (pgd_t *)page_address(page) + pgd_index(address);
		pud = pud_offset(pgd, address);
		pmd = pmd_offset(pud, address);
		set_pte_atomic((pte_t *)pmd, pte);
	}
}

static int split_large_page(pte_t *kpte, unsigned long address)
{
	pgprot_t ref_prot = pte_pgprot(pte_clrhuge(*kpte));
	gfp_t gfp_flags = GFP_KERNEL;
	unsigned long flags;
	unsigned long addr;
	pte_t *pbase, *tmp;
	struct page *base;
	int i, level;

#ifdef CONFIG_DEBUG_PAGEALLOC
	gfp_flags = GFP_ATOMIC;
#endif
	base = alloc_pages(gfp_flags, 0);
	if (!base)
		return -ENOMEM;

	spin_lock_irqsave(&pgd_lock, flags);
	/*
	 * Check for races, another CPU might have split this page
	 * up for us already:
	 */
	tmp = lookup_address(address, &level);
	if (tmp != kpte) {
		WARN_ON_ONCE(1);
		goto out_unlock;
	}

	address = __pa(address);
	addr = address & LARGE_PAGE_MASK;
	pbase = (pte_t *)page_address(base);
	paravirt_alloc_pt(&init_mm, page_to_pfn(base));

	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE)
		set_pte(&pbase[i], pfn_pte(addr >> PAGE_SHIFT, ref_prot));

	/*
	 * Install the new, split up pagetable:
	 */
	__set_pmd_pte(kpte, address, mk_pte(base, ref_prot));
	base = NULL;

out_unlock:
	spin_unlock_irqrestore(&pgd_lock, flags);

	if (base)
		__free_pages(base, 0);

	return 0;
}

static int __change_page_attr(struct page *page, pgprot_t prot)
{
	struct page *kpte_page;
	unsigned long address;
	int level, err = 0;
	pte_t *kpte;

	BUG_ON(PageHighMem(page));
	address = (unsigned long)page_address(page);

repeat:
	kpte = lookup_address(address, &level);
	if (!kpte)
		return -EINVAL;

	kpte_page = virt_to_page(kpte);
	BUG_ON(PageLRU(kpte_page));
	BUG_ON(PageCompound(kpte_page));

	/*
	 * Better fail early if someone sets the kernel text to NX.
	 * Does not cover __inittext
	 */
	BUG_ON(address >= (unsigned long)&_text &&
		address < (unsigned long)&_etext &&
	       (pgprot_val(prot) & _PAGE_NX));

	if (level == 4) {
		set_pte_atomic(kpte, mk_pte(page, canon_pgprot(prot)));
	} else {
		err = split_large_page(kpte, address);
		if (!err)
			goto repeat;
	}
	return err;
}

/*
 * Change the page attributes of an page in the linear mapping.
 *
 * This should be used when a page is mapped with a different caching policy
 * than write-back somewhere - some CPUs do not like it when mappings with
 * different caching policies exist. This changes the page attributes of the
 * in kernel linear mapping too.
 *
 * The caller needs to ensure that there are no conflicting mappings elsewhere.
 * This function only deals with the kernel linear map.
 *
 * Caller must call global_flush_tlb() after this.
 */
int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	int err = 0, i;

	for (i = 0; i < numpages; i++, page++) {
		err = __change_page_attr(page, prot);
		if (err)
			break;
	}

	return err;
}
EXPORT_SYMBOL(change_page_attr);

int change_page_attr_addr(unsigned long addr, int numpages, pgprot_t prot)
{
	int i;
	unsigned long pfn = (__pa(addr) >> PAGE_SHIFT);

	for (i = 0; i < numpages; i++) {
		if (!pfn_valid(pfn + i)) {
			WARN_ON_ONCE(1);
			break;
		} else {
			int level;
			pte_t *pte = lookup_address(addr + i*PAGE_SIZE, &level);
			BUG_ON(pte && pte_none(*pte));
		}
	}

	return change_page_attr(virt_to_page(addr), i, prot);
}

static void flush_kernel_map(void *arg)
{
	/*
	 * Flush all to work around Errata in early athlons regarding
	 * large page flushing.
	 */
	__flush_tlb_all();

	if (boot_cpu_data.x86_model >= 4)
		wbinvd();
}

void global_flush_tlb(void)
{
	BUG_ON(irqs_disabled());

	on_each_cpu(flush_kernel_map, NULL, 1, 1);
}
EXPORT_SYMBOL(global_flush_tlb);

#ifdef CONFIG_DEBUG_PAGEALLOC
void kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;
	if (!enable) {
		debug_check_no_locks_freed(page_address(page),
					   numpages * PAGE_SIZE);
	}

	/*
	 * If page allocator is not up yet then do not call c_p_a():
	 */
	if (!debug_pagealloc_enabled)
		return;

	/*
	 * the return value is ignored - the calls cannot fail,
	 * large pages are disabled at boot time.
	 */
	change_page_attr(page, numpages, enable ? PAGE_KERNEL : __pgprot(0));

	/*
	 * we should perform an IPI and flush all tlbs,
	 * but that can deadlock->flush only current cpu.
	 */
	__flush_tlb_all();
}
#endif
