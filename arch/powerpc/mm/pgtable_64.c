/*
 *  This file contains ioremap and related functions for 64-bit machines.
 *
 *  Derived from arch/ppc64/mm/init.c
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@samba.org)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Dave Engebretsen <engebret@us.ibm.com>
 *      Rework for PPC64 port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <linux/vmalloc.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>

#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/smp.h>
#include <asm/machdep.h>
#include <asm/tlb.h>
#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/sections.h>
#include <asm/firmware.h>
#include <asm/dma.h>

#include "mmu_decl.h"

#define CREATE_TRACE_POINTS
#include <trace/events/thp.h>

/* Some sanity checking */
#if TASK_SIZE_USER64 > PGTABLE_RANGE
#error TASK_SIZE_USER64 exceeds pagetable range
#endif

#ifdef CONFIG_PPC_STD_MMU_64
#if TASK_SIZE_USER64 > (1UL << (ESID_BITS + SID_SHIFT))
#error TASK_SIZE_USER64 exceeds user VSID range
#endif
#endif

unsigned long ioremap_bot = IOREMAP_BASE;

#ifdef CONFIG_PPC_MMU_NOHASH
static __ref void *early_alloc_pgtable(unsigned long size)
{
	void *pt;

	pt = __va(memblock_alloc_base(size, size, __pa(MAX_DMA_ADDRESS)));
	memset(pt, 0, size);

	return pt;
}
#endif /* CONFIG_PPC_MMU_NOHASH */

/*
 * map_kernel_page currently only called by __ioremap
 * map_kernel_page adds an entry to the ioremap page table
 * and adds an entry to the HPT, possibly bolting it
 */
int map_kernel_page(unsigned long ea, unsigned long pa, int flags)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	if (slab_is_available()) {
		pgdp = pgd_offset_k(ea);
		pudp = pud_alloc(&init_mm, pgdp, ea);
		if (!pudp)
			return -ENOMEM;
		pmdp = pmd_alloc(&init_mm, pudp, ea);
		if (!pmdp)
			return -ENOMEM;
		ptep = pte_alloc_kernel(pmdp, ea);
		if (!ptep)
			return -ENOMEM;
		set_pte_at(&init_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT,
							  __pgprot(flags)));
	} else {
#ifdef CONFIG_PPC_MMU_NOHASH
		pgdp = pgd_offset_k(ea);
#ifdef PUD_TABLE_SIZE
		if (pgd_none(*pgdp)) {
			pudp = early_alloc_pgtable(PUD_TABLE_SIZE);
			BUG_ON(pudp == NULL);
			pgd_populate(&init_mm, pgdp, pudp);
		}
#endif /* PUD_TABLE_SIZE */
		pudp = pud_offset(pgdp, ea);
		if (pud_none(*pudp)) {
			pmdp = early_alloc_pgtable(PMD_TABLE_SIZE);
			BUG_ON(pmdp == NULL);
			pud_populate(&init_mm, pudp, pmdp);
		}
		pmdp = pmd_offset(pudp, ea);
		if (!pmd_present(*pmdp)) {
			ptep = early_alloc_pgtable(PAGE_SIZE);
			BUG_ON(ptep == NULL);
			pmd_populate_kernel(&init_mm, pmdp, ptep);
		}
		ptep = pte_offset_kernel(pmdp, ea);
		set_pte_at(&init_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT,
							  __pgprot(flags)));
#else /* CONFIG_PPC_MMU_NOHASH */
		/*
		 * If the mm subsystem is not fully up, we cannot create a
		 * linux page table entry for this mapping.  Simply bolt an
		 * entry in the hardware page table.
		 *
		 */
		if (htab_bolt_mapping(ea, ea + PAGE_SIZE, pa, flags,
				      mmu_io_psize, mmu_kernel_ssize)) {
			printk(KERN_ERR "Failed to do bolted mapping IO "
			       "memory at %016lx !\n", pa);
			return -ENOMEM;
		}
#endif /* !CONFIG_PPC_MMU_NOHASH */
	}

