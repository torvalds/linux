// SPDX-License-Identifier: GPL-2.0
#include <linux/mm.h>
#include <linux/gfp.h>
#include <linux/hugetlb.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/fixmap.h>
#include <asm/mtrr.h>

#ifdef CONFIG_DYNAMIC_PHYSICAL_MASK
phys_addr_t physical_mask __ro_after_init = (1ULL << __PHYSICAL_MASK_SHIFT) - 1;
EXPORT_SYMBOL(physical_mask);
#endif

#ifdef CONFIG_HIGHPTE
#define PGTABLE_HIGHMEM __GFP_HIGHMEM
#else
#define PGTABLE_HIGHMEM 0
#endif

gfp_t __userpte_alloc_gfp = GFP_PGTABLE_USER | PGTABLE_HIGHMEM;

pgtable_t pte_alloc_one(struct mm_struct *mm)
{
	return __pte_alloc_one(mm, __userpte_alloc_gfp);
}

static int __init setup_userpte(char *arg)
{
	if (!arg)
		return -EINVAL;

	/*
	 * "userpte=nohigh" disables allocation of user pagetables in
	 * high memory.
	 */
	if (strcmp(arg, "nohigh") == 0)
		__userpte_alloc_gfp &= ~__GFP_HIGHMEM;
	else
		return -EINVAL;
	return 0;
}
early_param("userpte", setup_userpte);

void ___pte_free_tlb(struct mmu_gather *tlb, struct page *pte)
{
	paravirt_release_pte(page_to_pfn(pte));
	tlb_remove_table(tlb, page_ptdesc(pte));
}

#if CONFIG_PGTABLE_LEVELS > 2
void ___pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd)
{
	paravirt_release_pmd(__pa(pmd) >> PAGE_SHIFT);
	/*
	 * NOTE! For PAE, any changes to the top page-directory-pointer-table
	 * entries need a full cr3 reload to flush.
	 */
#ifdef CONFIG_X86_PAE
	tlb->need_flush_all = 1;
#endif
	tlb_remove_table(tlb, virt_to_ptdesc(pmd));
}

#if CONFIG_PGTABLE_LEVELS > 3
void ___pud_free_tlb(struct mmu_gather *tlb, pud_t *pud)
{
	paravirt_release_pud(__pa(pud) >> PAGE_SHIFT);
	tlb_remove_table(tlb, virt_to_ptdesc(pud));
}

#if CONFIG_PGTABLE_LEVELS > 4
void ___p4d_free_tlb(struct mmu_gather *tlb, p4d_t *p4d)
{
	paravirt_release_p4d(__pa(p4d) >> PAGE_SHIFT);
	tlb_remove_table(tlb, virt_to_ptdesc(p4d));
}
#endif	/* CONFIG_PGTABLE_LEVELS > 4 */
#endif	/* CONFIG_PGTABLE_LEVELS > 3 */
#endif	/* CONFIG_PGTABLE_LEVELS > 2 */

static inline void pgd_list_add(pgd_t *pgd)
{
	struct ptdesc *ptdesc = virt_to_ptdesc(pgd);

	list_add(&ptdesc->pt_list, &pgd_list);
}

static inline void pgd_list_del(pgd_t *pgd)
{
	struct ptdesc *ptdesc = virt_to_ptdesc(pgd);

	list_del(&ptdesc->pt_list);
}

#define UNSHARED_PTRS_PER_PGD				\
	(SHARED_KERNEL_PMD ? KERNEL_PGD_BOUNDARY : PTRS_PER_PGD)
#define MAX_UNSHARED_PTRS_PER_PGD			\
	MAX_T(size_t, KERNEL_PGD_BOUNDARY, PTRS_PER_PGD)


static void pgd_set_mm(pgd_t *pgd, struct mm_struct *mm)
{
	virt_to_ptdesc(pgd)->pt_mm = mm;
}

