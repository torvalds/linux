/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * KVM/MIPS MMU handling in the KVM module.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Sanjay Lal <sanjayl@kymasys.com>
 */

#include <linux/highmem.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>

/*
 * KVM_MMU_CACHE_MIN_PAGES is the number of GPA page table translation levels
 * for which pages need to be cached.
 */
#if defined(__PAGETABLE_PMD_FOLDED)
#define KVM_MMU_CACHE_MIN_PAGES 1
#else
#define KVM_MMU_CACHE_MIN_PAGES 2
#endif

static int mmu_topup_memory_cache(struct kvm_mmu_memory_cache *cache,
				  int min, int max)
{
	void *page;

	BUG_ON(max > KVM_NR_MEM_OBJS);
	if (cache->nobjs >= min)
		return 0;
	while (cache->nobjs < max) {
		page = (void *)__get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		cache->objects[cache->nobjs++] = page;
	}
	return 0;
}

static void mmu_free_memory_cache(struct kvm_mmu_memory_cache *mc)
{
	while (mc->nobjs)
		free_page((unsigned long)mc->objects[--mc->nobjs]);
}

static void *mmu_memory_cache_alloc(struct kvm_mmu_memory_cache *mc)
{
	void *p;

	BUG_ON(!mc || !mc->nobjs);
	p = mc->objects[--mc->nobjs];
	return p;
}

void kvm_mmu_free_memory_caches(struct kvm_vcpu *vcpu)
{
	mmu_free_memory_cache(&vcpu->arch.mmu_page_cache);
}

/**
 * kvm_pgd_init() - Initialise KVM GPA page directory.
 * @page:	Pointer to page directory (PGD) for KVM GPA.
 *
 * Initialise a KVM GPA page directory with pointers to the invalid table, i.e.
 * representing no mappings. This is similar to pgd_init(), however it
 * initialises all the page directory pointers, not just the ones corresponding
 * to the userland address space (since it is for the guest physical address
 * space rather than a virtual address space).
 */
static void kvm_pgd_init(void *page)
{
	unsigned long *p, *end;
	unsigned long entry;

#ifdef __PAGETABLE_PMD_FOLDED
	entry = (unsigned long)invalid_pte_table;
#else
	entry = (unsigned long)invalid_pmd_table;
#endif

	p = (unsigned long *)page;
	end = p + PTRS_PER_PGD;

	do {
		p[0] = entry;
		p[1] = entry;
		p[2] = entry;
		p[3] = entry;
		p[4] = entry;
		p += 8;
		p[-3] = entry;
		p[-2] = entry;
		p[-1] = entry;
	} while (p != end);
}

/**
 * kvm_pgd_alloc() - Allocate and initialise a KVM GPA page directory.
 *
 * Allocate a blank KVM GPA page directory (PGD) for representing guest physical
 * to host physical page mappings.
 *
 * Returns:	Pointer to new KVM GPA page directory.
 *		NULL on allocation failure.
 */
pgd_t *kvm_pgd_alloc(void)
{
	pgd_t *ret;

	ret = (pgd_t *)__get_free_pages(GFP_KERNEL, PGD_ORDER);
	if (ret)
		kvm_pgd_init(ret);

	return ret;
}

/**
 * kvm_mips_walk_pgd() - Walk page table with optional allocation.
 * @pgd:	Page directory pointer.
 * @addr:	Address to index page table using.
 * @cache:	MMU page cache to allocate new page tables from, or NULL.
 *
 * Walk the page tables pointed to by @pgd to find the PTE corresponding to the
 * address @addr. If page tables don't exist for @addr, they will be created
 * from the MMU cache if @cache is not NULL.
 *
 * Returns:	Pointer to pte_t corresponding to @addr.
 *		NULL if a page table doesn't exist for @addr and !@cache.
 *		NULL if a page table allocation failed.
 */
static pte_t *kvm_mips_walk_pgd(pgd_t *pgd, struct kvm_mmu_memory_cache *cache,
				unsigned long addr)
{
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd += pgd_index(addr);
	if (pgd_none(*pgd)) {
		/* Not used on MIPS yet */
		BUG();
		return NULL;
	}
	p4d = p4d_offset(pgd, addr);
	pud = pud_offset(p4d, addr);
	if (pud_none(*pud)) {
		pmd_t *new_pmd;

		if (!cache)
			return NULL;
		new_pmd = mmu_memory_cache_alloc(cache);
		pmd_init((unsigned long)new_pmd,
			 (unsigned long)invalid_pte_table);
		pud_populate(NULL, pud, new_pmd);
	}
	pmd = pmd_offset(pud, addr);
	if (pmd_none(*pmd)) {
		pte_t *new_pte;

		if (!cache)
			return NULL;
		new_pte = mmu_memory_cache_alloc(cache);
		clear_page(new_pte);
		pmd_populate_kernel(NULL, pmd, new_pte);
	}
	return pte_offset_kernel(pmd, addr);
}

/* Caller must hold kvm->mm_lock */
static pte_t *kvm_mips_pte_for_gpa(struct kvm *kvm,
				   struct kvm_mmu_memory_cache *cache,
				   unsigned long addr)
{
	return kvm_mips_walk_pgd(kvm->arch.gpa_mm.pgd, cache, addr);
}

/*
 * kvm_mips_flush_gpa_{pte,pmd,pud,pgd,pt}.
 * Flush a range of guest physical address space from the VM's GPA page tables.
 */

