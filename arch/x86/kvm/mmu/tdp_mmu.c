// SPDX-License-Identifier: GPL-2.0

#include "mmu.h"
#include "mmu_internal.h"
#include "mmutrace.h"
#include "tdp_iter.h"
#include "tdp_mmu.h"
#include "spte.h"

#include <asm/cmpxchg.h>
#include <trace/events/kvm.h>

static bool __read_mostly tdp_mmu_enabled = false;
module_param_named(tdp_mmu, tdp_mmu_enabled, bool, 0644);

/* Initializes the TDP MMU for the VM, if enabled. */
void kvm_mmu_init_tdp_mmu(struct kvm *kvm)
{
	if (!tdp_enabled || !READ_ONCE(tdp_mmu_enabled))
		return;

	/* This should not be changed for the lifetime of the VM. */
	kvm->arch.tdp_mmu_enabled = true;

	INIT_LIST_HEAD(&kvm->arch.tdp_mmu_roots);
	spin_lock_init(&kvm->arch.tdp_mmu_pages_lock);
	INIT_LIST_HEAD(&kvm->arch.tdp_mmu_pages);
}

void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm)
{
	if (!kvm->arch.tdp_mmu_enabled)
		return;

	WARN_ON(!list_empty(&kvm->arch.tdp_mmu_roots));

	/*
	 * Ensure that all the outstanding RCU callbacks to free shadow pages
	 * can run before the VM is torn down.
	 */
	rcu_barrier();
}

static void tdp_mmu_put_root(struct kvm *kvm, struct kvm_mmu_page *root)
{
	if (kvm_mmu_put_root(kvm, root))
		kvm_tdp_mmu_free_root(kvm, root);
}

static inline bool tdp_mmu_next_root_valid(struct kvm *kvm,
					   struct kvm_mmu_page *root)
{
	lockdep_assert_held_write(&kvm->mmu_lock);

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

static bool zap_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
			  gfn_t start, gfn_t end, bool can_yield, bool flush);

void kvm_tdp_mmu_free_root(struct kvm *kvm, struct kvm_mmu_page *root)
{
	gfn_t max_gfn = 1ULL << (shadow_phys_bits - PAGE_SHIFT);

	lockdep_assert_held_write(&kvm->mmu_lock);

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

	trace_kvm_mmu_get_page(sp, true);

	return sp;
}

static struct kvm_mmu_page *get_tdp_mmu_vcpu_root(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_page_role role;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_page *root;

	role = page_role_for_level(vcpu, vcpu->arch.mmu->shadow_root_level);

	write_lock(&kvm->mmu_lock);

	/* Check for an existing root before allocating a new one. */
	for_each_tdp_mmu_root(kvm, root) {
		if (root->role.word == role.word) {
			kvm_mmu_get_root(kvm, root);
			write_unlock(&kvm->mmu_lock);
			return root;
		}
	}

	root = alloc_tdp_mmu_page(vcpu, 0, vcpu->arch.mmu->shadow_root_level);
	root->root_count = 1;

	list_add(&root->link, &kvm->arch.tdp_mmu_roots);

	write_unlock(&kvm->mmu_lock);

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

static void tdp_mmu_free_sp(struct kvm_mmu_page *sp)
{
	free_page((unsigned long)sp->spt);
	kmem_cache_free(mmu_page_header_cache, sp);
}

/*
 * This is called through call_rcu in order to free TDP page table memory
 * safely with respect to other kernel threads that may be operating on
 * the memory.
 * By only accessing TDP MMU page table memory in an RCU read critical
 * section, and freeing it after a grace period, lockless access to that
 * memory won't use it after it is freed.
 */
static void tdp_mmu_free_sp_rcu_callback(struct rcu_head *head)
{
	struct kvm_mmu_page *sp = container_of(head, struct kvm_mmu_page,
					       rcu_head);

	tdp_mmu_free_sp(sp);
}

static void handle_changed_spte(struct kvm *kvm, int as_id, gfn_t gfn,
				u64 old_spte, u64 new_spte, int level,
				bool shared);

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
		mark_page_dirty_in_slot(kvm, slot, gfn);
	}
}