struct mm_struct *pgd_page_get_mm(struct page *page)
{
	return page_ptdesc(page)->pt_mm;
}

static void pgd_ctor(struct mm_struct *mm, pgd_t *pgd)
{
	/* If the pgd points to a shared pagetable level (either the
	   ptes in non-PAE, or shared PMD in PAE), then just copy the
	   references from swapper_pg_dir. */
	if (CONFIG_PGTABLE_LEVELS == 2 ||
	    (CONFIG_PGTABLE_LEVELS == 3 && SHARED_KERNEL_PMD) ||
	    CONFIG_PGTABLE_LEVELS >= 4) {
		clone_pgd_range(pgd + KERNEL_PGD_BOUNDARY,
				swapper_pg_dir + KERNEL_PGD_BOUNDARY,
				KERNEL_PGD_PTRS);
	}

	/* list required to sync kernel mapping updates */
	if (!SHARED_KERNEL_PMD) {
		pgd_set_mm(pgd, mm);
		pgd_list_add(pgd);
	}
}

static void pgd_dtor(pgd_t *pgd)
{
	if (SHARED_KERNEL_PMD)
		return;

	spin_lock(&pgd_lock);
	pgd_list_del(pgd);
	spin_unlock(&pgd_lock);
}

/*
 * List of all pgd's needed for non-PAE so it can invalidate entries
 * in both cached and uncached pgd's; not needed for PAE since the
 * kernel pmd is shared. If PAE were not to share the pmd a similar
 * tactic would be needed. This is essentially codepath-based locking
 * against pageattr.c; it is the unique case in which a valid change
 * of kernel pagetables can't be lazily synchronized by vmalloc faults.
 * vmalloc faults work because attached pagetables are never freed.
 * -- nyc
 */

#ifdef CONFIG_X86_PAE
/*
 * In PAE mode, we need to do a cr3 reload (=tlb flush) when
 * updating the top-level pagetable entries to guarantee the
 * processor notices the update.  Since this is expensive, and
 * all 4 top-level entries are used almost immediately in a
 * new process's life, we just pre-populate them here.
 *
 * Also, if we're in a paravirt environment where the kernel pmd is
 * not shared between pagetables (!SHARED_KERNEL_PMDS), we allocate
 * and initialize the kernel pmds here.
 */
#define PREALLOCATED_PMDS	UNSHARED_PTRS_PER_PGD
#define MAX_PREALLOCATED_PMDS	MAX_UNSHARED_PTRS_PER_PGD

/*
 * We allocate separate PMDs for the kernel part of the user page-table
 * when PTI is enabled. We need them to map the per-process LDT into the
 * user-space page-table.
 */
#define PREALLOCATED_USER_PMDS	 (boot_cpu_has(X86_FEATURE_PTI) ? \
					KERNEL_PGD_PTRS : 0)
#define MAX_PREALLOCATED_USER_PMDS KERNEL_PGD_PTRS

void pud_populate(struct mm_struct *mm, pud_t *pudp, pmd_t *pmd)
{
	paravirt_alloc_pmd(mm, __pa(pmd) >> PAGE_SHIFT);

	/* Note: almost everything apart from _PAGE_PRESENT is
	   reserved at the pmd (PDPT) level. */
	set_pud(pudp, __pud(__pa(pmd) | _PAGE_PRESENT));

	/*
	 * According to Intel App note "TLBs, Paging-Structure Caches,
	 * and Their Invalidation", April 2007, document 317080-001,
	 * section 8.1: in PAE mode we explicitly have to flush the
	 * TLB via cr3 if the top-level pgd is changed...
	 */
	flush_tlb_mm(mm);
}
#else  /* !CONFIG_X86_PAE */

/* No need to prepopulate any pagetable entries in non-PAE modes. */
#define PREALLOCATED_PMDS	0
#define MAX_PREALLOCATED_PMDS	0
#define PREALLOCATED_USER_PMDS	 0
#define MAX_PREALLOCATED_USER_PMDS 0
#endif	/* CONFIG_X86_PAE */

