// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 */

#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/kvm_host.h>

#include <asm/kvm_vcpu_sbi.h>
#include <asm/sbi.h>

void kvm_riscv_vcpu_sbi_sta_reset(struct kvm_vcpu *vcpu)
{
	vcpu->arch.sta.shmem = INVALID_GPA;
	vcpu->arch.sta.last_steal = 0;
}

void kvm_riscv_vcpu_record_steal_time(struct kvm_vcpu *vcpu)
{
	gpa_t shmem = vcpu->arch.sta.shmem;

	if (shmem == INVALID_GPA)
		return;
}

static int kvm_sbi_sta_steal_time_set_shmem(struct kvm_vcpu *vcpu)
{
	return SBI_ERR_FAILURE;
}

static int kvm_sbi_ext_sta_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				   struct kvm_vcpu_sbi_return *retdata)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	unsigned long funcid = cp->a6;
	int ret;

	switch (funcid) {
	case SBI_EXT_STA_STEAL_TIME_SET_SHMEM:
		ret = kvm_sbi_sta_steal_time_set_shmem(vcpu);
		break;
	default:
		ret = SBI_ERR_NOT_SUPPORTED;
		break;
	}

	retdata->err_val = ret;

	return 0;
}

static unsigned long kvm_sbi_ext_sta_probe(struct kvm_vcpu *vcpu)
{
	return 0;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_sta = {
	.extid_start = SBI_EXT_STA,
	.extid_end = SBI_EXT_STA,
	.handler = kvm_sbi_ext_sta_handler,
	.probe = kvm_sbi_ext_sta_probe,
};

int kvm_riscv_vcpu_get_reg_sbi_sta(struct kvm_vcpu *vcpu,
				   unsigned long reg_num,
				   unsigned long *reg_val)
{
	switch (reg_num) {
	case KVM_REG_RISCV_SBI_STA_REG(shmem_lo):
		*reg_val = (unsigned long)vcpu->arch.sta.shmem;
		break;
	case KVM_REG_RISCV_SBI_STA_REG(shmem_hi):
		if (IS_ENABLED(CONFIG_32BIT))
			*reg_val = upper_32_bits(vcpu->arch.sta.shmem);
		else
			*reg_val = 0;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int kvm_riscv_vcpu_set_reg_sbi_sta(struct kvm_vcpu *vcpu,
				   unsigned long reg_num,
				   unsigned long reg_val)
{
	switch (reg_num) {
	case KVM_REG_RISCV_SBI_STA_REG(shmem_lo):
		if (IS_ENABLED(CONFIG_32BIT)) {
			gpa_t hi = upper_32_bits(vcpu->arch.sta.shmem);

			vcpu->arch.sta.shmem = reg_val;
			vcpu->arch.sta.shmem |= hi << 32;
		} else {
			vcpu->arch.sta.shmem = reg_val;
		}
		break;
	case KVM_REG_RISCV_SBI_STA_REG(shmem_hi):
		if (IS_ENABLED(CONFIG_32BIT)) {
			gpa_t lo = lower_32_bits(vcpu->arch.sta.shmem);

			vcpu->arch.sta.shmem = ((gpa_t)reg_val << 32);
			vcpu->arch.sta.shmem |= lo;
		} else if (reg_val != 0) {
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
