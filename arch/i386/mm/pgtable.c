/*
 *  linux/arch/i386/mm/pgtable.c
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/smp.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/spinlock.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/fixmap.h>
#include <asm/e820.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>

void show_mem(void)
{
	int total = 0, reserved = 0;
	int shared = 0, cached = 0;
	int highmem = 0;
	struct page *page;
	pg_data_t *pgdat;
	unsigned long i;
	unsigned long flags;

	printk(KERN_INFO "Mem-info:\n");
	show_free_areas();
	printk(KERN_INFO "Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	for_each_online_pgdat(pgdat) {
		pgdat_resize_lock(pgdat, &flags);
		for (i = 0; i < pgdat->node_spanned_pages; ++i) {
			page = pgdat_page_nr(pgdat, i);
			total++;
			if (PageHighMem(page))
				highmem++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
		pgdat_resize_unlock(pgdat, &flags);
	}
	printk(KERN_INFO "%d pages of RAM\n", total);
	printk(KERN_INFO "%d pages of HIGHMEM\n", highmem);
	printk(KERN_INFO "%d reserved pages\n", reserved);
	printk(KERN_INFO "%d pages shared\n", shared);
	printk(KERN_INFO "%d pages swap cached\n", cached);

	printk(KERN_INFO "%lu pages dirty\n", global_page_state(NR_FILE_DIRTY));
	printk(KERN_INFO "%lu pages writeback\n",
					global_page_state(NR_WRITEBACK));
	printk(KERN_INFO "%lu pages mapped\n", global_page_state(NR_FILE_MAPPED));
	printk(KERN_INFO "%lu pages slab\n",
		global_page_state(NR_SLAB_RECLAIMABLE) +
		global_page_state(NR_SLAB_UNRECLAIMABLE));
	printk(KERN_INFO "%lu pages pagetables\n",
					global_page_state(NR_PAGETABLE));
}

/*
 * Associate a virtual page frame with a given physical page frame 
 * and protection flags for that frame.
 */ 
static void set_pte_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		BUG();
		return;
	}
	pud = pud_offset(pgd, vaddr);
	if (pud_none(*pud)) {
		BUG();
		return;
	}
	pmd = pmd_offset(pud, vaddr);
	if (pmd_none(*pmd)) {
		BUG();
		return;
	}
	pte = pte_offset_kernel(pmd, vaddr);
	if (pgprot_val(flags))
		/* <pfn,flags> stored as-is, to permit clearing entries */
		set_pte(pte, pfn_pte(pfn, flags));
	else
		pte_clear(&init_mm, vaddr, pte);

	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

/*
 * Associate a large virtual page frame with a given physical page frame 
 * and protection flags for that frame. pfn is for the base of the page,
 * vaddr is what the page gets mapped to - both must be properly aligned. 
 * The pmd must already be instantiated. Assumes PAE mode.
 */ 
void set_pmd_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;

	if (vaddr & (PMD_SIZE-1)) {		/* vaddr is misaligned */
		printk(KERN_WARNING "set_pmd_pfn: vaddr misaligned\n");
		return; /* BUG(); */
	}
	if (pfn & (PTRS_PER_PTE-1)) {		/* pfn is misaligned */
		printk(KERN_WARNING "set_pmd_pfn: pfn misaligned\n");
		return; /* BUG(); */
	}
	pgd = swapper_pg_dir + pgd_index(vaddr);
	if (pgd_none(*pgd)) {
		printk(KERN_WARNING "set_pmd_pfn: pgd_none\n");
		return; /* BUG(); */
	}
	pud = pud_offset(pgd, vaddr);
	pmd = pmd_offset(pud, vaddr);
	set_pmd(pmd, pfn_pmd(pfn, flags));
	/*
	 * It's enough to flush this one mapping.
	 * (PGE mappings get flushed as well)
	 */
	__flush_tlb_one(vaddr);
}

static int fixmaps;
#ifndef CONFIG_COMPAT_VDSO
unsigned long __FIXADDR_TOP = 0xfffff000;
EXPORT_SYMBOL(__FIXADDR_TOP);
#endif

void __set_fixmap (enum fixed_addresses idx, unsigned long phys, pgprot_t flags)
{
	unsigned long address = __fix_to_virt(idx);

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	set_pte_pfn(address, phys >> PAGE_SHIFT, flags);
	fixmaps++;
}

/**
 * reserve_top_address - reserves a hole in the top of kernel address space
 * @reserve - size of hole to reserve
 *
 * Can be used to relocate the fixmap area and poke a hole in the top
 * of kernel address space to make room for a hypervisor.
 */
