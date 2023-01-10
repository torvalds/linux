/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#ifndef __KVM_RISCV_AIA_H
#define __KVM_RISCV_AIA_H

#include <linux/jump_label.h>
#include <linux/kvm_types.h>

struct kvm_aia {
	/* In-kernel irqchip created */
	bool		in_kernel;

	/* In-kernel irqchip initialized */
	bool		initialized;
};

struct kvm_vcpu_aia {
};

#define kvm_riscv_aia_initialized(k)	((k)->arch.aia.initialized)

#define irqchip_in_kernel(k)		((k)->arch.aia.in_kernel)

DECLARE_STATIC_KEY_FALSE(kvm_riscv_aia_available);
#define kvm_riscv_aia_available() \
	static_branch_unlikely(&kvm_riscv_aia_available)

static inline void kvm_riscv_vcpu_aia_flush_interrupts(struct kvm_vcpu *vcpu)
{
}

static inline void kvm_riscv_vcpu_aia_sync_interrupts(struct kvm_vcpu *vcpu)
{
}

static inline bool kvm_riscv_vcpu_aia_has_interrupts(struct kvm_vcpu *vcpu,
						     u64 mask)
{
	return false;
}

static inline void kvm_riscv_vcpu_aia_update_hvip(struct kvm_vcpu *vcpu)
{
}

static inline void kvm_riscv_vcpu_aia_load(struct kvm_vcpu *vcpu, int cpu)
{
}

static inline void kvm_riscv_vcpu_aia_put(struct kvm_vcpu *vcpu)
{
}

static inline int kvm_riscv_vcpu_aia_get_csr(struct kvm_vcpu *vcpu,
					     unsigned long reg_num,
					     unsigned long *out_val)
{
	*out_val = 0;
	return 0;
}

static inline int kvm_riscv_vcpu_aia_set_csr(struct kvm_vcpu *vcpu,
					     unsigned long reg_num,
					     unsigned long val)
{
	return 0;
}

#define KVM_RISCV_VCPU_AIA_CSR_FUNCS

static inline int kvm_riscv_vcpu_aia_update(struct kvm_vcpu *vcpu)
{
	return 1;
}

static inline void kvm_riscv_vcpu_aia_reset(struct kvm_vcpu *vcpu)
{
}

static inline int kvm_riscv_vcpu_aia_init(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline void kvm_riscv_vcpu_aia_deinit(struct kvm_vcpu *vcpu)
{
}

static inline void kvm_riscv_aia_init_vm(struct kvm *kvm)
{
}

static inline void kvm_riscv_aia_destroy_vm(struct kvm *kvm)
{
}

void kvm_riscv_aia_enable(void);
void kvm_riscv_aia_disable(void);
int kvm_riscv_aia_init(void);
void kvm_riscv_aia_exit(void);

#endif
