/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_KVM_PAGE_TRACK_H
#define _ASM_X86_KVM_PAGE_TRACK_H

enum kvm_page_track_mode {
	KVM_PAGE_TRACK_WRITE,
	KVM_PAGE_TRACK_MAX,
};

/*
 * The yestifier represented by @kvm_page_track_yestifier_yesde is linked into
 * the head which will be yestified when guest is triggering the track event.
 *
 * Write access on the head is protected by kvm->mmu_lock, read access
 * is protected by track_srcu.
 */
struct kvm_page_track_yestifier_head {
	struct srcu_struct track_srcu;
	struct hlist_head track_yestifier_list;
};

struct kvm_page_track_yestifier_yesde {
	struct hlist_yesde yesde;

	/*
	 * It is called when guest is writing the write-tracked page
	 * and write emulation is finished at that time.
	 *
	 * @vcpu: the vcpu where the write access happened.
	 * @gpa: the physical address written by guest.
	 * @new: the data was written to the address.
	 * @bytes: the written length.
	 * @yesde: this yesde
	 */
	void (*track_write)(struct kvm_vcpu *vcpu, gpa_t gpa, const u8 *new,
			    int bytes, struct kvm_page_track_yestifier_yesde *yesde);
	/*
	 * It is called when memory slot is being moved or removed
	 * users can drop write-protection for the pages in that memory slot
	 *
	 * @kvm: the kvm where memory slot being moved or removed
	 * @slot: the memory slot being moved or removed
	 * @yesde: this yesde
	 */
	void (*track_flush_slot)(struct kvm *kvm, struct kvm_memory_slot *slot,
			    struct kvm_page_track_yestifier_yesde *yesde);
};

void kvm_page_track_init(struct kvm *kvm);
void kvm_page_track_cleanup(struct kvm *kvm);

void kvm_page_track_free_memslot(struct kvm_memory_slot *free,
				 struct kvm_memory_slot *dont);
int kvm_page_track_create_memslot(struct kvm_memory_slot *slot,
				  unsigned long npages);

void kvm_slot_page_track_add_page(struct kvm *kvm,
				  struct kvm_memory_slot *slot, gfn_t gfn,
				  enum kvm_page_track_mode mode);
void kvm_slot_page_track_remove_page(struct kvm *kvm,
				     struct kvm_memory_slot *slot, gfn_t gfn,
				     enum kvm_page_track_mode mode);
bool kvm_page_track_is_active(struct kvm_vcpu *vcpu, gfn_t gfn,
			      enum kvm_page_track_mode mode);

void
kvm_page_track_register_yestifier(struct kvm *kvm,
				 struct kvm_page_track_yestifier_yesde *n);
void
kvm_page_track_unregister_yestifier(struct kvm *kvm,
				   struct kvm_page_track_yestifier_yesde *n);
void kvm_page_track_write(struct kvm_vcpu *vcpu, gpa_t gpa, const u8 *new,
			  int bytes);
void kvm_page_track_flush_slot(struct kvm *kvm, struct kvm_memory_slot *slot);
#endif
