/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_X86_MMU_INTERNAL_H
#define __KVM_X86_MMU_INTERNAL_H

void kvm_mmu_gfn_disallow_lpage(struct kvm_memory_slot *slot, gfn_t gfn);
void kvm_mmu_gfn_allow_lpage(struct kvm_memory_slot *slot, gfn_t gfn);
bool kvm_mmu_slot_gfn_write_protect(struct kvm *kvm,
				    struct kvm_memory_slot *slot, u64 gfn);

#endif /* __KVM_X86_MMU_INTERNAL_H */
