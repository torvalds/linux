// SPDX-License-Identifier: GPL-2.0

#include "mmu.h"
#include "mmu_internal.h"
#include "mmutrace.h"
#include "tdp_iter.h"
#include "tdp_mmu.h"
#include "spte.h"

#ifdef CONFIG_X86_64
static bool __read_mostly tdp_mmu_enabled = false;
module_param_named(tdp_mmu, tdp_mmu_enabled, bool, 0644);
#endif

static bool is_tdp_mmu_enabled(void)
{
#ifdef CONFIG_X86_64
	return tdp_enabled && READ_ONCE(tdp_mmu_enabled);
#else
	return false;
#endif /* CONFIG_X86_64 */
}

/* Initializes the TDP MMU for the VM, if enabled. */
void kvm_mmu_init_tdp_mmu(struct kvm *kvm)
{
	if (!is_tdp_mmu_enabled())
		return;

	/* This should not be changed for the lifetime of the VM. */
	kvm->arch.tdp_mmu_enabled = true;

	INIT_LIST_HEAD(&kvm->arch.tdp_mmu_roots);
	INIT_LIST_HEAD(&kvm->arch.tdp_mmu_pages);
}

void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm)
{
	if (!kvm->arch.tdp_mmu_enabled)
		return;

	WARN_ON(!list_empty(&kvm->arch.tdp_mmu_roots));
}

static void tdp_mmu_put_root(struct kvm *kvm, struct kvm_mmu_page *root)
{
	if (kvm_mmu_put_root(kvm, root))
		kvm_tdp_mmu_free_root(kvm, root);
}

static inline bool tdp_mmu_next_root_valid(struct kvm *kvm,
					   struct kvm_mmu_page *root)
{
	lockdep_assert_held(&kvm->mmu_lock);

	if (list_entry_is_head(root, &kvm->arch.tdp_mmu_roots, link))
		return false;

	kvm_mmu_get_root(kvm, root);
	return true;

}

static inline struct kvm_mmu_page *tdp_mmu_next_root(struct kvm *kvm,
						     struct kvm_mmu_page *root)
{
	struct kvm_mmu_page *next_root;

	next_root = list_next_entry(root, link);
	tdp_mmu_put_root(kvm, root);
	return next_root;
}

/*
 * Note: this iterator gets and puts references to the roots it iterates over.
 * This makes it safe to release the MMU lock and yield within the loop, but
 * if exiting the loop early, the caller must drop the reference to the most
 * recent root. (Unless keeping a live reference is desirable.)
 */
#define for_each_tdp_mmu_root_yield_safe(_kvm, _root)				\
	for (_root = list_first_entry(&_kvm->arch.tdp_mmu_roots,	\
				      typeof(*_root), link);		\
	     tdp_mmu_next_root_valid(_kvm, _root);			\
	     _root = tdp_mmu_next_root(_kvm, _root))

#define for_each_tdp_mmu_root(_kvm, _root)				\
	list_for_each_entry(_root, &_kvm->arch.tdp_mmu_roots, link)

bool is_tdp_mmu_root(struct kvm *kvm, hpa_t hpa)
{
	struct kvm_mmu_page *sp;

	if (!kvm->arch.tdp_mmu_enabled)
		return false;
	if (WARN_ON(!VALID_PAGE(hpa)))
		return false;

	sp = to_shadow_page(hpa);
	if (WARN_ON(!sp))
		return false;

	return sp->tdp_mmu_page && sp->root_count;
}

static bool zap_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
			  gfn_t start, gfn_t end, bool can_yield, bool flush);

void kvm_tdp_mmu_free_root(struct kvm *kvm, struct kvm_mmu_page *root)
{
	gfn_t max_gfn = 1ULL << (shadow_phys_bits - PAGE_SHIFT);

	lockdep_assert_held(&kvm->mmu_lock);

	WARN_ON(root->root_count);
	WARN_ON(!root->tdp_mmu_page);

	list_del(&root->link);

	zap_gfn_range(kvm, root, 0, max_gfn, false, false);

	free_page((unsigned long)root->spt);
	kmem_cache_free(mmu_page_header_cache, root);
}

static union kvm_mmu_page_role page_role_for_level(struct kvm_vcpu *vcpu,
						   int level)
{
	union kvm_mmu_page_role role;

	role = vcpu->arch.mmu->mmu_role.base;
	role.level = level;
	role.direct = true;
	role.gpte_is_8_bytes = true;
	role.access = ACC_ALL;

	return role;
}

static struct kvm_mmu_page *alloc_tdp_mmu_page(struct kvm_vcpu *vcpu, gfn_t gfn,
					       int level)
{
	struct kvm_mmu_page *sp;

	sp = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_page_header_cache);
	sp->spt = kvm_mmu_memory_cache_alloc(&vcpu->arch.mmu_shadow_page_cache);
	set_page_private(virt_to_page(sp->spt), (unsigned long)sp);

	sp->role.word = page_role_for_level(vcpu, level).word;
	sp->gfn = gfn;
	sp->tdp_mmu_page = true;

	return sp;
}