#ifdef CONFIG_PPC_BOOK3E_64
	/*
	 * With hardware tablewalk, a sync is needed to ensure that
	 * subsequent accesses see the PTE we just wrote.  Unlike userspace
	 * mappings, we can't tolerate spurious faults, so make sure
	 * the new PTE will be seen the first time.
	 */
	mb();
#else
	smp_wmb();
#endif
	return 0;
}


/**
 * __ioremap_at - Low level function to establish the page tables
 *                for an IO mapping
 */
void __iomem * __ioremap_at(phys_addr_t pa, void *ea, unsigned long size,
			    unsigned long flags)
{
	unsigned long i;

	/* Make sure we have the base flags */
	if ((flags & _PAGE_PRESENT) == 0)
		flags |= pgprot_val(PAGE_KERNEL);

	/* Non-cacheable page cannot be coherent */
	if (flags & _PAGE_NO_CACHE)
		flags &= ~_PAGE_COHERENT;

	/* We don't support the 4K PFN hack with ioremap */
	if (flags & _PAGE_4K_PFN)
		return NULL;

	WARN_ON(pa & ~PAGE_MASK);
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	for (i = 0; i < size; i += PAGE_SIZE)
		if (map_kernel_page((unsigned long)ea+i, pa+i, flags))
			return NULL;

	return (void __iomem *)ea;
}

/**
 * __iounmap_from - Low level function to tear down the page tables
 *                  for an IO mapping. This is used for mappings that
 *                  are manipulated manually, like partial unmapping of
 *                  PCI IOs or ISA space.
 */
void __iounmap_at(void *ea, unsigned long size)
{
	WARN_ON(((unsigned long)ea) & ~PAGE_MASK);
	WARN_ON(size & ~PAGE_MASK);

	unmap_kernel_range((unsigned long)ea, size);
}

void __iomem * __ioremap_caller(phys_addr_t addr, unsigned long size,
				unsigned long flags, void *caller)
{
	phys_addr_t paligned;
	void __iomem *ret;

	/*
	 * Choose an address to map it to.
	 * Once the imalloc system is running, we use it.
	 * Before that, we map using addresses going
	 * up from ioremap_bot.  imalloc will use
	 * the addresses from ioremap_bot through
	 * IMALLOC_END
	 * 
	 */
	paligned = addr & PAGE_MASK;
	size = PAGE_ALIGN(addr + size) - paligned;

	if ((size == 0) || (paligned == 0))
		return NULL;

	if (slab_is_available()) {
		struct vm_struct *area;

		area = __get_vm_area_caller(size, VM_IOREMAP,
					    ioremap_bot, IOREMAP_END,
					    caller);
		if (area == NULL)
			return NULL;

		area->phys_addr = paligned;
		ret = __ioremap_at(paligned, area->addr, size, flags);
		if (!ret)
			vunmap(area->addr);
	} else {
		ret = __ioremap_at(paligned, (void *)ioremap_bot, size, flags);
		if (ret)
			ioremap_bot += size;
	}

	if (ret)
		ret += addr & ~PAGE_MASK;
	return ret;
}

void __iomem * __ioremap(phys_addr_t addr, unsigned long size,
			 unsigned long flags)
{
	return __ioremap_caller(addr, size, flags, __builtin_return_address(0));
}

void __iomem * ioremap(phys_addr_t addr, unsigned long size)
{
	unsigned long flags = _PAGE_NO_CACHE | _PAGE_GUARDED;
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}

void __iomem * ioremap_wc(phys_addr_t addr, unsigned long size)
{
	unsigned long flags = _PAGE_NO_CACHE;
	void *caller = __builtin_return_address(0);

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}

