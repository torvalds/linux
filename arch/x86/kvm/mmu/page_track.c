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

#include <linux/kvm_host.h>
#include <linux/rculist.h>

#include <asm/kvm_page_track.h>

#include "mmu_internal.h"

void kvm_page_track_free_memslot(struct kvm_memory_slot *slot)
{
	int i;

	for (i = 0; i < KVM_PAGE_TRACK_MAX; i++) {
		kvfree(slot->arch.gfn_track[i]);
		slot->arch.gfn_track[i] = NULL;
	}
}

int kvm_page_track_create_memslot(struct kvm_memory_slot *slot,
				  unsigned long npages)
{
	int  i;

	for (i = 0; i < KVM_PAGE_TRACK_MAX; i++) {
		slot->arch.gfn_track[i] =
			kvcalloc(npages, sizeof(*slot->arch.gfn_track[i]),
				 GFP_KERNEL_ACCOUNT);
		if (!slot->arch.gfn_track[i])
			goto track_free;
	}

	return 0;

track_free:
	kvm_page_track_free_memslot(slot);
	return -ENOMEM;
}

static inline bool page_track_mode_is_valid(enum kvm_page_track_mode mode)
{
	if (mode < 0 || mode >= KVM_PAGE_TRACK_MAX)
		return false;

	return true;
}

static void update_gfn_track(struct kvm_memory_slot *slot, gfn_t gfn,
			     enum kvm_page_track_mode mode, short count)
{
	int index, val;

	index = gfn_to_index(gfn, slot->base_gfn, PG_LEVEL_4K);

	val = slot->arch.gfn_track[mode][index];

	if (WARN_ON(val + count < 0 || val + count > USHRT_MAX))
		return;

	slot->arch.gfn_track[mode][index] += count;
}

/*
 * add guest page to the tracking pool so that corresponding access on that
 * page will be intercepted.
 *
 * It should be called under the protection both of mmu-lock and kvm->srcu
 * or kvm->slots_lock.
 *
 * @kvm: the guest instance we are interested in.
 * @slot: the @gfn belongs to.
 * @gfn: the guest page.
 * @mode: tracking mode, currently only write track is supported.
 */
void kvm_slot_page_track_add_page(struct kvm *kvm,
				  struct kvm_memory_slot *slot, gfn_t gfn,
				  enum kvm_page_track_mode mode)
{

	if (WARN_ON(!page_track_mode_is_valid(mode)))
		return;

	update_gfn_track(slot, gfn, mode, 1);

	/*
	 * new track stops large page mapping for the
	 * tracked page.
	 */
	kvm_mmu_gfn_disallow_lpage(slot, gfn);

	if (mode == KVM_PAGE_TRACK_WRITE)
		if (kvm_mmu_slot_gfn_write_protect(kvm, slot, gfn))
			kvm_flush_remote_tlbs(kvm);
}
EXPORT_SYMBOL_GPL(kvm_slot_page_track_add_page);

/*
 * remove the guest page from the tracking pool which stops the interception
 * of corresponding access on that page. It is the opposed operation of
 * kvm_slot_page_track_add_page().
 *
 * It should be called under the protection both of mmu-lock and kvm->srcu
 * or kvm->slots_lock.
 *
 * @kvm: the guest instance we are interested in.
 * @slot: the @gfn belongs to.
 * @gfn: the guest page.
 * @mode: tracking mode, currently only write track is supported.
 */
void kvm_slot_page_track_remove_page(struct kvm *kvm,
				     struct kvm_memory_slot *slot, gfn_t gfn,
				     enum kvm_page_track_mode mode)
{
	if (WARN_ON(!page_track_mode_is_valid(mode)))
		return;

	update_gfn_track(slot, gfn, mode, -1);

	/*
	 * allow large page mapping for the tracked page
	 * after the tracker is gone.
	 */
	kvm_mmu_gfn_allow_lpage(slot, gfn);
}
EXPORT_SYMBOL_GPL(kvm_slot_page_track_remove_page);

/*
 * check if the corresponding access on the specified guest page is tracked.
 */
bool kvm_page_track_is_active(struct kvm_vcpu *vcpu, gfn_t gfn,
			      enum kvm_page_track_mode mode)
{
	struct kvm_memory_slot *slot;
	int index;

	if (WARN_ON(!page_track_mode_is_valid(mode)))
		return false;

	slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	if (!slot)
		return false;

	index = gfn_to_index(gfn, slot->base_gfn, PG_LEVEL_4K);
	return !!READ_ONCE(slot->arch.gfn_track[mode][index]);
}

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

/*
 * register the notifier so that event interception for the tracked guest
 * pages can be received.
 */
void
kvm_page_track_register_notifier(struct kvm *kvm,
				 struct kvm_page_track_notifier_node *n)
{
	struct kvm_page_track_notifier_head *head;

	head = &kvm->arch.track_notifier_head;

	spin_lock(&kvm->mmu_lock);
	hlist_add_head_rcu(&n->node, &head->track_notifier_list);
	spin_unlock(&kvm->mmu_lock);
}
EXPORT_SYMBOL_GPL(kvm_page_track_register_notifier);

/*
 * stop receiving the event interception. It is the opposed operation of
 * kvm_page_track_register_notifier().
 */
void
kvm_page_track_unregister_notifier(struct kvm *kvm,
				   struct kvm_page_track_notifier_node *n)
{
	struct kvm_page_track_notifier_head *head;

	head = &kvm->arch.track_notifier_head;

	spin_lock(&kvm->mmu_lock);
	hlist_del_rcu(&n->node);
	spin_unlock(&kvm->mmu_lock);
	synchronize_srcu(&head->track_srcu);
}
EXPORT_SYMBOL_GPL(kvm_page_track_unregister_notifier);

/*
 * Notify the node that write access is intercepted and write emulation is
 * finished at this time.
 *
 * The node should figure out if the written page is the one that node is
 * interested in by itself.
 */
void kvm_page_track_write(struct kvm_vcpu *vcpu, gpa_t gpa, const u8 *new,
			  int bytes)
{
	struct kvm_page_track_notifier_head *head;
	struct kvm_page_track_notifier_node *n;
	int idx;

	head = &vcpu->kvm->arch.track_notifier_head;

	if (hlist_empty(&head->track_notifier_list))
		return;

	idx = srcu_read_lock(&head->track_srcu);
	hlist_for_each_entry_srcu(n, &head->track_notifier_list, node,
				srcu_read_lock_held(&head->track_srcu))
		if (n->track_write)
			n->track_write(vcpu, gpa, new, bytes, n);
	srcu_read_unlock(&head->track_srcu, idx);
}

/*
 * Notify the node that memory slot is being removed or moved so that it can
 * drop write-protection for the pages in the memory slot.
 *
 * The node should figure out it has any write-protected pages in this slot
 * by itself.
 */
void kvm_page_track_flush_slot(struct kvm *kvm, struct kvm_memory_slot *slot)
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
		if (n->track_flush_slot)
			n->track_flush_slot(kvm, slot, n);
	srcu_read_unlock(&head->track_srcu, idx);
}
