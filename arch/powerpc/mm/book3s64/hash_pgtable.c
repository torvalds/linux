// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2005, Paul Mackerras, IBM Corporation.
 * Copyright 2009, Benjamin Herrenschmidt, IBM Corporation.
 * Copyright 2015-2016, Aneesh Kumar K.V, IBM Corporation.
 */

#include <linux/sched.h>
#include <linux/mm_types.h>
#include <linux/mm.h>

#include <asm/sections.h>
#include <asm/mmu.h>
#include <asm/tlb.h>

#include <mm/mmu_decl.h>

#define CREATE_TRACE_POINTS
#include <trace/events/thp.h>

#if H_PGTABLE_RANGE > (USER_VSID_RANGE * (TASK_SIZE_USER64 / TASK_CONTEXT_SIZE))
#warning Limited user VSID range means pagetable space is wasted
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * vmemmap is the starting address of the virtual address space where
 * struct pages are allocated for all possible PFNs present on the system
 * including holes and bad memory (hence sparse). These virtual struct
 * pages are stored in sequence in this virtual address space irrespective
 * of the fact whether the corresponding PFN is valid or not. This achieves
 * constant relationship between address of struct page and its PFN.
 *
 * During boot or memory hotplug operation when a new memory section is
 * added, physical memory allocation (including hash table bolting) will
 * be performed for the set of struct pages which are part of the memory
 * section. This saves memory by not allocating struct pages for PFNs
 * which are not valid.
 *
 *		----------------------------------------------
 *		| PHYSICAL ALLOCATION OF VIRTUAL STRUCT PAGES|
 *		----------------------------------------------
 *
 *	   f000000000000000                  c000000000000000
 * vmemmap +--------------+                  +--------------+
 *  +      |  page struct | +--------------> |  page struct |
 *  |      +--------------+                  +--------------+
 *  |      |  page struct | +--------------> |  page struct |
 *  |      +--------------+ |                +--------------+
 *  |      |  page struct | +       +------> |  page struct |
 *  |      +--------------+         |        +--------------+
 *  |      |  page struct |         |   +--> |  page struct |
 *  |      +--------------+         |   |    +--------------+
 *  |      |  page struct |         |   |
 *  |      +--------------+         |   |
 *  |      |  page struct |         |   |
 *  |      +--------------+         |   |
 *  |      |  page struct |         |   |
 *  |      +--------------+         |   |
 *  |      |  page struct |         |   |
 *  |      +--------------+         |   |
 *  |      |  page struct | +-------+   |
 *  |      +--------------+             |
 *  |      |  page struct | +-----------+
 *  |      +--------------+
 *  |      |  page struct | No mapping
 *  |      +--------------+
 *  |      |  page struct | No mapping
 *  v      +--------------+
 *
 *		-----------------------------------------
 *		| RELATION BETWEEN STRUCT PAGES AND PFNS|
 *		-----------------------------------------
 *
 * vmemmap +--------------+                 +---------------+
 *  +      |  page struct | +-------------> |      PFN      |
 *  |      +--------------+                 +---------------+
 *  |      |  page struct | +-------------> |      PFN      |
 *  |      +--------------+                 +---------------+
 *  |      |  page struct | +-------------> |      PFN      |
 *  |      +--------------+                 +---------------+
 *  |      |  page struct | +-------------> |      PFN      |
 *  |      +--------------+                 +---------------+
 *  |      |              |
 *  |      +--------------+
 *  |      |              |
 *  |      +--------------+
 *  |      |              |
 *  |      +--------------+                 +---------------+
 *  |      |  page struct | +-------------> |      PFN      |
 *  |      +--------------+                 +---------------+
 *  |      |              |
 *  |      +--------------+
 *  |      |              |
 *  |      +--------------+                 +---------------+
 *  |      |  page struct | +-------------> |      PFN      |
 *  |      +--------------+                 +---------------+
 *  |      |  page struct | +-------------> |      PFN      |
 *  v      +--------------+                 +---------------+
 */
/*
 * On hash-based CPUs, the vmemmap is bolted in the hash table.
 *
 */