void __iomem * ioremap_prot(phys_addr_t addr, unsigned long size,
			     unsigned long flags)
{
	void *caller = __builtin_return_address(0);

	/* writeable implies dirty for kernel addresses */
	if (flags & _PAGE_RW)
		flags |= _PAGE_DIRTY;

	/* we don't want to let _PAGE_USER and _PAGE_EXEC leak out */
	flags &= ~(_PAGE_USER | _PAGE_EXEC);

#ifdef _PAGE_BAP_SR
	/* _PAGE_USER contains _PAGE_BAP_SR on BookE using the new PTE format
	 * which means that we just cleared supervisor access... oops ;-) This
	 * restores it
	 */
	flags |= _PAGE_BAP_SR;
#endif

	if (ppc_md.ioremap)
		return ppc_md.ioremap(addr, size, flags, caller);
	return __ioremap_caller(addr, size, flags, caller);
}


/*  
 * Unmap an IO region and remove it from imalloc'd list.
 * Access to IO memory should be serialized by driver.
 */
void __iounmap(volatile void __iomem *token)
{
	void *addr;

	if (!slab_is_available())
		return;
	
	addr = (void *) ((unsigned long __force)
			 PCI_FIX_ADDR(token) & PAGE_MASK);
	if ((unsigned long)addr < ioremap_bot) {
		printk(KERN_WARNING "Attempt to iounmap early bolted mapping"
		       " at 0x%p\n", addr);
		return;
	}
	vunmap(addr);
}

void iounmap(volatile void __iomem *token)
{
	if (ppc_md.iounmap)
		ppc_md.iounmap(token);
	else
		__iounmap(token);
}

EXPORT_SYMBOL(ioremap);
EXPORT_SYMBOL(ioremap_wc);
EXPORT_SYMBOL(ioremap_prot);
EXPORT_SYMBOL(__ioremap);
EXPORT_SYMBOL(__ioremap_at);
EXPORT_SYMBOL(iounmap);
EXPORT_SYMBOL(__iounmap);
EXPORT_SYMBOL(__iounmap_at);

#ifndef __PAGETABLE_PUD_FOLDED
/* 4 level page table */
struct page *pgd_page(pgd_t pgd)
{
	if (pgd_huge(pgd))
		return pte_page(pgd_pte(pgd));
	return virt_to_page(pgd_page_vaddr(pgd));
}
#endif

struct page *pud_page(pud_t pud)
{
	if (pud_huge(pud))
		return pte_page(pud_pte(pud));
	return virt_to_page(pud_page_vaddr(pud));
}

/*
 * For hugepage we have pfn in the pmd, we use PTE_RPN_SHIFT bits for flags
 * For PTE page, we have a PTE_FRAG_SIZE (4K) aligned virtual address.
 */
struct page *pmd_page(pmd_t pmd)
{
	if (pmd_trans_huge(pmd) || pmd_huge(pmd))
		return pfn_to_page(pmd_pfn(pmd));
	return virt_to_page(pmd_page_vaddr(pmd));
}

#ifdef CONFIG_PPC_64K_PAGES
static pte_t *get_from_cache(struct mm_struct *mm)
{
	void *pte_frag, *ret;

	spin_lock(&mm->page_table_lock);
	ret = mm->context.pte_frag;
	if (ret) {
		pte_frag = ret + PTE_FRAG_SIZE;
		/*
		 * If we have taken up all the fragments mark PTE page NULL
		 */
		if (((unsigned long)pte_frag & ~PAGE_MASK) == 0)
			pte_frag = NULL;
		mm->context.pte_frag = pte_frag;
	}
	spin_unlock(&mm->page_table_lock);
	return (pte_t *)ret;
}

static pte_t *__alloc_for_cache(struct mm_struct *mm, int kernel)
{
	void *ret = NULL;
	struct page *page = alloc_page(GFP_KERNEL | __GFP_NOTRACK |
				       __GFP_REPEAT | __GFP_ZERO);
	if (!page)
		return NULL;
	if (!kernel && !pgtable_page_ctor(page)) {
		__free_page(page);
		return NULL;
	}

	ret = page_address(page);
	spin_lock(&mm->page_table_lock);
	/*
	 * If we find pgtable_page set, we return
	 * the allocated page with single fragement
	 * count.
	 */
	if (likely(!mm->context.pte_frag)) {
		atomic_set(&page->_count, PTE_FRAG_NR);
		mm->context.pte_frag = ret + PTE_FRAG_SIZE;
	}
	spin_unlock(&mm->page_table_lock);

	return (pte_t *)ret;
}

