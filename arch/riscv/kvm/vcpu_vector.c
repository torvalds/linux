// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 SiFive
 *
 * Authors:
 *     Vincent Chen <vincent.chen@sifive.com>
 *     Greentime Hu <greentime.hu@sifive.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <asm/hwcap.h>
#include <asm/kvm_vcpu_vector.h>
#include <asm/vector.h>

#ifdef CONFIG_RISCV_ISA_V
void kvm_riscv_vcpu_vector_reset(struct kvm_vcpu *vcpu)
{
	unsigned long *isa = vcpu->arch.isa;
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;

	cntx->sstatus &= ~SR_VS;
	if (riscv_isa_extension_available(isa, v)) {
		cntx->sstatus |= SR_VS_INITIAL;
		WARN_ON(!cntx->vector.datap);
		memset(cntx->vector.datap, 0, riscv_v_vsize);
	} else {
		cntx->sstatus |= SR_VS_OFF;
	}
}

static void kvm_riscv_vcpu_vector_clean(struct kvm_cpu_context *cntx)
{
	cntx->sstatus &= ~SR_VS;
	cntx->sstatus |= SR_VS_CLEAN;
}

void kvm_riscv_vcpu_guest_vector_save(struct kvm_cpu_context *cntx,
				      unsigned long *isa)
{
	if ((cntx->sstatus & SR_VS) == SR_VS_DIRTY) {
		if (riscv_isa_extension_available(isa, v))
			__kvm_riscv_vector_save(cntx);
		kvm_riscv_vcpu_vector_clean(cntx);
	}
}

void kvm_riscv_vcpu_guest_vector_restore(struct kvm_cpu_context *cntx,
					 unsigned long *isa)
{
	if ((cntx->sstatus & SR_VS) != SR_VS_OFF) {
		if (riscv_isa_extension_available(isa, v))
			__kvm_riscv_vector_restore(cntx);
		kvm_riscv_vcpu_vector_clean(cntx);
	}
}

void kvm_riscv_vcpu_host_vector_save(struct kvm_cpu_context *cntx)
{
	/* No need to check host sstatus as it can be modified outside */
	if (riscv_isa_extension_available(NULL, v))
		__kvm_riscv_vector_save(cntx);
}

void kvm_riscv_vcpu_host_vector_restore(struct kvm_cpu_context *cntx)
{
	if (riscv_isa_extension_available(NULL, v))
		__kvm_riscv_vector_restore(cntx);
}

int kvm_riscv_vcpu_alloc_vector_context(struct kvm_vcpu *vcpu,
					struct kvm_cpu_context *cntx)
{
	cntx->vector.datap = kmalloc(riscv_v_vsize, GFP_KERNEL);
	if (!cntx->vector.datap)
		return -ENOMEM;

	vcpu->arch.host_context.vector.datap = kzalloc(riscv_v_vsize, GFP_KERNEL);
	if (!vcpu->arch.host_context.vector.datap)
		return -ENOMEM;

	return 0;
}

void kvm_riscv_vcpu_free_vector_context(struct kvm_vcpu *vcpu)
{
	kfree(vcpu->arch.guest_reset_context.vector.datap);
	kfree(vcpu->arch.host_context.vector.datap);
}
#endif

static void *kvm_riscv_vcpu_vreg_addr(struct kvm_vcpu *vcpu,
				      unsigned long reg_num,
				      size_t reg_size)
{
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;
	void *reg_val;
	size_t vlenb = riscv_v_vsize / 32;

	if (reg_num < KVM_REG_RISCV_VECTOR_REG(0)) {
		if (reg_size != sizeof(unsigned long))
			return NULL;
		switch (reg_num) {
		case KVM_REG_RISCV_VECTOR_CSR_REG(vstart):
			reg_val = &cntx->vector.vstart;
			break;
		case KVM_REG_RISCV_VECTOR_CSR_REG(vl):
			reg_val = &cntx->vector.vl;
			break;
		case KVM_REG_RISCV_VECTOR_CSR_REG(vtype):
			reg_val = &cntx->vector.vtype;
			break;
		case KVM_REG_RISCV_VECTOR_CSR_REG(vcsr):
			reg_val = &cntx->vector.vcsr;
			break;
		case KVM_REG_RISCV_VECTOR_CSR_REG(datap):
		default:
			return NULL;
		}
	} else if (reg_num <= KVM_REG_RISCV_VECTOR_REG(31)) {
		if (reg_size != vlenb)
			return NULL;
		reg_val = cntx->vector.datap
			  + (reg_num - KVM_REG_RISCV_VECTOR_REG(0)) * vlenb;
	} else {
		return NULL;
	}

	return reg_val;
}

int kvm_riscv_vcpu_get_reg_vector(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg,
				  unsigned long rtype)
{
	unsigned long *isa = vcpu->arch.isa;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    rtype);
	void *reg_val = NULL;
	size_t reg_size = KVM_REG_SIZE(reg->id);

	if (rtype == KVM_REG_RISCV_VECTOR &&
	    riscv_isa_extension_available(isa, v)) {
		reg_val = kvm_riscv_vcpu_vreg_addr(vcpu, reg_num, reg_size);
	}

	if (!reg_val)
		return -EINVAL;

	if (copy_to_user(uaddr, reg_val, reg_size))
		return -EFAULT;

	return 0;
}

int kvm_riscv_vcpu_set_reg_vector(struct kvm_vcpu *vcpu,
				  const struct kvm_one_reg *reg,
				  unsigned long rtype)
{
	unsigned long *isa = vcpu->arch.isa;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    rtype);
	void *reg_val = NULL;
	size_t reg_size = KVM_REG_SIZE(reg->id);

	if (rtype == KVM_REG_RISCV_VECTOR &&
	    riscv_isa_extension_available(isa, v)) {
		reg_val = kvm_riscv_vcpu_vreg_addr(vcpu, reg_num, reg_size);
	}

	if (!reg_val)
		return -EINVAL;

	if (copy_from_user(reg_val, uaddr, reg_size))
		return -EFAULT;

	return 0;
}