static struct kvm_mmu_page *get_tdp_mmu_vcpu_root(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_page_role role;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_page *root;

	role = page_role_for_level(vcpu, vcpu->arch.mmu->shadow_root_level);

	spin_lock(&kvm->mmu_lock);

	/* Check for an existing root before allocating a new one. */
	for_each_tdp_mmu_root(kvm, root) {
		if (root->role.word == role.word) {
			kvm_mmu_get_root(kvm, root);
			spin_unlock(&kvm->mmu_lock);
			return root;
		}
	}

	root = alloc_tdp_mmu_page(vcpu, 0, vcpu->arch.mmu->shadow_root_level);
	root->root_count = 1;

	list_add(&root->link, &kvm->arch.tdp_mmu_roots);

	spin_unlock(&kvm->mmu_lock);

	return root;
}

hpa_t kvm_tdp_mmu_get_vcpu_root_hpa(struct kvm_vcpu *vcpu)
{
	struct kvm_mmu_page *root;

	root = get_tdp_mmu_vcpu_root(vcpu);
	if (!root)
		return INVALID_PAGE;

	return __pa(root->spt);
}

static void handle_changed_spte(struct kvm *kvm, int as_id, gfn_t gfn,
				u64 old_spte, u64 new_spte, int level);

static int kvm_mmu_page_as_id(struct kvm_mmu_page *sp)
{
	return sp->role.smm ? 1 : 0;
}

static void handle_changed_spte_acc_track(u64 old_spte, u64 new_spte, int level)
{
	bool pfn_changed = spte_to_pfn(old_spte) != spte_to_pfn(new_spte);

	if (!is_shadow_present_pte(old_spte) || !is_last_spte(old_spte, level))
		return;

	if (is_accessed_spte(old_spte) &&
	    (!is_accessed_spte(new_spte) || pfn_changed))
		kvm_set_pfn_accessed(spte_to_pfn(old_spte));
}

static void handle_changed_spte_dirty_log(struct kvm *kvm, int as_id, gfn_t gfn,
					  u64 old_spte, u64 new_spte, int level)
{
	bool pfn_changed;
	struct kvm_memory_slot *slot;

	if (level > PG_LEVEL_4K)
		return;

	pfn_changed = spte_to_pfn(old_spte) != spte_to_pfn(new_spte);

	if ((!is_writable_pte(old_spte) || pfn_changed) &&
	    is_writable_pte(new_spte)) {
		slot = __gfn_to_memslot(__kvm_memslots(kvm, as_id), gfn);
		mark_page_dirty_in_slot(slot, gfn);
	}
}

/**
 * handle_changed_spte - handle bookkeeping associated with an SPTE change
 * @kvm: kvm instance
 * @as_id: the address space of the paging structure the SPTE was a part of
 * @gfn: the base GFN that was mapped by the SPTE
 * @old_spte: The value of the SPTE before the change
 * @new_spte: The value of the SPTE after the change
 * @level: the level of the PT the SPTE is part of in the paging structure
 *
 * Handle bookkeeping that might result from the modification of a SPTE.
 * This function must be called for all TDP SPTE modifications.
 */
static void __handle_changed_spte(struct kvm *kvm, int as_id, gfn_t gfn,
				u64 old_spte, u64 new_spte, int level)
{
	bool was_present = is_shadow_present_pte(old_spte);
	bool is_present = is_shadow_present_pte(new_spte);
	bool was_leaf = was_present && is_last_spte(old_spte, level);
	bool is_leaf = is_present && is_last_spte(new_spte, level);
	bool pfn_changed = spte_to_pfn(old_spte) != spte_to_pfn(new_spte);
	u64 *pt;
	struct kvm_mmu_page *sp;
	u64 old_child_spte;
	int i;

	WARN_ON(level > PT64_ROOT_MAX_LEVEL);
	WARN_ON(level < PG_LEVEL_4K);
	WARN_ON(gfn & (KVM_PAGES_PER_HPAGE(level) - 1));

	/*
	 * If this warning were to trigger it would indicate that there was a
	 * missing MMU notifier or a race with some notifier handler.
	 * A present, leaf SPTE should never be directly replaced with another
	 * present leaf SPTE pointing to a differnt PFN. A notifier handler
	 * should be zapping the SPTE before the main MM's page table is
	 * changed, or the SPTE should be zeroed, and the TLBs flushed by the
	 * thread before replacement.
	 */
	if (was_leaf && is_leaf && pfn_changed) {
		pr_err("Invalid SPTE change: cannot replace a present leaf\n"
		       "SPTE with another present leaf SPTE mapping a\n"
		       "different PFN!\n"
		       "as_id: %d gfn: %llx old_spte: %llx new_spte: %llx level: %d",
		       as_id, gfn, old_spte, new_spte, level);

		/*
		 * Crash the host to prevent error propagation and guest data
		 * courruption.
		 */
		BUG();
	}

	if (old_spte == new_spte)
		return;

	/*
	 * The only times a SPTE should be changed from a non-present to
	 * non-present state is when an MMIO entry is installed/modified/
	 * removed. In that case, there is nothing to do here.
	 */
	if (!was_present && !is_present) {
		/*
		 * If this change does not involve a MMIO SPTE, it is
		 * unexpected. Log the change, though it should not impact the
		 * guest since both the former and current SPTEs are nonpresent.
		 */
		if (WARN_ON(!is_mmio_spte(old_spte) && !is_mmio_spte(new_spte)))
			pr_err("Unexpected SPTE change! Nonpresent SPTEs\n"
			       "should not be replaced with another,\n"
			       "different nonpresent SPTE, unless one or both\n"
			       "are MMIO SPTEs.\n"
			       "as_id: %d gfn: %llx old_spte: %llx new_spte: %llx level: %d",
			       as_id, gfn, old_spte, new_spte, level);
		return;
	}


	if (was_leaf && is_dirty_spte(old_spte) &&
	    (!is_dirty_spte(new_spte) || pfn_changed))
		kvm_set_pfn_dirty(spte_to_pfn(old_spte));

	/*
	 * Recursively handle child PTs if the change removed a subtree from
	 * the paging structure.
	 */
	if (was_present && !was_leaf && (pfn_changed || !is_present)) {
		pt = spte_to_child_pt(old_spte, level);
		sp = sptep_to_sp(pt);

		list_del(&sp->link);

		if (sp->lpage_disallowed)
			unaccount_huge_nx_page(kvm, sp);

		for (i = 0; i < PT64_ENT_PER_PAGE; i++) {
			old_child_spte = READ_ONCE(*(pt + i));
			WRITE_ONCE(*(pt + i), 0);
			handle_changed_spte(kvm, as_id,
				gfn + (i * KVM_PAGES_PER_HPAGE(level - 1)),
				old_child_spte, 0, level - 1);
		}

		kvm_flush_remote_tlbs_with_address(kvm, gfn,
						   KVM_PAGES_PER_HPAGE(level));

		free_page((unsigned long)pt);
		kmem_cache_free(mmu_page_header_cache, sp);
	}
}

