/*
 * Copyright IBM Corporation, 2013
 * Author Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License or (at your optional) any later version of the license.
 *
 */

#ifndef __POWERPC_KVM_BOOK3S_H__
#define __POWERPC_KVM_BOOK3S_H__

extern void kvmppc_core_flush_memslot_hv(struct kvm *kvm,
					 struct kvm_memory_slot *memslot);
extern int kvm_unmap_hva_range_hv(struct kvm *kvm, unsigned long start,
				  unsigned long end);
extern int kvm_age_hva_hv(struct kvm *kvm, unsigned long start,
			  unsigned long end);
extern int kvm_test_age_hva_hv(struct kvm *kvm, unsigned long hva);
extern void kvm_set_spte_hva_hv(struct kvm *kvm, unsigned long hva, pte_t pte);

extern void kvmppc_mmu_destroy_pr(struct kvm_vcpu *vcpu);
extern int kvmppc_core_emulate_op_pr(struct kvm_run *run, struct kvm_vcpu *vcpu,
				     unsigned int inst, int *advance);
extern int kvmppc_core_emulate_mtspr_pr(struct kvm_vcpu *vcpu,
					int sprn, ulong spr_val);
extern int kvmppc_core_emulate_mfspr_pr(struct kvm_vcpu *vcpu,
					int sprn, ulong *spr_val);
extern int kvmppc_book3s_init_pr(void);
extern void kvmppc_book3s_exit_pr(void);

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
extern void kvmppc_emulate_tabort(struct kvm_vcpu *vcpu, int ra_val);
#else
static inline void kvmppc_emulate_tabort(struct kvm_vcpu *vcpu, int ra_val) {}
#endif

#endif
