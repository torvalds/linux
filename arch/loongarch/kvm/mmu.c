// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/highmem.h>
#include <linux/hugetlb.h>
#include <linux/kvm_host.h>
#include <linux/page-flags.h>
#include <linux/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/kvm_mmu.h>

static inline bool kvm_hugepage_capable(struct kvm_memory_slot *slot)
{
	return slot->arch.flags & KVM_MEM_HUGEPAGE_CAPABLE;
}

static inline bool kvm_hugepage_incapable(struct kvm_memory_slot *slot)
{
	return slot->arch.flags & KVM_MEM_HUGEPAGE_INCAPABLE;
}

static inline void kvm_ptw_prepare(struct kvm *kvm, kvm_ptw_ctx *ctx)
{
	ctx->level = kvm->arch.root_level;
	/* pte table */
	ctx->invalid_ptes  = kvm->arch.invalid_ptes;
	ctx->pte_shifts    = kvm->arch.pte_shifts;
	ctx->pgtable_shift = ctx->pte_shifts[ctx->level];
	ctx->invalid_entry = ctx->invalid_ptes[ctx->level];
	ctx->opaque        = kvm;
}

/*
 * Mark a range of guest physical address space old (all accesses fault) in the
 * VM's GPA page table to allow detection of commonly used pages.
 */
static int kvm_mkold_pte(kvm_pte_t *pte, phys_addr_t addr, kvm_ptw_ctx *ctx)
{
	if (kvm_pte_young(*pte)) {
		*pte = kvm_pte_mkold(*pte);
		return 1;
	}

	return 0;
}

/*
 * Mark a range of guest physical address space clean (writes fault) in the VM's
 * GPA page table to allow dirty page tracking.
 */
static int kvm_mkclean_pte(kvm_pte_t *pte, phys_addr_t addr, kvm_ptw_ctx *ctx)
{
	gfn_t offset;
	kvm_pte_t val;

	val = *pte;
	/*
	 * For kvm_arch_mmu_enable_log_dirty_pt_masked with mask, start and end
	 * may cross hugepage, for first huge page parameter addr is equal to
	 * start, however for the second huge page addr is base address of
	 * this huge page, rather than start or end address
	 */
	if ((ctx->flag & _KVM_HAS_PGMASK) && !kvm_pte_huge(val)) {
		offset = (addr >> PAGE_SHIFT) - ctx->gfn;
		if (!(BIT(offset) & ctx->mask))
			return 0;
	}

	/*
	 * Need not split huge page now, just set write-proect pte bit
	 * Split huge page until next write fault
	 */
	if (kvm_pte_dirty(val)) {
		*pte = kvm_pte_mkclean(val);
		return 1;
	}

	return 0;
}

/*
 * Clear pte entry
 */
static int kvm_flush_pte(kvm_pte_t *pte, phys_addr_t addr, kvm_ptw_ctx *ctx)
{
	struct kvm *kvm;

	kvm = ctx->opaque;
	if (ctx->level)
		kvm->stat.hugepages--;
	else
		kvm->stat.pages--;

	*pte = ctx->invalid_entry;

	return 1;
}

/*
 * kvm_pgd_alloc() - Allocate and initialise a KVM GPA page directory.
 *
 * Allocate a blank KVM GPA page directory (PGD) for representing guest physical
 * to host physical page mappings.
 *
 * Returns:	Pointer to new KVM GPA page directory.
 *		NULL on allocation failure.
 */
kvm_pte_t *kvm_pgd_alloc(void)
{
	kvm_pte_t *pgd;

	pgd = (kvm_pte_t *)__get_free_pages(GFP_KERNEL, 0);
	if (pgd)
		pgd_init((void *)pgd);

	return pgd;
}