pte_t *page_table_alloc(struct mm_struct *mm, unsigned long vmaddr, int kernel)
{
	pte_t *pte;

	pte = get_from_cache(mm);
	if (pte)
		return pte;

	return __alloc_for_cache(mm, kernel);
}

void page_table_free(struct mm_struct *mm, unsigned long *table, int kernel)
{
	struct page *page = virt_to_page(table);
	if (put_page_testzero(page)) {
		if (!kernel)
			pgtable_page_dtor(page);
		free_hot_cold_page(page, 0);
	}
}

#ifdef CONFIG_SMP
static void page_table_free_rcu(void *table)
{
	struct page *page = virt_to_page(table);
	if (put_page_testzero(page)) {
		pgtable_page_dtor(page);
		free_hot_cold_page(page, 0);
	}
}

void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift)
{
	unsigned long pgf = (unsigned long)table;

	BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
	pgf |= shift;
	tlb_remove_table(tlb, (void *)pgf);
}

void __tlb_remove_table(void *_table)
{
	void *table = (void *)((unsigned long)_table & ~MAX_PGTABLE_INDEX_SIZE);
	unsigned shift = (unsigned long)_table & MAX_PGTABLE_INDEX_SIZE;

	if (!shift)
		/* PTE page needs special handling */
		page_table_free_rcu(table);
	else {
		BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(shift), table);
	}
}
#else
void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift)
{
	if (!shift) {
		/* PTE page needs special handling */
		struct page *page = virt_to_page(table);
		if (put_page_testzero(page)) {
			pgtable_page_dtor(page);
			free_hot_cold_page(page, 0);
		}
	} else {
		BUG_ON(shift > MAX_PGTABLE_INDEX_SIZE);
		kmem_cache_free(PGT_CACHE(shift), table);
	}
}
#endif
#endif /* CONFIG_PPC_64K_PAGES */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

/*
 * This is called when relaxing access to a hugepage. It's also called in the page
 * fault path when we don't hit any of the major fault cases, ie, a minor
 * update of _PAGE_ACCESSED, _PAGE_DIRTY, etc... The generic code will have
 * handled those two for us, we additionally deal with missing execute
 * permission here on some processors
 */
int pmdp_set_access_flags(struct vm_area_struct *vma, unsigned long address,
			  pmd_t *pmdp, pmd_t entry, int dirty)
{
	int changed;
#ifdef CONFIG_DEBUG_VM
	WARN_ON(!pmd_trans_huge(*pmdp));
	assert_spin_locked(&vma->vm_mm->page_table_lock);
#endif
	changed = !pmd_same(*(pmdp), entry);
	if (changed) {
		__ptep_set_access_flags(pmdp_ptep(pmdp), pmd_pte(entry));
		/*
		 * Since we are not supporting SW TLB systems, we don't
		 * have any thing similar to flush_tlb_page_nohash()
		 */
	}
	return changed;
}

unsigned long pmd_hugepage_update(struct mm_struct *mm, unsigned long addr,
				  pmd_t *pmdp, unsigned long clr,
				  unsigned long set)
{

	unsigned long old, tmp;

#ifdef CONFIG_DEBUG_VM
	WARN_ON(!pmd_trans_huge(*pmdp));
	assert_spin_locked(&mm->page_table_lock);
#endif

#ifdef PTE_ATOMIC_UPDATES
	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3\n\
		andi.	%1,%0,%6\n\
		bne-	1b \n\
		andc	%1,%0,%4 \n\
		or	%1,%1,%7\n\
		stdcx.	%1,0,%3 \n\
		bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*pmdp)
	: "r" (pmdp), "r" (clr), "m" (*pmdp), "i" (_PAGE_BUSY), "r" (set)
	: "cc" );