static void handle_changed_spte(struct kvm *kvm, int as_id, gfn_t gfn,
				u64 old_spte, u64 new_spte, int level)
{
	__handle_changed_spte(kvm, as_id, gfn, old_spte, new_spte, level);
	handle_changed_spte_acc_track(old_spte, new_spte, level);
	handle_changed_spte_dirty_log(kvm, as_id, gfn, old_spte,
				      new_spte, level);
}

static inline void __tdp_mmu_set_spte(struct kvm *kvm, struct tdp_iter *iter,
				      u64 new_spte, bool record_acc_track,
				      bool record_dirty_log)
{
	u64 *root_pt = tdp_iter_root_pt(iter);
	struct kvm_mmu_page *root = sptep_to_sp(root_pt);
	int as_id = kvm_mmu_page_as_id(root);

	WRITE_ONCE(*iter->sptep, new_spte);

	__handle_changed_spte(kvm, as_id, iter->gfn, iter->old_spte, new_spte,
			      iter->level);
	if (record_acc_track)
		handle_changed_spte_acc_track(iter->old_spte, new_spte,
					      iter->level);
	if (record_dirty_log)
		handle_changed_spte_dirty_log(kvm, as_id, iter->gfn,
					      iter->old_spte, new_spte,
					      iter->level);
}

static inline void tdp_mmu_set_spte(struct kvm *kvm, struct tdp_iter *iter,
				    u64 new_spte)
{
	__tdp_mmu_set_spte(kvm, iter, new_spte, true, true);
}

static inline void tdp_mmu_set_spte_no_acc_track(struct kvm *kvm,
						 struct tdp_iter *iter,
						 u64 new_spte)
{
	__tdp_mmu_set_spte(kvm, iter, new_spte, false, true);
}

static inline void tdp_mmu_set_spte_no_dirty_log(struct kvm *kvm,
						 struct tdp_iter *iter,
						 u64 new_spte)
{
	__tdp_mmu_set_spte(kvm, iter, new_spte, true, false);
}

#define tdp_root_for_each_pte(_iter, _root, _start, _end) \
	for_each_tdp_pte(_iter, _root->spt, _root->role.level, _start, _end)

#define tdp_root_for_each_leaf_pte(_iter, _root, _start, _end)	\
	tdp_root_for_each_pte(_iter, _root, _start, _end)		\
		if (!is_shadow_present_pte(_iter.old_spte) ||		\
		    !is_last_spte(_iter.old_spte, _iter.level))		\
			continue;					\
		else

#define tdp_mmu_for_each_pte(_iter, _mmu, _start, _end)		\
	for_each_tdp_pte(_iter, __va(_mmu->root_hpa),		\
			 _mmu->shadow_root_level, _start, _end)

/*
 * Yield if the MMU lock is contended or this thread needs to return control
 * to the scheduler.
 *
 * If this function should yield and flush is set, it will perform a remote
 * TLB flush before yielding.
 *
 * If this function yields, it will also reset the tdp_iter's walk over the
 * paging structure and the calling function should skip to the next
 * iteration to allow the iterator to continue its traversal from the
 * paging structure root.
 *
 * Return true if this function yielded and the iterator's traversal was reset.
 * Return false if a yield was not needed.
 */