int __meminit hash__vmemmap_create_mapping(unsigned long start,
				       unsigned long page_size,
				       unsigned long phys)
{
	int rc;

	if ((start + page_size) >= H_VMEMMAP_END) {
		pr_warn("Outside the supported range\n");
		return -1;
	}

	rc = htab_bolt_mapping(start, start + page_size, phys,
			       pgprot_val(PAGE_KERNEL),
			       mmu_vmemmap_psize, mmu_kernel_ssize);
	if (rc < 0) {
		int rc2 = htab_remove_mapping(start, start + page_size,
					      mmu_vmemmap_psize,
					      mmu_kernel_ssize);
		BUG_ON(rc2 && (rc2 != -ENOENT));
	}
	return rc;
}

#ifdef CONFIG_MEMORY_HOTPLUG
void hash__vmemmap_remove_mapping(unsigned long start,
			      unsigned long page_size)
{
	int rc = htab_remove_mapping(start, start + page_size,
				     mmu_vmemmap_psize,
				     mmu_kernel_ssize);
	BUG_ON((rc < 0) && (rc != -ENOENT));
	WARN_ON(rc == -ENOENT);
}
#endif
#endif /* CONFIG_SPARSEMEM_VMEMMAP */

/*
 * map_kernel_page currently only called by __ioremap
 * map_kernel_page adds an entry to the ioremap page table
 * and adds an entry to the HPT, possibly bolting it
 */
int hash__map_kernel_page(unsigned long ea, unsigned long pa, pgprot_t prot)
{
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	BUILD_BUG_ON(TASK_SIZE_USER64 > H_PGTABLE_RANGE);
	if (slab_is_available()) {
		pgdp = pgd_offset_k(ea);
		p4dp = p4d_offset(pgdp, ea);
		pudp = pud_alloc(&init_mm, p4dp, ea);
		if (!pudp)
			return -ENOMEM;
		pmdp = pmd_alloc(&init_mm, pudp, ea);
		if (!pmdp)
			return -ENOMEM;
		ptep = pte_alloc_kernel(pmdp, ea);
		if (!ptep)
			return -ENOMEM;
		set_pte_at(&init_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT, prot));
	} else {
		/*
		 * If the mm subsystem is not fully up, we cannot create a
		 * linux page table entry for this mapping.  Simply bolt an
		 * entry in the hardware page table.
		 *
		 */
		if (htab_bolt_mapping(ea, ea + PAGE_SIZE, pa, pgprot_val(prot),
				      mmu_io_psize, mmu_kernel_ssize)) {
			printk(KERN_ERR "Failed to do bolted mapping IO "
			       "memory at %016lx !\n", pa);
			return -ENOMEM;
		}
	}

	smp_wmb();
	return 0;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE

unsigned long hash__pmd_hugepage_update(struct mm_struct *mm, unsigned long addr,
				    pmd_t *pmdp, unsigned long clr,
				    unsigned long set)
{
	__be64 old_be, tmp;
	unsigned long old;

#ifdef CONFIG_DEBUG_VM
	WARN_ON(!hash__pmd_trans_huge(*pmdp) && !pmd_devmap(*pmdp));
	assert_spin_locked(pmd_lockptr(mm, pmdp));
#endif

	__asm__ __volatile__(
	"1:	ldarx	%0,0,%3\n\
		and.	%1,%0,%6\n\
		bne-	1b \n\
		andc	%1,%0,%4 \n\
		or	%1,%1,%7\n\
		stdcx.	%1,0,%3 \n\
		bne-	1b"
	: "=&r" (old_be), "=&r" (tmp), "=m" (*pmdp)
	: "r" (pmdp), "r" (cpu_to_be64(clr)), "m" (*pmdp),
	  "r" (cpu_to_be64(H_PAGE_BUSY)), "r" (cpu_to_be64(set))
	: "cc" );

	old = be64_to_cpu(old_be);

	trace_hugepage_update(addr, old, clr, set);
	if (old & H_PAGE_HASHPTE)
		hpte_do_hugepage_flush(mm, addr, pmdp, old);
	return old;
}

