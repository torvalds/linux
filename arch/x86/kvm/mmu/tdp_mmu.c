// SPDX-License-Identifier: GPL-2.0

#include "mmu.h"
#include "mmu_internal.h"
#include "mmutrace.h"
#include "tdp_iter.h"
#include "tdp_mmu.h"
#include "spte.h"

#include <asm/cmpxchg.h>
#include <trace/events/kvm.h>

static bool __read_mostly tdp_mmu_enabled = true;
module_param_named(tdp_mmu, tdp_mmu_enabled, bool, 0644);

/* Initializes the TDP MMU for the VM, if enabled. */
bool kvm_mmu_init_tdp_mmu(struct kvm *kvm)
{
	if (!tdp_enabled || !READ_ONCE(tdp_mmu_enabled))
		return false;

	/* This should not be changed for the lifetime of the VM. */
	kvm->arch.tdp_mmu_enabled = true;

	INIT_LIST_HEAD(&kvm->arch.tdp_mmu_roots);
	spin_lock_init(&kvm->arch.tdp_mmu_pages_lock);
	INIT_LIST_HEAD(&kvm->arch.tdp_mmu_pages);

	return true;
}

static __always_inline void kvm_lockdep_assert_mmu_lock_held(struct kvm *kvm,
							     bool shared)
{
	if (shared)
		lockdep_assert_held_read(&kvm->mmu_lock);
	else
		lockdep_assert_held_write(&kvm->mmu_lock);
}

void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm)
{
	if (!kvm->arch.tdp_mmu_enabled)
		return;

	WARN_ON(!list_empty(&kvm->arch.tdp_mmu_pages));
	WARN_ON(!list_empty(&kvm->arch.tdp_mmu_roots));

	/*
	 * Ensure that all the outstanding RCU callbacks to free shadow pages
	 * can run before the VM is torn down.
	 */
	rcu_barrier();
}

static bool zap_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
			  gfn_t start, gfn_t end, bool can_yield, bool flush,
			  bool shared);

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

void kvm_tdp_mmu_put_root(struct kvm *kvm, struct kvm_mmu_page *root,
			  bool shared)
{
	kvm_lockdep_assert_mmu_lock_held(kvm, shared);

	if (!refcount_dec_and_test(&root->tdp_mmu_root_count))
		return;

	WARN_ON(!root->tdp_mmu_page);

	spin_lock(&kvm->arch.tdp_mmu_pages_lock);
	list_del_rcu(&root->link);
	spin_unlock(&kvm->arch.tdp_mmu_pages_lock);

	zap_gfn_range(kvm, root, 0, -1ull, false, false, shared);

	call_rcu(&root->rcu_head, tdp_mmu_free_sp_rcu_callback);
}

/*
 * Finds the next valid root after root (or the first valid root if root
 * is NULL), takes a reference on it, and returns that next root. If root
 * is not NULL, this thread should have already taken a reference on it, and
 * that reference will be dropped. If no valid root is found, this
 * function will return NULL.
 */
static struct kvm_mmu_page *tdp_mmu_next_root(struct kvm *kvm,
					      struct kvm_mmu_page *prev_root,
					      bool shared)
{
	struct kvm_mmu_page *next_root;

	rcu_read_lock();

	if (prev_root)
		next_root = list_next_or_null_rcu(&kvm->arch.tdp_mmu_roots,
						  &prev_root->link,
						  typeof(*prev_root), link);
	else
		next_root = list_first_or_null_rcu(&kvm->arch.tdp_mmu_roots,
						   typeof(*next_root), link);

	while (next_root && !kvm_tdp_mmu_get_root(kvm, next_root))
		next_root = list_next_or_null_rcu(&kvm->arch.tdp_mmu_roots,
				&next_root->link, typeof(*next_root), link);

	rcu_read_unlock();

	if (prev_root)
		kvm_tdp_mmu_put_root(kvm, prev_root, shared);

	return next_root;
}

