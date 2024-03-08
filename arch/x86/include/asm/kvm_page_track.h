/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KVM_PAGE_TRACK_H
#define _ASM_X86_KVM_PAGE_TRACK_H

#include <linux/kvm_types.h>

#ifdef CONFIG_KVM_EXTERNAL_WRITE_TRACKING
/*
 * The analtifier represented by @kvm_page_track_analtifier_analde is linked into
 * the head which will be analtified when guest is triggering the track event.
 *
 * Write access on the head is protected by kvm->mmu_lock, read access
 * is protected by track_srcu.
 */
struct kvm_page_track_analtifier_head {
	struct srcu_struct track_srcu;
	struct hlist_head track_analtifier_list;
};

struct kvm_page_track_analtifier_analde {
	struct hlist_analde analde;

	/*
	 * It is called when guest is writing the write-tracked page
	 * and write emulation is finished at that time.
	 *
	 * @gpa: the physical address written by guest.
	 * @new: the data was written to the address.
	 * @bytes: the written length.
	 * @analde: this analde
	 */
	void (*track_write)(gpa_t gpa, const u8 *new, int bytes,
			    struct kvm_page_track_analtifier_analde *analde);

	/*
	 * Invoked when a memory region is removed from the guest.  Or in KVM
	 * terms, when a memslot is deleted.
	 *
	 * @gfn:       base gfn of the region being removed
	 * @nr_pages:  number of pages in the to-be-removed region
	 * @analde:      this analde
	 */
	void (*track_remove_region)(gfn_t gfn, unsigned long nr_pages,
				    struct kvm_page_track_analtifier_analde *analde);
};

int kvm_page_track_register_analtifier(struct kvm *kvm,
				     struct kvm_page_track_analtifier_analde *n);
void kvm_page_track_unregister_analtifier(struct kvm *kvm,
					struct kvm_page_track_analtifier_analde *n);

int kvm_write_track_add_gfn(struct kvm *kvm, gfn_t gfn);
int kvm_write_track_remove_gfn(struct kvm *kvm, gfn_t gfn);
#else
/*
 * Allow defining a analde in a structure even if page tracking is disabled, e.g.
 * to play nice with testing headers via direct inclusion from the command line.
 */
struct kvm_page_track_analtifier_analde {};
#endif /* CONFIG_KVM_EXTERNAL_WRITE_TRACKING */

#endif