#else
	old = pmd_val(*pmdp);
	*pmdp = __pmd((old & ~clr) | set);
#endif
	trace_hugepage_update(addr, old, clr, set);
	if (old & _PAGE_HASHPTE)
		hpte_do_hugepage_flush(mm, addr, pmdp, old);
	return old;
}

pmd_t pmdp_clear_flush(struct vm_area_struct *vma, unsigned long address,
		       pmd_t *pmdp)
{
	pmd_t pmd;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	if (pmd_trans_huge(*pmdp)) {
		pmd = pmdp_get_and_clear(vma->vm_mm, address, pmdp);
	} else {
		/*
		 * khugepaged calls this for normal pmd
		 */
		pmd = *pmdp;
		pmd_clear(pmdp);
		/*
		 * Wait for all pending hash_page to finish. This is needed
		 * in case of subpage collapse. When we collapse normal pages
		 * to hugepage, we first clear the pmd, then invalidate all
		 * the PTE entries. The assumption here is that any low level
		 * page fault will see a none pmd and take the slow path that
		 * will wait on mmap_sem. But we could very well be in a
		 * hash_page with local ptep pointer value. Such a hash page
		 * can result in adding new HPTE entries for normal subpages.
		 * That means we could be modifying the page content as we
		 * copy them to a huge page. So wait for parallel hash_page
		 * to finish before invalidating HPTE entries. We can do this
		 * by sending an IPI to all the cpus and executing a dummy
		 * function there.
		 */
		kick_all_cpus_sync();
		/*
		 * Now invalidate the hpte entries in the range
		 * covered by pmd. This make sure we take a
		 * fault and will find the pmd as none, which will
		 * result in a major fault which takes mmap_sem and
		 * hence wait for collapse to complete. Without this
		 * the __collapse_huge_page_copy can result in copying
		 * the old content.
		 */
		flush_tlb_pmd_range(vma->vm_mm, &pmd, address);
	}
	return pmd;
}

int pmdp_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long address, pmd_t *pmdp)
{
	return __pmdp_test_and_clear_young(vma->vm_mm, address, pmdp);
}

/*
 * We currently remove entries from the hashtable regardless of whether
 * the entry was young or dirty. The generic routines only flush if the
 * entry was young or dirty which is not good enough.
 *
 * We should be more intelligent about this but for the moment we override
 * these functions and force a tlb flush unconditionally
 */
int pmdp_clear_flush_young(struct vm_area_struct *vma,
				  unsigned long address, pmd_t *pmdp)
{
	return __pmdp_test_and_clear_young(vma->vm_mm, address, pmdp);
}

/*
 * We mark the pmd splitting and invalidate all the hpte
 * entries for this hugepage.
 */
void pmdp_splitting_flush(struct vm_area_struct *vma,
			  unsigned long address, pmd_t *pmdp)
{
	unsigned long old, tmp;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

#ifdef CONFIG_DEBUG_VM
	WARN_ON(!pmd_trans_huge(*pmdp));
	assert_spin_locked(&vma->vm_mm->page_table_lock);
#endif

#ifdef PTE_ATOMIC_UPDATES

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3\n\
		andi.	%1,%0,%6\n\
		bne-	1b \n\
		ori	%1,%0,%4 \n\
		stdcx.	%1,0,%3 \n\
		bne-	1b"
	: "=&r" (old), "=&r" (tmp), "=m" (*pmdp)
	: "r" (pmdp), "i" (_PAGE_SPLITTING), "m" (*pmdp), "i" (_PAGE_BUSY)
	: "cc" );
#else
	old = pmd_val(*pmdp);
	*pmdp = __pmd(old | _PAGE_SPLITTING);
#endif
	/*
	 * If we didn't had the splitting flag set, go and flush the
	 * HPTE entries.
	 */
	trace_hugepage_splitting(address, old);
	if (!(old & _PAGE_SPLITTING)) {
		/* We need to flush the hpte */
		if (old & _PAGE_HASHPTE)
			hpte_do_hugepage_flush(vma->vm_mm, address, pmdp, old);
	}
	/*
	 * This ensures that generic code that rely on IRQ disabling
	 * to prevent a parallel THP split work as expected.
	 */
	kick_all_cpus_sync();
}