static void free_pmds(struct mm_struct *mm, pmd_t *pmds[], int count)
{
	int i;
	struct ptdesc *ptdesc;

	for (i = 0; i < count; i++)
		if (pmds[i]) {
			ptdesc = virt_to_ptdesc(pmds[i]);

			pagetable_dtor(ptdesc);
			pagetable_free(ptdesc);
			mm_dec_nr_pmds(mm);
		}
}

static int preallocate_pmds(struct mm_struct *mm, pmd_t *pmds[], int count)
{
	int i;
	bool failed = false;
	gfp_t gfp = GFP_PGTABLE_USER;

	if (mm == &init_mm)
		gfp &= ~__GFP_ACCOUNT;
	gfp &= ~__GFP_HIGHMEM;

	for (i = 0; i < count; i++) {
		pmd_t *pmd = NULL;
		struct ptdesc *ptdesc = pagetable_alloc(gfp, 0);

		if (!ptdesc)
			failed = true;
		if (ptdesc && !pagetable_pmd_ctor(ptdesc)) {
			pagetable_free(ptdesc);
			ptdesc = NULL;
			failed = true;
		}
		if (ptdesc) {
			mm_inc_nr_pmds(mm);
			pmd = ptdesc_address(ptdesc);
		}

		pmds[i] = pmd;
	}

	if (failed) {
		free_pmds(mm, pmds, count);
		return -ENOMEM;
	}

	return 0;
}

/*
 * Mop up any pmd pages which may still be attached to the pgd.
 * Normally they will be freed by munmap/exit_mmap, but any pmd we
 * preallocate which never got a corresponding vma will need to be
 * freed manually.
 */
static void mop_up_one_pmd(struct mm_struct *mm, pgd_t *pgdp)
{
	pgd_t pgd = *pgdp;

	if (pgd_val(pgd) != 0) {
		pmd_t *pmd = (pmd_t *)pgd_page_vaddr(pgd);

		pgd_clear(pgdp);

		paravirt_release_pmd(pgd_val(pgd) >> PAGE_SHIFT);
		pmd_free(mm, pmd);
		mm_dec_nr_pmds(mm);
	}
}

static void pgd_mop_up_pmds(struct mm_struct *mm, pgd_t *pgdp)
{
	int i;

	for (i = 0; i < PREALLOCATED_PMDS; i++)
		mop_up_one_pmd(mm, &pgdp[i]);

#ifdef CONFIG_MITIGATION_PAGE_TABLE_ISOLATION

	if (!boot_cpu_has(X86_FEATURE_PTI))
		return;

	pgdp = kernel_to_user_pgdp(pgdp);

	for (i = 0; i < PREALLOCATED_USER_PMDS; i++)
		mop_up_one_pmd(mm, &pgdp[i + KERNEL_PGD_BOUNDARY]);
#endif
}

static void pgd_prepopulate_pmd(struct mm_struct *mm, pgd_t *pgd, pmd_t *pmds[])
{
	p4d_t *p4d;
	pud_t *pud;
	int i;

	p4d = p4d_offset(pgd, 0);
	pud = pud_offset(p4d, 0);

	for (i = 0; i < PREALLOCATED_PMDS; i++, pud++) {
		pmd_t *pmd = pmds[i];

		if (i >= KERNEL_PGD_BOUNDARY)
			memcpy(pmd, (pmd_t *)pgd_page_vaddr(swapper_pg_dir[i]),
			       sizeof(pmd_t) * PTRS_PER_PMD);

		pud_populate(mm, pud, pmd);
	}
}

