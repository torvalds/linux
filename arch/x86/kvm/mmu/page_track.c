// SPDX-License-Identifier: GPL-2.0-only
/*
 * Support KVM gust page tracking
 *
 * This feature allows us to track page access in guest. Currently, only
 * write access is tracked.
 *
 * Copyright(C) 2015 Intel Corporation.
 *
 * Author:
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/lockdep.h>
#include <linux/kvm_host.h>
#include <linux/rculist.h>

#include "mmu.h"
#include "mmu_internal.h"
#include "page_track.h"

static bool kvm_external_write_tracking_enabled(struct kvm *kvm)
{
#ifdef CONFIG_KVM_EXTERNAL_WRITE_TRACKING
	/*
	 * Read external_write_tracking_enabled before related pointers.  Pairs
	 * with the smp_store_release in kvm_page_track_write_tracking_enable().
	 */
	return smp_load_acquire(&kvm->arch.external_write_tracking_enabled);
#else
	return false;
#endif
}

bool kvm_page_track_write_tracking_enabled(struct kvm *kvm)
{
	return kvm_external_write_tracking_enabled(kvm) ||
	       kvm_shadow_root_allocated(kvm) || !tdp_enabled;
}

void kvm_page_track_free_memslot(struct kvm_memory_slot *slot)
{
	vfree(slot->arch.gfn_write_track);
	slot->arch.gfn_write_track = NULL;
}

static int __kvm_page_track_write_tracking_alloc(struct kvm_memory_slot *slot,
						 unsigned long npages)
{
	const size_t size = sizeof(*slot->arch.gfn_write_track);

	if (!slot->arch.gfn_write_track)
		slot->arch.gfn_write_track = __vcalloc(npages, size,
						       GFP_KERNEL_ACCOUNT);

	return slot->arch.gfn_write_track ? 0 : -ENOMEM;
}

int kvm_page_track_create_memslot(struct kvm *kvm,
				  struct kvm_memory_slot *slot,
				  unsigned long npages)
{
	if (!kvm_page_track_write_tracking_enabled(kvm))
		return 0;

	return __kvm_page_track_write_tracking_alloc(slot, npages);
}

int kvm_page_track_write_tracking_alloc(struct kvm_memory_slot *slot)
{
	return __kvm_page_track_write_tracking_alloc(slot, slot->npages);
}

static void update_gfn_write_track(struct kvm_memory_slot *slot, gfn_t gfn,
				   short count)
{
	int index, val;

	index = gfn_to_index(gfn, slot->base_gfn, PG_LEVEL_4K);

	val = slot->arch.gfn_write_track[index];

	if (WARN_ON_ONCE(val + count < 0 || val + count > USHRT_MAX))
		return;

	slot->arch.gfn_write_track[index] += count;
}

void __kvm_write_track_add_gfn(struct kvm *kvm, struct kvm_memory_slot *slot,
			       gfn_t gfn)
{
	lockdep_assert_held_write(&kvm->mmu_lock);

	lockdep_assert_once(lockdep_is_held(&kvm->slots_lock) ||
			    srcu_read_lock_held(&kvm->srcu));

	if (KVM_BUG_ON(!kvm_page_track_write_tracking_enabled(kvm), kvm))
		return;

	update_gfn_write_track(slot, gfn, 1);

	/*
	 * new track stops large page mapping for the
	 * tracked page.
	 */
	kvm_mmu_gfn_disallow_lpage(slot, gfn);

	if (kvm_mmu_slot_gfn_write_protect(kvm, slot, gfn, PG_LEVEL_4K))
		kvm_flush_remote_tlbs(kvm);
}

void __kvm_write_track_remove_gfn(struct kvm *kvm,
				  struct kvm_memory_slot *slot, gfn_t gfn)
{
	lockdep_assert_held_write(&kvm->mmu_lock);