static void _kvm_pte_init(void *addr, unsigned long val)
{
	unsigned long *p, *end;

	p = (unsigned long *)addr;
	end = p + PTRS_PER_PTE;
	do {
		p[0] = val;
		p[1] = val;
		p[2] = val;
		p[3] = val;
		p[4] = val;
		p += 8;
		p[-3] = val;
		p[-2] = val;
		p[-1] = val;
	} while (p != end);
}

/*
 * Caller must hold kvm->mm_lock
 *
 * Walk the page tables of kvm to find the PTE corresponding to the
 * address @addr. If page tables don't exist for @addr, they will be created
 * from the MMU cache if @cache is not NULL.
 */
static kvm_pte_t *kvm_populate_gpa(struct kvm *kvm,
				struct kvm_mmu_memory_cache *cache,
				unsigned long addr, int level)
{
	kvm_ptw_ctx ctx;
	kvm_pte_t *entry, *child;

	kvm_ptw_prepare(kvm, &ctx);
	child = kvm->arch.pgd;
	while (ctx.level > level) {
		entry = kvm_pgtable_offset(&ctx, child, addr);
		if (kvm_pte_none(&ctx, entry)) {
			if (!cache)
				return NULL;

			child = kvm_mmu_memory_cache_alloc(cache);
			_kvm_pte_init(child, ctx.invalid_ptes[ctx.level - 1]);
			kvm_set_pte(entry, __pa(child));
		} else if (kvm_pte_huge(*entry)) {
			return entry;
		} else
			child = (kvm_pte_t *)__va(PHYSADDR(*entry));
		kvm_ptw_enter(&ctx);
	}

	entry = kvm_pgtable_offset(&ctx, child, addr);

	return entry;
}

/*
 * Page walker for VM shadow mmu at last level
 * The last level is small pte page or huge pmd page
 */
static int kvm_ptw_leaf(kvm_pte_t *dir, phys_addr_t addr, phys_addr_t end, kvm_ptw_ctx *ctx)
{
	int ret;
	phys_addr_t next, start, size;
	struct list_head *list;
	kvm_pte_t *entry, *child;

	ret = 0;
	start = addr;
	child = (kvm_pte_t *)__va(PHYSADDR(*dir));
	entry = kvm_pgtable_offset(ctx, child, addr);
	do {
		next = addr + (0x1UL << ctx->pgtable_shift);
		if (!kvm_pte_present(ctx, entry))
			continue;

		ret |= ctx->ops(entry, addr, ctx);
	} while (entry++, addr = next, addr < end);

	if (kvm_need_flush(ctx)) {
		size = 0x1UL << (ctx->pgtable_shift + PAGE_SHIFT - 3);
		if (start + size == end) {
			list = (struct list_head *)child;
			list_add_tail(list, &ctx->list);
			*dir = ctx->invalid_ptes[ctx->level + 1];
		}
	}

	return ret;
}

/*
 * Page walker for VM shadow mmu at page table dir level
 */
static int kvm_ptw_dir(kvm_pte_t *dir, phys_addr_t addr, phys_addr_t end, kvm_ptw_ctx *ctx)
{
	int ret;
	phys_addr_t next, start, size;
	struct list_head *list;
	kvm_pte_t *entry, *child;

	ret = 0;
	start = addr;
	child = (kvm_pte_t *)__va(PHYSADDR(*dir));
	entry = kvm_pgtable_offset(ctx, child, addr);
	do {
		next = kvm_pgtable_addr_end(ctx, addr, end);
		if (!kvm_pte_present(ctx, entry))
			continue;

		if (kvm_pte_huge(*entry)) {
			ret |= ctx->ops(entry, addr, ctx);
			continue;
		}

		kvm_ptw_enter(ctx);
		if (ctx->level == 0)
			ret |= kvm_ptw_leaf(entry, addr, next, ctx);
		else
			ret |= kvm_ptw_dir(entry, addr, next, ctx);
		kvm_ptw_exit(ctx);
	}  while (entry++, addr = next, addr < end);

	if (kvm_need_flush(ctx)) {
		size = 0x1UL << (ctx->pgtable_shift + PAGE_SHIFT - 3);
		if (start + size == end) {
			list = (struct list_head *)child;
			list_add_tail(list, &ctx->list);
			*dir = ctx->invalid_ptes[ctx->level + 1];
		}
	}

	return ret;
}

