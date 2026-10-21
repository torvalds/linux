/* SPDX-License-Identifier: GPL-2.0 */

#ifndef ARCH_KVM_GMAP_KVM_MMU_H
#define ARCH_KVM_GMAP_KVM_MMU_H

#include <linux/kvm_host.h>

int s390_kvm_mmu_get_dirty_log(struct kvm *kvm, struct kvm_dirty_log *log);
int s390_kvm_mmu_prepare_memory_region(struct kvm *kvm,
				       const struct kvm_memory_slot *old,
				       struct kvm_memory_slot *new,
				       enum kvm_mr_change change);
void s390_kvm_mmu_commit_memory_region(struct kvm *kvm,
				       struct kvm_memory_slot *old,
				       const struct kvm_memory_slot *new,
				       enum kvm_mr_change change);

#endif /* ARCH_KVM_GMAP_KVM_MMU_H */