#ifdef CONFIG_MITIGATION_PAGE_TABLE_ISOLATION
static void pgd_prepopulate_user_pmd(struct mm_struct *mm,
				     pgd_t *k_pgd, pmd_t *pmds[])
{
	pgd_t *s_pgd = kernel_to_user_pgdp(swapper_pg_dir);
	pgd_t *u_pgd = kernel_to_user_pgdp(k_pgd);
	p4d_t *u_p4d;
	pud_t *u_pud;
	int i;

	u_p4d = p4d_offset(u_pgd, 0);
	u_pud = pud_offset(u_p4d, 0);

	s_pgd += KERNEL_PGD_BOUNDARY;
	u_pud += KERNEL_PGD_BOUNDARY;

	for (i = 0; i < PREALLOCATED_USER_PMDS; i++, u_pud++, s_pgd++) {
		pmd_t *pmd = pmds[i];

		memcpy(pmd, (pmd_t *)pgd_page_vaddr(*s_pgd),
		       sizeof(pmd_t) * PTRS_PER_PMD);

		pud_populate(mm, u_pud, pmd);
	}

}
#else
static void pgd_prepopulate_user_pmd(struct mm_struct *mm,
				     pgd_t *k_pgd, pmd_t *pmds[])
{
}
#endif
/*
 * Xen paravirt assumes pgd table should be in one page. 64 bit kernel also
 * assumes that pgd should be in one page.
 *
 * But kernel with PAE paging that is not running as a Xen domain
 * only needs to allocate 32 bytes for pgd instead of one page.
 */
#ifdef CONFIG_X86_PAE

#include <linux/slab.h>

#define PGD_SIZE	(PTRS_PER_PGD * sizeof(pgd_t))
#define PGD_ALIGN	32

static struct kmem_cache *pgd_cache;

void __init pgtable_cache_init(void)
{
	/*
	 * When PAE kernel is running as a Xen domain, it does not use
	 * shared kernel pmd. And this requires a whole page for pgd.
	 */
	if (!SHARED_KERNEL_PMD)
		return;

	/*
	 * when PAE kernel is not running as a Xen domain, it uses
	 * shared kernel pmd. Shared kernel pmd does not require a whole
	 * page for pgd. We are able to just allocate a 32-byte for pgd.
	 * During boot time, we create a 32-byte slab for pgd table allocation.
	 */
	pgd_cache = kmem_cache_create("pgd_cache", PGD_SIZE, PGD_ALIGN,
				      SLAB_PANIC, NULL);
}

static inline pgd_t *_pgd_alloc(struct mm_struct *mm)
{
	/*
	 * If no SHARED_KERNEL_PMD, PAE kernel is running as a Xen domain.
	 * We allocate one page for pgd.
	 */
	if (!SHARED_KERNEL_PMD)
		return __pgd_alloc(mm, PGD_ALLOCATION_ORDER);

	/*
	 * Now PAE kernel is not running as a Xen domain. We can allocate
	 * a 32-byte slab for pgd to save memory space.
	 */
	return kmem_cache_alloc(pgd_cache, GFP_PGTABLE_USER);
}

static inline void _pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	if (!SHARED_KERNEL_PMD)
		__pgd_free(mm, pgd);
	else
		kmem_cache_free(pgd_cache, pgd);
}
#else

static inline pgd_t *_pgd_alloc(struct mm_struct *mm)
{
	return __pgd_alloc(mm, PGD_ALLOCATION_ORDER);
}