/*
 * Page walker for VM shadow mmu at page root table
 */
static int kvm_ptw_top(kvm_pte_t *dir, phys_addr_t addr, phys_addr_t end, kvm_ptw_ctx *ctx)
{
	int ret;
	phys_addr_t next;
	kvm_pte_t *entry;

	ret = 0;
	entry = kvm_pgtable_offset(ctx, dir, addr);
	do {
		next = kvm_pgtable_addr_end(ctx, addr, end);
		if (!kvm_pte_present(ctx, entry))
			continue;

		kvm_ptw_enter(ctx);
		ret |= kvm_ptw_dir(entry, addr, next, ctx);
		kvm_ptw_exit(ctx);
	}  while (entry++, addr = next, addr < end);

	return ret;
}

/*
 * kvm_flush_range() - Flush a range of guest physical addresses.
 * @kvm:	KVM pointer.
 * @start_gfn:	Guest frame number of first page in GPA range to flush.
 * @end_gfn:	Guest frame number of last page in GPA range to flush.
 * @lock:	Whether to hold mmu_lock or not
 *
 * Flushes a range of GPA mappings from the GPA page tables.
 */
static void kvm_flush_range(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn, int lock)
{
	int ret;
	kvm_ptw_ctx ctx;
	struct list_head *pos, *temp;

	ctx.ops = kvm_flush_pte;
	ctx.flag = _KVM_FLUSH_PGTABLE;
	kvm_ptw_prepare(kvm, &ctx);
	INIT_LIST_HEAD(&ctx.list);

	if (lock) {
		spin_lock(&kvm->mmu_lock);
		ret = kvm_ptw_top(kvm->arch.pgd, start_gfn << PAGE_SHIFT,
					end_gfn << PAGE_SHIFT, &ctx);
		spin_unlock(&kvm->mmu_lock);
	} else
		ret = kvm_ptw_top(kvm->arch.pgd, start_gfn << PAGE_SHIFT,
					end_gfn << PAGE_SHIFT, &ctx);

	/* Flush vpid for each vCPU individually */
	if (ret)
		kvm_flush_remote_tlbs(kvm);

	/*
	 * free pte table page after mmu_lock
	 * the pte table page is linked together with ctx.list
	 */
	list_for_each_safe(pos, temp, &ctx.list) {
		list_del(pos);
		free_page((unsigned long)pos);
	}
}

