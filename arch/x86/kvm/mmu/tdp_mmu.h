// SPDX-License-Identifier: GPL-2.0

#ifndef __KVM_X86_MMU_TDP_MMU_H
#define __KVM_X86_MMU_TDP_MMU_H

#include <linux/kvm_host.h>

void kvm_mmu_init_tdp_mmu(struct kvm *kvm);
void kvm_mmu_uninit_tdp_mmu(struct kvm *kvm);
#endif /* __KVM_X86_MMU_TDP_MMU_H */