	lockdep_assert_once(lockdep_is_held(&kvm->slots_lock) ||
			    srcu_read_lock_held(&kvm->srcu));

	if (KVM_BUG_ON(!kvm_page_track_write_tracking_enabled(kvm), kvm))
		return;

	update_gfn_write_track(slot, gfn, -1);

	/*
	 * allow large page mapping for the tracked page
	 * after the tracker is gone.
	 */
	kvm_mmu_gfn_allow_lpage(slot, gfn);
}

/*
 * check if the corresponding access on the specified guest page is tracked.
 */
bool kvm_gfn_is_write_tracked(struct kvm *kvm,
			      const struct kvm_memory_slot *slot, gfn_t gfn)
{
	int index;

	if (!slot)
		return false;

	if (!kvm_page_track_write_tracking_enabled(kvm))
		return false;

	index = gfn_to_index(gfn, slot->base_gfn, PG_LEVEL_4K);
	return !!READ_ONCE(slot->arch.gfn_write_track[index]);
}

#ifdef CONFIG_KVM_EXTERNAL_WRITE_TRACKING
void kvm_page_track_cleanup(struct kvm *kvm)
{
	struct kvm_page_track_notifier_head *head;

	head = &kvm->arch.track_notifier_head;
	cleanup_srcu_struct(&head->track_srcu);
}

int kvm_page_track_init(struct kvm *kvm)
{
	struct kvm_page_track_notifier_head *head;

	head = &kvm->arch.track_notifier_head;
	INIT_HLIST_HEAD(&head->track_notifier_list);
	return init_srcu_struct(&head->track_srcu);
}

static int kvm_enable_external_write_tracking(struct kvm *kvm)
{
	struct kvm_memslots *slots;
	struct kvm_memory_slot *slot;
	int r = 0, i, bkt;

	mutex_lock(&kvm->slots_arch_lock);

	/*
	 * Check for *any* write tracking user (not just external users) under
	 * lock.  This avoids unnecessary work, e.g. if KVM itself is using
	 * write tracking, or if two external users raced when registering.
	 */
	if (kvm_page_track_write_tracking_enabled(kvm))
		goto out_success;

	for (i = 0; i < kvm_arch_nr_memslot_as_ids(kvm); i++) {
		slots = __kvm_memslots(kvm, i);
		kvm_for_each_memslot(slot, bkt, slots) {
			/*
			 * Intentionally do NOT free allocations on failure to
			 * avoid having to track which allocations were made
			 * now versus when the memslot was created.  The
			 * metadata is guaranteed to be freed when the slot is
			 * freed, and will be kept/used if userspace retries
			 * the failed ioctl() instead of killing the VM.
			 */
			r = kvm_page_track_write_tracking_alloc(slot);
			if (r)
				goto out_unlock;
		}
	}

out_success:
	/*
	 * Ensure that external_write_tracking_enabled becomes true strictly
	 * after all the related pointers are set.
	 */
	smp_store_release(&kvm->arch.external_write_tracking_enabled, true);
out_unlock:
	mutex_unlock(&kvm->slots_arch_lock);
	return r;
}

/*
 * register the notifier so that event interception for the tracked guest
 * pages can be received.
 */
int kvm_page_track_register_notifier(struct kvm *kvm,
				     struct kvm_page_track_notifier_node *n)
{
	struct kvm_page_track_notifier_head *head;
	int r;

	if (!kvm || kvm->mm != current->mm)
		return -ESRCH;

	if (!kvm_external_write_tracking_enabled(kvm)) {
		r = kvm_enable_external_write_tracking(kvm);
		if (r)
			return r;
	}

	kvm_get_kvm(kvm);

	head = &kvm->arch.track_notifier_head;