static bool kvm_mips_flush_gpa_pte(pte_t *pte, unsigned long start_gpa,
				   unsigned long end_gpa)
{
	int i_min = pte_index(start_gpa);
	int i_max = pte_index(end_gpa);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PTE - 1);
	int i;

	for (i = i_min; i <= i_max; ++i) {
		if (!pte_present(pte[i]))
			continue;

		set_pte(pte + i, __pte(0));
	}
	return safe_to_remove;
}

static bool kvm_mips_flush_gpa_pmd(pmd_t *pmd, unsigned long start_gpa,
				   unsigned long end_gpa)
{
	pte_t *pte;
	unsigned long end = ~0ul;
	int i_min = pmd_index(start_gpa);
	int i_max = pmd_index(end_gpa);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PMD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gpa = 0) {
		if (!pmd_present(pmd[i]))
			continue;

		pte = pte_offset_kernel(pmd + i, 0);
		if (i == i_max)
			end = end_gpa;

		if (kvm_mips_flush_gpa_pte(pte, start_gpa, end)) {
			pmd_clear(pmd + i);
			pte_free_kernel(NULL, pte);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

static bool kvm_mips_flush_gpa_pud(pud_t *pud, unsigned long start_gpa,
				   unsigned long end_gpa)
{
	pmd_t *pmd;
	unsigned long end = ~0ul;
	int i_min = pud_index(start_gpa);
	int i_max = pud_index(end_gpa);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PUD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gpa = 0) {
		if (!pud_present(pud[i]))
			continue;

		pmd = pmd_offset(pud + i, 0);
		if (i == i_max)
			end = end_gpa;

		if (kvm_mips_flush_gpa_pmd(pmd, start_gpa, end)) {
			pud_clear(pud + i);
			pmd_free(NULL, pmd);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

static bool kvm_mips_flush_gpa_pgd(pgd_t *pgd, unsigned long start_gpa,
				   unsigned long end_gpa)
{
	p4d_t *p4d;
	pud_t *pud;
	unsigned long end = ~0ul;
	int i_min = pgd_index(start_gpa);
	int i_max = pgd_index(end_gpa);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PGD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gpa = 0) {
		if (!pgd_present(pgd[i]))
			continue;

		p4d = p4d_offset(pgd, 0);
		pud = pud_offset(p4d + i, 0);
		if (i == i_max)
			end = end_gpa;

		if (kvm_mips_flush_gpa_pud(pud, start_gpa, end)) {
			pgd_clear(pgd + i);
			pud_free(NULL, pud);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

/**
 * kvm_mips_flush_gpa_pt() - Flush a range of guest physical addresses.
 * @kvm:	KVM pointer.
 * @start_gfn:	Guest frame number of first page in GPA range to flush.
 * @end_gfn:	Guest frame number of last page in GPA range to flush.
 *
 * Flushes a range of GPA mappings from the GPA page tables.
 *
 * The caller must hold the @kvm->mmu_lock spinlock.
 *
 * Returns:	Whether its safe to remove the top level page directory because
 *		all lower levels have been removed.
 */
bool kvm_mips_flush_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn)
{
	return kvm_mips_flush_gpa_pgd(kvm->arch.gpa_mm.pgd,
				      start_gfn << PAGE_SHIFT,
				      end_gfn << PAGE_SHIFT);
}

#define BUILD_PTE_RANGE_OP(name, op)					\
static int kvm_mips_##name##_pte(pte_t *pte, unsigned long start,	\
				 unsigned long end)			\
{									\
	int ret = 0;							\
	int i_min = pte_index(start);				\
	int i_max = pte_index(end);					\
	int i;								\
	pte_t old, new;							\
									\
	for (i = i_min; i <= i_max; ++i) {				\
		if (!pte_present(pte[i]))				\
			continue;					\
									\
		old = pte[i];						\
		new = op(old);						\
		if (pte_val(new) == pte_val(old))			\
			continue;					\
		set_pte(pte + i, new);					\
		ret = 1;						\
	}								\
	return ret;							\
}									\
									\
/* returns true if anything was done */					\
static int kvm_mips_##name##_pmd(pmd_t *pmd, unsigned long start,	\
				 unsigned long end)			\
{									\
	int ret = 0;							\
	pte_t *pte;							\
	unsigned long cur_end = ~0ul;					\
	int i_min = pmd_index(start);				\
	int i_max = pmd_index(end);					\
	int i;								\
									\
	for (i = i_min; i <= i_max; ++i, start = 0) {			\
		if (!pmd_present(pmd[i]))				\
			continue;					\
									\
		pte = pte_offset_kernel(pmd + i, 0);				\
		if (i == i_max)						\
			cur_end = end;					\
									\
		ret |= kvm_mips_##name##_pte(pte, start, cur_end);	\
	}								\
	return ret;							\
}									\
									\
static int kvm_mips_##name##_pud(pud_t *pud, unsigned long start,	\
				 unsigned long end)			\
{									\
	int ret = 0;							\
	pmd_t *pmd;							\
	unsigned long cur_end = ~0ul;					\
	int i_min = pud_index(start);				\
	int i_max = pud_index(end);					\
	int i;								\
									\
	for (i = i_min; i <= i_max; ++i, start = 0) {			\
		if (!pud_present(pud[i]))				\
			continue;					\
									\
		pmd = pmd_offset(pud + i, 0);				\
		if (i == i_max)						\
			cur_end = end;					\
									\
		ret |= kvm_mips_##name##_pmd(pmd, start, cur_end);	\
	}								\
	return ret;							\
}									\
									\
static int kvm_mips_##name##_pgd(pgd_t *pgd, unsigned long start,	\
				 unsigned long end)			\
{									\
	int ret = 0;							\
	p4d_t *p4d;							\
	pud_t *pud;							\
	unsigned long cur_end = ~0ul;					\
	int i_min = pgd_index(start);					\
	int i_max = pgd_index(end);					\
	int i;								\
									\
	for (i = i_min; i <= i_max; ++i, start = 0) {			\
		if (!pgd_present(pgd[i]))				\
			continue;					\
									\
		p4d = p4d_offset(pgd, 0);				\
		pud = pud_offset(p4d + i, 0);				\
		if (i == i_max)						\
			cur_end = end;					\
									\
		ret |= kvm_mips_##name##_pud(pud, start, cur_end);	\
	}								\
	return ret;							\
}

/*
 * kvm_mips_mkclean_gpa_pt.
 * Mark a range of guest physical address space clean (writes fault) in the VM's
 * GPA page table to allow dirty page tracking.
 */

BUILD_PTE_RANGE_OP(mkclean, pte_mkclean)

/**
 * kvm_mips_mkclean_gpa_pt() - Make a range of guest physical addresses clean.
 * @kvm:	KVM pointer.
 * @start_gfn:	Guest frame number of first page in GPA range to flush.
 * @end_gfn:	Guest frame number of last page in GPA range to flush.
 *
 * Make a range of GPA mappings clean so that guest writes will fault and
 * trigger dirty page logging.
 *
 * The caller must hold the @kvm->mmu_lock spinlock.
 *
 * Returns:	Whether any GPA mappings were modified, which would require
 *		derived mappings (GVA page tables & TLB enties) to be
 *		invalidated.
 */
int kvm_mips_mkclean_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn)
{
	return kvm_mips_mkclean_pgd(kvm->arch.gpa_mm.pgd,
				    start_gfn << PAGE_SHIFT,
				    end_gfn << PAGE_SHIFT);
}

