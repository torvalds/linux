#ifndef _ASM_X86_KVM_PAGE_TRACK_H
#define _ASM_X86_KVM_PAGE_TRACK_H

enum kvm_page_track_mode {
	KVM_PAGE_TRACK_WRITE,
	KVM_PAGE_TRACK_MAX,
};

void kvm_page_track_free_memslot(struct kvm_memory_slot *free,
				 struct kvm_memory_slot *dont);
int kvm_page_track_create_memslot(struct kvm_memory_slot *slot,
				  unsigned long npages);
#endif