void reserve_top_address(unsigned long reserve)
{
	BUG_ON(fixmaps > 0);
#ifdef CONFIG_COMPAT_VDSO
	BUG_ON(reserve != 0);
#else
	__FIXADDR_TOP = -reserve - PAGE_SIZE;
	__VMALLOC_RESERVE += reserve;
#endif
}

pte_t *pte_alloc_one_kernel(struct mm_struct *mm, unsigned long address)
{
	return (pte_t *)__get_free_page(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO);
}

struct page *pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	struct page *pte;

#ifdef CONFIG_HIGHPTE
	pte = alloc_pages(GFP_KERNEL|__GFP_HIGHMEM|__GFP_REPEAT|__GFP_ZERO, 0);
#else
	pte = alloc_pages(GFP_KERNEL|__GFP_REPEAT|__GFP_ZERO, 0);
#endif
	return pte;
}

void pmd_ctor(void *pmd, struct kmem_cache *cache, unsigned long flags)
{
	memset(pmd, 0, PTRS_PER_PMD*sizeof(pmd_t));
}

/*
 * List of all pgd's needed for non-PAE so it can invalidate entries
 * in both cached and uncached pgd's; not needed for PAE since the
 * kernel pmd is shared. If PAE were not to share the pmd a similar
 * tactic would be needed. This is essentially codepath-based locking
 * against pageattr.c; it is the unique case in which a valid change
 * of kernel pagetables can't be lazily synchronized by vmalloc faults.
 * vmalloc faults work because attached pagetables are never freed.
 * The locking scheme was chosen on the basis of manfred's
 * recommendations and having no core impact whatsoever.
 * -- wli
 */
DEFINE_SPINLOCK(pgd_lock);
struct page *pgd_list;

static inline void pgd_list_add(pgd_t *pgd)
{
	struct page *page = virt_to_page(pgd);
	page->index = (unsigned long)pgd_list;
	if (pgd_list)
		set_page_private(pgd_list, (unsigned long)&page->index);
	pgd_list = page;
	set_page_private(page, (unsigned long)&pgd_list);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct page *next, **pprev, *page = virt_to_page(pgd);
	next = (struct page *)page->index;
	pprev = (struct page **)page_private(page);
	*pprev = next;
	if (next)
		set_page_private(next, (unsigned long)pprev);
}

void pgd_ctor(void *pgd, struct kmem_cache *cache, unsigned long unused)
{
	unsigned long flags;

	if (PTRS_PER_PMD == 1) {
		memset(pgd, 0, USER_PTRS_PER_PGD*sizeof(pgd_t));
		spin_lock_irqsave(&pgd_lock, flags);
	}

	clone_pgd_range((pgd_t *)pgd + USER_PTRS_PER_PGD,
			swapper_pg_dir + USER_PTRS_PER_PGD,
			KERNEL_PGD_PTRS);
	if (PTRS_PER_PMD > 1)
		return;

	pgd_list_add(pgd);
	spin_unlock_irqrestore(&pgd_lock, flags);
}

/* never called when PTRS_PER_PMD > 1 */
void pgd_dtor(void *pgd, struct kmem_cache *cache, unsigned long unused)
{
	unsigned long flags; /* can be called from interrupt context */

	spin_lock_irqsave(&pgd_lock, flags);
	pgd_list_del(pgd);
	spin_unlock_irqrestore(&pgd_lock, flags);
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	int i;
	pgd_t *pgd = kmem_cache_alloc(pgd_cache, GFP_KERNEL);

	if (PTRS_PER_PMD == 1 || !pgd)
		return pgd;

	for (i = 0; i < USER_PTRS_PER_PGD; ++i) {
		pmd_t *pmd = kmem_cache_alloc(pmd_cache, GFP_KERNEL);
		if (!pmd)
			goto out_oom;
		set_pgd(&pgd[i], __pgd(1 + __pa(pmd)));
	}
	return pgd;

out_oom:
	for (i--; i >= 0; i--)
		kmem_cache_free(pmd_cache, (void *)__va(pgd_val(pgd[i])-1));
	kmem_cache_free(pgd_cache, pgd);
	return NULL;
}

void pgd_free(pgd_t *pgd)
{
	int i;

	/* in the PAE case user pgd entries are overwritten before usage */
	if (PTRS_PER_PMD > 1)
		for (i = 0; i < USER_PTRS_PER_PGD; ++i)
			kmem_cache_free(pmd_cache, (void *)__va(pgd_val(pgd[i])-1));
	/* in the non-PAE case, free_pgtables() clears user pgd entries */
	kmem_cache_free(pgd_cache, pgd);
}