/*
 * kvm_mkclean_gpa_pt() - Make a range of guest physical addresses clean.
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
static int kvm_mkclean_gpa_pt(struct kvm *kvm, gfn_t start_gfn, gfn_t end_gfn)
{
	kvm_ptw_ctx ctx;

	ctx.ops = kvm_mkclean_pte;
	ctx.flag = 0;
	kvm_ptw_prepare(kvm, &ctx);
	return kvm_ptw_top(kvm->arch.pgd, start_gfn << PAGE_SHIFT, end_gfn << PAGE_SHIFT, &ctx);
}

/*
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
		struct kvm_memory_slot *slot, gfn_t gfn_offset, unsigned long mask)
{
	kvm_ptw_ctx ctx;
	gfn_t base_gfn = slot->base_gfn + gfn_offset;
	gfn_t start = base_gfn + __ffs(mask);
	gfn_t end = base_gfn + __fls(mask) + 1;

	ctx.ops = kvm_mkclean_pte;
	ctx.flag = _KVM_HAS_PGMASK;
	ctx.mask = mask;
	ctx.gfn = base_gfn;
	kvm_ptw_prepare(kvm, &ctx);

	kvm_ptw_top(kvm->arch.pgd, start << PAGE_SHIFT, end << PAGE_SHIFT, &ctx);
}

int kvm_arch_prepare_memory_region(struct kvm *kvm, const struct kvm_memory_slot *old,
				   struct kvm_memory_slot *new, enum kvm_mr_change change)
{
	gpa_t gpa_start;
	hva_t hva_start;
	size_t size, gpa_offset, hva_offset;

	if ((change != KVM_MR_MOVE) && (change != KVM_MR_CREATE))
		return 0;
	/*
	 * Prevent userspace from creating a memory region outside of the
	 * VM GPA address space
	 */
	if ((new->base_gfn + new->npages) > (kvm->arch.gpa_size >> PAGE_SHIFT))
		return -ENOMEM;

	new->arch.flags = 0;
	size = new->npages * PAGE_SIZE;
	gpa_start = new->base_gfn << PAGE_SHIFT;
	hva_start = new->userspace_addr;
	if (IS_ALIGNED(size, PMD_SIZE) && IS_ALIGNED(gpa_start, PMD_SIZE)
			&& IS_ALIGNED(hva_start, PMD_SIZE))
		new->arch.flags |= KVM_MEM_HUGEPAGE_CAPABLE;
	else {
		/*
		 * Pages belonging to memslots that don't have the same
		 * alignment within a PMD for userspace and GPA cannot be
		 * mapped with PMD entries, because we'll end up mapping
		 * the wrong pages.
		 *
		 * Consider a layout like the following:
		 *
		 *    memslot->userspace_addr:
		 *    +-----+--------------------+--------------------+---+
		 *    |abcde|fgh  Stage-1 block  |    Stage-1 block tv|xyz|
		 *    +-----+--------------------+--------------------+---+
		 *
		 *    memslot->base_gfn << PAGE_SIZE:
		 *      +---+--------------------+--------------------+-----+
		 *      |abc|def  Stage-2 block  |    Stage-2 block   |tvxyz|
		 *      +---+--------------------+--------------------+-----+
		 *
		 * If we create those stage-2 blocks, we'll end up with this
		 * incorrect mapping:
		 *   d -> f
		 *   e -> g
		 *   f -> h
		 */
		gpa_offset = gpa_start & (PMD_SIZE - 1);
		hva_offset = hva_start & (PMD_SIZE - 1);
		if (gpa_offset != hva_offset) {
			new->arch.flags |= KVM_MEM_HUGEPAGE_INCAPABLE;
		} else {
			if (gpa_offset == 0)
				gpa_offset = PMD_SIZE;
			if ((size + gpa_offset) < (PMD_SIZE * 2))
				new->arch.flags |= KVM_MEM_HUGEPAGE_INCAPABLE;
		}
	}

	return 0;
}

void kvm_arch_commit_memory_region(struct kvm *kvm,
				   struct kvm_memory_slot *old,
				   const struct kvm_memory_slot *new,
				   enum kvm_mr_change change)
{
	int needs_flush;

	/*
	 * If dirty page logging is enabled, write protect all pages in the slot
	 * ready for dirty logging.
	 *
	 * There is no need to do this in any of the following cases:
	 * CREATE:	No dirty mappings will already exist.
	 * MOVE/DELETE:	The old mappings will already have been cleaned up by
	 *		kvm_arch_flush_shadow_memslot()
	 */
	if (change == KVM_MR_FLAGS_ONLY &&
	    (!(old->flags & KVM_MEM_LOG_DIRTY_PAGES) &&
	     new->flags & KVM_MEM_LOG_DIRTY_PAGES)) {
		spin_lock(&kvm->mmu_lock);
		/* Write protect GPA page table entries */
		needs_flush = kvm_mkclean_gpa_pt(kvm, new->base_gfn,
					new->base_gfn + new->npages);
		spin_unlock(&kvm->mmu_lock);
		if (needs_flush)
			kvm_flush_remote_tlbs(kvm);
	}
}