static inline void _pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	__pgd_free(mm, pgd);
}
#endif /* CONFIG_X86_PAE */

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;
	pmd_t *u_pmds[MAX_PREALLOCATED_USER_PMDS];
	pmd_t *pmds[MAX_PREALLOCATED_PMDS];

	pgd = _pgd_alloc(mm);

	if (pgd == NULL)
		goto out;

	mm->pgd = pgd;

	if (sizeof(pmds) != 0 &&
			preallocate_pmds(mm, pmds, PREALLOCATED_PMDS) != 0)
		goto out_free_pgd;

	if (sizeof(u_pmds) != 0 &&
			preallocate_pmds(mm, u_pmds, PREALLOCATED_USER_PMDS) != 0)
		goto out_free_pmds;

	if (paravirt_pgd_alloc(mm) != 0)
		goto out_free_user_pmds;

	/*
	 * Make sure that pre-populating the pmds is atomic with
	 * respect to anything walking the pgd_list, so that they
	 * never see a partially populated pgd.
	 */
	spin_lock(&pgd_lock);

	pgd_ctor(mm, pgd);
	if (sizeof(pmds) != 0)
		pgd_prepopulate_pmd(mm, pgd, pmds);

	if (sizeof(u_pmds) != 0)
		pgd_prepopulate_user_pmd(mm, pgd, u_pmds);

	spin_unlock(&pgd_lock);

	return pgd;

out_free_user_pmds:
	if (sizeof(u_pmds) != 0)
		free_pmds(mm, u_pmds, PREALLOCATED_USER_PMDS);
out_free_pmds:
	if (sizeof(pmds) != 0)
		free_pmds(mm, pmds, PREALLOCATED_PMDS);
out_free_pgd:
	_pgd_free(mm, pgd);
out:
	return NULL;
}

void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	pgd_mop_up_pmds(mm, pgd);
	pgd_dtor(pgd);
	paravirt_pgd_free(mm, pgd);
	_pgd_free(mm, pgd);
}

/*
 * Used to set accessed or dirty bits in the page table entries
 * on other architectures. On x86, the accessed and dirty bits
 * are tracked by hardware. However, do_wp_page calls this function
 * to also make the pte writeable at the same time the dirty bit is
 * set. In that case we do actually need to write the PTE.
 */
int ptep_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pte_t *ptep,
			  pte_t entry, int dirty)
{
	int changed = !pte_same(*ptep, entry);

	if (changed && dirty)
		set_pte(ptep, entry);

	return changed;
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
int pmdp_set_access_flags(struct vm_area_struct *vma,
			  unsigned long address, pmd_t *pmdp,
			  pmd_t entry, int dirty)
{
	int changed = !pmd_same(*pmdp, entry);

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	if (changed && dirty) {
		set_pmd(pmdp, entry);
		/*
		 * We had a write-protection fault here and changed the pmd
		 * to to more permissive. No need to flush the TLB for that,
		 * #PF is architecturally guaranteed to do that and in the
		 * worst-case we'll generate a spurious fault.
		 */
	}

	return changed;
}

int pudp_set_access_flags(struct vm_area_struct *vma, unsigned long address,
			  pud_t *pudp, pud_t entry, int dirty)
{
	int changed = !pud_same(*pudp, entry);

	VM_BUG_ON(address & ~HPAGE_PUD_MASK);

	if (changed && dirty) {
		set_pud(pudp, entry);
		/*
		 * We had a write-protection fault here and changed the pud
		 * to to more permissive. No need to flush the TLB for that,
		 * #PF is architecturally guaranteed to do that and in the
		 * worst-case we'll generate a spurious fault.
		 */
	}

	return changed;
}
#endif

int ptep_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long addr, pte_t *ptep)
{
	int ret = 0;

	if (pte_young(*ptep))
		ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,
					 (unsigned long *) &ptep->pte);

	return ret;
}

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) || defined(CONFIG_ARCH_HAS_NONLEAF_PMD_YOUNG)
int pmdp_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long addr, pmd_t *pmdp)
{
	int ret = 0;

	if (pmd_young(*pmdp))
		ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,
					 (unsigned long *)pmdp);

	return ret;
}
#endif

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
int pudp_test_and_clear_young(struct vm_area_struct *vma,
			      unsigned long addr, pud_t *pudp)
{
	int ret = 0;

	if (pud_young(*pudp))
		ret = test_and_clear_bit(_PAGE_BIT_ACCESSED,
					 (unsigned long *)pudp);

	return ret;
}
#endif

