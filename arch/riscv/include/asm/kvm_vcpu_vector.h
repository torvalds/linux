/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 SiFive
 *
 * Authors:
 *     Vincent Chen <vincent.chen@sifive.com>
 *     Greentime Hu <greentime.hu@sifive.com>
 */

#ifndef __KVM_VCPU_RISCV_VECTOR_H
#define __KVM_VCPU_RISCV_VECTOR_H

#include <linux/types.h>

#ifdef CONFIG_RISCV_ISA_V
#include <asm/vector.h>
#include <asm/kvm_host.h>

static __always_inline void __kvm_riscv_vector_save(struct kvm_cpu_context *context)
{
	__riscv_v_vstate_save(&context->vector, context->vector.datap);
}

static __always_inline void __kvm_riscv_vector_restore(struct kvm_cpu_context *context)
{
	__riscv_v_vstate_restore(&context->vector, context->vector.datap);
}

void kvm_riscv_vcpu_vector_reset(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_guest_vector_save(struct kvm_cpu_context *cntx,
				      unsigned long *isa);
void kvm_riscv_vcpu_guest_vector_restore(struct kvm_cpu_context *cntx,
					 unsigned long *isa);
void kvm_riscv_vcpu_host_vector_save(struct kvm_cpu_context *cntx);
void kvm_riscv_vcpu_host_vector_restore(struct kvm_cpu_context *cntx);
int kvm_riscv_vcpu_alloc_vector_context(struct kvm_vcpu *vcpu);
void kvm_riscv_vcpu_free_vector_context(struct kvm_vcpu *vcpu);
void kvm_riscv_register_vctx_callback(void (*func)(void));
void kvm_riscv_unregister_vctx_callback(void);
void kvm_riscv_vcpu_flush_vector(void);

static inline void kvm_riscv_v_init(void)
{
	if (has_vector())
		kvm_riscv_register_vctx_callback(&kvm_riscv_vcpu_flush_vector);
}

static inline void kvm_riscv_v_exit(void)
{
	if (has_vector())
		kvm_riscv_unregister_vctx_callback();
}

#else

struct kvm_cpu_context;

static inline void kvm_riscv_vcpu_vector_reset(struct kvm_vcpu *vcpu)
{
}

static inline void kvm_riscv_vcpu_guest_vector_save(struct kvm_cpu_context *cntx,
						    unsigned long *isa)
{
}

static inline void kvm_riscv_vcpu_guest_vector_restore(struct kvm_cpu_context *cntx,
						       unsigned long *isa)
{
}

static inline void kvm_riscv_vcpu_host_vector_save(struct kvm_cpu_context *cntx)
{
}

static inline void kvm_riscv_vcpu_host_vector_restore(struct kvm_cpu_context *cntx)
{
}

static inline int kvm_riscv_vcpu_alloc_vector_context(struct kvm_vcpu *vcpu)
{
	return 0;
}

static inline void kvm_riscv_vcpu_free_vector_context(struct kvm_vcpu *vcpu)
{
}

static inline void kvm_riscv_v_init(void)
{
}

static inline void kvm_riscv_v_exit(void)
{
}
#endif

int kvm_riscv_vcpu_get_reg_vector(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg);
int kvm_riscv_vcpu_set_reg_vector(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg);
#endif
