/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 *     Anup Patel <anup.patel@wdc.com>
 */

#ifndef __KVM_VCPU_RISCV_FP_H
#define __KVM_VCPU_RISCV_FP_H

#include <linux/types.h>

struct kvm_cpu_context;

#ifdef CONFIG_FPU
void __kvm_riscv_fp_f_save(struct kvm_cpu_context *context);
void __kvm_riscv_fp_f_restore(struct kvm_cpu_context *context);
void __kvm_riscv_fp_d_save(struct kvm_cpu_context *context);
void __kvm_riscv_fp_d_restore(struct kvm_cpu_context *context);

void kvm_riscv_vcpu_fp_reset(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_guest_fp_save(struct kvm_cpu_context *cntx,
				  const unsigned long *isa);
void kvm_riscv_vcpu_guest_fp_restore(struct kvm_cpu_context *cntx,
				     const unsigned long *isa);
void kvm_riscv_vcpu_host_fp_save(struct kvm_cpu_context *cntx);
void kvm_riscv_vcpu_host_fp_restore(struct kvm_cpu_context *cntx);
#else
static inline void kvm_riscv_vcpu_fp_reset(struct kvm_vcpu *vcpu)
{
}
static inline void kvm_riscv_vcpu_guest_fp_save(struct kvm_cpu_context *cntx,
						const unsigned long *isa)
{
}
static inline void kvm_riscv_vcpu_guest_fp_restore(
					struct kvm_cpu_context *cntx,
					const unsigned long *isa)
{
}
static inline void kvm_riscv_vcpu_host_fp_save(struct kvm_cpu_context *cntx)
{
}
static inline void kvm_riscv_vcpu_host_fp_restore(
					struct kvm_cpu_context *cntx)
{
}
#endif

int kvm_riscv_vcpu_get_reg_fp(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      unsigned long rtype);
int kvm_riscv_vcpu_set_reg_fp(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      unsigned long rtype);

#endif
