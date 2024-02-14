// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 *     Anup Patel <anup.patel@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>
#include <asm/cpufeature.h>

#ifdef CONFIG_FPU
void kvm_riscv_vcpu_fp_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;

	cntx->sstatus &= ~SR_FS;
	if (riscv_isa_extension_available(vcpu->arch.isa, f) ||
	    riscv_isa_extension_available(vcpu->arch.isa, d))
		cntx->sstatus |= SR_FS_INITIAL;
	else
		cntx->sstatus |= SR_FS_OFF;
}

static void kvm_riscv_vcpu_fp_clean(struct kvm_cpu_context *cntx)
{
	cntx->sstatus &= ~SR_FS;
	cntx->sstatus |= SR_FS_CLEAN;
}

void kvm_riscv_vcpu_guest_fp_save(struct kvm_cpu_context *cntx,
				  const unsigned long *isa)
{
	if ((cntx->sstatus & SR_FS) == SR_FS_DIRTY) {
		if (riscv_isa_extension_available(isa, d))
			__kvm_riscv_fp_d_save(cntx);
		else if (riscv_isa_extension_available(isa, f))
			__kvm_riscv_fp_f_save(cntx);
		kvm_riscv_vcpu_fp_clean(cntx);
	}
}

void kvm_riscv_vcpu_guest_fp_restore(struct kvm_cpu_context *cntx,
				     const unsigned long *isa)
{
	if ((cntx->sstatus & SR_FS) != SR_FS_OFF) {
		if (riscv_isa_extension_available(isa, d))
			__kvm_riscv_fp_d_restore(cntx);
		else if (riscv_isa_extension_available(isa, f))
			__kvm_riscv_fp_f_restore(cntx);
		kvm_riscv_vcpu_fp_clean(cntx);
	}
}

void kvm_riscv_vcpu_host_fp_save(struct kvm_cpu_context *cntx)
{
	/* No need to check host sstatus as it can be modified outside */
	if (riscv_isa_extension_available(NULL, d))
		__kvm_riscv_fp_d_save(cntx);
	else if (riscv_isa_extension_available(NULL, f))
		__kvm_riscv_fp_f_save(cntx);
}

void kvm_riscv_vcpu_host_fp_restore(struct kvm_cpu_context *cntx)
{
	if (riscv_isa_extension_available(NULL, d))
		__kvm_riscv_fp_d_restore(cntx);
	else if (riscv_isa_extension_available(NULL, f))
		__kvm_riscv_fp_f_restore(cntx);
}
#endif

int kvm_riscv_vcpu_get_reg_fp(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      unsigned long rtype)
{
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    rtype);
	void *reg_val;

	if ((rtype == KVM_REG_RISCV_FP_F) &&
	    riscv_isa_extension_available(vcpu->arch.isa, f)) {
		if (KVM_REG_SIZE(reg->id) != sizeof(u32))
			return -EINVAL;
		if (reg_num == KVM_REG_RISCV_FP_F_REG(fcsr))
			reg_val = &cntx->fp.f.fcsr;
		else if ((KVM_REG_RISCV_FP_F_REG(f[0]) <= reg_num) &&
			  reg_num <= KVM_REG_RISCV_FP_F_REG(f[31]))
			reg_val = &cntx->fp.f.f[reg_num];
		else
			return -ENOENT;
	} else if ((rtype == KVM_REG_RISCV_FP_D) &&
		   riscv_isa_extension_available(vcpu->arch.isa, d)) {
		if (reg_num == KVM_REG_RISCV_FP_D_REG(fcsr)) {
			if (KVM_REG_SIZE(reg->id) != sizeof(u32))
				return -EINVAL;
			reg_val = &cntx->fp.d.fcsr;
		} else if ((KVM_REG_RISCV_FP_D_REG(f[0]) <= reg_num) &&
			   reg_num <= KVM_REG_RISCV_FP_D_REG(f[31])) {
			if (KVM_REG_SIZE(reg->id) != sizeof(u64))
				return -EINVAL;
			reg_val = &cntx->fp.d.f[reg_num];
		} else
			return -ENOENT;
	} else
		return -ENOENT;

	if (copy_to_user(uaddr, reg_val, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}

int kvm_riscv_vcpu_set_reg_fp(struct kvm_vcpu *vcpu,
			      const struct kvm_one_reg *reg,
			      unsigned long rtype)
{
	struct kvm_cpu_context *cntx = &vcpu->arch.guest_context;
	unsigned long __user *uaddr =
			(unsigned long __user *)(unsigned long)reg->addr;
	unsigned long reg_num = reg->id & ~(KVM_REG_ARCH_MASK |
					    KVM_REG_SIZE_MASK |
					    rtype);
	void *reg_val;

	if ((rtype == KVM_REG_RISCV_FP_F) &&
	    riscv_isa_extension_available(vcpu->arch.isa, f)) {
		if (KVM_REG_SIZE(reg->id) != sizeof(u32))
			return -EINVAL;
		if (reg_num == KVM_REG_RISCV_FP_F_REG(fcsr))
			reg_val = &cntx->fp.f.fcsr;
		else if ((KVM_REG_RISCV_FP_F_REG(f[0]) <= reg_num) &&
			  reg_num <= KVM_REG_RISCV_FP_F_REG(f[31]))
			reg_val = &cntx->fp.f.f[reg_num];
		else
			return -ENOENT;
	} else if ((rtype == KVM_REG_RISCV_FP_D) &&
		   riscv_isa_extension_available(vcpu->arch.isa, d)) {
		if (reg_num == KVM_REG_RISCV_FP_D_REG(fcsr)) {
			if (KVM_REG_SIZE(reg->id) != sizeof(u32))
				return -EINVAL;
			reg_val = &cntx->fp.d.fcsr;
		} else if ((KVM_REG_RISCV_FP_D_REG(f[0]) <= reg_num) &&
			   reg_num <= KVM_REG_RISCV_FP_D_REG(f[31])) {
			if (KVM_REG_SIZE(reg->id) != sizeof(u64))
				return -EINVAL;
			reg_val = &cntx->fp.d.f[reg_num];
		} else
			return -ENOENT;
	} else
		return -ENOENT;

	if (copy_from_user(reg_val, uaddr, KVM_REG_SIZE(reg->id)))
		return -EFAULT;

	return 0;
}
