/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Google LLC
 * Author: Fuad Tabba <tabba@google.com>
 */

#ifndef __ARM64_KVM_NVHE_PKVM_H__
#define __ARM64_KVM_NVHE_PKVM_H__

#include <asm/kvm_pkvm.h>

#include <nvhe/gfp.h>
#include <nvhe/spinlock.h>

/*
 * Holds the relevant data for maintaining the vcpu state completely at hyp.
 */
struct pkvm_hyp_vcpu {
	struct kvm_vcpu vcpu;

	/* Backpointer to the host's (untrusted) vCPU instance. */
	struct kvm_vcpu *host_vcpu;
};

/*
 * Holds the relevant data for running a protected vm.
 */
struct pkvm_hyp_vm {
	struct kvm kvm;

	/* Backpointer to the host's (untrusted) KVM instance. */
	struct kvm *host_kvm;

	/* The guest's stage-2 page-table managed by the hypervisor. */
	struct kvm_pgtable pgt;
	struct kvm_pgtable_mm_ops mm_ops;
	struct hyp_pool pool;
	hyp_spinlock_t lock;

	/*
	 * The number of vcpus initialized and ready to run.
	 * Modifying this is protected by 'vm_table_lock'.
	 */
	unsigned int nr_vcpus;

	/* Array of the hyp vCPU structures for this VM. */
	struct pkvm_hyp_vcpu *vcpus[];
};

static inline struct pkvm_hyp_vm *
pkvm_hyp_vcpu_to_hyp_vm(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	return container_of(hyp_vcpu->vcpu.kvm, struct pkvm_hyp_vm, kvm);
}

static inline bool pkvm_hyp_vcpu_is_protected(struct pkvm_hyp_vcpu *hyp_vcpu)
{
	return vcpu_is_protected(&hyp_vcpu->vcpu);
}

void pkvm_hyp_vm_table_init(void *tbl);
void pkvm_host_fpsimd_state_init(void);

int __pkvm_init_vm(struct kvm *host_kvm, unsigned long vm_hva,
		   unsigned long pgd_hva);
int __pkvm_init_vcpu(pkvm_handle_t handle, struct kvm_vcpu *host_vcpu,
		     unsigned long vcpu_hva);
int __pkvm_teardown_vm(pkvm_handle_t handle);

struct pkvm_hyp_vcpu *pkvm_load_hyp_vcpu(pkvm_handle_t handle,
					 unsigned int vcpu_idx);
void pkvm_put_hyp_vcpu(struct pkvm_hyp_vcpu *hyp_vcpu);

#endif /* __ARM64_KVM_NVHE_PKVM_H__ */