/**
 * tdp_mmu_link_page - Add a new page to the list of pages used by the TDP MMU
 *
 * @kvm: kvm instance
 * @sp: the new page
 * @shared: This operation may not be running under the exclusive use of
 *	    the MMU lock and the operation must synchronize with other
 *	    threads that might be adding or removing pages.
 * @account_nx: This page replaces a NX large page and should be marked for
 *		eventual reclaim.
 */
static void tdp_mmu_link_page(struct kvm *kvm, struct kvm_mmu_page *sp,
			      bool shared, bool account_nx)
{
	if (shared)
		spin_lock(&kvm->arch.tdp_mmu_pages_lock);
	else
		lockdep_assert_held_write(&kvm->mmu_lock);

	list_add(&sp->link, &kvm->arch.tdp_mmu_pages);
	if (account_nx)
		account_huge_nx_page(kvm, sp);

	if (shared)
		spin_unlock(&kvm->arch.tdp_mmu_pages_lock);
}

/**
 * tdp_mmu_unlink_page - Remove page from the list of pages used by the TDP MMU
 *
 * @kvm: kvm instance
 * @sp: the page to be removed
 * @shared: This operation may not be running under the exclusive use of
 *	    the MMU lock and the operation must synchronize with other
 *	    threads that might be adding or removing pages.
 */
static void tdp_mmu_unlink_page(struct kvm *kvm, struct kvm_mmu_page *sp,
				bool shared)
{
	if (shared)
		spin_lock(&kvm->arch.tdp_mmu_pages_lock);
	else
		lockdep_assert_held_write(&kvm->mmu_lock);

	list_del(&sp->link);
	if (sp->lpage_disallowed)
		unaccount_huge_nx_page(kvm, sp);

	if (shared)
		spin_unlock(&kvm->arch.tdp_mmu_pages_lock);
}

/**
 * handle_removed_tdp_mmu_page - handle a pt removed from the TDP structure
 *
 * @kvm: kvm instance
 * @pt: the page removed from the paging structure
 * @shared: This operation may not be running under the exclusive use
 *	    of the MMU lock and the operation must synchronize with other
 *	    threads that might be modifying SPTEs.
 *
 * Given a page table that has been removed from the TDP paging structure,
 * iterates through the page table to clear SPTEs and free child page tables.
 *
 * Note that pt is passed in as a tdp_ptep_t, but it does not need RCU
 * protection. Since this thread removed it from the paging structure,
 * this thread will be responsible for ensuring the page is freed. Hence the
 * early rcu_dereferences in the function.
 */
static void handle_removed_tdp_mmu_page(struct kvm *kvm, tdp_ptep_t pt,
					bool shared)
{
	struct kvm_mmu_page *sp = sptep_to_sp(rcu_dereference(pt));
	int level = sp->role.level;
	gfn_t base_gfn = sp->gfn;
	u64 old_child_spte;
	u64 *sptep;
	gfn_t gfn;
	int i;

	trace_kvm_mmu_prepare_zap_page(sp);

	tdp_mmu_unlink_page(kvm, sp, shared);

	for (i = 0; i < PT64_ENT_PER_PAGE; i++) {
		sptep = rcu_dereference(pt) + i;
		gfn = base_gfn + (i * KVM_PAGES_PER_HPAGE(level - 1));

		if (shared) {
			/*
			 * Set the SPTE to a nonpresent value that other
			 * threads will not overwrite. If the SPTE was
			 * already marked as removed then another thread
			 * handling a page fault could overwrite it, so
			 * set the SPTE until it is set from some other
			 * value to the removed SPTE value.
			 */
			for (;;) {
				old_child_spte = xchg(sptep, REMOVED_SPTE);
				if (!is_removed_spte(old_child_spte))
					break;
				cpu_relax();
			}
		} else {
			/*
			 * If the SPTE is not MMU-present, there is no backing
			 * page associated with the SPTE and so no side effects
			 * that need to be recorded, and exclusive ownership of
			 * mmu_lock ensures the SPTE can't be made present.
			 * Note, zapping MMIO SPTEs is also unnecessary as they
			 * are guarded by the memslots generation, not by being
			 * unreachable.
			 */
			old_child_spte = READ_ONCE(*sptep);
			if (!is_shadow_present_pte(old_child_spte))
				continue;

			/*
			 * Marking the SPTE as a removed SPTE is not
			 * strictly necessary here as the MMU lock will
			 * stop other threads from concurrently modifying
			 * this SPTE. Using the removed SPTE value keeps
			 * the two branches consistent and simplifies
			 * the function.
			 */
			WRITE_ONCE(*sptep, REMOVED_SPTE);
		}
		handle_changed_spte(kvm, kvm_mmu_page_as_id(sp), gfn,
				    old_child_spte, REMOVED_SPTE, level - 1,
				    shared);
	}

	kvm_flush_remote_tlbs_with_address(kvm, gfn,
					   KVM_PAGES_PER_HPAGE(level));

	call_rcu(&sp->rcu_head, tdp_mmu_free_sp_rcu_callback);
}