void kvm_arch_flush_shadow_all(struct kvm *kvm)
{
	kvm_flush_range(kvm, 0, kvm->arch.gpa_size >> PAGE_SHIFT, 0);
}

void kvm_arch_flush_shadow_memslot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	/*
	 * The slot has been made invalid (ready for moving or deletion), so we
	 * need to ensure that it can no longer be accessed by any guest vCPUs.
	 */
	kvm_flush_range(kvm, slot->base_gfn, slot->base_gfn + slot->npages, 1);
}

bool kvm_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	kvm_ptw_ctx ctx;

	ctx.flag = 0;
	ctx.ops = kvm_flush_pte;
	kvm_ptw_prepare(kvm, &ctx);
	INIT_LIST_HEAD(&ctx.list);

	return kvm_ptw_top(kvm->arch.pgd, range->start << PAGE_SHIFT,
			range->end << PAGE_SHIFT, &ctx);
}

bool kvm_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	kvm_ptw_ctx ctx;

	ctx.flag = 0;
	ctx.ops = kvm_mkold_pte;
	kvm_ptw_prepare(kvm, &ctx);

	return kvm_ptw_top(kvm->arch.pgd, range->start << PAGE_SHIFT,
				range->end << PAGE_SHIFT, &ctx);
}

bool kvm_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	gpa_t gpa = range->start << PAGE_SHIFT;
	kvm_pte_t *ptep = kvm_populate_gpa(kvm, NULL, gpa, 0);

	if (ptep && kvm_pte_present(NULL, ptep) && kvm_pte_young(*ptep))
		return true;

	return false;
}

/*
 * kvm_map_page_fast() - Fast path GPA fault handler.
 * @vcpu:		vCPU pointer.
 * @gpa:		Guest physical address of fault.
 * @write:	Whether the fault was due to a write.
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
static int kvm_map_page_fast(struct kvm_vcpu *vcpu, unsigned long gpa, bool write)
{
	int ret = 0;
	kvm_pfn_t pfn = 0;
	kvm_pte_t *ptep, changed, new;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_memory_slot *slot;

	spin_lock(&kvm->mmu_lock);

	/* Fast path - just check GPA page table for an existing entry */
	ptep = kvm_populate_gpa(kvm, NULL, gpa, 0);
	if (!ptep || !kvm_pte_present(NULL, ptep)) {
		ret = -EFAULT;
		goto out;
	}

	/* Track access to pages marked old */
	new = *ptep;
	if (!kvm_pte_young(new))
		new = kvm_pte_mkyoung(new);
		/* call kvm_set_pfn_accessed() after unlock */

	if (write && !kvm_pte_dirty(new)) {
		if (!kvm_pte_write(new)) {
			ret = -EFAULT;
			goto out;
		}

		if (kvm_pte_huge(new)) {
			/*
			 * Do not set write permission when dirty logging is
			 * enabled for HugePages
			 */
			slot = gfn_to_memslot(kvm, gfn);
			if (kvm_slot_dirty_track_enabled(slot)) {
				ret = -EFAULT;
				goto out;
			}
		}

		/* Track dirtying of writeable pages */
		new = kvm_pte_mkdirty(new);
	}

	changed = new ^ (*ptep);
	if (changed) {
		kvm_set_pte(ptep, new);
		pfn = kvm_pte_pfn(new);
	}
	spin_unlock(&kvm->mmu_lock);

	/*
	 * Fixme: pfn may be freed after mmu_lock
	 * kvm_try_get_pfn(pfn)/kvm_release_pfn pair to prevent this?
	 */
	if (kvm_pte_young(changed))
		kvm_set_pfn_accessed(pfn);

	if (kvm_pte_dirty(changed)) {
		mark_page_dirty(kvm, gfn);
		kvm_set_pfn_dirty(pfn);
	}
	return ret;
out:
	spin_unlock(&kvm->mmu_lock);
	return ret;
}

