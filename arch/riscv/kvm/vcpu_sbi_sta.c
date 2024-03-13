// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Ventana Micro Systems Inc.
 */

#include <linux/kconfig.h>
#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/sizes.h>

#include <asm/bug.h>
#include <asm/current.h>
#include <asm/kvm_vcpu_sbi.h>
#include <asm/page.h>
#include <asm/sbi.h>
#include <asm/uaccess.h>

void kvm_riscv_vcpu_sbi_sta_reset(struct kvm_vcpu *vcpu)
{
	vcpu->arch.sta.shmem = INVALID_GPA;
	vcpu->arch.sta.last_steal = 0;
}

void kvm_riscv_vcpu_record_steal_time(struct kvm_vcpu *vcpu)
{
	gpa_t shmem = vcpu->arch.sta.shmem;
	u64 last_steal = vcpu->arch.sta.last_steal;
	u32 *sequence_ptr, sequence;
	u64 *steal_ptr, steal;
	unsigned long hva;
	gfn_t gfn;

	if (shmem == INVALID_GPA)
		return;

	/*
	 * shmem is 64-byte aligned (see the enforcement in
	 * kvm_sbi_sta_steal_time_set_shmem()) and the size of sbi_sta_struct
	 * is 64 bytes, so we know all its offsets are in the same page.
	 */
	gfn = shmem >> PAGE_SHIFT;
	hva = kvm_vcpu_gfn_to_hva(vcpu, gfn);

	if (WARN_ON(kvm_is_error_hva(hva))) {
		vcpu->arch.sta.shmem = INVALID_GPA;
		return;
	}

	sequence_ptr = (u32 *)(hva + offset_in_page(shmem) +
			       offsetof(struct sbi_sta_struct, sequence));
	steal_ptr = (u64 *)(hva + offset_in_page(shmem) +
			    offsetof(struct sbi_sta_struct, steal));

	if (WARN_ON(get_user(sequence, sequence_ptr)))
		return;

	sequence = le32_to_cpu(sequence);
	sequence += 1;

	if (WARN_ON(put_user(cpu_to_le32(sequence), sequence_ptr)))
		return;

	if (!WARN_ON(get_user(steal, steal_ptr))) {
		steal = le64_to_cpu(steal);
		vcpu->arch.sta.last_steal = READ_ONCE(current->sched_info.run_delay);
		steal += vcpu->arch.sta.last_steal - last_steal;
		WARN_ON(put_user(cpu_to_le64(steal), steal_ptr));
	}

	sequence += 1;
	WARN_ON(put_user(cpu_to_le32(sequence), sequence_ptr));

	kvm_vcpu_mark_page_dirty(vcpu, gfn);
}

static int kvm_sbi_sta_steal_time_set_shmem(struct kvm_vcpu *vcpu)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	unsigned long shmem_phys_lo = cp->a0;
	unsigned long shmem_phys_hi = cp->a1;
	u32 flags = cp->a2;
	struct sbi_sta_struct zero_sta = {0};
	unsigned long hva;
	bool writable;
	gpa_t shmem;
	int ret;

	if (flags != 0)
		return SBI_ERR_INVALID_PARAM;

	if (shmem_phys_lo == SBI_STA_SHMEM_DISABLE &&
	    shmem_phys_hi == SBI_STA_SHMEM_DISABLE) {
		vcpu->arch.sta.shmem = INVALID_GPA;
		return 0;
	}

	if (shmem_phys_lo & (SZ_64 - 1))
		return SBI_ERR_INVALID_PARAM;

	shmem = shmem_phys_lo;

	if (shmem_phys_hi != 0) {
		if (IS_ENABLED(CONFIG_32BIT))
			shmem |= ((gpa_t)shmem_phys_hi << 32);
		else
			return SBI_ERR_INVALID_ADDRESS;
	}

	hva = kvm_vcpu_gfn_to_hva_prot(vcpu, shmem >> PAGE_SHIFT, &writable);
	if (kvm_is_error_hva(hva) || !writable)
		return SBI_ERR_INVALID_ADDRESS;

	ret = kvm_vcpu_write_guest(vcpu, shmem, &zero_sta, sizeof(zero_sta));
	if (ret)
		return SBI_ERR_FAILURE;

	vcpu->arch.sta.shmem = shmem;
	vcpu->arch.sta.last_steal = current->sched_info.run_delay;

	return 0;
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
	return !!sched_info_on();
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
