/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_PAGE_TRACK_H
#define __KVM_X86_PAGE_TRACK_H

#include <linux/kvm_host.h>

#include <asm/kvm_page_track.h>

int kvm_page_track_init(struct kvm *kvm);
void kvm_page_track_cleanup(struct kvm *kvm);

bool kvm_page_track_write_tracking_enabled(struct kvm *kvm);
int kvm_page_track_write_tracking_alloc(struct kvm_memory_slot *slot);

void kvm_page_track_free_memslot(struct kvm_memory_slot *slot);
int kvm_page_track_create_memslot(struct kvm *kvm,
				  struct kvm_memory_slot *slot,
				  unsigned long npages);

bool kvm_slot_page_track_is_active(struct kvm *kvm,
				   const struct kvm_memory_slot *slot,
				   gfn_t gfn, enum kvm_page_track_mode mode);

void kvm_page_track_write(struct kvm_vcpu *vcpu, gpa_t gpa, const u8 *new,
			  int bytes);
void kvm_page_track_delete_slot(struct kvm *kvm, struct kvm_memory_slot *slot);

static inline bool kvm_page_track_has_external_user(struct kvm *kvm)
{
	return !hlist_empty(&kvm->arch.track_notifier_head.track_notifier_list);
}

#endif /* __KVM_X86_PAGE_TRACK_H */