static inline bool tdp_mmu_iter_cond_resched(struct kvm *kvm,
					     struct tdp_iter *iter, bool flush)
{
	/* Ensure forward progress has been made before yielding. */
	if (iter->next_last_level_gfn == iter->yielded_gfn)
		return false;

	if (need_resched() || spin_needbreak(&kvm->mmu_lock)) {
		if (flush)
			kvm_flush_remote_tlbs(kvm);

		cond_resched_lock(&kvm->mmu_lock);

		WARN_ON(iter->gfn > iter->next_last_level_gfn);

		tdp_iter_start(iter, iter->pt_path[iter->root_level - 1],
			       iter->root_level, iter->min_level,
			       iter->next_last_level_gfn);

		return true;
	}

	return false;
}

/*
 * Tears down the mappings for the range of gfns, [start, end), and frees the
 * non-root pages mapping GFNs strictly within that range. Returns true if
 * SPTEs have been cleared and a TLB flush is needed before releasing the
 * MMU lock.
 * If can_yield is true, will release the MMU lock and reschedule if the
 * scheduler needs the CPU or there is contention on the MMU lock. If this
 * function cannot yield, it will not release the MMU lock or reschedule and
 * the caller must ensure it does not supply too large a GFN range, or the
 * operation can cause a soft lockup.  Note, in some use cases a flush may be
 * required by prior actions.  Ensure the pending flush is performed prior to
 * yielding.
 */
static bool zap_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
			  gfn_t start, gfn_t end, bool can_yield, bool flush)
{
	struct tdp_iter iter;

	tdp_root_for_each_pte(iter, root, start, end) {
		if (can_yield &&
		    tdp_mmu_iter_cond_resched(kvm, &iter, flush)) {
			flush = false;
			continue;
		}

		if (!is_shadow_present_pte(iter.old_spte))
			continue;

		/*
		 * If this is a non-last-level SPTE that covers a larger range
		 * than should be zapped, continue, and zap the mappings at a
		 * lower level.
		 */
		if ((iter.gfn < start ||
		     iter.gfn + KVM_PAGES_PER_HPAGE(iter.level) > end) &&
		    !is_last_spte(iter.old_spte, iter.level))
			continue;

		tdp_mmu_set_spte(kvm, &iter, 0);
		flush = true;
	}

	return flush;
}

/*
 * Tears down the mappings for the range of gfns, [start, end), and frees the
 * non-root pages mapping GFNs strictly within that range. Returns true if
 * SPTEs have been cleared and a TLB flush is needed before releasing the
 * MMU lock.
 */
bool __kvm_tdp_mmu_zap_gfn_range(struct kvm *kvm, gfn_t start, gfn_t end,
				 bool can_yield)
{
	struct kvm_mmu_page *root;
	bool flush = false;

	for_each_tdp_mmu_root_yield_safe(kvm, root)
		flush = zap_gfn_range(kvm, root, start, end, can_yield, flush);

	return flush;
}

void kvm_tdp_mmu_zap_all(struct kvm *kvm)
{
	gfn_t max_gfn = 1ULL << (shadow_phys_bits - PAGE_SHIFT);
	bool flush;

	flush = kvm_tdp_mmu_zap_gfn_range(kvm, 0, max_gfn);
	if (flush)
		kvm_flush_remote_tlbs(kvm);
}

/*
 * Installs a last-level SPTE to handle a TDP page fault.
 * (NPT/EPT violation/misconfiguration)
 */
static int tdp_mmu_map_handle_target_level(struct kvm_vcpu *vcpu, int write,
					  int map_writable,
					  struct tdp_iter *iter,
					  kvm_pfn_t pfn, bool prefault)
{
	u64 new_spte;
	int ret = RET_PF_FIXED;
	int make_spte_ret = 0;

	if (unlikely(is_noslot_pfn(pfn))) {
		new_spte = make_mmio_spte(vcpu, iter->gfn, ACC_ALL);
		trace_mark_mmio_spte(iter->sptep, iter->gfn, new_spte);
	} else
		make_spte_ret = make_spte(vcpu, ACC_ALL, iter->level, iter->gfn,
					 pfn, iter->old_spte, prefault, true,
					 map_writable, !shadow_accessed_mask,
					 &new_spte);

	if (new_spte == iter->old_spte)
		ret = RET_PF_SPURIOUS;
	else
		tdp_mmu_set_spte(vcpu->kvm, iter, new_spte);

	/*
	 * If the page fault was caused by a write but the page is write
	 * protected, emulation is needed. If the emulation was skipped,
	 * the vCPU would have the same fault again.
	 */
	if (make_spte_ret & SET_SPTE_WRITE_PROTECTED_PT) {
		if (write)
			ret = RET_PF_EMULATE;
		kvm_make_request(KVM_REQ_TLB_FLUSH_CURRENT, vcpu);
	}

	/* If a MMIO SPTE is installed, the MMIO will need to be emulated. */
	if (unlikely(is_mmio_spte(new_spte)))
		ret = RET_PF_EMULATE;

	trace_kvm_mmu_set_spte(iter->level, iter->gfn, iter->sptep);
	if (!prefault)
		vcpu->stat.pf_fixed++;

	return ret;
}