int ptep_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pte_t *ptep)
{
	/*
	 * On x86 CPUs, clearing the accessed bit without a TLB flush
	 * doesn't cause data corruption. [ It could cause incorrect
	 * page aging and the (mistaken) reclaim of hot pages, but the
	 * chance of that should be relatively low. ]
	 *
	 * So as a performance optimization don't flush the TLB when
	 * clearing the accessed bit, it will eventually be flushed by
	 * a context switch or a VM operation anyway. [ In the rare
	 * event of it not getting flushed for a long time the delay
	 * shouldn't really matter because there's no real memory
	 * pressure for swapout to react to. ]
	 */
	return ptep_test_and_clear_young(vma, address, ptep);
}

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
int pmdp_clear_flush_young(struct vm_area_struct *vma,
			   unsigned long address, pmd_t *pmdp)
{
	int young;

	VM_BUG_ON(address & ~HPAGE_PMD_MASK);

	young = pmdp_test_and_clear_young(vma, address, pmdp);
	if (young)
		flush_tlb_range(vma, address, address + HPAGE_PMD_SIZE);

	return young;
}

pmd_t pmdp_invalidate_ad(struct vm_area_struct *vma, unsigned long address,
			 pmd_t *pmdp)
{
	VM_WARN_ON_ONCE(!pmd_present(*pmdp));

	/*
	 * No flush is necessary. Once an invalid PTE is established, the PTE's
	 * access and dirty bits cannot be updated.
	 */
	return pmdp_establish(vma, address, pmdp, pmd_mkinvalid(*pmdp));
}
#endif

#if defined(CONFIG_TRANSPARENT_HUGEPAGE) && \
	defined(CONFIG_HAVE_ARCH_TRANSPARENT_HUGEPAGE_PUD)
pud_t pudp_invalidate(struct vm_area_struct *vma, unsigned long address,
		     pud_t *pudp)
{
	VM_WARN_ON_ONCE(!pud_present(*pudp));
	pud_t old = pudp_establish(vma, address, pudp, pud_mkinvalid(*pudp));
	flush_pud_tlb_range(vma, address, address + HPAGE_PUD_SIZE);
	return old;
}
#endif

/**
 * reserve_top_address - reserves a hole in the top of kernel address space
 * @reserve - size of hole to reserve
 *
 * Can be used to relocate the fixmap area and poke a hole in the top
 * of kernel address space to make room for a hypervisor.
 */
void __init reserve_top_address(unsigned long reserve)
{
#ifdef CONFIG_X86_32
	BUG_ON(fixmaps_set > 0);
	__FIXADDR_TOP = round_down(-reserve, 1 << PMD_SHIFT) - PAGE_SIZE;
	printk(KERN_INFO "Reserving virtual address space above 0x%08lx (rounded to 0x%08lx)\n",
	       -reserve, __FIXADDR_TOP + PAGE_SIZE);
#endif
}

int fixmaps_set;

void __native_set_fixmap(enum fixed_addresses idx, pte_t pte)
{
	unsigned long address = __fix_to_virt(idx);

#ifdef CONFIG_X86_64
       /*
	* Ensure that the static initial page tables are covering the
	* fixmap completely.
	*/
	BUILD_BUG_ON(__end_of_permanent_fixed_addresses >
		     (FIXMAP_PMD_NUM * PTRS_PER_PTE));
#endif

	if (idx >= __end_of_fixed_addresses) {
		BUG();
		return;
	}
	set_pte_vaddr(address, pte);
	fixmaps_set++;
}

void native_set_fixmap(unsigned /* enum fixed_addresses */ idx,
		       phys_addr_t phys, pgprot_t flags)
{
	/* Sanitize 'prot' against any unsupported bits: */
	pgprot_val(flags) &= __default_kernel_pte_mask;

	__native_set_fixmap(idx, pfn_pte(phys >> PAGE_SHIFT, flags));
}

