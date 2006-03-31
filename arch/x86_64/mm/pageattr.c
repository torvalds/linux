/* 
 * Copyright 2002 Andi Kleen, SuSE Labs. 
 * Thanks to Ben LaHaise for precious feedback.
 */ 

#include <linux/config.h>
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


static void flush_kernel_map(void *address) 
{
	if (0 && address && cpu_has_clflush) {
		/* is this worth it? */ 
		int i;
		for (i = 0; i < PAGE_SIZE; i += boot_cpu_data.x86_clflush_size) 
			asm volatile("clflush (%0)" :: "r" (address + i)); 
	} else
		asm volatile("wbinvd":::"memory"); 
	if (address)
		__flush_tlb_one(address);
	else
		__flush_tlb_all();
}


static inline void flush_map(unsigned long address)
{	
	on_each_cpu(flush_kernel_map, (void *)address, 1, 1);
}

static struct page *deferred_pages; /* protected by init_mm.mmap_sem */

static inline void save_page(struct page *fpage)
{
	fpage->lru.next = (struct list_head *)deferred_pages;
	deferred_pages = fpage;
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

	pgd = pgd_offset_k(address);
	BUG_ON(pgd_none(*pgd));
	pud = pud_offset(pgd,address);
	BUG_ON(pud_none(*pud));
	pmd = pmd_offset(pud, address);
	BUG_ON(pmd_val(*pmd) & _PAGE_PSE);
	pgprot_val(ref_prot) |= _PAGE_PSE;
	large_pte = mk_pte_phys(__pa(address) & LARGE_PAGE_MASK, ref_prot);
	set_pte((pte_t *)pmd, large_pte);
}      

static int
__change_page_attr(unsigned long address, unsigned long pfn, pgprot_t prot,
				   pgprot_t ref_prot)
{ 
	pte_t *kpte; 
	struct page *kpte_page;
	unsigned kpte_flags;
	pgprot_t ref_prot2;
	kpte = lookup_address(address);
	if (!kpte) return 0;
	kpte_page = virt_to_page(((unsigned long)kpte) & PAGE_MASK);
	kpte_flags = pte_val(*kpte); 
	if (pgprot_val(prot) != pgprot_val(ref_prot)) { 
		if ((kpte_flags & _PAGE_PSE) == 0) { 
			set_pte(kpte, pfn_pte(pfn, prot));
		} else {
 			/*
			 * split_large_page will take the reference for this
			 * change_page_attr on the split page.
 			 */

			struct page *split;
			ref_prot2 = __pgprot(pgprot_val(pte_pgprot(*lookup_address(address))) & ~(1<<_PAGE_BIT_PSE));

			split = split_large_page(address, prot, ref_prot2);
			if (!split)
				return -ENOMEM;
			set_pte(kpte,mk_pte(split, ref_prot2));
			kpte_page = split;
		}	
		page_private(kpte_page)++;
	} else if ((kpte_flags & _PAGE_PSE) == 0) { 
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
	int err = 0; 
	int i; 

	down_write(&init_mm.mmap_sem);
	for (i = 0; i < numpages; i++, address += PAGE_SIZE) {
		unsigned long pfn = __pa(address) >> PAGE_SHIFT;

		err = __change_page_attr(address, pfn, prot, PAGE_KERNEL);
		if (err) 
			break; 
		/* Handle kernel mapping too which aliases part of the
		 * lowmem */
		if (__pa(address) < KERNEL_TEXT_SIZE) {
			unsigned long addr2;
			pgprot_t prot2 = prot;
			addr2 = __START_KERNEL_map + __pa(address);
 			pgprot_val(prot2) &= ~_PAGE_NX;
			err = __change_page_attr(addr2, pfn, prot2, PAGE_KERNEL_EXEC);
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
	struct page *dpage;

	down_read(&init_mm.mmap_sem);
	dpage = xchg(&deferred_pages, NULL);
	up_read(&init_mm.mmap_sem);

	flush_map((dpage && !dpage->lru.next) ? (unsigned long)page_address(dpage) : 0);
	while (dpage) {
		struct page *tmp = dpage;
		dpage = (struct page *)dpage->lru.next;
		ClearPagePrivate(tmp);
		__free_page(tmp);
	} 
} 

EXPORT_SYMBOL(change_page_attr);
EXPORT_SYMBOL(global_flush_tlb);