/*
 * Handle a TDP page fault (NPT/EPT violation/misconfiguration) by installing
 * page tables and SPTEs to translate the faulting guest physical address.
 */
int kvm_tdp_mmu_map(struct kvm_vcpu *vcpu, gpa_t gpa, u32 error_code,
		    int map_writable, int max_level, kvm_pfn_t pfn,
		    bool prefault)
{
	bool nx_huge_page_workaround_enabled = is_nx_huge_page_enabled();
	bool write = error_code & PFERR_WRITE_MASK;
	bool exec = error_code & PFERR_FETCH_MASK;
	bool huge_page_disallowed = exec && nx_huge_page_workaround_enabled;
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	struct tdp_iter iter;
	struct kvm_mmu_page *sp;
	u64 *child_pt;
	u64 new_spte;
	int ret;
	gfn_t gfn = gpa >> PAGE_SHIFT;
	int level;
	int req_level;

	if (WARN_ON(!VALID_PAGE(vcpu->arch.mmu->root_hpa)))
		return RET_PF_RETRY;
	if (WARN_ON(!is_tdp_mmu_root(vcpu->kvm, vcpu->arch.mmu->root_hpa)))
		return RET_PF_RETRY;

	level = kvm_mmu_hugepage_adjust(vcpu, gfn, max_level, &pfn,
					huge_page_disallowed, &req_level);

	trace_kvm_mmu_spte_requested(gpa, level, pfn);
	tdp_mmu_for_each_pte(iter, mmu, gfn, gfn + 1) {
		if (nx_huge_page_workaround_enabled)
			disallowed_hugepage_adjust(iter.old_spte, gfn,
						   iter.level, &pfn, &level);

		if (iter.level == level)
			break;

		/*
		 * If there is an SPTE mapping a large page at a higher level
		 * than the target, that SPTE must be cleared and replaced
		 * with a non-leaf SPTE.
		 */
		if (is_shadow_present_pte(iter.old_spte) &&
		    is_large_pte(iter.old_spte)) {
			tdp_mmu_set_spte(vcpu->kvm, &iter, 0);

			kvm_flush_remote_tlbs_with_address(vcpu->kvm, iter.gfn,
					KVM_PAGES_PER_HPAGE(iter.level));

			/*
			 * The iter must explicitly re-read the spte here
			 * because the new value informs the !present
			 * path below.
			 */
			iter.old_spte = READ_ONCE(*iter.sptep);
		}

		if (!is_shadow_present_pte(iter.old_spte)) {
			sp = alloc_tdp_mmu_page(vcpu, iter.gfn, iter.level);
			list_add(&sp->link, &vcpu->kvm->arch.tdp_mmu_pages);
			child_pt = sp->spt;
			clear_page(child_pt);
			new_spte = make_nonleaf_spte(child_pt,
						     !shadow_accessed_mask);

			trace_kvm_mmu_get_page(sp, true);
			if (huge_page_disallowed && req_level >= iter.level)
				account_huge_nx_page(vcpu->kvm, sp);

			tdp_mmu_set_spte(vcpu->kvm, &iter, new_spte);
		}
	}

	if (WARN_ON(iter.level != level))
		return RET_PF_RETRY;

	ret = tdp_mmu_map_handle_target_level(vcpu, write, map_writable, &iter,
					      pfn, prefault);

	return ret;
}

static int kvm_tdp_mmu_handle_hva_range(struct kvm *kvm, unsigned long start,
		unsigned long end, unsigned long data,
		int (*handler)(struct kvm *kvm, struct kvm_memory_slot *slot,
			       struct kvm_mmu_page *root, gfn_t start,
			       gfn_t end, unsigned long data))
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *memslot;
	struct kvm_mmu_page *root;
	int ret = 0;
	int as_id;

	for_each_tdp_mmu_root_yield_safe(kvm, root) {
		as_id = kvm_mmu_page_as_id(root);
		slots = __kvm_memslots(kvm, as_id);
		kvm_for_each_memslot(memslot, slots) {
			unsigned long hva_start, hva_end;
			gfn_t gfn_start, gfn_end;

			hva_start = max(start, memslot->userspace_addr);
			hva_end = min(end, memslot->userspace_addr +
				      (memslot->npages << PAGE_SHIFT));
			if (hva_start >= hva_end)
				continue;
			/*
			 * {gfn(page) | page intersects with [hva_start, hva_end)} =
			 * {gfn_start, gfn_start+1, ..., gfn_end-1}.
			 */
			gfn_start = hva_to_gfn_memslot(hva_start, memslot);
			gfn_end = hva_to_gfn_memslot(hva_end + PAGE_SIZE - 1, memslot);

			ret |= handler(kvm, memslot, root, gfn_start,
				       gfn_end, data);
		}
	}

	return ret;
}

static int zap_gfn_range_hva_wrapper(struct kvm *kvm,
				     struct kvm_memory_slot *slot,
				     struct kvm_mmu_page *root, gfn_t start,
				     gfn_t end, unsigned long unused)
{
	return zap_gfn_range(kvm, root, start, end, false, false);
}

int kvm_tdp_mmu_zap_hva_range(struct kvm *kvm, unsigned long start,
			      unsigned long end)
{
	return kvm_tdp_mmu_handle_hva_range(kvm, start, end, 0,
					    zap_gfn_range_hva_wrapper);
}