/*
 * We want to put the pgtable in pmd and use pgtable for tracking
 * the base page size hptes
 */
void pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				pgtable_t pgtable)
{
	pgtable_t *pgtable_slot;
	assert_spin_locked(&mm->page_table_lock);
	/*
	 * we store the pgtable in the second half of PMD
	 */
	pgtable_slot = (pgtable_t *)pmdp + PTRS_PER_PMD;
	*pgtable_slot = pgtable;
	/*
	 * expose the deposited pgtable to other cpus.
	 * before we set the hugepage PTE at pmd level
	 * hash fault code looks at the deposted pgtable
	 * to store hash index values.
	 */
	smp_wmb();
}

pgtable_t pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp)
{
	pgtable_t pgtable;
	pgtable_t *pgtable_slot;

	assert_spin_locked(&mm->page_table_lock);
	pgtable_slot = (pgtable_t *)pmdp + PTRS_PER_PMD;
	pgtable = *pgtable_slot;
	/*
	 * Once we withdraw, mark the entry NULL.
	 */
	*pgtable_slot = NULL;
	/*
	 * We store HPTE information in the deposited PTE fragment.
	 * zero out the content on withdraw.
	 */
	memset(pgtable, 0, PTE_FRAG_SIZE);
	return pgtable;
}

/*
 * set a new huge pmd. We should not be called for updating
 * an existing pmd entry. That should go via pmd_hugepage_update.
 */
void set_pmd_at(struct mm_struct *mm, unsigned long addr,
		pmd_t *pmdp, pmd_t pmd)
{
#ifdef CONFIG_DEBUG_VM
	WARN_ON((pmd_val(*pmdp) & (_PAGE_PRESENT | _PAGE_USER)) ==
		(_PAGE_PRESENT | _PAGE_USER));
	assert_spin_locked(&mm->page_table_lock);
	WARN_ON(!pmd_trans_huge(pmd));
#endif
	trace_hugepage_set_pmd(addr, pmd_val(pmd));
	return set_pte_at(mm, addr, pmdp_ptep(pmdp), pmd_pte(pmd));
}

void pmdp_invalidate(struct vm_area_struct *vma, unsigned long address,
		     pmd_t *pmdp)
{
	pmd_hugepage_update(vma->vm_mm, address, pmdp, _PAGE_PRESENT, 0);
}

/*
 * A linux hugepage PMD was changed and the corresponding hash table entries
 * neesd to be flushed.
 */
void hpte_do_hugepage_flush(struct mm_struct *mm, unsigned long addr,
			    pmd_t *pmdp, unsigned long old_pmd)
{
	int ssize;
	unsigned int psize;
	unsigned long vsid;
	unsigned long flags = 0;
	const struct cpumask *tmp;

	/* get the base page size,vsid and segment size */
#ifdef CONFIG_DEBUG_VM
	psize = get_slice_psize(mm, addr);
	BUG_ON(psize == MMU_PAGE_16M);
#endif
	if (old_pmd & _PAGE_COMBO)
		psize = MMU_PAGE_4K;
	else
		psize = MMU_PAGE_64K;

	if (!is_kernel_addr(addr)) {
		ssize = user_segment_size(addr);
		vsid = get_vsid(mm->context.id, addr, ssize);
		WARN_ON(vsid == 0);
	} else {
		vsid = get_kernel_vsid(addr, mmu_kernel_ssize);
		ssize = mmu_kernel_ssize;
	}

	tmp = cpumask_of(smp_processor_id());
	if (cpumask_equal(mm_cpumask(mm), tmp))
		flags |= HPTE_LOCAL_UPDATE;

	return flush_hash_hugepage(vsid, addr, pmdp, psize, ssize, flags);
}