static bool fault_supports_huge_mapping(struct kvm_memory_slot *memslot,
				unsigned long hva, bool write)
{
	hva_t start, end;

	/* Disable dirty logging on HugePages */
	if (kvm_slot_dirty_track_enabled(memslot) && write)
		return false;

	if (kvm_hugepage_capable(memslot))
		return true;

	if (kvm_hugepage_incapable(memslot))
		return false;

	start = memslot->userspace_addr;
	end = start + memslot->npages * PAGE_SIZE;

	/*
	 * Next, let's make sure we're not trying to map anything not covered
	 * by the memslot. This means we have to prohibit block size mappings
	 * for the beginning and end of a non-block aligned and non-block sized
	 * memory slot (illustrated by the head and tail parts of the
	 * userspace view above containing pages 'abcde' and 'xyz',
	 * respectively).
	 *
	 * Note that it doesn't matter if we do the check using the
	 * userspace_addr or the base_gfn, as both are equally aligned (per
	 * the check above) and equally sized.
	 */
	return (hva >= ALIGN(start, PMD_SIZE)) && (hva < ALIGN_DOWN(end, PMD_SIZE));
}

/*
 * Lookup the mapping level for @gfn in the current mm.
 *
 * WARNING!  Use of host_pfn_mapping_level() requires the caller and the end
 * consumer to be tied into KVM's handlers for MMU notifier events!
 *
 * There are several ways to safely use this helper:
 *
 * - Check mmu_invalidate_retry_gfn() after grabbing the mapping level, before
 *   consuming it.  In this case, mmu_lock doesn't need to be held during the
 *   lookup, but it does need to be held while checking the MMU notifier.
 *
 * - Hold mmu_lock AND ensure there is no in-progress MMU notifier invalidation
 *   event for the hva.  This can be done by explicit checking the MMU notifier
 *   or by ensuring that KVM already has a valid mapping that covers the hva.
 *
 * - Do not use the result to install new mappings, e.g. use the host mapping
 *   level only to decide whether or not to zap an entry.  In this case, it's
 *   not required to hold mmu_lock (though it's highly likely the caller will
 *   want to hold mmu_lock anyways, e.g. to modify SPTEs).
 *
 * Note!  The lookup can still race with modifications to host page tables, but
 * the above "rules" ensure KVM will not _consume_ the result of the walk if a
 * race with the primary MMU occurs.
 */
static int host_pfn_mapping_level(struct kvm *kvm, gfn_t gfn,
				const struct kvm_memory_slot *slot)
{
	int level = 0;
	unsigned long hva;
	unsigned long flags;
	pgd_t pgd;
	p4d_t p4d;
	pud_t pud;
	pmd_t pmd;

	/*
	 * Note, using the already-retrieved memslot and __gfn_to_hva_memslot()
	 * is not solely for performance, it's also necessary to avoid the
	 * "writable" check in __gfn_to_hva_many(), which will always fail on
	 * read-only memslots due to gfn_to_hva() assuming writes.  Earlier
	 * page fault steps have already verified the guest isn't writing a
	 * read-only memslot.
	 */
	hva = __gfn_to_hva_memslot(slot, gfn);

	/*
	 * Disable IRQs to prevent concurrent tear down of host page tables,
	 * e.g. if the primary MMU promotes a P*D to a huge page and then frees
	 * the original page table.
	 */
	local_irq_save(flags);

	/*
	 * Read each entry once.  As above, a non-leaf entry can be promoted to
	 * a huge page _during_ this walk.  Re-reading the entry could send the
	 * walk into the weeks, e.g. p*d_leaf() returns false (sees the old
	 * value) and then p*d_offset() walks into the target huge page instead
	 * of the old page table (sees the new value).
	 */
	pgd = READ_ONCE(*pgd_offset(kvm->mm, hva));
	if (pgd_none(pgd))
		goto out;

	p4d = READ_ONCE(*p4d_offset(&pgd, hva));
	if (p4d_none(p4d) || !p4d_present(p4d))
		goto out;

	pud = READ_ONCE(*pud_offset(&p4d, hva));
	if (pud_none(pud) || !pud_present(pud))
		goto out;

	pmd = READ_ONCE(*pmd_offset(&pud, hva));
	if (pmd_none(pmd) || !pmd_present(pmd))
		goto out;

	if (kvm_pte_huge(pmd_val(pmd)))
		level = 1;

out:
	local_irq_restore(flags);
	return level;
}