/*
 * Mark the SPTEs range of GFNs [start, end) unaccessed and return non-zero
 * if any of the GFNs in the range have been accessed.
 */
static int age_gfn_range(struct kvm *kvm, struct kvm_memory_slot *slot,
			 struct kvm_mmu_page *root, gfn_t start, gfn_t end,
			 unsigned long unused)
{
	struct tdp_iter iter;
	int young = 0;
	u64 new_spte = 0;

	tdp_root_for_each_leaf_pte(iter, root, start, end) {
		/*
		 * If we have a non-accessed entry we don't need to change the
		 * pte.
		 */
		if (!is_accessed_spte(iter.old_spte))
			continue;

		new_spte = iter.old_spte;

		if (spte_ad_enabled(new_spte)) {
			clear_bit((ffs(shadow_accessed_mask) - 1),
				  (unsigned long *)&new_spte);
		} else {
			/*
			 * Capture the dirty status of the page, so that it doesn't get
			 * lost when the SPTE is marked for access tracking.
			 */
			if (is_writable_pte(new_spte))
				kvm_set_pfn_dirty(spte_to_pfn(new_spte));

			new_spte = mark_spte_for_access_track(new_spte);
		}
		new_spte &= ~shadow_dirty_mask;

		tdp_mmu_set_spte_no_acc_track(kvm, &iter, new_spte);
		young = 1;
	}

	return young;
}

int kvm_tdp_mmu_age_hva_range(struct kvm *kvm, unsigned long start,
			      unsigned long end)
{
	return kvm_tdp_mmu_handle_hva_range(kvm, start, end, 0,
					    age_gfn_range);
}

static int test_age_gfn(struct kvm *kvm, struct kvm_memory_slot *slot,
			struct kvm_mmu_page *root, gfn_t gfn, gfn_t unused,
			unsigned long unused2)
{
	struct tdp_iter iter;

	tdp_root_for_each_leaf_pte(iter, root, gfn, gfn + 1)
		if (is_accessed_spte(iter.old_spte))
			return 1;

	return 0;
}

int kvm_tdp_mmu_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	return kvm_tdp_mmu_handle_hva_range(kvm, hva, hva + 1, 0,
					    test_age_gfn);
}

/*
 * Handle the changed_pte MMU notifier for the TDP MMU.
 * data is a pointer to the new pte_t mapping the HVA specified by the MMU
 * notifier.
 * Returns non-zero if a flush is needed before releasing the MMU lock.
 */
static int set_tdp_spte(struct kvm *kvm, struct kvm_memory_slot *slot,
			struct kvm_mmu_page *root, gfn_t gfn, gfn_t unused,
			unsigned long data)
{
	struct tdp_iter iter;
	pte_t *ptep = (pte_t *)data;
	kvm_pfn_t new_pfn;
	u64 new_spte;
	int need_flush = 0;

	WARN_ON(pte_huge(*ptep));

	new_pfn = pte_pfn(*ptep);

	tdp_root_for_each_pte(iter, root, gfn, gfn + 1) {
		if (iter.level != PG_LEVEL_4K)
			continue;

		if (!is_shadow_present_pte(iter.old_spte))
			break;

		tdp_mmu_set_spte(kvm, &iter, 0);

		kvm_flush_remote_tlbs_with_address(kvm, iter.gfn, 1);

		if (!pte_write(*ptep)) {
			new_spte = kvm_mmu_changed_pte_notifier_make_spte(
					iter.old_spte, new_pfn);

			tdp_mmu_set_spte(kvm, &iter, new_spte);
		}

		need_flush = 1;
	}

	if (need_flush)
		kvm_flush_remote_tlbs_with_address(kvm, gfn, 1);

	return 0;
}

int kvm_tdp_mmu_set_spte_hva(struct kvm *kvm, unsigned long address,
			     pte_t *host_ptep)
{
	return kvm_tdp_mmu_handle_hva_range(kvm, address, address + 1,
					    (unsigned long)host_ptep,
					    set_tdp_spte);
}

/*
 * Remove write access from all the SPTEs mapping GFNs [start, end). If
 * skip_4k is set, SPTEs that map 4k pages, will not be write-protected.
 * Returns true if an SPTE has been changed and the TLBs need to be flushed.
 */
static bool wrprot_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
			     gfn_t start, gfn_t end, int min_level)
{
	struct tdp_iter iter;
	u64 new_spte;
	bool spte_set = false;

	BUG_ON(min_level > KVM_MAX_HUGEPAGE_LEVEL);

	for_each_tdp_pte_min_level(iter, root->spt, root->role.level,
				   min_level, start, end) {
		if (tdp_mmu_iter_cond_resched(kvm, &iter, false))
			continue;

		if (!is_shadow_present_pte(iter.old_spte) ||
		    !is_last_spte(iter.old_spte, iter.level))
			continue;

		new_spte = iter.old_spte & ~PT_WRITABLE_MASK;

		tdp_mmu_set_spte_no_dirty_log(kvm, &iter, new_spte);
		spte_set = true;
	}
	return spte_set;
}