/*
 * Note: this iterator gets and puts references to the roots it iterates over.
 * This makes it safe to release the MMU lock and yield within the loop, but
 * if exiting the loop early, the caller must drop the reference to the most
 * recent root. (Unless keeping a live reference is desirable.)
 *
 * If shared is set, this function is operating under the MMU lock in read
 * mode. In the unlikely event that this thread must free a root, the lock
 * will be temporarily dropped and reacquired in write mode.
 */
#define for_each_tdp_mmu_root_yield_safe(_kvm, _root, _as_id, _shared)	\
	for (_root = tdp_mmu_next_root(_kvm, NULL, _shared);		\
	     _root;							\
	     _root = tdp_mmu_next_root(_kvm, _root, _shared))		\
		if (kvm_mmu_page_as_id(_root) != _as_id) {		\
		} else

#define for_each_tdp_mmu_root(_kvm, _root, _as_id)				\
	list_for_each_entry_rcu(_root, &_kvm->arch.tdp_mmu_roots, link,		\
				lockdep_is_held_type(&kvm->mmu_lock, 0) ||	\
				lockdep_is_held(&kvm->arch.tdp_mmu_pages_lock))	\
		if (kvm_mmu_page_as_id(_root) != _as_id) {		\
		} else

static union kvm_mmu_page_role page_role_for_level(struct kvm_vcpu *vcpu,
						   int level)
{
	union kvm_mmu_page_role role;

	role = vcpu->arch.mmu->mmu_role.base;
	role.level = level;
	role.direct = true;
	role.gpte_is_8_bytes = true;
	role.access = ACC_ALL;
	role.ad_disabled = !shadow_accessed_mask;

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

hpa_t kvm_tdp_mmu_get_vcpu_root_hpa(struct kvm_vcpu *vcpu)
{
	union kvm_mmu_page_role role;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_mmu_page *root;

	lockdep_assert_held_write(&kvm->mmu_lock);

	role = page_role_for_level(vcpu, vcpu->arch.mmu->shadow_root_level);

	/* Check for an existing root before allocating a new one. */
	for_each_tdp_mmu_root(kvm, root, kvm_mmu_role_as_id(role)) {
		if (root->role.word == role.word &&
		    kvm_tdp_mmu_get_root(kvm, root))
			goto out;
	}

	root = alloc_tdp_mmu_page(vcpu, 0, vcpu->arch.mmu->shadow_root_level);
	refcount_set(&root->tdp_mmu_root_count, 1);

	spin_lock(&kvm->arch.tdp_mmu_pages_lock);
	list_add_rcu(&root->link, &kvm->arch.tdp_mmu_roots);
	spin_unlock(&kvm->arch.tdp_mmu_pages_lock);

out:
	return __pa(root->spt);
}

static void handle_changed_spte(struct kvm *kvm, int as_id, gfn_t gfn,
				u64 old_spte, u64 new_spte, int level,
				bool shared);

static void handle_changed_spte_acc_track(u64 old_spte, u64 new_spte, int level)
{
	if (!is_shadow_present_pte(old_spte) || !is_last_spte(old_spte, level))
		return;

	if (is_accessed_spte(old_spte) &&
	    (!is_shadow_present_pte(new_spte) || !is_accessed_spte(new_spte) ||
	     spte_to_pfn(old_spte) != spte_to_pfn(new_spte)))
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
 * @account_nx: This page replaces a NX large page and should be marked for
 *		eventual reclaim.
 */
static void tdp_mmu_link_page(struct kvm *kvm, struct kvm_mmu_page *sp,
			      bool account_nx)
{
	spin_lock(&kvm->arch.tdp_mmu_pages_lock);
	list_add(&sp->link, &kvm->arch.tdp_mmu_pages);
	if (account_nx)
		account_huge_nx_page(kvm, sp);
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
		gfn = base_gfn + i * KVM_PAGES_PER_HPAGE(level);

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
				    old_child_spte, REMOVED_SPTE, level,
				    shared);
	}

	kvm_flush_remote_tlbs_with_address(kvm, gfn,
					   KVM_PAGES_PER_HPAGE(level + 1));

	call_rcu(&sp->rcu_head, tdp_mmu_free_sp_rcu_callback);
}

