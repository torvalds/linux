/*
 * Copyright 2002 Andi Kleen, SuSE Labs.
 * Thanks to Ben LaHaise for precious feedback.
 */
#include <linux/highmem.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

void clflush_cache_range(void *addr, int size)
{
	int i;

	for (i = 0; i < size; i += boot_cpu_data.x86_clflush_size)
		clflush(addr+i);
}

#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

/*
 * We allow the BIOS range to be executable:
 */
#define BIOS_BEGIN		0x000a0000
#define BIOS_END		0x00100000

static inline pgprot_t check_exec(pgprot_t prot, unsigned long address)
{
	if (__pa(address) >= BIOS_BEGIN && __pa(address) < BIOS_END)
		pgprot_val(prot) &= ~_PAGE_NX;
	/*
	 * Better fail early if someone sets the kernel text to NX.
	 * Does not cover __inittext
	 */
	BUG_ON(address >= (unsigned long)&_text &&
		address < (unsigned long)&_etext &&
	       (pgprot_val(prot) & _PAGE_NX));

	return prot;
}

pte_t *lookup_address(unsigned long address, int *level)
{
	pgd_t *pgd = pgd_offset_k(address);
	pud_t *pud;
	pmd_t *pmd;

	*level = PG_LEVEL_NONE;

	if (pgd_none(*pgd))
		return NULL;
	pud = pud_offset(pgd, address);
	if (pud_none(*pud))
		return NULL;
	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd))
		return NULL;

	*level = PG_LEVEL_2M;
	if (pmd_large(*pmd))
		return (pte_t *)pmd;

	*level = PG_LEVEL_4K;
	return pte_offset_kernel(pmd, address);
}

static void __set_pmd_pte(pte_t *kpte, unsigned long address, pte_t pte)
{
	/* change init_mm */
	set_pte_atomic(kpte, pte);
#ifdef CONFIG_X86_32
	if (!SHARED_KERNEL_PMD) {
		struct page *page;

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
#endif
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
#ifdef CONFIG_X86_32
	paravirt_alloc_pt(&init_mm, page_to_pfn(base));
#endif

	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE)
		set_pte(&pbase[i], pfn_pte(addr >> PAGE_SHIFT, ref_prot));

	/*
	 * Install the new, split up pagetable. Important detail here:
	 *
	 * On Intel the NX bit of all levels must be cleared to make a
	 * page executable. See section 4.13.2 of Intel 64 and IA-32
	 * Architectures Software Developer's Manual).
	 */
	ref_prot = pte_pgprot(pte_mkexec(pte_clrhuge(*kpte)));
	__set_pmd_pte(kpte, address, mk_pte(base, ref_prot));
	base = NULL;

out_unlock:
	spin_unlock_irqrestore(&pgd_lock, flags);

	if (base)
		__free_pages(base, 0);

	return 0;
}

static int
__change_page_attr(unsigned long address, unsigned long pfn, pgprot_t prot)
{
	struct page *kpte_page;
	int level, err = 0;
	pte_t *kpte;

#ifdef CONFIG_X86_32
	BUG_ON(pfn > max_low_pfn);
#endif

repeat:
	kpte = lookup_address(address, &level);
	if (!kpte)
		return -EINVAL;

	kpte_page = virt_to_page(kpte);
	BUG_ON(PageLRU(kpte_page));
	BUG_ON(PageCompound(kpte_page));

	prot = check_exec(prot, address);

	if (level == PG_LEVEL_4K) {
		set_pte_atomic(kpte, pfn_pte(pfn, canon_pgprot(prot)));
	} else {
		err = split_large_page(kpte, address);
		if (!err)
			goto repeat;
	}
	return err;
}

/**
 * change_page_attr_addr - Change page table attributes in linear mapping
 * @address: Virtual address in linear mapping.
 * @numpages: Number of pages to change
 * @prot:    New page table attribute (PAGE_*)
 *
 * Change page attributes of a page in the direct mapping. This is a variant
 * of change_page_attr() that also works on memory holes that do not have
 * mem_map entry (pfn_valid() is false).
 *
 * See change_page_attr() documentation for more details.
 */

int change_page_attr_addr(unsigned long address, int numpages, pgprot_t prot)
{
	int err = 0, kernel_map = 0, i;

#ifdef CONFIG_X86_64
	if (address >= __START_KERNEL_map &&
			address < __START_KERNEL_map + KERNEL_TEXT_SIZE) {

		address = (unsigned long)__va(__pa(address));
		kernel_map = 1;
	}
#endif

	for (i = 0; i < numpages; i++, address += PAGE_SIZE) {
		unsigned long pfn = __pa(address) >> PAGE_SHIFT;

		if (!kernel_map || pte_present(pfn_pte(0, prot))) {
			err = __change_page_attr(address, pfn, prot);
			if (err)
				break;
		}
#ifdef CONFIG_X86_64
		/*
		 * Handle kernel mapping too which aliases part of
		 * lowmem:
		 */
		if (__pa(address) < KERNEL_TEXT_SIZE) {
			unsigned long addr2;
			pgprot_t prot2;

			addr2 = __START_KERNEL_map + __pa(address);
			/* Make sure the kernel mappings stay executable */
			prot2 = pte_pgprot(pte_mkexec(pfn_pte(0, prot)));
			err = __change_page_attr(addr2, pfn, prot2);
		}
#endif
	}

	return err;
}

/**
 * change_page_attr - Change page table attributes in the linear mapping.
 * @page: First page to change
 * @numpages: Number of pages to change
 * @prot: New protection/caching type (PAGE_*)
 *
 * Returns 0 on success, otherwise a negated errno.
 *
 * This should be used when a page is mapped with a different caching policy
 * than write-back somewhere - some CPUs do not like it when mappings with
 * different caching policies exist. This changes the page attributes of the
 * in kernel linear mapping too.
 *
 * Caller must call global_flush_tlb() later to make the changes active.
 *
 * The caller needs to ensure that there are no conflicting mappings elsewhere
 * (e.g. in user space) * This function only deals with the kernel linear map.
 *
 * For MMIO areas without mem_map use change_page_attr_addr() instead.
 */
int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	unsigned long addr = (unsigned long)page_address(page);

	return change_page_attr_addr(addr, numpages, prot);
}
EXPORT_SYMBOL(change_page_attr);

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
	 * The return value is ignored - the calls cannot fail,
	 * large pages are disabled at boot time:
	 */
	change_page_attr(page, numpages, enable ? PAGE_KERNEL : __pgprot(0));

	/*
	 * We should perform an IPI and flush all tlbs,
	 * but that can deadlock->flush only current cpu:
	 */
	__flush_tlb_all();
}
#endif
