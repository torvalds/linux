/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Rusty Russell IBM Corporation
 */

#ifndef __ARM_KVM_COPROC_H__
#define __ARM_KVM_COPROC_H__
#include <linux/kvm_host.h>

void kvm_reset_coprocs(struct kvm_vcpu *vcpu);

struct kvm_coproc_target_table {
	unsigned target;
	const struct coproc_reg *table;
	size_t num;
};
void kvm_register_target_coproc_table(struct kvm_coproc_target_table *table);

int kvm_handle_cp10_id(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp_0_13_access(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp14_32(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp14_64(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp15_32(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp15_64(struct kvm_vcpu *vcpu, struct kvm_run *run);

unsigned long kvm_arm_num_guest_msrs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_msrindices(struct kvm_vcpu *vcpu, u64 __user *uindices);
void kvm_coproc_table_init(void);

struct kvm_one_reg;
int kvm_arm_copy_coproc_indices(struct kvm_vcpu *vcpu, u64 __user *uindices);
int kvm_arm_coproc_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);
int kvm_arm_coproc_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);
unsigned long kvm_arm_num_coproc_regs(struct kvm_vcpu *vcpu);
#endif /* __ARM_KVM_COPROC_H__ */
