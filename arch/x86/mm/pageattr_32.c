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
	*level = 2;
	if (pmd_large(*pmd))
		return (pte_t *)pmd;
	*level = 3;

	return pte_offset_kernel(pmd, address);
}

static struct page *
split_large_page(unsigned long address, pgprot_t prot, pgprot_t ref_prot)
{
	unsigned long addr;
	struct page *base;
	pte_t *pbase;
	int i;

	base = alloc_pages(GFP_KERNEL, 0);
	if (!base)
		return NULL;

	/*
	 * page_private is used to track the number of entries in
	 * the page table page that have non standard attributes.
	 */
	address = __pa(address);
	addr = address & LARGE_PAGE_MASK;
	pbase = (pte_t *)page_address(base);
	paravirt_alloc_pt(&init_mm, page_to_pfn(base));

	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE) {
		set_pte(&pbase[i], pfn_pte(addr >> PAGE_SHIFT,
					   addr == address ? prot : ref_prot));
	}
	return base;
}

static void set_pmd_pte(pte_t *kpte, unsigned long address, pte_t pte)
{
	unsigned long flags;
	struct page *page;

	/* change init_mm */
	set_pte_atomic(kpte, pte);
	if (SHARED_KERNEL_PMD)
		return;

	spin_lock_irqsave(&pgd_lock, flags);
	for (page = pgd_list; page; page = (struct page *)page->index) {
		pgd_t *pgd;
		pud_t *pud;
		pmd_t *pmd;

		pgd = (pgd_t *)page_address(page) + pgd_index(address);
		pud = pud_offset(pgd, address);
		pmd = pmd_offset(pud, address);
		set_pte_atomic((pte_t *)pmd, pte);
	}
	spin_unlock_irqrestore(&pgd_lock, flags);
}

static int __change_page_attr(struct page *page, pgprot_t prot)
{
	pgprot_t ref_prot = PAGE_KERNEL;
	struct page *kpte_page;
	unsigned long address;
	pgprot_t oldprot;
	pte_t *kpte;
	int level;

	BUG_ON(PageHighMem(page));
	address = (unsigned long)page_address(page);

	kpte = lookup_address(address, &level);
	if (!kpte)
		return -EINVAL;

	oldprot = pte_pgprot(*kpte);
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

	if ((address & LARGE_PAGE_MASK) < (unsigned long)&_etext)
		ref_prot = PAGE_KERNEL_EXEC;

	ref_prot = canon_pgprot(ref_prot);
	prot = canon_pgprot(prot);

	if (level == 3) {
		set_pte_atomic(kpte, mk_pte(page, prot));
	} else {
		struct page *split;
		split = split_large_page(address, prot, ref_prot);
		if (!split)
			return -ENOMEM;

		/*
		 * There's a small window here to waste a bit of RAM:
		 */
		set_pmd_pte(kpte, address, mk_pte(split, ref_prot));
	}
	return 0;
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
	unsigned long pfn = (addr >> PAGE_SHIFT);

	for (i = 0; i < numpages; i++) {
		if (!pfn_valid(pfn + i)) {
			break;
		} else {
			int level;
			pte_t *pte = lookup_address(addr + i*PAGE_SIZE, &level);
			BUG_ON(pte && !pte_none(*pte));
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