/**
 * handle_changed_spte - handle bookkeeping associated with an SPTE change
 * @kvm: kvm instance
 * @as_id: the address space of the paging structure the SPTE was a part of
 * @gfn: the base GFN that was mapped by the SPTE
 * @old_spte: The value of the SPTE before the change
 * @new_spte: The value of the SPTE after the change
 * @level: the level of the PT the SPTE is part of in the paging structure
 * @shared: This operation may not be running under the exclusive use of
 *	    the MMU lock and the operation must synchronize with other
 *	    threads that might be modifying SPTEs.
 *
 * Handle bookkeeping that might result from the modification of a SPTE.
 * This function must be called for all TDP SPTE modifications.
 */
static void __handle_changed_spte(struct kvm *kvm, int as_id, gfn_t gfn,
				  u64 old_spte, u64 new_spte, int level,
				  bool shared)
{
	bool was_present = is_shadow_present_pte(old_spte);
	bool is_present = is_shadow_present_pte(new_spte);
	bool was_leaf = was_present && is_last_spte(old_spte, level);
	bool is_leaf = is_present && is_last_spte(new_spte, level);
	bool pfn_changed = spte_to_pfn(old_spte) != spte_to_pfn(new_spte);

	WARN_ON(level > PT64_ROOT_MAX_LEVEL);
	WARN_ON(level < PG_LEVEL_4K);
	WARN_ON(gfn & (KVM_PAGES_PER_HPAGE(level) - 1));

	/*
	 * If this warning were to trigger it would indicate that there was a
	 * missing MMU notifier or a race with some notifier handler.
	 * A present, leaf SPTE should never be directly replaced with another
	 * present leaf SPTE pointing to a different PFN. A notifier handler
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
		 * corruption.
		 */
		BUG();
	}

	if (old_spte == new_spte)
		return;

	trace_kvm_tdp_mmu_spte_changed(as_id, gfn, level, old_spte, new_spte);

	/*
	 * The only times a SPTE should be changed from a non-present to
	 * non-present state is when an MMIO entry is installed/modified/
	 * removed. In that case, there is nothing to do here.
	 */
	if (!was_present && !is_present) {
		/*
		 * If this change does not involve a MMIO SPTE or removed SPTE,
		 * it is unexpected. Log the change, though it should not
		 * impact the guest since both the former and current SPTEs
		 * are nonpresent.
		 */
		if (WARN_ON(!is_mmio_spte(old_spte) &&
			    !is_mmio_spte(new_spte) &&
			    !is_removed_spte(new_spte)))
			pr_err("Unexpected SPTE change! Nonpresent SPTEs\n"
			       "should not be replaced with another,\n"
			       "different nonpresent SPTE, unless one or both\n"
			       "are MMIO SPTEs, or the new SPTE is\n"
			       "a temporary removed SPTE.\n"
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
	if (was_present && !was_leaf && (pfn_changed || !is_present))
		handle_removed_tdp_mmu_page(kvm,
				spte_to_child_pt(old_spte, level), shared);
}

static void handle_changed_spte(struct kvm *kvm, int as_id, gfn_t gfn,
				u64 old_spte, u64 new_spte, int level,
				bool shared)
{
	__handle_changed_spte(kvm, as_id, gfn, old_spte, new_spte, level,
			      shared);
	handle_changed_spte_acc_track(old_spte, new_spte, level);
	handle_changed_spte_dirty_log(kvm, as_id, gfn, old_spte,
				      new_spte, level);
}

/*
 * tdp_mmu_set_spte_atomic - Set a TDP MMU SPTE atomically and handle the
 * associated bookkeeping
 *
 * @kvm: kvm instance
 * @iter: a tdp_iter instance currently on the SPTE that should be set
 * @new_spte: The value the SPTE should be set to
 * Returns: true if the SPTE was set, false if it was not. If false is returned,
 *	    this function will have no side-effects.
 */
static inline bool tdp_mmu_set_spte_atomic(struct kvm *kvm,
					   struct tdp_iter *iter,
					   u64 new_spte)
{
	lockdep_assert_held_read(&kvm->mmu_lock);

	/*
	 * Do not change removed SPTEs. Only the thread that froze the SPTE
	 * may modify it.
	 */
	if (iter->old_spte == REMOVED_SPTE)
		return false;

	if (cmpxchg64(rcu_dereference(iter->sptep), iter->old_spte,
		      new_spte) != iter->old_spte)
		return false;

	handle_changed_spte(kvm, iter->as_id, iter->gfn, iter->old_spte,
			    new_spte, iter->level, true);

	return true;
}

static inline bool tdp_mmu_zap_spte_atomic(struct kvm *kvm,
					   struct tdp_iter *iter)
{
	/*
	 * Freeze the SPTE by setting it to a special,
	 * non-present value. This will stop other threads from
	 * immediately installing a present entry in its place
	 * before the TLBs are flushed.
	 */
	if (!tdp_mmu_set_spte_atomic(kvm, iter, REMOVED_SPTE))
		return false;

	kvm_flush_remote_tlbs_with_address(kvm, iter->gfn,
					   KVM_PAGES_PER_HPAGE(iter->level));

	/*
	 * No other thread can overwrite the removed SPTE as they
	 * must either wait on the MMU lock or use
	 * tdp_mmu_set_spte_atomic which will not overwrite the
	 * special removed SPTE value. No bookkeeping is needed
	 * here since the SPTE is going from non-present
	 * to non-present.
	 */
	WRITE_ONCE(*rcu_dereference(iter->sptep), 0);

	return true;
}


/*
 * __tdp_mmu_set_spte - Set a TDP MMU SPTE and handle the associated bookkeeping
 * @kvm: kvm instance
 * @iter: a tdp_iter instance currently on the SPTE that should be set
 * @new_spte: The value the SPTE should be set to
 * @record_acc_track: Notify the MM subsystem of changes to the accessed state
 *		      of the page. Should be set unless handling an MMU
 *		      notifier for access tracking. Leaving record_acc_track
 *		      unset in that case prevents page accesses from being
 *		      double counted.
 * @record_dirty_log: Record the page as dirty in the dirty bitmap if
 *		      appropriate for the change being made. Should be set
 *		      unless performing certain dirty logging operations.
 *		      Leaving record_dirty_log unset in that case prevents page
 *		      writes from being double counted.
 */
static inline void __tdp_mmu_set_spte(struct kvm *kvm, struct tdp_iter *iter,
				      u64 new_spte, bool record_acc_track,
				      bool record_dirty_log)
{
	lockdep_assert_held_write(&kvm->mmu_lock);

	/*
	 * No thread should be using this function to set SPTEs to the
	 * temporary removed SPTE value.
	 * If operating under the MMU lock in read mode, tdp_mmu_set_spte_atomic
	 * should be used. If operating under the MMU lock in write mode, the
	 * use of the removed SPTE should not be necessary.
	 */
	WARN_ON(iter->old_spte == REMOVED_SPTE);

	WRITE_ONCE(*rcu_dereference(iter->sptep), new_spte);

	__handle_changed_spte(kvm, iter->as_id, iter->gfn, iter->old_spte,
			      new_spte, iter->level, false);
	if (record_acc_track)
		handle_changed_spte_acc_track(iter->old_spte, new_spte,
					      iter->level);
	if (record_dirty_log)
		handle_changed_spte_dirty_log(kvm, iter->as_id, iter->gfn,
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

	if (need_resched() || rwlock_needbreak(&kvm->mmu_lock)) {
		rcu_read_unlock();

		if (flush)
			kvm_flush_remote_tlbs(kvm);

		cond_resched_rwlock_write(&kvm->mmu_lock);
		rcu_read_lock();

		WARN_ON(iter->gfn > iter->next_last_level_gfn);

		tdp_iter_restart(iter);

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

	rcu_read_lock();

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

	rcu_read_unlock();
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
	int ret = 0;
	int make_spte_ret = 0;

	if (unlikely(is_noslot_pfn(pfn)))
		new_spte = make_mmio_spte(vcpu, iter->gfn, ACC_ALL);
	else
		make_spte_ret = make_spte(vcpu, ACC_ALL, iter->level, iter->gfn,
					 pfn, iter->old_spte, prefault, true,
					 map_writable, !shadow_accessed_mask,
					 &new_spte);

	if (new_spte == iter->old_spte)
		ret = RET_PF_SPURIOUS;
	else if (!tdp_mmu_set_spte_atomic(vcpu->kvm, iter, new_spte))
		return RET_PF_RETRY;

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
	if (unlikely(is_mmio_spte(new_spte))) {
		trace_mark_mmio_spte(rcu_dereference(iter->sptep), iter->gfn,
				     new_spte);
		ret = RET_PF_EMULATE;
	} else
		trace_kvm_mmu_set_spte(iter->level, iter->gfn,
				       rcu_dereference(iter->sptep));

	trace_kvm_mmu_set_spte(iter->level, iter->gfn,
			       rcu_dereference(iter->sptep));
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

	rcu_read_lock();

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
			if (!tdp_mmu_zap_spte_atomic(vcpu->kvm, &iter))
				break;

			/*
			 * The iter must explicitly re-read the spte here
			 * because the new value informs the !present
			 * path below.
			 */
			iter.old_spte = READ_ONCE(*rcu_dereference(iter.sptep));
		}

		if (!is_shadow_present_pte(iter.old_spte)) {
			sp = alloc_tdp_mmu_page(vcpu, iter.gfn, iter.level);
			child_pt = sp->spt;

			new_spte = make_nonleaf_spte(child_pt,
						     !shadow_accessed_mask);

			if (tdp_mmu_set_spte_atomic(vcpu->kvm, &iter,
						    new_spte)) {
				tdp_mmu_link_page(vcpu->kvm, sp, true,
						  huge_page_disallowed &&
						  req_level >= iter.level);

				trace_kvm_mmu_get_page(sp, true);
			} else {
				tdp_mmu_free_sp(sp);
				break;
			}
		}
	}

	if (iter.level != level) {
		rcu_read_unlock();
		return RET_PF_RETRY;
	}

	ret = tdp_mmu_map_handle_target_level(vcpu, write, map_writable, &iter,
					      pfn, prefault);
	rcu_read_unlock();

	return ret;
}

static __always_inline int
kvm_tdp_mmu_handle_hva_range(struct kvm *kvm,
			     unsigned long start,
			     unsigned long end,
			     unsigned long data,
			     int (*handler)(struct kvm *kvm,
					    struct kvm_memory_slot *slot,
					    struct kvm_mmu_page *root,
					    gfn_t start,
					    gfn_t end,
					    unsigned long data))
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

	rcu_read_lock();

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

		trace_kvm_age_page(iter.gfn, iter.level, slot, young);
	}

	rcu_read_unlock();

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

	rcu_read_lock();

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

	rcu_read_unlock();

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

	rcu_read_lock();

	BUG_ON(min_level > KVM_MAX_HUGEPAGE_LEVEL);

	for_each_tdp_pte_min_level(iter, root->spt, root->role.level,
				   min_level, start, end) {
		if (tdp_mmu_iter_cond_resched(kvm, &iter, false))
			continue;

		if (!is_shadow_present_pte(iter.old_spte) ||
		    !is_last_spte(iter.old_spte, iter.level) ||
		    !(iter.old_spte & PT_WRITABLE_MASK))
			continue;

		new_spte = iter.old_spte & ~PT_WRITABLE_MASK;

		tdp_mmu_set_spte_no_dirty_log(kvm, &iter, new_spte);
		spte_set = true;
	}

	rcu_read_unlock();
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

	rcu_read_lock();

	tdp_root_for_each_leaf_pte(iter, root, start, end) {
		if (tdp_mmu_iter_cond_resched(kvm, &iter, false))
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

	rcu_read_unlock();
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

	rcu_read_lock();

	tdp_root_for_each_leaf_pte(iter, root, gfn + __ffs(mask),
				    gfn + BITS_PER_LONG) {
		if (!mask)
			break;

		if (iter.level > PG_LEVEL_4K ||
		    !(mask & (1UL << (iter.gfn - gfn))))
			continue;

		mask &= ~(1UL << (iter.gfn - gfn));

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
	}

	rcu_read_unlock();
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

	lockdep_assert_held_write(&kvm->mmu_lock);
	for_each_tdp_mmu_root(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		clear_dirty_pt_masked(kvm, root, gfn, mask, wrprot);
	}
}