/*
 * Remove write access from all the SPTEs mapping GFNs in the memslot. Will
 * only affect leaf SPTEs down to min_level.
 * Returns true if an SPTE has been changed and the TLBs need to be flushed.
 */
bool kvm_tdp_mmu_wrprot_slot(struct kvm *kvm, struct kvm_memory_slot *slot,
			     int min_level)
{
	struct kvm_mmu_page *root;
	int root_as_id;
	bool spte_set = false;

	for_each_tdp_mmu_root_yield_safe(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		spte_set |= wrprot_gfn_range(kvm, root, slot->base_gfn,
			     slot->base_gfn + slot->npages, min_level);
	}

	return spte_set;
}

/*
 * Clear the dirty status of all the SPTEs mapping GFNs in the memslot. If
 * AD bits are enabled, this will involve clearing the dirty bit on each SPTE.
 * If AD bits are not enabled, this will require clearing the writable bit on
 * each SPTE. Returns true if an SPTE has been changed and the TLBs need to
 * be flushed.
 */
static bool clear_dirty_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
			   gfn_t start, gfn_t end)
{
	struct tdp_iter iter;
	u64 new_spte;
	bool spte_set = false;

	tdp_root_for_each_leaf_pte(iter, root, start, end) {
		if (tdp_mmu_iter_cond_resched(kvm, &iter, false))
			continue;

		if (!is_shadow_present_pte(iter.old_spte))
			continue;

		if (spte_ad_need_write_protect(iter.old_spte)) {
			if (is_writable_pte(iter.old_spte))
				new_spte = iter.old_spte & ~PT_WRITABLE_MASK;
			else
				continue;
		} else {
			if (iter.old_spte & shadow_dirty_mask)
				new_spte = iter.old_spte & ~shadow_dirty_mask;
			else
				continue;
		}

		tdp_mmu_set_spte_no_dirty_log(kvm, &iter, new_spte);
		spte_set = true;
	}
	return spte_set;
}

/*
 * Clear the dirty status of all the SPTEs mapping GFNs in the memslot. If
 * AD bits are enabled, this will involve clearing the dirty bit on each SPTE.
 * If AD bits are not enabled, this will require clearing the writable bit on
 * each SPTE. Returns true if an SPTE has been changed and the TLBs need to
 * be flushed.
 */
bool kvm_tdp_mmu_clear_dirty_slot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	struct kvm_mmu_page *root;
	int root_as_id;
	bool spte_set = false;

	for_each_tdp_mmu_root_yield_safe(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		spte_set |= clear_dirty_gfn_range(kvm, root, slot->base_gfn,
				slot->base_gfn + slot->npages);
	}

	return spte_set;
}

/*
 * Clears the dirty status of all the 4k SPTEs mapping GFNs for which a bit is
 * set in mask, starting at gfn. The given memslot is expected to contain all
 * the GFNs represented by set bits in the mask. If AD bits are enabled,
 * clearing the dirty status will involve clearing the dirty bit on each SPTE
 * or, if AD bits are not enabled, clearing the writable bit on each SPTE.
 */
static void clear_dirty_pt_masked(struct kvm *kvm, struct kvm_mmu_page *root,
				  gfn_t gfn, unsigned long mask, bool wrprot)
{
	struct tdp_iter iter;
	u64 new_spte;

	tdp_root_for_each_leaf_pte(iter, root, gfn + __ffs(mask),
				    gfn + BITS_PER_LONG) {
		if (!mask)
			break;

		if (iter.level > PG_LEVEL_4K ||
		    !(mask & (1UL << (iter.gfn - gfn))))
			continue;

		if (wrprot || spte_ad_need_write_protect(iter.old_spte)) {
			if (is_writable_pte(iter.old_spte))
				new_spte = iter.old_spte & ~PT_WRITABLE_MASK;
			else
				continue;
		} else {
			if (iter.old_spte & shadow_dirty_mask)
				new_spte = iter.old_spte & ~shadow_dirty_mask;
			else
				continue;
		}

		tdp_mmu_set_spte_no_dirty_log(kvm, &iter, new_spte);

		mask &= ~(1UL << (iter.gfn - gfn));
	}
}

/*
 * Clears the dirty status of all the 4k SPTEs mapping GFNs for which a bit is
 * set in mask, starting at gfn. The given memslot is expected to contain all
 * the GFNs represented by set bits in the mask. If AD bits are enabled,
 * clearing the dirty status will involve clearing the dirty bit on each SPTE
 * or, if AD bits are not enabled, clearing the writable bit on each SPTE.
 */
void kvm_tdp_mmu_clear_dirty_pt_masked(struct kvm *kvm,
				       struct kvm_memory_slot *slot,
				       gfn_t gfn, unsigned long mask,
				       bool wrprot)
{
	struct kvm_mmu_page *root;
	int root_as_id;

	lockdep_assert_held(&kvm->mmu_lock);
	for_each_tdp_mmu_root(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		clear_dirty_pt_masked(kvm, root, gfn, mask, wrprot);
	}
}