/**
 * __handle_changed_spte - handle bookkeeping associated with an SPTE change
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

	if (is_leaf != was_leaf)
		kvm_update_page_stats(kvm, level, is_leaf ? 1 : -1);

	if (was_leaf && is_dirty_spte(old_spte) &&
	    (!is_present || !is_dirty_spte(new_spte) || pfn_changed))
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
 * tdp_mmu_set_spte_atomic - Set a TDP MMU SPTE atomically
 * and handle the associated bookkeeping.  Do not mark the page dirty
 * in KVM's dirty bitmaps.
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
	if (is_removed_spte(iter->old_spte))
		return false;

	/*
	 * Note, fast_pf_fix_direct_spte() can also modify TDP MMU SPTEs and
	 * does not hold the mmu_lock.
	 */
	if (cmpxchg64(rcu_dereference(iter->sptep), iter->old_spte,
		      new_spte) != iter->old_spte)
		return false;

	__handle_changed_spte(kvm, iter->as_id, iter->gfn, iter->old_spte,
			      new_spte, iter->level, true);
	handle_changed_spte_acc_track(iter->old_spte, new_spte, iter->level);

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
	WARN_ON(is_removed_spte(iter->old_spte));

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
					     struct tdp_iter *iter, bool flush,
					     bool shared)
{
	/* Ensure forward progress has been made before yielding. */
	if (iter->next_last_level_gfn == iter->yielded_gfn)
		return false;

	if (need_resched() || rwlock_needbreak(&kvm->mmu_lock)) {
		rcu_read_unlock();

		if (flush)
			kvm_flush_remote_tlbs(kvm);

		if (shared)
			cond_resched_rwlock_read(&kvm->mmu_lock);
		else
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
 *
 * If can_yield is true, will release the MMU lock and reschedule if the
 * scheduler needs the CPU or there is contention on the MMU lock. If this
 * function cannot yield, it will not release the MMU lock or reschedule and
 * the caller must ensure it does not supply too large a GFN range, or the
 * operation can cause a soft lockup.
 *
 * If shared is true, this thread holds the MMU lock in read mode and must
 * account for the possibility that other threads are modifying the paging
 * structures concurrently. If shared is false, this thread should hold the
 * MMU lock in write mode.
 */
static bool zap_gfn_range(struct kvm *kvm, struct kvm_mmu_page *root,
			  gfn_t start, gfn_t end, bool can_yield, bool flush,
			  bool shared)
{
	gfn_t max_gfn_host = 1ULL << (shadow_phys_bits - PAGE_SHIFT);
	bool zap_all = (start == 0 && end >= max_gfn_host);
	struct tdp_iter iter;

	/*
	 * No need to try to step down in the iterator when zapping all SPTEs,
	 * zapping the top-level non-leaf SPTEs will recurse on their children.
	 */
	int min_level = zap_all ? root->role.level : PG_LEVEL_4K;

	/*
	 * Bound the walk at host.MAXPHYADDR, guest accesses beyond that will
	 * hit a #PF(RSVD) and never get to an EPT Violation/Misconfig / #NPF,
	 * and so KVM will never install a SPTE for such addresses.
	 */
	end = min(end, max_gfn_host);

	kvm_lockdep_assert_mmu_lock_held(kvm, shared);

	rcu_read_lock();

	for_each_tdp_pte_min_level(iter, root->spt, root->role.level,
				   min_level, start, end) {
retry:
		if (can_yield &&
		    tdp_mmu_iter_cond_resched(kvm, &iter, flush, shared)) {
			flush = false;
			continue;
		}

		if (!is_shadow_present_pte(iter.old_spte))
			continue;

		/*
		 * If this is a non-last-level SPTE that covers a larger range
		 * than should be zapped, continue, and zap the mappings at a
		 * lower level, except when zapping all SPTEs.
		 */
		if (!zap_all &&
		    (iter.gfn < start ||
		     iter.gfn + KVM_PAGES_PER_HPAGE(iter.level) > end) &&
		    !is_last_spte(iter.old_spte, iter.level))
			continue;

		if (!shared) {
			tdp_mmu_set_spte(kvm, &iter, 0);
			flush = true;
		} else if (!tdp_mmu_zap_spte_atomic(kvm, &iter)) {
			/*
			 * The iter must explicitly re-read the SPTE because
			 * the atomic cmpxchg failed.
			 */
			iter.old_spte = READ_ONCE(*rcu_dereference(iter.sptep));
			goto retry;
		}
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
bool __kvm_tdp_mmu_zap_gfn_range(struct kvm *kvm, int as_id, gfn_t start,
				 gfn_t end, bool can_yield, bool flush)
{
	struct kvm_mmu_page *root;

	for_each_tdp_mmu_root_yield_safe(kvm, root, as_id, false)
		flush = zap_gfn_range(kvm, root, start, end, can_yield, flush,
				      false);

	return flush;
}

void kvm_tdp_mmu_zap_all(struct kvm *kvm)
{
	bool flush = false;
	int i;

	for (i = 0; i < KVM_ADDRESS_SPACE_NUM; i++)
		flush = kvm_tdp_mmu_zap_gfn_range(kvm, i, 0, -1ull, flush);

	if (flush)
		kvm_flush_remote_tlbs(kvm);
}

static struct kvm_mmu_page *next_invalidated_root(struct kvm *kvm,
						  struct kvm_mmu_page *prev_root)
{
	struct kvm_mmu_page *next_root;

	if (prev_root)
		next_root = list_next_or_null_rcu(&kvm->arch.tdp_mmu_roots,
						  &prev_root->link,
						  typeof(*prev_root), link);
	else
		next_root = list_first_or_null_rcu(&kvm->arch.tdp_mmu_roots,
						   typeof(*next_root), link);

	while (next_root && !(next_root->role.invalid &&
			      refcount_read(&next_root->tdp_mmu_root_count)))
		next_root = list_next_or_null_rcu(&kvm->arch.tdp_mmu_roots,
						  &next_root->link,
						  typeof(*next_root), link);

	return next_root;
}

/*
 * Since kvm_tdp_mmu_zap_all_fast has acquired a reference to each
 * invalidated root, they will not be freed until this function drops the
 * reference. Before dropping that reference, tear down the paging
 * structure so that whichever thread does drop the last reference
 * only has to do a trivial amount of work. Since the roots are invalid,
 * no new SPTEs should be created under them.
 */
void kvm_tdp_mmu_zap_invalidated_roots(struct kvm *kvm)
{
	struct kvm_mmu_page *next_root;
	struct kvm_mmu_page *root;
	bool flush = false;

	lockdep_assert_held_read(&kvm->mmu_lock);

	rcu_read_lock();

	root = next_invalidated_root(kvm, NULL);

	while (root) {
		next_root = next_invalidated_root(kvm, root);

		rcu_read_unlock();

		flush = zap_gfn_range(kvm, root, 0, -1ull, true, flush, true);

		/*
		 * Put the reference acquired in
		 * kvm_tdp_mmu_invalidate_roots
		 */
		kvm_tdp_mmu_put_root(kvm, root, true);

		root = next_root;

		rcu_read_lock();
	}

	rcu_read_unlock();

	if (flush)
		kvm_flush_remote_tlbs(kvm);
}

/*
 * Mark each TDP MMU root as invalid so that other threads
 * will drop their references and allow the root count to
 * go to 0.
 *
 * Also take a reference on all roots so that this thread
 * can do the bulk of the work required to free the roots
 * once they are invalidated. Without this reference, a
 * vCPU thread might drop the last reference to a root and
 * get stuck with tearing down the entire paging structure.
 *
 * Roots which have a zero refcount should be skipped as
 * they're already being torn down.
 * Already invalid roots should be referenced again so that
 * they aren't freed before kvm_tdp_mmu_zap_all_fast is
 * done with them.
 *
 * This has essentially the same effect for the TDP MMU
 * as updating mmu_valid_gen does for the shadow MMU.
 */
void kvm_tdp_mmu_invalidate_all_roots(struct kvm *kvm)
{
	struct kvm_mmu_page *root;

	lockdep_assert_held_write(&kvm->mmu_lock);
	list_for_each_entry(root, &kvm->arch.tdp_mmu_roots, link)
		if (refcount_inc_not_zero(&root->tdp_mmu_root_count))
			root->role.invalid = true;
}

/*
 * Installs a last-level SPTE to handle a TDP page fault.
 * (NPT/EPT violation/misconfiguration)
 */
static int tdp_mmu_map_handle_target_level(struct kvm_vcpu *vcpu,
					  struct kvm_page_fault *fault,
					  struct tdp_iter *iter)
{
	struct kvm_mmu_page *sp = sptep_to_sp(rcu_dereference(iter->sptep));
	u64 new_spte;
	int ret = RET_PF_FIXED;
	bool wrprot = false;

	WARN_ON(sp->role.level != fault->goal_level);
	if (unlikely(!fault->slot))
		new_spte = make_mmio_spte(vcpu, iter->gfn, ACC_ALL);
	else
		wrprot = make_spte(vcpu, sp, fault->slot, ACC_ALL, iter->gfn,
					 fault->pfn, iter->old_spte, fault->prefetch, true,
					 fault->map_writable, &new_spte);

	if (new_spte == iter->old_spte)
		ret = RET_PF_SPURIOUS;
	else if (!tdp_mmu_set_spte_atomic(vcpu->kvm, iter, new_spte))
		return RET_PF_RETRY;

	/*
	 * If the page fault was caused by a write but the page is write
	 * protected, emulation is needed. If the emulation was skipped,
	 * the vCPU would have the same fault again.
	 */
	if (wrprot) {
		if (fault->write)
			ret = RET_PF_EMULATE;
	}

	/* If a MMIO SPTE is installed, the MMIO will need to be emulated. */
	if (unlikely(is_mmio_spte(new_spte))) {
		trace_mark_mmio_spte(rcu_dereference(iter->sptep), iter->gfn,
				     new_spte);
		ret = RET_PF_EMULATE;
	} else {
		trace_kvm_mmu_set_spte(iter->level, iter->gfn,
				       rcu_dereference(iter->sptep));
	}

	/*
	 * Increase pf_fixed in both RET_PF_EMULATE and RET_PF_FIXED to be
	 * consistent with legacy MMU behavior.
	 */
	if (ret != RET_PF_SPURIOUS)
		vcpu->stat.pf_fixed++;

	return ret;
}

/*
 * Handle a TDP page fault (NPT/EPT violation/misconfiguration) by installing
 * page tables and SPTEs to translate the faulting guest physical address.
 */
int kvm_tdp_mmu_map(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault)
{
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	struct tdp_iter iter;
	struct kvm_mmu_page *sp;
	u64 *child_pt;
	u64 new_spte;
	int ret;

	kvm_mmu_hugepage_adjust(vcpu, fault);

	trace_kvm_mmu_spte_requested(fault);

	rcu_read_lock();

	tdp_mmu_for_each_pte(iter, mmu, fault->gfn, fault->gfn + 1) {
		if (fault->nx_huge_page_workaround_enabled)
			disallowed_hugepage_adjust(fault, iter.old_spte, iter.level);

		if (iter.level == fault->goal_level)
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
			/*
			 * If SPTE has been frozen by another thread, just
			 * give up and retry, avoiding unnecessary page table
			 * allocation and free.
			 */
			if (is_removed_spte(iter.old_spte))
				break;

			sp = alloc_tdp_mmu_page(vcpu, iter.gfn, iter.level - 1);
			child_pt = sp->spt;

			new_spte = make_nonleaf_spte(child_pt,
						     !shadow_accessed_mask);

			if (tdp_mmu_set_spte_atomic(vcpu->kvm, &iter, new_spte)) {
				tdp_mmu_link_page(vcpu->kvm, sp,
						  fault->huge_page_disallowed &&
						  fault->req_level >= iter.level);

				trace_kvm_mmu_get_page(sp, true);
			} else {
				tdp_mmu_free_sp(sp);
				break;
			}
		}
	}

	if (iter.level != fault->goal_level) {
		rcu_read_unlock();
		return RET_PF_RETRY;
	}

	ret = tdp_mmu_map_handle_target_level(vcpu, fault, &iter);
	rcu_read_unlock();

	return ret;
}

bool kvm_tdp_mmu_unmap_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range,
				 bool flush)
{
	struct kvm_mmu_page *root;

	for_each_tdp_mmu_root(kvm, root, range->slot->as_id)
		flush |= zap_gfn_range(kvm, root, range->start, range->end,
				       range->may_block, flush, false);

	return flush;
}

typedef bool (*tdp_handler_t)(struct kvm *kvm, struct tdp_iter *iter,
			      struct kvm_gfn_range *range);

static __always_inline bool kvm_tdp_mmu_handle_gfn(struct kvm *kvm,
						   struct kvm_gfn_range *range,
						   tdp_handler_t handler)
{
	struct kvm_mmu_page *root;
	struct tdp_iter iter;
	bool ret = false;

	rcu_read_lock();

	/*
	 * Don't support rescheduling, none of the MMU notifiers that funnel
	 * into this helper allow blocking; it'd be dead, wasteful code.
	 */
	for_each_tdp_mmu_root(kvm, root, range->slot->as_id) {
		tdp_root_for_each_leaf_pte(iter, root, range->start, range->end)
			ret |= handler(kvm, &iter, range);
	}

	rcu_read_unlock();

	return ret;
}

/*
 * Mark the SPTEs range of GFNs [start, end) unaccessed and return non-zero
 * if any of the GFNs in the range have been accessed.
 */
static bool age_gfn_range(struct kvm *kvm, struct tdp_iter *iter,
			  struct kvm_gfn_range *range)
{
	u64 new_spte = 0;

	/* If we have a non-accessed entry we don't need to change the pte. */
	if (!is_accessed_spte(iter->old_spte))
		return false;

	new_spte = iter->old_spte;

	if (spte_ad_enabled(new_spte)) {
		new_spte &= ~shadow_accessed_mask;
	} else {
		/*
		 * Capture the dirty status of the page, so that it doesn't get
		 * lost when the SPTE is marked for access tracking.
		 */
		if (is_writable_pte(new_spte))
			kvm_set_pfn_dirty(spte_to_pfn(new_spte));

		new_spte = mark_spte_for_access_track(new_spte);
	}

	tdp_mmu_set_spte_no_acc_track(kvm, iter, new_spte);

	return true;
}

bool kvm_tdp_mmu_age_gfn_range(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return kvm_tdp_mmu_handle_gfn(kvm, range, age_gfn_range);
}

static bool test_age_gfn(struct kvm *kvm, struct tdp_iter *iter,
			 struct kvm_gfn_range *range)
{
	return is_accessed_spte(iter->old_spte);
}

bool kvm_tdp_mmu_test_age_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	return kvm_tdp_mmu_handle_gfn(kvm, range, test_age_gfn);
}

static bool set_spte_gfn(struct kvm *kvm, struct tdp_iter *iter,
			 struct kvm_gfn_range *range)
{
	u64 new_spte;

	/* Huge pages aren't expected to be modified without first being zapped. */
	WARN_ON(pte_huge(range->pte) || range->start + 1 != range->end);

	if (iter->level != PG_LEVEL_4K ||
	    !is_shadow_present_pte(iter->old_spte))
		return false;

	/*
	 * Note, when changing a read-only SPTE, it's not strictly necessary to
	 * zero the SPTE before setting the new PFN, but doing so preserves the
	 * invariant that the PFN of a present * leaf SPTE can never change.
	 * See __handle_changed_spte().
	 */
	tdp_mmu_set_spte(kvm, iter, 0);

	if (!pte_write(range->pte)) {
		new_spte = kvm_mmu_changed_pte_notifier_make_spte(iter->old_spte,
								  pte_pfn(range->pte));

		tdp_mmu_set_spte(kvm, iter, new_spte);
	}

	return true;
}

/*
 * Handle the changed_pte MMU notifier for the TDP MMU.
 * data is a pointer to the new pte_t mapping the HVA specified by the MMU
 * notifier.
 * Returns non-zero if a flush is needed before releasing the MMU lock.
 */
bool kvm_tdp_mmu_set_spte_gfn(struct kvm *kvm, struct kvm_gfn_range *range)
{
	bool flush = kvm_tdp_mmu_handle_gfn(kvm, range, set_spte_gfn);

	/* FIXME: return 'flush' instead of flushing here. */
	if (flush)
		kvm_flush_remote_tlbs_with_address(kvm, range->start, 1);

	return false;
}

/*
 * Remove write access from all SPTEs at or above min_level that map GFNs
 * [start, end). Returns true if an SPTE has been changed and the TLBs need to
 * be flushed.
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
retry:
		if (tdp_mmu_iter_cond_resched(kvm, &iter, false, true))
			continue;

		if (!is_shadow_present_pte(iter.old_spte) ||
		    !is_last_spte(iter.old_spte, iter.level) ||
		    !(iter.old_spte & PT_WRITABLE_MASK))
			continue;

		new_spte = iter.old_spte & ~PT_WRITABLE_MASK;

		if (!tdp_mmu_set_spte_atomic(kvm, &iter, new_spte)) {
			/*
			 * The iter must explicitly re-read the SPTE because
			 * the atomic cmpxchg failed.
			 */
			iter.old_spte = READ_ONCE(*rcu_dereference(iter.sptep));
			goto retry;
		}
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
bool kvm_tdp_mmu_wrprot_slot(struct kvm *kvm,
			     const struct kvm_memory_slot *slot, int min_level)
{
	struct kvm_mmu_page *root;
	bool spte_set = false;

	lockdep_assert_held_read(&kvm->mmu_lock);

	for_each_tdp_mmu_root_yield_safe(kvm, root, slot->as_id, true)
		spte_set |= wrprot_gfn_range(kvm, root, slot->base_gfn,
			     slot->base_gfn + slot->npages, min_level);

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
retry:
		if (tdp_mmu_iter_cond_resched(kvm, &iter, false, true))
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

		if (!tdp_mmu_set_spte_atomic(kvm, &iter, new_spte)) {
			/*
			 * The iter must explicitly re-read the SPTE because
			 * the atomic cmpxchg failed.
			 */
			iter.old_spte = READ_ONCE(*rcu_dereference(iter.sptep));
			goto retry;
		}
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
bool kvm_tdp_mmu_clear_dirty_slot(struct kvm *kvm,
				  const struct kvm_memory_slot *slot)
{
	struct kvm_mmu_page *root;
	bool spte_set = false;

	lockdep_assert_held_read(&kvm->mmu_lock);

	for_each_tdp_mmu_root_yield_safe(kvm, root, slot->as_id, true)
		spte_set |= clear_dirty_gfn_range(kvm, root, slot->base_gfn,
				slot->base_gfn + slot->npages);

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

	lockdep_assert_held_write(&kvm->mmu_lock);
	for_each_tdp_mmu_root(kvm, root, slot->as_id)
		clear_dirty_pt_masked(kvm, root, gfn, mask, wrprot);
}

/*
 * Clear leaf entries which could be replaced by large mappings, for
 * GFNs within the slot.
 */
static bool zap_collapsible_spte_range(struct kvm *kvm,
				       struct kvm_mmu_page *root,
				       const struct kvm_memory_slot *slot,
				       bool flush)
{
	gfn_t start = slot->base_gfn;
	gfn_t end = start + slot->npages;
	struct tdp_iter iter;
	kvm_pfn_t pfn;

	rcu_read_lock();

	tdp_root_for_each_pte(iter, root, start, end) {
retry:
		if (tdp_mmu_iter_cond_resched(kvm, &iter, flush, true)) {
			flush = false;
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

		if (!tdp_mmu_zap_spte_atomic(kvm, &iter)) {
			/*
			 * The iter must explicitly re-read the SPTE because
			 * the atomic cmpxchg failed.
			 */
			iter.old_spte = READ_ONCE(*rcu_dereference(iter.sptep));
			goto retry;
		}
		flush = true;
	}

	rcu_read_unlock();

	return flush;
}

/*
 * Clear non-leaf entries (and free associated page tables) which could
 * be replaced by large mappings, for GFNs within the slot.
 */
bool kvm_tdp_mmu_zap_collapsible_sptes(struct kvm *kvm,
				       const struct kvm_memory_slot *slot,
				       bool flush)
{
	struct kvm_mmu_page *root;

	lockdep_assert_held_read(&kvm->mmu_lock);

	for_each_tdp_mmu_root_yield_safe(kvm, root, slot->as_id, true)
		flush = zap_collapsible_spte_range(kvm, root, slot, flush);

	return flush;
}

/*
 * Removes write access on the last level SPTE mapping this GFN and unsets the
 * MMU-writable bit to ensure future writes continue to be intercepted.
 * Returns true if an SPTE was set and a TLB flush is needed.
 */
static bool write_protect_gfn(struct kvm *kvm, struct kvm_mmu_page *root,
			      gfn_t gfn, int min_level)
{
	struct tdp_iter iter;
	u64 new_spte;
	bool spte_set = false;

	BUG_ON(min_level > KVM_MAX_HUGEPAGE_LEVEL);

	rcu_read_lock();

	for_each_tdp_pte_min_level(iter, root->spt, root->role.level,
				   min_level, gfn, gfn + 1) {
		if (!is_shadow_present_pte(iter.old_spte) ||
		    !is_last_spte(iter.old_spte, iter.level))
			continue;

		if (!is_writable_pte(iter.old_spte))
			break;

		new_spte = iter.old_spte &
			~(PT_WRITABLE_MASK | shadow_mmu_writable_mask);

		tdp_mmu_set_spte(kvm, &iter, new_spte);
		spte_set = true;
	}

	rcu_read_unlock();

	return spte_set;
}

/*
 * Removes write access on the last level SPTE mapping this GFN and unsets the
 * MMU-writable bit to ensure future writes continue to be intercepted.
 * Returns true if an SPTE was set and a TLB flush is needed.
 */
bool kvm_tdp_mmu_write_protect_gfn(struct kvm *kvm,
				   struct kvm_memory_slot *slot, gfn_t gfn,
				   int min_level)
{
	struct kvm_mmu_page *root;
	bool spte_set = false;

	lockdep_assert_held_write(&kvm->mmu_lock);
	for_each_tdp_mmu_root(kvm, root, slot->as_id)
		spte_set |= write_protect_gfn(kvm, root, gfn, min_level);

	return spte_set;
}

/*
 * Return the level of the lowest level SPTE added to sptes.
 * That SPTE may be non-present.
 *
 * Must be called between kvm_tdp_mmu_walk_lockless_{begin,end}.
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
		sptes[leaf] = iter.old_spte;
	}

	return leaf;
}

/*
 * Returns the last level spte pointer of the shadow page walk for the given
 * gpa, and sets *spte to the spte value. This spte may be non-preset. If no
 * walk could be performed, returns NULL and *spte does not contain valid data.
 *
 * Contract:
 *  - Must be called between kvm_tdp_mmu_walk_lockless_{begin,end}.
 *  - The returned sptep must not be used after kvm_tdp_mmu_walk_lockless_end.
 *
 * WARNING: This function is only intended to be called during fast_page_fault.
 */
u64 *kvm_tdp_mmu_fast_pf_get_last_sptep(struct kvm_vcpu *vcpu, u64 addr,
					u64 *spte)
{
	struct tdp_iter iter;
	struct kvm_mmu *mmu = vcpu->arch.mmu;
	gfn_t gfn = addr >> PAGE_SHIFT;
	tdp_ptep_t sptep = NULL;

	tdp_mmu_for_each_pte(iter, mmu, gfn, gfn + 1) {
		*spte = iter.old_spte;
		sptep = iter.sptep;
	}

	/*
	 * Perform the rcu_dereference to get the raw spte pointer value since
	 * we are passing it up to fast_page_fault, which is shared with the
	 * legacy MMU and thus does not retain the TDP MMU-specific __rcu
	 * annotation.
	 *
	 * This is safe since fast_page_fault obeys the contracts of this
	 * function as well as all TDP MMU contracts around modifying SPTEs
	 * outside of mmu_lock.
	 */
	return rcu_dereference(sptep);
}