	write_lock(&kvm->mmu_lock);
	hlist_add_head_rcu(&n->node, &head->track_notifier_list);
	write_unlock(&kvm->mmu_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_page_track_register_notifier);

/*
 * stop receiving the event interception. It is the opposed operation of
 * kvm_page_track_register_notifier().
 */
void kvm_page_track_unregister_notifier(struct kvm *kvm,
					struct kvm_page_track_notifier_node *n)
{
	struct kvm_page_track_notifier_head *head;

	head = &kvm->arch.track_notifier_head;

	write_lock(&kvm->mmu_lock);
	hlist_del_rcu(&n->node);
	write_unlock(&kvm->mmu_lock);
	synchronize_srcu(&head->track_srcu);

	kvm_put_kvm(kvm);
}
EXPORT_SYMBOL_GPL(kvm_page_track_unregister_notifier);

/*
 * Notify the node that write access is intercepted and write emulation is
 * finished at this time.
 *
 * The node should figure out if the written page is the one that node is
 * interested in by itself.
 */
void __kvm_page_track_write(struct kvm *kvm, gpa_t gpa, const u8 *new, int bytes)
{
	struct kvm_page_track_notifier_head *head;
	struct kvm_page_track_notifier_node *n;
	int idx;

	head = &kvm->arch.track_notifier_head;

	if (hlist_empty(&head->track_notifier_list))
		return;

	idx = srcu_read_lock(&head->track_srcu);
	hlist_for_each_entry_srcu(n, &head->track_notifier_list, node,
				  srcu_read_lock_held(&head->track_srcu))
		if (n->track_write)
			n->track_write(gpa, new, bytes, n);
	srcu_read_unlock(&head->track_srcu, idx);
}

/*
 * Notify external page track nodes that a memory region is being removed from
 * the VM, e.g. so that users can free any associated metadata.
 */
void kvm_page_track_delete_slot(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	struct kvm_page_track_notifier_head *head;
	struct kvm_page_track_notifier_node *n;
	int idx;

	head = &kvm->arch.track_notifier_head;

	if (hlist_empty(&head->track_notifier_list))
		return;

	idx = srcu_read_lock(&head->track_srcu);
	hlist_for_each_entry_srcu(n, &head->track_notifier_list, node,
				  srcu_read_lock_held(&head->track_srcu))
		if (n->track_remove_region)
			n->track_remove_region(slot->base_gfn, slot->npages, n);
	srcu_read_unlock(&head->track_srcu, idx);
}

/*
 * add guest page to the tracking pool so that corresponding access on that
 * page will be intercepted.
 *
 * @kvm: the guest instance we are interested in.
 * @gfn: the guest page.
 */
int kvm_write_track_add_gfn(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);

	slot = gfn_to_memslot(kvm, gfn);
	if (!slot) {
		srcu_read_unlock(&kvm->srcu, idx);
		return -EINVAL;
	}

	write_lock(&kvm->mmu_lock);
	__kvm_write_track_add_gfn(kvm, slot, gfn);
	write_unlock(&kvm->mmu_lock);

	srcu_read_unlock(&kvm->srcu, idx);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_write_track_add_gfn);

/*
 * remove the guest page from the tracking pool which stops the interception
 * of corresponding access on that page.
 *
 * @kvm: the guest instance we are interested in.
 * @gfn: the guest page.
 */
int kvm_write_track_remove_gfn(struct kvm *kvm, gfn_t gfn)
{
	struct kvm_memory_slot *slot;
	int idx;

	idx = srcu_read_lock(&kvm->srcu);

	slot = gfn_to_memslot(kvm, gfn);
	if (!slot) {
		srcu_read_unlock(&kvm->srcu, idx);
		return -EINVAL;
	}

	write_lock(&kvm->mmu_lock);
	__kvm_write_track_remove_gfn(kvm, slot, gfn);
	write_unlock(&kvm->mmu_lock);

	srcu_read_unlock(&kvm->srcu, idx);

	return 0;
}
EXPORT_SYMBOL_GPL(kvm_write_track_remove_gfn);
#endif