/**
 * kvm_arch_mmu_enable_log_dirty_pt_masked() - write protect dirty pages
 * @kvm:	The KVM pointer
 * @slot:	The memory slot associated with mask
 * @gfn_offset:	The gfn offset in memory slot
 * @mask:	The mask of dirty pages at offset 'gfn_offset' in this memory
 *		slot to be write protected
 *
 * Walks bits set in mask write protects the associated pte's. Caller must
 * acquire @kvm->mmu_lock.
 */
void kvm_arch_mmu_enable_log_dirty_pt_masked(struct kvm *kvm,
		struct kvm_memory_slot *slot,
		gfn_t gfn_offset, unsigned long mask)
{
	gfn_t base_gfn = slot->base_gfn + gfn_offset;
	gfn_t start = base_gfn +  __ffs(mask);
	gfn_t end = base_gfn + __fls(mask);

	kvm_mips_mkclean_gpa_pt(kvm, start, end);
}

/*
 * kvm_mips_mkold_gpa_pt.
 * Mark a range of guest physical address space old (all accesses fault) in the
 * VM's GPA page table to allow detection of commonly used pages.
 */

BUILD_PTE_RANGE_OP(mkold, pte_mkold)

static int kvm_mips_mkold_gpa_pt(struct kvm *kvm, gfn_t start_gfn,
				 gfn_t end_gfn)
{
	return kvm_mips_mkold_pgd(kvm->arch.gpa_mm.pgd,
				  start_gfn << PAGE_SHIFT,
				  end_gfn << PAGE_SHIFT);
}

static int handle_hva_to_gpa(struct kvm *kvm,
			     unsigned long start,
			     unsigned long end,
			     int (*handler)(struct kvm *kvm, gfn_t gfn,
					    gpa_t gfn_end,
					    struct kvm_memory_slot *memslot,
					    void *data),
			     void *data)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	int ret = 0;

	slots = kvm_memslots(kvm);

	/* we only care about the pages that the guest sees */
	kvm_for_each_memslot(memslot, slots) {
		unsigned long hva_start, hva_end;
		gfn_t gfn, gfn_end;

		hva_start = max(start, memslot->userspace_addr);
		hva_end = min(end, memslot->userspace_addr +
					(memslot->npages << PAGE_SHIFT));
		if (hva_start >= hva_end)
			continue;

		/*
		 * {gfn(page) | page intersects with [hva_start, hva_end)} =
		 * {gfn_start, gfn_start+1, ..., gfn_end-1}.
		 */
		gfn = hva_to_gfn_memslot(hva_start, memslot);
		gfn_end = hva_to_gfn_memslot(hva_end + PAGE_SIZE - 1, memslot);

		ret |= handler(kvm, gfn, gfn_end, memslot, data);
	}

	return ret;
}


static int kvm_unmap_hva_handler(struct kvm *kvm, gfn_t gfn, gfn_t gfn_end,
				 struct kvm_memory_slot *memslot, void *data)
{
	kvm_mips_flush_gpa_pt(kvm, gfn, gfn_end);
	return 1;
}

int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end)
{
	handle_hva_to_gpa(kvm, start, end, &kvm_unmap_hva_handler, NULL);

	kvm_mips_callbacks->flush_shadow_all(kvm);
	return 0;
}