#ifdef CONFIG_HAVE_ARCH_HUGE_VMAP
#ifdef CONFIG_X86_5LEVEL
/**
 * p4d_set_huge - setup kernel P4D mapping
 *
 * No 512GB pages yet -- always return 0
 */
int p4d_set_huge(p4d_t *p4d, phys_addr_t addr, pgprot_t prot)
{
	return 0;
}

/**
 * p4d_clear_huge - clear kernel P4D mapping when it is set
 *
 * No 512GB pages yet -- always return 0
 */
void p4d_clear_huge(p4d_t *p4d)
{
}
#endif

/**
 * pud_set_huge - setup kernel PUD mapping
 *
 * MTRRs can override PAT memory types with 4KiB granularity. Therefore, this
 * function sets up a huge page only if the complete range has the same MTRR
 * caching mode.
 *
 * Callers should try to decrease page size (1GB -> 2MB -> 4K) if the bigger
 * page mapping attempt fails.
 *
 * Returns 1 on success and 0 on failure.
 */
int pud_set_huge(pud_t *pud, phys_addr_t addr, pgprot_t prot)
{
	u8 uniform;

	mtrr_type_lookup(addr, addr + PUD_SIZE, &uniform);
	if (!uniform)
		return 0;

	/* Bail out if we are we on a populated non-leaf entry: */
	if (pud_present(*pud) && !pud_leaf(*pud))
		return 0;

	set_pte((pte_t *)pud, pfn_pte(
		(u64)addr >> PAGE_SHIFT,
		__pgprot(protval_4k_2_large(pgprot_val(prot)) | _PAGE_PSE)));

	return 1;
}

/**
 * pmd_set_huge - setup kernel PMD mapping
 *
 * See text over pud_set_huge() above.
 *
 * Returns 1 on success and 0 on failure.
 */
int pmd_set_huge(pmd_t *pmd, phys_addr_t addr, pgprot_t prot)
{
	u8 uniform;

	mtrr_type_lookup(addr, addr + PMD_SIZE, &uniform);
	if (!uniform) {
		pr_warn_once("%s: Cannot satisfy [mem %#010llx-%#010llx] with a huge-page mapping due to MTRR override.\n",
			     __func__, addr, addr + PMD_SIZE);
		return 0;
	}

	/* Bail out if we are we on a populated non-leaf entry: */
	if (pmd_present(*pmd) && !pmd_leaf(*pmd))
		return 0;

	set_pte((pte_t *)pmd, pfn_pte(
		(u64)addr >> PAGE_SHIFT,
		__pgprot(protval_4k_2_large(pgprot_val(prot)) | _PAGE_PSE)));

	return 1;
}

/**
 * pud_clear_huge - clear kernel PUD mapping when it is set
 *
 * Returns 1 on success and 0 on failure (no PUD map is found).
 */
int pud_clear_huge(pud_t *pud)
{
	if (pud_leaf(*pud)) {
		pud_clear(pud);
		return 1;
	}

	return 0;
}

/**
 * pmd_clear_huge - clear kernel PMD mapping when it is set
 *
 * Returns 1 on success and 0 on failure (no PMD map is found).
 */
int pmd_clear_huge(pmd_t *pmd)
{
	if (pmd_leaf(*pmd)) {
		pmd_clear(pmd);
		return 1;
	}

	return 0;
}

#ifdef CONFIG_X86_64
/**
 * pud_free_pmd_page - Clear pud entry and free pmd page.
 * @pud: Pointer to a PUD.
 * @addr: Virtual address associated with pud.
 *
 * Context: The pud range has been unmapped and TLB purged.
 * Return: 1 if clearing the entry succeeded. 0 otherwise.
 *
 * NOTE: Callers must allow a single page allocation.
 */
