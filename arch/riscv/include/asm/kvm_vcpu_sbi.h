/* SPDX-License-Identifier: GPL-2.0-only */
/**
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 */

#ifndef __RISCV_KVM_VCPU_SBI_H__
#define __RISCV_KVM_VCPU_SBI_H__

#define KVM_SBI_IMPID 3

#define KVM_SBI_VERSION_MAJOR 0
#define KVM_SBI_VERSION_MINOR 2

struct kvm_vcpu_sbi_extension {
	unsigned long extid_start;
	unsigned long extid_end;
	/**
	 * SBI extension handler. It can be defined for a given extension or group of
	 * extension. But it should always return linux error codes rather than SBI
	 * specific error codes.
	 */
	int (*handler)(struct kvm_vcpu *vcpu, struct kvm_run *run,
		       unsigned long *out_val, struct kvm_cpu_trap *utrap,
		       bool *exit);
};

void kvm_riscv_vcpu_sbi_forward(struct kvm_vcpu *vcpu, struct kvm_run *run);
const struct kvm_vcpu_sbi_extension *kvm_vcpu_sbi_find_ext(unsigned long extid);

#endif /* __RISCV_KVM_VCPU_SBI_H__ */