static int kvm_set_spte_handler(struct kvm *kvm, gfn_t gfn, gfn_t gfn_end,
				struct kvm_memory_slot *memslot, void *data)
{
	gpa_t gpa = gfn << PAGE_SHIFT;
	pte_t hva_pte = *(pte_t *)data;
	pte_t *gpa_pte = kvm_mips_pte_for_gpa(kvm, NULL, gpa);
	pte_t old_pte;

	if (!gpa_pte)
		return 0;

	/* Mapping may need adjusting depending on memslot flags */
	old_pte = *gpa_pte;
	if (memslot->flags & KVM_MEM_LOG_DIRTY_PAGES && !pte_dirty(old_pte))
		hva_pte = pte_mkclean(hva_pte);
	else if (memslot->flags & KVM_MEM_READONLY)
		hva_pte = pte_wrprotect(hva_pte);

	set_pte(gpa_pte, hva_pte);

	/* Replacing an absent or old page doesn't need flushes */
	if (!pte_present(old_pte) || !pte_young(old_pte))
		return 0;

	/* Pages swapped, aged, moved, or cleaned require flushes */
	return !pte_present(hva_pte) ||
	       !pte_young(hva_pte) ||
	       pte_pfn(old_pte) != pte_pfn(hva_pte) ||
	       (pte_dirty(old_pte) && !pte_dirty(hva_pte));
}

int kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte)
{
	unsigned long end = hva + PAGE_SIZE;
	int ret;

	ret = handle_hva_to_gpa(kvm, hva, end, &kvm_set_spte_handler, &pte);
	if (ret)
		kvm_mips_callbacks->flush_shadow_all(kvm);
	return 0;
}

static int kvm_age_hva_handler(struct kvm *kvm, gfn_t gfn, gfn_t gfn_end,
			       struct kvm_memory_slot *memslot, void *data)
{
	return kvm_mips_mkold_gpa_pt(kvm, gfn, gfn_end);
}

static int kvm_test_age_hva_handler(struct kvm *kvm, gfn_t gfn, gfn_t gfn_end,
				    struct kvm_memory_slot *memslot, void *data)
{
	gpa_t gpa = gfn << PAGE_SHIFT;
	pte_t *gpa_pte = kvm_mips_pte_for_gpa(kvm, NULL, gpa);

	if (!gpa_pte)
		return 0;
	return pte_young(*gpa_pte);
}

int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end)
{
	return handle_hva_to_gpa(kvm, start, end, kvm_age_hva_handler, NULL);
}

int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	return handle_hva_to_gpa(kvm, hva, hva, kvm_test_age_hva_handler, NULL);
}

/**
 * _kvm_mips_map_page_fast() - Fast path GPA fault handler.
 * @vcpu:		VCPU pointer.
 * @gpa:		Guest physical address of fault.
 * @write_fault:	Whether the fault was due to a write.
 * @out_entry:		New PTE for @gpa (written on success unless NULL).
 * @out_buddy:		New PTE for @gpa's buddy (written on success unless
 *			NULL).
 *
 * Perform fast path GPA fault handling, doing all that can be done without
 * calling into KVM. This handles marking old pages young (for idle page
 * tracking), and dirtying of clean pages (for dirty page logging).
 *
 * Returns:	0 on success, in which case we can update derived mappings and
 *		resume guest execution.
 *		-EFAULT on failure due to absent GPA mapping or write to
 *		read-only page, in which case KVM must be consulted.
 */
static int _kvm_mips_map_page_fast(struct kvm_vcpu *vcpu, unsigned long gpa,
				   bool write_fault,
				   pte_t *out_entry, pte_t *out_buddy)
{
	struct kvm *kvm = vcpu->kvm;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	pte_t *ptep;
	kvm_pfn_t pfn = 0;	/* silence bogus GCC warning */
	bool pfn_valid = false;
	int ret = 0;

	spin_lock(&kvm->mmu_lock);

	/* Fast path - just check GPA page table for an existing entry */
	ptep = kvm_mips_pte_for_gpa(kvm, NULL, gpa);
	if (!ptep || !pte_present(*ptep)) {
		ret = -EFAULT;
		goto out;
	}

	/* Track access to pages marked old */
	if (!pte_young(*ptep)) {
		set_pte(ptep, pte_mkyoung(*ptep));
		pfn = pte_pfn(*ptep);
		pfn_valid = true;
		/* call kvm_set_pfn_accessed() after unlock */
	}
	if (write_fault && !pte_dirty(*ptep)) {
		if (!pte_write(*ptep)) {
			ret = -EFAULT;
			goto out;
		}

		/* Track dirtying of writeable pages */
		set_pte(ptep, pte_mkdirty(*ptep));
		pfn = pte_pfn(*ptep);
		mark_page_dirty(kvm, gfn);
		kvm_set_pfn_dirty(pfn);
	}

	if (out_entry)
		*out_entry = *ptep;
	if (out_buddy)
		*out_buddy = *ptep_buddy(ptep);

out:
	spin_unlock(&kvm->mmu_lock);
	if (pfn_valid)
		kvm_set_pfn_accessed(pfn);
	return ret;
}

