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
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/kvm_host.h>
#include <asm/kvm_host.h>
#include <asm/kvm_page_track.h>

#include "mmu.h"

void kvm_page_track_free_memslot(struct kvm_memory_slot *free,
				 struct kvm_memory_slot *dont)
{
	int i;

	for (i = 0; i < KVM_PAGE_TRACK_MAX; i++)
		if (!dont || free->arch.gfn_track[i] !=
		      dont->arch.gfn_track[i]) {
			kvfree(free->arch.gfn_track[i]);
			free->arch.gfn_track[i] = NULL;
		}
}

int kvm_page_track_create_memslot(struct kvm_memory_slot *slot,
				  unsigned long npages)
{
	int  i;

	for (i = 0; i < KVM_PAGE_TRACK_MAX; i++) {
		slot->arch.gfn_track[i] = kvm_kvzalloc(npages *
					    sizeof(*slot->arch.gfn_track[i]));
		if (!slot->arch.gfn_track[i])
			goto track_free;
	}

	return 0;

track_free:
	kvm_page_track_free_memslot(slot, NULL);
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

	index = gfn_to_index(gfn, slot->base_gfn, PT_PAGE_TABLE_LEVEL);

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

/*
 * check if the corresponding access on the specified guest page is tracked.
 */
bool kvm_page_track_is_active(struct kvm_vcpu *vcpu, gfn_t gfn,
			      enum kvm_page_track_mode mode)
{
	struct kvm_memory_slot *slot = kvm_vcpu_gfn_to_memslot(vcpu, gfn);
	int index = gfn_to_index(gfn, slot->base_gfn, PT_PAGE_TABLE_LEVEL);

	if (WARN_ON(!page_track_mode_is_valid(mode)))
		return false;

	return !!ACCESS_ONCE(slot->arch.gfn_track[mode][index]);
}
