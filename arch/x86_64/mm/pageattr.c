/* 
 * Copyright 2002 Andi Kleen, SuSE Labs. 
 * Thanks to Ben LaHaise for precious feedback.
 */ 

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

static inline pte_t *lookup_address(unsigned long address) 
{ 
	pgd_t *pgd = pgd_offset_k(address);
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	if (pgd_none(*pgd))
		return NULL;
	pud = pud_offset(pgd, address);
	if (!pud_present(*pud))
		return NULL; 
	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return NULL; 
	if (pmd_large(*pmd))
		return (pte_t *)pmd;
	pte = pte_offset_kernel(pmd, address);
	if (pte && !pte_present(*pte))
		pte = NULL; 
	return pte;
} 

static struct page *split_large_page(unsigned long address, pgprot_t prot,
				     pgprot_t ref_prot)
{ 
	int i; 
	unsigned long addr;
	struct page *base = alloc_pages(GFP_KERNEL, 0);
	pte_t *pbase;
	if (!base) 
		return NULL;
	/*
	 * page_private is used to track the number of entries in
	 * the page table page have non standard attributes.
	 */
	SetPagePrivate(base);
	page_private(base) = 0;

	address = __pa(address);
	addr = address & LARGE_PAGE_MASK; 
	pbase = (pte_t *)page_address(base);
	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE) {
		pbase[i] = pfn_pte(addr >> PAGE_SHIFT, 
				   addr == address ? prot : ref_prot);
	}
	return base;
} 

static void cache_flush_page(void *adr)
{
	int i;
	for (i = 0; i < PAGE_SIZE; i += boot_cpu_data.x86_clflush_size)
		asm volatile("clflush (%0)" :: "r" (adr + i));
}

static void flush_kernel_map(void *arg)
{
	struct list_head *l = (struct list_head *)arg;
	struct page *pg;

	/* When clflush is available always use it because it is
	   much cheaper than WBINVD. Disable clflush for now because
	   the high level code is not ready yet */
	if (1 || !cpu_has_clflush)
		asm volatile("wbinvd" ::: "memory");
	else list_for_each_entry(pg, l, lru) {
		void *adr = page_address(pg);
		if (cpu_has_clflush)
			cache_flush_page(adr);
	}
	__flush_tlb_all();
}

static inline void flush_map(struct list_head *l)
{	
	on_each_cpu(flush_kernel_map, l, 1, 1);
}

static LIST_HEAD(deferred_pages); /* protected by init_mm.mmap_sem */

static inline void save_page(struct page *fpage)
{
	list_add(&fpage->lru, &deferred_pages);
}

/* 
 * No more special protections in this 2/4MB area - revert to a
 * large page again. 
 */
static void revert_page(unsigned long address, pgprot_t ref_prot)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t large_pte;
	unsigned long pfn;

	pgd = pgd_offset_k(address);
	BUG_ON(pgd_none(*pgd));
	pud = pud_offset(pgd,address);
	BUG_ON(pud_none(*pud));
	pmd = pmd_offset(pud, address);
	BUG_ON(pmd_val(*pmd) & _PAGE_PSE);
	pfn = (__pa(address) & LARGE_PAGE_MASK) >> PAGE_SHIFT;
	large_pte = pfn_pte(pfn, ref_prot);
	large_pte = pte_mkhuge(large_pte);
	set_pte((pte_t *)pmd, large_pte);
}      

static int
__change_page_attr(unsigned long address, unsigned long pfn, pgprot_t prot,
				   pgprot_t ref_prot)
{ 
	pte_t *kpte; 
	struct page *kpte_page;
	pgprot_t ref_prot2;
	kpte = lookup_address(address);
	if (!kpte) return 0;
	kpte_page = virt_to_page(((unsigned long)kpte) & PAGE_MASK);
	if (pgprot_val(prot) != pgprot_val(ref_prot)) { 
		if (!pte_huge(*kpte)) {
			set_pte(kpte, pfn_pte(pfn, prot));
		} else {
 			/*
			 * split_large_page will take the reference for this
			 * change_page_attr on the split page.
 			 */
			struct page *split;
			ref_prot2 = pte_pgprot(pte_clrhuge(*kpte));
			split = split_large_page(address, prot, ref_prot2);
			if (!split)
				return -ENOMEM;
			set_pte(kpte, mk_pte(split, ref_prot2));
			kpte_page = split;
		}
		page_private(kpte_page)++;
	} else if (!pte_huge(*kpte)) {
		set_pte(kpte, pfn_pte(pfn, ref_prot));
		BUG_ON(page_private(kpte_page) == 0);
		page_private(kpte_page)--;
	} else
		BUG();

	/* on x86-64 the direct mapping set at boot is not using 4k pages */
 	BUG_ON(PageReserved(kpte_page));

	if (page_private(kpte_page) == 0) {
		save_page(kpte_page);
		revert_page(address, ref_prot);
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
int change_page_attr_addr(unsigned long address, int numpages, pgprot_t prot)
{
	int err = 0, kernel_map = 0;
	int i; 

	if (address >= __START_KERNEL_map
	    && address < __START_KERNEL_map + KERNEL_TEXT_SIZE) {
		address = (unsigned long)__va(__pa(address));
		kernel_map = 1;
	}

	down_write(&init_mm.mmap_sem);
	for (i = 0; i < numpages; i++, address += PAGE_SIZE) {
		unsigned long pfn = __pa(address) >> PAGE_SHIFT;

		if (!kernel_map || pte_present(pfn_pte(0, prot))) {
			err = __change_page_attr(address, pfn, prot, PAGE_KERNEL);
			if (err)
				break;
		}
		/* Handle kernel mapping too which aliases part of the
		 * lowmem */
		if (__pa(address) < KERNEL_TEXT_SIZE) {
			unsigned long addr2;
			pgprot_t prot2;
			addr2 = __START_KERNEL_map + __pa(address);
			/* Make sure the kernel mappings stay executable */
			prot2 = pte_pgprot(pte_mkexec(pfn_pte(0, prot)));
			err = __change_page_attr(addr2, pfn, prot2,
						 PAGE_KERNEL_EXEC);
		} 
	} 	
	up_write(&init_mm.mmap_sem); 
	return err;
}

/* Don't call this for MMIO areas that may not have a mem_map entry */
int change_page_attr(struct page *page, int numpages, pgprot_t prot)
{
	unsigned long addr = (unsigned long)page_address(page);
	return change_page_attr_addr(addr, numpages, prot);
}

void global_flush_tlb(void)
{ 
	struct page *pg, *next;
	struct list_head l;

	down_read(&init_mm.mmap_sem);
	list_replace_init(&deferred_pages, &l);
	up_read(&init_mm.mmap_sem);

	flush_map(&l);

	list_for_each_entry_safe(pg, next, &l, lru) {
		ClearPagePrivate(pg);
		__free_page(pg);
	} 
} 

EXPORT_SYMBOL(change_page_attr);
EXPORT_SYMBOL(global_flush_tlb);