/*
 * Split huge page
 */
static kvm_pte_t *kvm_split_huge(struct kvm_vcpu *vcpu, kvm_pte_t *ptep, gfn_t gfn)
{
	int i;
	kvm_pte_t val, *child;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_memory_cache *memcache;

	memcache = &vcpu->arch.mmu_page_cache;
	child = kvm_mmu_memory_cache_alloc(memcache);
	val = kvm_pte_mksmall(*ptep);
	for (i = 0; i < PTRS_PER_PTE; i++) {
		kvm_set_pte(child + i, val);
		val += PAGE_SIZE;
	}

	/* The later kvm_flush_tlb_gpa() will flush hugepage tlb */
	kvm_set_pte(ptep, __pa(child));

	kvm->stat.hugepages--;
	kvm->stat.pages += PTRS_PER_PTE;

	return child + (gfn & (PTRS_PER_PTE - 1));
}

/*
 * kvm_map_page() - Map a guest physical page.
 * @vcpu:		vCPU pointer.
 * @gpa:		Guest physical address of fault.
 * @write:	Whether the fault was due to a write.
 *
 * Handle GPA faults by creating a new GPA mapping (or updating an existing
 * one).
 *
 * This takes care of marking pages young or dirty (idle/dirty page tracking),
 * asking KVM for the corresponding PFN, and creating a mapping in the GPA page
 * tables. Derived mappings (GVA page tables and TLBs) must be handled by the
 * caller.
 *
 * Returns:	0 on success
 *		-EFAULT if there is no memory region at @gpa or a write was
 *		attempted to a read-only memory region. This is usually handled
 *		as an MMIO access.
 */