int pud_free_pmd_page(pud_t *pud, unsigned long addr)
{
	pmd_t *pmd, *pmd_sv;
	pte_t *pte;
	int i;

	pmd = pud_pgtable(*pud);
	pmd_sv = (pmd_t *)__get_free_page(GFP_KERNEL);
	if (!pmd_sv)
		return 0;

	for (i = 0; i < PTRS_PER_PMD; i++) {
		pmd_sv[i] = pmd[i];
		if (!pmd_none(pmd[i]))
			pmd_clear(&pmd[i]);
	}

	pud_clear(pud);

	/* INVLPG to clear all paging-structure caches */
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE-1);

	for (i = 0; i < PTRS_PER_PMD; i++) {
		if (!pmd_none(pmd_sv[i])) {
			pte = (pte_t *)pmd_page_vaddr(pmd_sv[i]);
			free_page((unsigned long)pte);
		}
	}

	free_page((unsigned long)pmd_sv);

	pagetable_dtor(virt_to_ptdesc(pmd));
	free_page((unsigned long)pmd);

	return 1;
}

/**
 * pmd_free_pte_page - Clear pmd entry and free pte page.
 * @pmd: Pointer to a PMD.
 * @addr: Virtual address associated with pmd.
 *
 * Context: The pmd range has been unmapped and TLB purged.
 * Return: 1 if clearing the entry succeeded. 0 otherwise.
 */
int pmd_free_pte_page(pmd_t *pmd, unsigned long addr)
{
	pte_t *pte;

	pte = (pte_t *)pmd_page_vaddr(*pmd);
	pmd_clear(pmd);

	/* INVLPG to clear all paging-structure caches */
	flush_tlb_kernel_range(addr, addr + PAGE_SIZE-1);

	free_page((unsigned long)pte);

	return 1;
}

#else /* !CONFIG_X86_64 */

/*
 * Disable free page handling on x86-PAE. This assures that ioremap()
 * does not update sync'd pmd entries. See vmalloc_sync_one().
 */
int pmd_free_pte_page(pmd_t *pmd, unsigned long addr)
{
	return pmd_none(*pmd);
}

#endif /* CONFIG_X86_64 */
#endif	/* CONFIG_HAVE_ARCH_HUGE_VMAP */

pte_t pte_mkwrite(pte_t pte, struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_SHADOW_STACK)
		return pte_mkwrite_shstk(pte);

	pte = pte_mkwrite_novma(pte);

	return pte_clear_saveddirty(pte);
}

pmd_t pmd_mkwrite(pmd_t pmd, struct vm_area_struct *vma)
{
	if (vma->vm_flags & VM_SHADOW_STACK)
		return pmd_mkwrite_shstk(pmd);

	pmd = pmd_mkwrite_novma(pmd);

	return pmd_clear_saveddirty(pmd);
}

void arch_check_zapped_pte(struct vm_area_struct *vma, pte_t pte)
{
	/*
	 * Hardware before shadow stack can (rarely) set Dirty=1
	 * on a Write=0 PTE. So the below condition
	 * only indicates a software bug when shadow stack is
	 * supported by the HW. This checking is covered in
	 * pte_shstk().
	 */
	VM_WARN_ON_ONCE(!(vma->vm_flags & VM_SHADOW_STACK) &&
			pte_shstk(pte));
}

void arch_check_zapped_pmd(struct vm_area_struct *vma, pmd_t pmd)
{
	/* See note in arch_check_zapped_pte() */
	VM_WARN_ON_ONCE(!(vma->vm_flags & VM_SHADOW_STACK) &&
			pmd_shstk(pmd));
}

void arch_check_zapped_pud(struct vm_area_struct *vma, pud_t pud)
{
	/* See note in arch_check_zapped_pte() */
	VM_WARN_ON_ONCE(!(vma->vm_flags & VM_SHADOW_STACK) && pud_shstk(pud));
}
