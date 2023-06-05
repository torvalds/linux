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
#include <asm/csr.h>

struct kvm_aia {
	/* In-kernel irqchip created */
	bool		in_kernel;

	/* In-kernel irqchip initialized */
	bool		initialized;
};

struct kvm_vcpu_aia_csr {
	unsigned long vsiselect;
	unsigned long hviprio1;
	unsigned long hviprio2;
	unsigned long vsieh;
	unsigned long hviph;
	unsigned long hviprio1h;
	unsigned long hviprio2h;
};

struct kvm_vcpu_aia {
	/* CPU AIA CSR context of Guest VCPU */
	struct kvm_vcpu_aia_csr guest_csr;

	/* CPU AIA CSR context upon Guest VCPU reset */
	struct kvm_vcpu_aia_csr guest_reset_csr;
};

#define kvm_riscv_aia_initialized(k)	((k)->arch.aia.initialized)

#define irqchip_in_kernel(k)		((k)->arch.aia.in_kernel)

DECLARE_STATIC_KEY_FALSE(kvm_riscv_aia_available);
#define kvm_riscv_aia_available() \
	static_branch_unlikely(&kvm_riscv_aia_available)

#define KVM_RISCV_AIA_IMSIC_TOPEI	(ISELECT_MASK + 1)
static inline int kvm_riscv_vcpu_aia_imsic_rmw(struct kvm_vcpu *vcpu,
					       unsigned long isel,
					       unsigned long *val,
					       unsigned long new_val,
					       unsigned long wr_mask)
{
	return 0;
}

#ifdef CONFIG_32BIT
void kvm_riscv_vcpu_aia_flush_interrupts(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_aia_sync_interrupts(struct kvm_vcpu *vcpu);
#else
static inline void kvm_riscv_vcpu_aia_flush_interrupts(struct kvm_vcpu *vcpu)
{
}
static inline void kvm_riscv_vcpu_aia_sync_interrupts(struct kvm_vcpu *vcpu)
{
}
#endif
bool kvm_riscv_vcpu_aia_has_interrupts(struct kvm_vcpu *vcpu, u64 mask);

void kvm_riscv_vcpu_aia_update_hvip(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_aia_load(struct kvm_vcpu *vcpu, int cpu);
void kvm_riscv_vcpu_aia_put(struct kvm_vcpu *vcpu);
int kvm_riscv_vcpu_aia_get_csr(struct kvm_vcpu *vcpu,
			       unsigned long reg_num,
			       unsigned long *out_val);
int kvm_riscv_vcpu_aia_set_csr(struct kvm_vcpu *vcpu,
			       unsigned long reg_num,
			       unsigned long val);

int kvm_riscv_vcpu_aia_rmw_topei(struct kvm_vcpu *vcpu,
				 unsigned int csr_num,
				 unsigned long *val,
				 unsigned long new_val,
				 unsigned long wr_mask);
int kvm_riscv_vcpu_aia_rmw_ireg(struct kvm_vcpu *vcpu, unsigned int csr_num,
				unsigned long *val, unsigned long new_val,
				unsigned long wr_mask);
#define KVM_RISCV_VCPU_AIA_CSR_FUNCS \
{ .base = CSR_SIREG,      .count = 1, .func = kvm_riscv_vcpu_aia_rmw_ireg }, \
{ .base = CSR_STOPEI,     .count = 1, .func = kvm_riscv_vcpu_aia_rmw_topei },

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