static int kvm_map_page(struct kvm_vcpu *vcpu, unsigned long gpa, bool write)
{
	bool writeable;
	int srcu_idx, err, retry_no = 0, level;
	unsigned long hva, mmu_seq, prot_bits;
	kvm_pfn_t pfn;
	kvm_pte_t *ptep, new_pte;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_memory_slot *memslot;
	struct kvm_mmu_memory_cache *memcache = &vcpu->arch.mmu_page_cache;

	/* Try the fast path to handle old / clean pages */
	srcu_idx = srcu_read_lock(&kvm->srcu);
	err = kvm_map_page_fast(vcpu, gpa, write);
	if (!err)
		goto out;

	memslot = gfn_to_memslot(kvm, gfn);
	hva = gfn_to_hva_memslot_prot(memslot, gfn, &writeable);
	if (kvm_is_error_hva(hva) || (write && !writeable)) {
		err = -EFAULT;
		goto out;
	}

	/* We need a minimum of cached pages ready for page table creation */
	err = kvm_mmu_topup_memory_cache(memcache, KVM_MMU_CACHE_MIN_PAGES);
	if (err)
		goto out;

retry:
	/*
	 * Used to check for invalidations in progress, of the pfn that is
	 * returned by pfn_to_pfn_prot below.
	 */
	mmu_seq = kvm->mmu_invalidate_seq;
	/*
	 * Ensure the read of mmu_invalidate_seq isn't reordered with PTE reads in
	 * gfn_to_pfn_prot() (which calls get_user_pages()), so that we don't
	 * risk the page we get a reference to getting unmapped before we have a
	 * chance to grab the mmu_lock without mmu_invalidate_retry() noticing.
	 *
	 * This smp_rmb() pairs with the effective smp_wmb() of the combination
	 * of the pte_unmap_unlock() after the PTE is zapped, and the
	 * spin_lock() in kvm_mmu_invalidate_invalidate_<page|range_end>() before
	 * mmu_invalidate_seq is incremented.
	 */
	smp_rmb();

	/* Slow path - ask KVM core whether we can access this GPA */
	pfn = gfn_to_pfn_prot(kvm, gfn, write, &writeable);
	if (is_error_noslot_pfn(pfn)) {
		err = -EFAULT;
		goto out;
	}

	/* Check if an invalidation has taken place since we got pfn */
	spin_lock(&kvm->mmu_lock);
	if (mmu_invalidate_retry_gfn(kvm, mmu_seq, gfn)) {
		/*
		 * This can happen when mappings are changed asynchronously, but
		 * also synchronously if a COW is triggered by
		 * gfn_to_pfn_prot().
		 */
		spin_unlock(&kvm->mmu_lock);
		kvm_release_pfn_clean(pfn);
		if (retry_no > 100) {
			retry_no = 0;
			schedule();
		}
		retry_no++;
		goto retry;
	}

	/*
	 * For emulated devices such virtio device, actual cache attribute is
	 * determined by physical machine.
	 * For pass through physical device, it should be uncachable
	 */
	prot_bits = _PAGE_PRESENT | __READABLE;
	if (pfn_valid(pfn))
		prot_bits |= _CACHE_CC;
	else
		prot_bits |= _CACHE_SUC;

	if (writeable) {
		prot_bits |= _PAGE_WRITE;
		if (write)
			prot_bits |= __WRITEABLE;
	}

	/* Disable dirty logging on HugePages */
	level = 0;
	if (!fault_supports_huge_mapping(memslot, hva, write)) {
		level = 0;
	} else {
		level = host_pfn_mapping_level(kvm, gfn, memslot);
		if (level == 1) {
			gfn = gfn & ~(PTRS_PER_PTE - 1);
			pfn = pfn & ~(PTRS_PER_PTE - 1);
		}
	}

	/* Ensure page tables are allocated */
	ptep = kvm_populate_gpa(kvm, memcache, gpa, level);
	new_pte = kvm_pfn_pte(pfn, __pgprot(prot_bits));
	if (level == 1) {
		new_pte = kvm_pte_mkhuge(new_pte);
		/*
		 * previous pmd entry is invalid_pte_table
		 * there is invalid tlb with small page
		 * need flush these invalid tlbs for current vcpu
		 */
		kvm_make_request(KVM_REQ_TLB_FLUSH, vcpu);
		++kvm->stat.hugepages;
	}  else if (kvm_pte_huge(*ptep) && write)
		ptep = kvm_split_huge(vcpu, ptep, gfn);
	else
		++kvm->stat.pages;
	kvm_set_pte(ptep, new_pte);
	spin_unlock(&kvm->mmu_lock);

	if (prot_bits & _PAGE_DIRTY) {
		mark_page_dirty_in_slot(kvm, memslot, gfn);
		kvm_set_pfn_dirty(pfn);
	}

	kvm_set_pfn_accessed(pfn);
	kvm_release_pfn_clean(pfn);
out:
	srcu_read_unlock(&kvm->srcu, srcu_idx);
	return err;
}

int kvm_handle_mm_fault(struct kvm_vcpu *vcpu, unsigned long gpa, bool write)
{
	int ret;

	ret = kvm_map_page(vcpu, gpa, write);
	if (ret)
		return ret;

	/* Invalidate this entry in the TLB */
	kvm_flush_tlb_gpa(vcpu, gpa);

	return 0;
}

void kvm_arch_sync_dirty_log(struct kvm *kvm, struct kvm_memory_slot *memslot)
{
}

void kvm_arch_flush_remote_tlbs_memslot(struct kvm *kvm,
					const struct kvm_memory_slot *memslot)
{
	kvm_flush_remote_tlbs(kvm);
}