/**
 * kvm_mips_map_page() - Map a guest physical page.
 * @vcpu:		VCPU pointer.
 * @gpa:		Guest physical address of fault.
 * @write_fault:	Whether the fault was due to a write.
 * @out_entry:		New PTE for @gpa (written on success unless NULL).
 * @out_buddy:		New PTE for @gpa's buddy (written on success unless
 *			NULL).
 *
 * Handle GPA faults by creating a new GPA mapping (or updating an existing
 * one).
 *
 * This takes care of marking pages young or dirty (idle/dirty page tracking),
 * asking KVM for the corresponding PFN, and creating a mapping in the GPA page
 * tables. Derived mappings (GVA page tables and TLBs) must be handled by the
 * caller.
 *
 * Returns:	0 on success, in which case the caller may use the @out_entry
 *		and @out_buddy PTEs to update derived mappings and resume guest
 *		execution.
 *		-EFAULT if there is no memory region at @gpa or a write was
 *		attempted to a read-only memory region. This is usually handled
 *		as an MMIO access.
 */
static int kvm_mips_map_page(struct kvm_vcpu *vcpu, unsigned long gpa,
			     bool write_fault,
			     pte_t *out_entry, pte_t *out_buddy)
{
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int srcu_idx, err;
	kvm_pfn_t pfn;
	pte_t *ptep, entry, old_pte;
	bool writeable;
	unsigned long prot_bits;
	unsigned long mmu_seq;

	/* Try the fast path to handle old / clean pages */
	srcu_idx = srcu_read_lock(&kvm->srcu);
	err = _kvm_mips_map_page_fast(vcpu, gpa, write_fault, out_entry,
				      out_buddy);
	if (!err)
		goto out;

	/* We need a minimum of cached pages ready for page table creation */
	err = mmu_topup_memory_cache(memcache, KVM_MMU_CACHE_MIN_PAGES,
				     KVM_NR_MEM_OBJS);
	if (err)
		goto out;

retry:
	/*
	 * Used to check for invalidations in progress, of the pfn that is
	 * returned by pfn_to_pfn_prot below.
	 */
	mmu_seq = kvm->mmu_notifier_seq;
	/*
	 * Ensure the read of mmu_notifier_seq isn't reordered with PTE reads in
	 * gfn_to_pfn_prot() (which calls get_user_pages()), so that we don't
	 * risk the page we get a reference to getting unmapped before we have a
	 * chance to grab the mmu_lock without mmu_notifier_retry() noticing.
	 *
	 * This smp_rmb() pairs with the effective smp_wmb() of the combination
	 * of the pte_unmap_unlock() after the PTE is zapped, and the
	 * spin_lock() in kvm_mmu_notifier_invalidate_<page|range_end>() before
	 * mmu_notifier_seq is incremented.
	 */
	smp_rmb();

	/* Slow path - ask KVM core whether we can access this GPA */
	pfn = gfn_to_pfn_prot(kvm, gfn, write_fault, &writeable);
	if (is_error_noslot_pfn(pfn)) {
		err = -EFAULT;
		goto out;
	}

	spin_lock(&kvm->mmu_lock);
	/* Check if an invalidation has taken place since we got pfn */
	if (mmu_notifier_retry(kvm, mmu_seq)) {
		/*
		 * This can happen when mappings are changed asynchronously, but
		 * also synchronously if a COW is triggered by
		 * gfn_to_pfn_prot().
		 */
		spin_unlock(&kvm->mmu_lock);
		kvm_release_pfn_clean(pfn);
		goto retry;
	}

	/* Ensure page tables are allocated */
	ptep = kvm_mips_pte_for_gpa(kvm, memcache, gpa);

	/* Set up the PTE */
	prot_bits = _PAGE_PRESENT | __READABLE | _page_cachable_default;
	if (writeable) {
		prot_bits |= _PAGE_WRITE;
		if (write_fault) {
			prot_bits |= __WRITEABLE;
			mark_page_dirty(kvm, gfn);
			kvm_set_pfn_dirty(pfn);
		}
	}
	entry = pfn_pte(pfn, __pgprot(prot_bits));

	/* Write the PTE */
	old_pte = *ptep;
	set_pte(ptep, entry);

	err = 0;
	if (out_entry)
		*out_entry = *ptep;
	if (out_buddy)
		*out_buddy = *ptep_buddy(ptep);

	spin_unlock(&kvm->mmu_lock);
	kvm_release_pfn_clean(pfn);
	kvm_set_pfn_accessed(pfn);
out:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return err;
}

static pte_t *kvm_trap_emul_pte_for_gva(struct kvm_vcpu *vcpu,
					unsigned long addr)
{
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;
	pgd_t *pgdp;
	int ret;

	/* We need a minimum of cached pages ready for page table creation */
	ret = mmu_topup_memory_cache(memcache, KVM_MMU_CACHE_MIN_PAGES,
				     KVM_NR_MEM_OBJS);
	if (ret)
		return NULL;

	if (KVM_GUEST_KERNEL_MODE(vcpu))
		pgdp = vcpu->arch.guest_kernel_mm.pgd;
	else
		pgdp = vcpu->arch.guest_user_mm.pgd;

	return kvm_mips_walk_pgd(pgdp, memcache, addr);
}

void kvm_trap_emul_invalidate_gva(struct kvm_vcpu *vcpu, unsigned long addr,
				  bool user)
{
	pgd_t *pgdp;
	pte_t *ptep;

	addr &= PAGE_MASK << 1;

	pgdp = vcpu->arch.guest_kernel_mm.pgd;
	ptep = kvm_mips_walk_pgd(pgdp, NULL, addr);
	if (ptep) {
		ptep[0] = pfn_pte(0, __pgprot(0));
		ptep[1] = pfn_pte(0, __pgprot(0));
	}

	if (user) {
		pgdp = vcpu->arch.guest_user_mm.pgd;
		ptep = kvm_mips_walk_pgd(pgdp, NULL, addr);
		if (ptep) {
			ptep[0] = pfn_pte(0, __pgprot(0));
			ptep[1] = pfn_pte(0, __pgprot(0));
		}
	}
}