/*
 * Set the dirty status of all the SPTEs mapping GFNs in the memslot. This is
 * only used for PML, and so will involve setting the dirty bit on each SPTE.
 * Returns true if an SPTE has been changed and the TLBs need to be flushed.
 */
static bool set_dirty_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
				gfn_t start, gfn_t end)
{
	struct tdp_iter iter;
	u64 new_spte;
	bool spte_set = false;

	tdp_root_for_each_pte(iter, root, start, end) {
		if (tdp_mmu_iter_cond_resched(kvm, &iter, false))
			continue;

		if (!is_shadow_present_pte(iter.old_spte))
			continue;

		new_spte = iter.old_spte | shadow_dirty_mask;

		tdp_mmu_set_spte(kvm, &iter, new_spte);
		spte_set = true;
	}

	return spte_set;
}

/*
 * Set the dirty status of all the SPTEs mapping GFNs in the memslot. This is
 * only used for PML, and so will involve setting the dirty bit on each SPTE.
 * Returns true if an SPTE has been changed and the TLBs need to be flushed.
 */
bool kvm_tdp_mmu_slot_set_dirty(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	struct kvm_mmu_page *root;
	int root_as_id;
	bool spte_set = false;

	for_each_tdp_mmu_root_yield_safe(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		spte_set |= set_dirty_gfn_range(kvm, root, slot->base_gfn,
				slot->base_gfn + slot->npages);
	}
	return spte_set;
}

/*
 * Clear leaf entries which could be replaced by large mappings, for
 * GFNs within the slot.
 */
static void zap_collapsible_spte_range(struct kvm *kvm,
				       struct kvm_mmu_page *root,
				       gfn_t start, gfn_t end)
{
	struct tdp_iter iter;
	kvm_pfn_t pfn;
	bool spte_set = false;

	tdp_root_for_each_pte(iter, root, start, end) {
		if (tdp_mmu_iter_cond_resched(kvm, &iter, spte_set)) {
			spte_set = false;
			continue;
		}

		if (!is_shadow_present_pte(iter.old_spte) ||
		    !is_last_spte(iter.old_spte, iter.level))
			continue;

		pfn = spte_to_pfn(iter.old_spte);
		if (kvm_is_reserved_pfn(pfn) ||
		    (!PageCompound(pfn_to_page(pfn)) &&
		     !kvm_is_zone_device_pfn(pfn)))
			continue;

		tdp_mmu_set_spte(kvm, &iter, 0);

		spte_set = true;
	}

	if (spte_set)
		kvm_flush_remote_tlbs(kvm);
}

/*
 * Clear non-leaf entries (and free associated page tables) which could
 * be replaced by large mappings, for GFNs within the slot.
 */
void kvm_tdp_mmu_zap_collapsible_sptes(struct kvm *kvm,
				       const struct kvm_memory_slot *slot)
{
	struct kvm_mmu_page *root;
	int root_as_id;

	for_each_tdp_mmu_root_yield_safe(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		zap_collapsible_spte_range(kvm, root, slot->base_gfn,
					   slot->base_gfn + slot->npages);
	}
}

/*
 * Removes write access on the last level SPTE mapping this GFN and unsets the
 * SPTE_MMU_WRITABLE bit to ensure future writes continue to be intercepted.
 * Returns true if an SPTE was set and a TLB flush is needed.
 */
static bool write_protect_gfn(struct kvm *kvm, struct kvm_mmu_page *root,
			      gfn_t gfn)
{
	struct tdp_iter iter;
	u64 new_spte;
	bool spte_set = false;

	tdp_root_for_each_leaf_pte(iter, root, gfn, gfn + 1) {
		new_spte = iter.old_spte &
			~(PT_WRITABLE_MASK | SPTE_MMU_WRITEABLE);

		if (new_spte == iter.old_spte)
			break;

		tdp_mmu_set_spte(kvm, &iter, new_spte);
		spte_set = true;
	}

	return spte_set;
}

/*
 * Removes write access on the last level SPTE mapping this GFN and unsets the
 * SPTE_MMU_WRITABLE bit to ensure future writes continue to be intercepted.
 * Returns true if an SPTE was set and a TLB flush is needed.
 */
bool kvm_tdp_mmu_write_protect_gfn(struct kvm *kvm,
				   struct kvm_memory_slot *slot, gfn_t gfn)
{
	struct kvm_mmu_page *root;
	int root_as_id;
	bool spte_set = false;

	lockdep_assert_held(&kvm->mmu_lock);
	for_each_tdp_mmu_root(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		spte_set |= write_protect_gfn(kvm, root, gfn);
	}
	return spte_set;
}

/*
 * Return the level of the lowest level SPTE added to sptes.
 * That SPTE may be non-present.
 */
int kvm_tdp_mmu_get_walk(struct kvm_vcpu *vcpu, u64 addr, u64 *sptes,
			 int *root_level)
{
	struct tdp_iter iter;
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	gfn_t gfn = addr >> PAGE_SHIFT;
	int leaf = -1;

	*root_level = vcpu->arch.mmu->shadow_root_level;

	tdp_mmu_for_each_pte(iter, mmu, gfn, gfn + 1) {
		leaf = iter.level;
		sptes[leaf - 1] = iter.old_spte;
	}

	return leaf;
}