static pmd_t pmd_set_protbits(pmd_t pmd, pgprot_t pgprot)
{
	pmd_val(pmd) |= pgprot_val(pgprot);
	return pmd;
}

pmd_t pfn_pmd(unsigned long pfn, pgprot_t pgprot)
{
	pmd_t pmd;
	/*
	 * For a valid pte, we would have _PAGE_PRESENT always
	 * set. We use this to check THP page at pmd level.
	 * leaf pte for huge page, bottom two bits != 00
	 */
	pmd_val(pmd) = pfn << PTE_RPN_SHIFT;
	pmd_val(pmd) |= _PAGE_THP_HUGE;
	pmd = pmd_set_protbits(pmd, pgprot);
	return pmd;
}

pmd_t mk_pmd(struct page *page, pgprot_t pgprot)
{
	return pfn_pmd(page_to_pfn(page), pgprot);
}

pmd_t pmd_modify(pmd_t pmd, pgprot_t newprot)
{

	pmd_val(pmd) &= _HPAGE_CHG_MASK;
	pmd = pmd_set_protbits(pmd, newprot);
	return pmd;
}

/*
 * This is called at the end of handling a user page fault, when the
 * fault has been handled by updating a HUGE PMD entry in the linux page tables.
 * We use it to preload an HPTE into the hash table corresponding to
 * the updated linux HUGE PMD entry.
 */
void update_mmu_cache_pmd(struct vm_area_struct *vma, unsigned long addr,
			  pmd_t *pmd)
{
	return;
}

pmd_t pmdp_get_and_clear(struct mm_struct *mm,
			 unsigned long addr, pmd_t *pmdp)
{
	pmd_t old_pmd;
	pgtable_t pgtable;
	unsigned long old;
	pgtable_t *pgtable_slot;

	old = pmd_hugepage_update(mm, addr, pmdp, ~0UL, 0);
	old_pmd = __pmd(old);
	/*
	 * We have pmd == none and we are holding page_table_lock.
	 * So we can safely go and clear the pgtable hash
	 * index info.
	 */
	pgtable_slot = (pgtable_t *)pmdp + PTRS_PER_PMD;
	pgtable = *pgtable_slot;
	/*
	 * Let's zero out old valid and hash index details
	 * hash fault look at them.
	 */
	memset(pgtable, 0, PTE_FRAG_SIZE);
	/*
	 * Serialize against find_linux_pte_or_hugepte which does lock-less
	 * lookup in page tables with local interrupts disabled. For huge pages
	 * it casts pmd_t to pte_t. Since format of pte_t is different from
	 * pmd_t we want to prevent transit from pmd pointing to page table
	 * to pmd pointing to huge page (and back) while interrupts are disabled.
	 * We clear pmd to possibly replace it with page table pointer in
	 * different code paths. So make sure we wait for the parallel
	 * find_linux_pte_or_hugepage to finish.
	 */
	kick_all_cpus_sync();
	return old_pmd;
}

int has_transparent_hugepage(void)
{
	if (!mmu_has_feature(MMU_FTR_16M_PAGE))
		return 0;
	/*
	 * We support THP only if PMD_SIZE is 16MB.
	 */
	if (mmu_psize_defs[MMU_PAGE_16M].shift != PMD_SHIFT)
		return 0;
	/*
	 * We need to make sure that we support 16MB hugepage in a segement
	 * with base page size 64K or 4K. We only enable THP with a PAGE_SIZE
	 * of 64K.
	 */
	/*
	 * If we have 64K HPTE, we will be using that by default
	 */
	if (mmu_psize_defs[MMU_PAGE_64K].shift &&
	    (mmu_psize_defs[MMU_PAGE_64K].penc[MMU_PAGE_16M] == -1))
		return 0;
	/*
	 * Ok we only have 4K HPTE
	 */
	if (mmu_psize_defs[MMU_PAGE_4K].penc[MMU_PAGE_16M] == -1)
		return 0;

	return 1;
}
#endif /* CONFIG_TRANSPARENT_HUGEPAGE */