/*
 * kvm_mips_flush_gva_{pte,pmd,pud,pgd,pt}.
 * Flush a range of guest physical address space from the VM's GPA page tables.
 */

static bool kvm_mips_flush_gva_pte(pte_t *pte, unsigned long start_gva,
				   unsigned long end_gva)
{
	int i_min = pte_index(start_gva);
	int i_max = pte_index(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PTE - 1);
	int i;

	/*
	 * There's no freeing to do, so there's no point clearing individual
	 * entries unless only part of the last level page table needs flushing.
	 */
	if (safe_to_remove)
		return true;

	for (i = i_min; i <= i_max; ++i) {
		if (!pte_present(pte[i]))
			continue;

		set_pte(pte + i, __pte(0));
	}
	return false;
}

static bool kvm_mips_flush_gva_pmd(pmd_t *pmd, unsigned long start_gva,
				   unsigned long end_gva)
{
	pte_t *pte;
	unsigned long end = ~0ul;
	int i_min = pmd_index(start_gva);
	int i_max = pmd_index(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PMD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gva = 0) {
		if (!pmd_present(pmd[i]))
			continue;

		pte = pte_offset_kernel(pmd + i, 0);
		if (i == i_max)
			end = end_gva;

		if (kvm_mips_flush_gva_pte(pte, start_gva, end)) {
			pmd_clear(pmd + i);
			pte_free_kernel(NULL, pte);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

static bool kvm_mips_flush_gva_pud(pud_t *pud, unsigned long start_gva,
				   unsigned long end_gva)
{
	pmd_t *pmd;
	unsigned long end = ~0ul;
	int i_min = pud_index(start_gva);
	int i_max = pud_index(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PUD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gva = 0) {
		if (!pud_present(pud[i]))
			continue;

		pmd = pmd_offset(pud + i, 0);
		if (i == i_max)
			end = end_gva;

		if (kvm_mips_flush_gva_pmd(pmd, start_gva, end)) {
			pud_clear(pud + i);
			pmd_free(NULL, pmd);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

static bool kvm_mips_flush_gva_pgd(pgd_t *pgd, unsigned long start_gva,
				   unsigned long end_gva)
{
	p4d_t *p4d;
	pud_t *pud;
	unsigned long end = ~0ul;
	int i_min = pgd_index(start_gva);
	int i_max = pgd_index(end_gva);
	bool safe_to_remove = (i_min == 0 && i_max == PTRS_PER_PGD - 1);
	int i;

	for (i = i_min; i <= i_max; ++i, start_gva = 0) {
		if (!pgd_present(pgd[i]))
			continue;

		p4d = p4d_offset(pgd, 0);
		pud = pud_offset(p4d + i, 0);
		if (i == i_max)
			end = end_gva;

		if (kvm_mips_flush_gva_pud(pud, start_gva, end)) {
			pgd_clear(pgd + i);
			pud_free(NULL, pud);
		} else {
			safe_to_remove = false;
		}
	}
	return safe_to_remove;
}

void kvm_mips_flush_gva_pt(pgd_t *pgd, enum kvm_mips_flush flags)
{
	if (flags & KMF_GPA) {
		/* all of guest virtual address space could be affected */
		if (flags & KMF_KERN)
			/* useg, kseg0, seg2/3 */
			kvm_mips_flush_gva_pgd(pgd, 0, 0x7fffffff);
		else
			/* useg */
			kvm_mips_flush_gva_pgd(pgd, 0, 0x3fffffff);
	} else {
		/* useg */
		kvm_mips_flush_gva_pgd(pgd, 0, 0x3fffffff);

		/* kseg2/3 */
		if (flags & KMF_KERN)
			kvm_mips_flush_gva_pgd(pgd, 0x60000000, 0x7fffffff);
	}
}

static pte_t kvm_mips_gpa_pte_to_gva_unmapped(pte_t pte)
{
	/*
	 * Don't leak writeable but clean entries from GPA page tables. We don't
	 * want the normal Linux tlbmod handler to handle dirtying when KVM
	 * accesses guest memory.
	 */
	if (!pte_dirty(pte))
		pte = pte_wrprotect(pte);

	return pte;
}

static pte_t kvm_mips_gpa_pte_to_gva_mapped(pte_t pte, long entrylo)
{
	/* Guest EntryLo overrides host EntryLo */
	if (!(entrylo & ENTRYLO_D))
		pte = pte_mkclean(pte);

	return kvm_mips_gpa_pte_to_gva_unmapped(pte);
}

#ifdef CONFIG_KVM_MIPS_VZ
int kvm_mips_handle_vz_root_tlb_fault(unsigned long badvaddr,
				      struct kvm_vcpu *vcpu,
				      bool write_fault)
{
	int ret;

	ret = kvm_mips_map_page(vcpu, badvaddr, write_fault, NULL, NULL);
	if (ret)
		return ret;

	/* Invalidate this entry in the TLB */
	return kvm_vz_host_tlb_inv(vcpu, badvaddr);
}
#endif

/* XXXKYMA: Must be called with interrupts disabled */
int kvm_mips_handle_kseg0_tlb_fault(unsigned long badvaddr,
				    struct kvm_vcpu *vcpu,
				    bool write_fault)
{
	unsigned long gpa;
	pte_t pte_gpa[2], *ptep_gva;
	int idx;

	if (KVM_GUEST_KSEGX(badvaddr) != KVM_GUEST_KSEG0) {
		kvm_err("%s: Invalid BadVaddr: %#lx\n", __func__, badvaddr);
		kvm_mips_dump_host_tlbs();
		return -1;
	}

	/* Get the GPA page table entry */
	gpa = KVM_GUEST_CPHYSADDR(badvaddr);
	idx = (badvaddr >> PAGE_SHIFT) & 1;
	if (kvm_mips_map_page(vcpu, gpa, write_fault, &pte_gpa[idx],
			      &pte_gpa[!idx]) < 0)
		return -1;

	/* Get the GVA page table entry */
	ptep_gva = kvm_trap_emul_pte_for_gva(vcpu, badvaddr & ~PAGE_SIZE);
	if (!ptep_gva) {
		kvm_err("No ptep for gva %lx\n", badvaddr);
		return -1;
	}

	/* Copy a pair of entries from GPA page table to GVA page table */
	ptep_gva[0] = kvm_mips_gpa_pte_to_gva_unmapped(pte_gpa[0]);
	ptep_gva[1] = kvm_mips_gpa_pte_to_gva_unmapped(pte_gpa[1]);

	/* Invalidate this entry in the TLB, guest kernel ASID only */
	kvm_mips_host_tlb_inv(vcpu, badvaddr, false, true);
	return 0;
}

int kvm_mips_handle_mapped_seg_tlb_fault(struct kvm_vcpu *vcpu,
					 struct kvm_mips_tlb *tlb,
					 unsigned long gva,
					 bool write_fault)
{
	struct kvm *kvm = vcpu->kvm;
	long tlb_lo[2];
	pte_t pte_gpa[2], *ptep_buddy, *ptep_gva;
	unsigned int idx = TLB_LO_IDX(*tlb, gva);
	bool kernel = KVM_GUEST_KERNEL_MODE(vcpu);

	tlb_lo[0] = tlb->tlb_lo[0];
	tlb_lo[1] = tlb->tlb_lo[1];

	/*
	 * The commpage address must not be mapped to anything else if the guest
	 * TLB contains entries nearby, or commpage accesses will break.
	 */
	if (!((gva ^ KVM_GUEST_COMMPAGE_ADDR) & VPN2_MASK & (PAGE_MASK << 1)))
		tlb_lo[TLB_LO_IDX(*tlb, KVM_GUEST_COMMPAGE_ADDR)] = 0;

	/* Get the GPA page table entry */
	if (kvm_mips_map_page(vcpu, mips3_tlbpfn_to_paddr(tlb_lo[idx]),
			      write_fault, &pte_gpa[idx], NULL) < 0)
		return -1;

	/* And its GVA buddy's GPA page table entry if it also exists */
	pte_gpa[!idx] = pfn_pte(0, __pgprot(0));
	if (tlb_lo[!idx] & ENTRYLO_V) {
		spin_lock(&kvm->mmu_lock);
		ptep_buddy = kvm_mips_pte_for_gpa(kvm, NULL,
					mips3_tlbpfn_to_paddr(tlb_lo[!idx]));
		if (ptep_buddy)
			pte_gpa[!idx] = *ptep_buddy;
		spin_unlock(&kvm->mmu_lock);
	}

	/* Get the GVA page table entry pair */
	ptep_gva = kvm_trap_emul_pte_for_gva(vcpu, gva & ~PAGE_SIZE);
	if (!ptep_gva) {
		kvm_err("No ptep for gva %lx\n", gva);
		return -1;
	}

	/* Copy a pair of entries from GPA page table to GVA page table */
	ptep_gva[0] = kvm_mips_gpa_pte_to_gva_mapped(pte_gpa[0], tlb_lo[0]);
	ptep_gva[1] = kvm_mips_gpa_pte_to_gva_mapped(pte_gpa[1], tlb_lo[1]);

	/* Invalidate this entry in the TLB, current guest mode ASID only */
	kvm_mips_host_tlb_inv(vcpu, gva, !kernel, kernel);

	kvm_debug("@ %#lx tlb_lo0: 0x%08lx tlb_lo1: 0x%08lx\n", vcpu->arch.pc,
		  tlb->tlb_lo[0], tlb->tlb_lo[1]);

	return 0;
}

int kvm_mips_handle_commpage_tlb_fault(unsigned long badvaddr,
				       struct kvm_vcpu *vcpu)
{
	kvm_pfn_t pfn;
	pte_t *ptep;

	ptep = kvm_trap_emul_pte_for_gva(vcpu, badvaddr);
	if (!ptep) {
		kvm_err("No ptep for commpage %lx\n", badvaddr);
		return -1;
	}

	pfn = PFN_DOWN(virt_to_phys(vcpu->arch.kseg0_commpage));
	/* Also set valid and dirty, so refill handler doesn't have to */
	*ptep = pte_mkyoung(pte_mkdirty(pfn_pte(pfn, PAGE_SHARED)));

	/* Invalidate this entry in the TLB, guest kernel ASID only */
	kvm_mips_host_tlb_inv(vcpu, badvaddr, false, true);
	return 0;
}

/**
 * kvm_mips_migrate_count() - Migrate timer.
 * @vcpu:	Virtual CPU.
 *
 * Migrate CP0_Count hrtimer to the current CPU by cancelling and restarting it
 * if it was running prior to being cancelled.
 *
 * Must be called when the VCPU is migrated to a different CPU to ensure that
 * timer expiry during guest execution interrupts the guest and causes the
 * interrupt to be delivered in a timely manner.
 */
static void kvm_mips_migrate_count(struct kvm_vcpu *vcpu)
{
	if (hrtimer_cancel(&vcpu->arch.comparecount_timer))
		hrtimer_restart(&vcpu->arch.comparecount_timer);
}

/* Restore ASID once we are scheduled back after preemption */
void kvm_arch_vcpu_load(struct kvm_vcpu *vcpu, int cpu)
{
	unsigned long flags;

	kvm_debug("%s: vcpu %p, cpu: %d\n", __func__, vcpu, cpu);

	local_irq_save(flags);

	vcpu->cpu = cpu;
	if (vcpu->arch.last_sched_cpu != cpu) {
		kvm_debug("[%d->%d]KVM VCPU[%d] switch\n",
			  vcpu->arch.last_sched_cpu, cpu, vcpu->vcpu_id);
		/*
		 * Migrate the timer interrupt to the current CPU so that it
		 * always interrupts the guest and synchronously triggers a
		 * guest timer interrupt.
		 */
		kvm_mips_migrate_count(vcpu);
	}

	/* restore guest state to registers */
	kvm_mips_callbacks->vcpu_load(vcpu, cpu);

	local_irq_restore(flags);
}

/* ASID can change if another task is scheduled during preemption */
void kvm_arch_vcpu_put(struct kvm_vcpu *vcpu)
{
	unsigned long flags;
	int cpu;

	local_irq_save(flags);

	cpu = smp_processor_id();
	vcpu->arch.last_sched_cpu = cpu;
	vcpu->cpu = -1;

	/* save guest state in registers */
	kvm_mips_callbacks->vcpu_put(vcpu, cpu);

	local_irq_restore(flags);
}

/**
 * kvm_trap_emul_gva_fault() - Safely attempt to handle a GVA access fault.
 * @vcpu:	Virtual CPU.
 * @gva:	Guest virtual address to be accessed.
 * @write:	True if write attempted (must be dirtied and made writable).
 *
 * Safely attempt to handle a GVA fault, mapping GVA pages if necessary, and
 * dirtying the page if @write so that guest instructions can be modified.
 *
 * Returns:	KVM_MIPS_MAPPED on success.
 *		KVM_MIPS_GVA if bad guest virtual address.
 *		KVM_MIPS_GPA if bad guest physical address.
 *		KVM_MIPS_TLB if guest TLB not present.
 *		KVM_MIPS_TLBINV if guest TLB present but not valid.
 *		KVM_MIPS_TLBMOD if guest TLB read only.
 */
enum kvm_mips_fault_result kvm_trap_emul_gva_fault(struct kvm_vcpu *vcpu,
						   unsigned long gva,
						   bool write)
{
	struct mips_coproc *cop0 = vcpu->arch.cop0;
	struct kvm_mips_tlb *tlb;
	int index;

	if (KVM_GUEST_KSEGX(gva) == KVM_GUEST_KSEG0) {
		if (kvm_mips_handle_kseg0_tlb_fault(gva, vcpu, write) < 0)
			return KVM_MIPS_GPA;
	} else if ((KVM_GUEST_KSEGX(gva) < KVM_GUEST_KSEG0) ||
		   KVM_GUEST_KSEGX(gva) == KVM_GUEST_KSEG23) {
		/* Address should be in the guest TLB */
		index = kvm_mips_guest_tlb_lookup(vcpu, (gva & VPN2_MASK) |
			  (kvm_read_c0_guest_entryhi(cop0) & KVM_ENTRYHI_ASID));
		if (index < 0)
			return KVM_MIPS_TLB;
		tlb = &vcpu->arch.guest_tlb[index];

		/* Entry should be valid, and dirty for writes */
		if (!TLB_IS_VALID(*tlb, gva))
			return KVM_MIPS_TLBINV;
		if (write && !TLB_IS_DIRTY(*tlb, gva))
			return KVM_MIPS_TLBMOD;

		if (kvm_mips_handle_mapped_seg_tlb_fault(vcpu, tlb, gva, write))
			return KVM_MIPS_GPA;
	} else {
		return KVM_MIPS_GVA;
	}

	return KVM_MIPS_MAPPED;
}

int kvm_get_inst(u32 *opc, struct kvm_vcpu *vcpu, u32 *out)
{
	int err;

	if (WARN(IS_ENABLED(CONFIG_KVM_MIPS_VZ),
		 "Expect BadInstr/BadInstrP registers to be used with VZ\n"))
		return -EINVAL;

retry:
	kvm_trap_emul_gva_lockless_begin(vcpu);
	err = get_user(*out, opc);
	kvm_trap_emul_gva_lockless_end(vcpu);

	if (unlikely(err)) {
		/*
		 * Try to handle the fault, maybe we just raced with a GVA
		 * invalidation.
		 */
		err = kvm_trap_emul_gva_fault(vcpu, (unsigned long)opc,
					      false);
		if (unlikely(err)) {
			kvm_err("%s: illegal address: %p\n",
				__func__, opc);
			return -EFAULT;
		}

		/* Hopefully it'll work now */
		goto retry;
	}
	return 0;
}