pmd_t hash__pmdp_collapse_flush(struct vm_area_struct *vma, unsigned long address,
			    pmd_t *pmdp)
{
	pmd_t pmd;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);
	VM_BUG_ON(pmd_trans_huge(*pmdp));
	VM_BUG_ON(pmd_devmap(*pmdp));

	pmd = *pmdp;
	pmd_clear(pmdp);
	/*
	 * Wait for all pending hash_page to finish. This is needed
	 * in case of subpage collapse. When we collapse normal pages
	 * to hugepage, we first clear the pmd, then invalidate all
	 * the PTE entries. The assumption here is that any low level
	 * page fault will see a none pmd and take the slow path that
	 * will wait on mmap_lock. But we could very well be in a
	 * hash_page with local ptep pointer value. Such a hash page
	 * can result in adding new HPTE entries for normal subpages.
	 * That means we could be modifying the page content as we
	 * copy them to a huge page. So wait for parallel hash_page
	 * to finish before invalidating HPTE entries. We can do this
	 * by sending an IPI to all the cpus and executing a dummy
	 * function there.
	 */
	serialize_against_pte_lookup(vma->vm_mm);
	/*
	 * Now invalidate the hpte entries in the range
	 * covered by pmd. This make sure we take a
	 * fault and will find the pmd as none, which will
	 * result in a major fault which takes mmap_lock and
	 * hence wait for collapse to complete. Without this
	 * the __collapse_huge_page_copy can result in copying
	 * the old content.
	 */
	flush_tlb_pmd_range(vma->vm_mm, &pmd, address);
	return pmd;
}

/*
 * We want to put the pgtable in pmd and use pgtable for tracking
 * the base page size hptes
 */
void hash__pgtable_trans_huge_deposit(struct mm_struct *mm, pmd_t *pmdp,
				  pgtable_t pgtable)
{
	pgtable_t *pgtable_slot;

	assert_spin_locked(pmd_lockptr(mm, pmdp));
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

pgtable_t hash__pgtable_trans_huge_withdraw(struct mm_struct *mm, pmd_t *pmdp)
{
	pgtable_t pgtable;
	pgtable_t *pgtable_slot;

	assert_spin_locked(pmd_lockptr(mm, pmdp));

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

	/* get the base page size,vsid and segment size */
#ifdef CONFIG_DEBUG_VM
	psize = get_slice_psize(mm, addr);
	BUG_ON(psize == MMU_PAGE_16M);
#endif
	if (old_pmd & H_PAGE_COMBO)
		psize = MMU_PAGE_4K;
	else
		psize = MMU_PAGE_64K;

	if (!is_kernel_addr(addr)) {
		ssize = user_segment_size(addr);
		vsid = get_user_vsid(&mm->context, addr, ssize);
		WARN_ON(vsid == 0);
	} else {
		vsid = get_kernel_vsid(addr, mmu_kernel_ssize);
		ssize = mmu_kernel_ssize;
	}

	if (mm_is_thread_local(mm))
		flags |= HPTE_LOCAL_UPDATE;

	return flush_hash_hugepage(vsid, addr, pmdp, psize, ssize, flags);
}

pmd_t hash__pmdp_huge_get_and_clear(struct mm_struct *mm,
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
	return old_pmd;
}

int hash__has_transparent_hugepage(void)
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
EXPORT_SYMBOL_GPL(hash__has_transparent_hugepage);

#endif /* CONFIG_TRANSPARENT_HUGEPAGE */

#ifdef CONFIG_STRICT_KERNEL_RWX
static bool hash__change_memory_range(unsigned long start, unsigned long end,
				      unsigned long newpp)
{
	unsigned long idx;
	unsigned int step, shift;

	shift = mmu_psize_defs[mmu_linear_psize].shift;
	step = 1 << shift;

	start = ALIGN_DOWN(start, step);
	end = ALIGN(end, step); // aligns up

	if (start >= end)
		return false;

	pr_debug("Changing page protection on range 0x%lx-0x%lx, to 0x%lx, step 0x%x\n",
		 start, end, newpp, step);

	for (idx = start; idx < end; idx += step)
		/* Not sure if we can do much with the return value */
		mmu_hash_ops.hpte_updateboltedpp(newpp, idx, mmu_linear_psize,
							mmu_kernel_ssize);

	return true;
}

void hash__mark_rodata_ro(void)
{
	unsigned long start, end;

	start = (unsigned long)_stext;
	end = (unsigned long)__init_begin;

	WARN_ON(!hash__change_memory_range(start, end, PP_RXXX));
}

void hash__mark_initmem_nx(void)
{
	unsigned long start, end, pp;

	start = (unsigned long)__init_begin;
	end = (unsigned long)__init_end;

	pp = htab_convert_pte_flags(pgprot_val(PAGE_KERNEL));

	WARN_ON(!hash__change_memory_range(start, end, pp));
}
#endif