/*
 * Clear leaf entries which could be replaced by large mappings, for
 * GFNs within the slot.
 */
static void zap_collapsible_spte_range(struct kvm *kvm,
				       struct kvm_mmu_page *root,
				       struct kvm_memory_slot *slot)
{
	gfn_t start = slot->base_gfn;
	gfn_t end = start + slot->npages;
	struct tdp_iter iter;
	kvm_pfn_t pfn;
	bool spte_set = false;

	rcu_read_lock();

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
		    iter.level >= kvm_mmu_max_mapping_level(kvm, slot, iter.gfn,
							    pfn, PG_LEVEL_NUM))
			continue;

		tdp_mmu_set_spte(kvm, &iter, 0);

		spte_set = true;
	}

	rcu_read_unlock();
	if (spte_set)
		kvm_flush_remote_tlbs(kvm);
}

/*
 * Clear non-leaf entries (and free associated page tables) which could
 * be replaced by large mappings, for GFNs within the slot.
 */
void kvm_tdp_mmu_zap_collapsible_sptes(struct kvm *kvm,
				       struct kvm_memory_slot *slot)
{
	struct kvm_mmu_page *root;
	int root_as_id;

	for_each_tdp_mmu_root_yield_safe(kvm, root) {
		root_as_id = kvm_mmu_page_as_id(root);
		if (root_as_id != slot->as_id)
			continue;

		zap_collapsible_spte_range(kvm, root, slot);
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

	rcu_read_lock();

	tdp_root_for_each_leaf_pte(iter, root, gfn, gfn + 1) {
		if (!is_writable_pte(iter.old_spte))
			break;

		new_spte = iter.old_spte &
			~(PT_WRITABLE_MASK | SPTE_MMU_WRITEABLE);

		tdp_mmu_set_spte(kvm, &iter, new_spte);
		spte_set = true;
	}

	rcu_read_unlock();

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

	lockdep_assert_held_write(&kvm->mmu_lock);
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

	rcu_read_lock();

	tdp_mmu_for_each_pte(iter, mmu, gfn, gfn + 1) {
		leaf = iter.level;
		sptes[leaf] = iter.old_spte;
	}

	rcu_read_unlock();

	return leaf;
}
