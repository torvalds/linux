/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KVM_PAGE_TRACK_H
#define _ASM_X86_KVM_PAGE_TRACK_H

#include <linux/kvm_types.h>

void kvm_slot_page_track_add_page(struct kvm *kvm,
				  struct kvm_memory_slot *slot, gfn_t gfn);
void kvm_slot_page_track_remove_page(struct kvm *kvm,
				     struct kvm_memory_slot *slot, gfn_t gfn);

#ifdef CONFIG_KVM_EXTERNAL_WRITE_TRACKING
/*
 * The notifier represented by @kvm_page_track_notifier_node is linked into
 * the head which will be notified when guest is triggering the track event.
 *
 * Write access on the head is protected by kvm->mmu_lock, read access
 * is protected by track_srcu.
 */
struct kvm_page_track_notifier_head {
	struct srcu_struct track_srcu;
	struct hlist_head track_notifier_list;
};

struct kvm_page_track_notifier_node {
	struct hlist_node node;

	/*
	 * It is called when guest is writing the write-tracked page
	 * and write emulation is finished at that time.
	 *
	 * @gpa: the physical address written by guest.
	 * @new: the data was written to the address.
	 * @bytes: the written length.
	 * @node: this node
	 */
	void (*track_write)(gpa_t gpa, const u8 *new, int bytes,
			    struct kvm_page_track_notifier_node *node);

	/*
	 * Invoked when a memory region is removed from the guest.  Or in KVM
	 * terms, when a memslot is deleted.
	 *
	 * @gfn:       base gfn of the region being removed
	 * @nr_pages:  number of pages in the to-be-removed region
	 * @node:      this node
	 */
	void (*track_remove_region)(gfn_t gfn, unsigned long nr_pages,
				    struct kvm_page_track_notifier_node *node);
};

void
kvm_page_track_register_notifier(struct kvm *kvm,
				 struct kvm_page_track_notifier_node *n);
void
kvm_page_track_unregister_notifier(struct kvm *kvm,
				   struct kvm_page_track_notifier_node *n);
#else
/*
 * Allow defining a node in a structure even if page tracking is disabled, e.g.
 * to play nice with testing headers via direct inclusion from the command line.
 */
struct kvm_page_track_notifier_node {};
#endif /* CONFIG_KVM_EXTERNAL_WRITE_TRACKING */

#endif
