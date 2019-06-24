/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/include/asm/kvm_coproc.h
 * Copyright (C) 2012 Rusty Russell IBM Corporation
 */

#ifndef __ARM64_KVM_COPROC_H__
#define __ARM64_KVM_COPROC_H__

#include <linux/kvm_host.h>

void kvm_reset_sys_regs(struct kvm_vcpu *vcpu);

struct kvm_sys_reg_table {
	const struct sys_reg_desc *table;
	size_t num;
};

struct kvm_sys_reg_target_table {
	struct kvm_sys_reg_table table64;
	struct kvm_sys_reg_table table32;
};

void kvm_register_target_sys_reg_table(unsigned int target,
				       struct kvm_sys_reg_target_table *table);

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp14_32(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp14_64(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp15_32(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_cp15_64(struct kvm_vcpu *vcpu, struct kvm_run *run);
int kvm_handle_sys_reg(struct kvm_vcpu *vcpu, struct kvm_run *run);

#define kvm_coproc_table_init kvm_sys_reg_table_init
void kvm_sys_reg_table_init(void);

struct kvm_one_reg;
int kvm_arm_copy_sys_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices);
int kvm_arm_sys_reg_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);
int kvm_arm_sys_reg_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);
unsigned long kvm_arm_num_sys_reg_descs(struct kvm_vcpu *vcpu);

#endif /* __ARM64_KVM_COPROC_H__ */
