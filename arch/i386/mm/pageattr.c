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
#include <asm/pgalloc.h>
#include <asm/sections.h>

static DEFINE_SPINLOCK(cpa_lock);
static struct list_head df_list = LIST_HEAD_INIT(df_list);


pte_t *lookup_address(unsigned long address) 
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
	if (pmd_large(*pmd))
		return (pte_t *)pmd;
        return pte_offset_kernel(pmd, address);
} 

static struct page *split_large_page(unsigned long address, pgprot_t prot,
					pgprot_t ref_prot)
{ 
	int i; 
	unsigned long addr;
	struct page *base;
	pte_t *pbase;

	spin_unlock_irq(&cpa_lock);
	base = alloc_pages(GFP_KERNEL, 0);
	spin_lock_irq(&cpa_lock);
	if (!base) 
		return NULL;

	address = __pa(address);
	addr = address & LARGE_PAGE_MASK; 
	pbase = (pte_t *)page_address(base);
	for (i = 0; i < PTRS_PER_PTE; i++, addr += PAGE_SIZE) {
               set_pte(&pbase[i], pfn_pte(addr >> PAGE_SHIFT,
                                          addr == address ? prot : ref_prot));
	}
	return base;
} 

static void flush_kernel_map(void *dummy) 
{ 
	/* Could use CLFLUSH here if the CPU supports it (Hammer,P4) */
	if (boot_cpu_data.x86_model >= 4) 
		wbinvd();
	/* Flush all to work around Errata in early athlons regarding 
	 * large page flushing. 
	 */
	__flush_tlb_all(); 	
}

static void set_pmd_pte(pte_t *kpte, unsigned long address, pte_t pte) 
{ 
	struct page *page;
	unsigned long flags;

	set_pte_atomic(kpte, pte); 	/* change init_mm */
	if (PTRS_PER_PMD > 1)
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

/* 
 * No more special protections in this 2/4MB area - revert to a
 * large page again. 
 */
static inline void revert_page(struct page *kpte_page, unsigned long address)
{
	pgprot_t ref_prot;
	pte_t *linear;

	ref_prot =
	((address & LARGE_PAGE_MASK) < (unsigned long)&_etext)
		? PAGE_KERNEL_LARGE_EXEC : PAGE_KERNEL_LARGE;

	linear = (pte_t *)
		pmd_offset(pud_offset(pgd_offset_k(address), address), address);
	set_pmd_pte(linear,  address,
		    pfn_pte((__pa(address) & LARGE_PAGE_MASK) >> PAGE_SHIFT,
			    ref_prot));
}

static int
__change_page_attr(struct page *page, pgprot_t prot)
{ 
	pte_t *kpte; 
	unsigned long address;
	struct page *kpte_page;

	BUG_ON(PageHighMem(page));
	address = (unsigned long)page_address(page);

	kpte = lookup_address(address);
	if (!kpte)
		return -EINVAL;
	kpte_page = virt_to_page(kpte);
	if (pgprot_val(prot) != pgprot_val(PAGE_KERNEL)) { 
		if ((pte_val(*kpte) & _PAGE_PSE) == 0) { 
			set_pte_atomic(kpte, mk_pte(page, prot)); 
		} else {
			pgprot_t ref_prot;
			struct page *split;

			ref_prot =
			((address & LARGE_PAGE_MASK) < (unsigned long)&_etext)
				? PAGE_KERNEL_EXEC : PAGE_KERNEL;
			split = split_large_page(address, prot, ref_prot);
			if (!split)
				return -ENOMEM;
			set_pmd_pte(kpte,address,mk_pte(split, ref_prot));
			kpte_page = split;
		}	
		get_page(kpte_page);
	} else if ((pte_val(*kpte) & _PAGE_PSE) == 0) { 
		set_pte_atomic(kpte, mk_pte(page, PAGE_KERNEL));
		__put_page(kpte_page);
	} else
		BUG();

	/*
	 * If the pte was reserved, it means it was created at boot
	 * time (not via split_large_page) and in turn we must not
	 * replace it with a largepage.
	 */
	if (!PageReserved(kpte_page)) {
		/* memleak and potential failed 2M page regeneration */
		BUG_ON(!page_count(kpte_page));

		if (cpu_has_pse && (page_count(kpte_page) == 1)) {
			list_add(&kpte_page->lru, &df_list);
			revert_page(kpte_page, address);
		}
	}
	return 0;
} 

static inline void flush_map(void)
{
	on_each_cpu(flush_kernel_map, NULL, 1, 1);
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
	int err = 0; 
	int i; 
	unsigned long flags;

	spin_lock_irqsave(&cpa_lock, flags);
	for (i = 0; i < numpages; i++, page++) { 
		err = __change_page_attr(page, prot);
		if (err) 
			break; 
	} 	
	spin_unlock_irqrestore(&cpa_lock, flags);
	return err;
}

void global_flush_tlb(void)
{ 
	LIST_HEAD(l);
	struct page *pg, *next;

	BUG_ON(irqs_disabled());

	spin_lock_irq(&cpa_lock);
	list_splice_init(&df_list, &l);
	spin_unlock_irq(&cpa_lock);
	flush_map();
	list_for_each_entry_safe(pg, next, &l, lru)
		__free_page(pg);
} 

#ifdef CONFIG_DEBUG_PAGEALLOC
void kernel_map_pages(struct page *page, int numpages, int enable)
{
	if (PageHighMem(page))
		return;
	if (!enable)
		mutex_debug_check_no_locks_freed(page_address(page),
						 numpages * PAGE_SIZE);

	/* the return value is ignored - the calls cannot fail,
	 * large pages are disabled at boot time.
	 */
	change_page_attr(page, numpages, enable ? PAGE_KERNEL : __pgprot(0));
	/* we should perform an IPI and flush all tlbs,
	 * but that can deadlock->flush only current cpu.
	 */
	__flush_tlb_all();
}
#endif

EXPORT_SYMBOL(change_page_attr);
EXPORT_SYMBOL(global_flush_tlb);
