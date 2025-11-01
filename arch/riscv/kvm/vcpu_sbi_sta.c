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

static void kvm_riscv_vcpu_sbi_sta_reset(struct kvm_vcpu *vcpu)
{
	vcpu->arch.sta.shmem = INVALID_GPA;
	vcpu->arch.sta.last_steal = 0;
}

void kvm_riscv_vcpu_record_steal_time(struct kvm_vcpu *vcpu)
{
	gpa_t shmem = vcpu->arch.sta.shmem;
	u64 last_steal = vcpu->arch.sta.last_steal;
	__le32 __user *sequence_ptr;
	__le64 __user *steal_ptr;
	__le32 sequence_le;
	__le64 steal_le;
	u32 sequence;
	u64 steal;
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

	sequence_ptr = (__le32 __user *)(hva + offset_in_page(shmem) +
			       offsetof(struct sbi_sta_struct, sequence));
	steal_ptr = (__le64 __user *)(hva + offset_in_page(shmem) +
			    offsetof(struct sbi_sta_struct, steal));

	if (WARN_ON(get_user(sequence_le, sequence_ptr)))
		return;

	sequence = le32_to_cpu(sequence_le);
	sequence += 1;

	if (WARN_ON(put_user(cpu_to_le32(sequence), sequence_ptr)))
		return;

	if (!WARN_ON(get_user(steal_le, steal_ptr))) {
		steal = le64_to_cpu(steal_le);
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
	gpa_t shmem;
	int ret;

	if (flags != 0)
		return SBI_ERR_INVALID_PARAM;

	if (shmem_phys_lo == SBI_SHMEM_DISABLE &&
	    shmem_phys_hi == SBI_SHMEM_DISABLE) {
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

	/* No need to check writable slot explicitly as kvm_vcpu_write_guest does it internally */
	ret = kvm_vcpu_write_guest(vcpu, shmem, &zero_sta, sizeof(zero_sta));
	if (ret)
		return SBI_ERR_INVALID_ADDRESS;

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

static unsigned long kvm_sbi_ext_sta_get_state_reg_count(struct kvm_vcpu *vcpu)
{
	return sizeof(struct kvm_riscv_sbi_sta) / sizeof(unsigned long);
}

static int kvm_sbi_ext_sta_get_reg(struct kvm_vcpu *vcpu, unsigned long reg_num,
				   unsigned long reg_size, void *reg_val)
{
	unsigned long *value;

	if (reg_size != sizeof(unsigned long))
		return -EINVAL;
	value = reg_val;

	switch (reg_num) {
	case KVM_REG_RISCV_SBI_STA_REG(shmem_lo):
		*value = (unsigned long)vcpu->arch.sta.shmem;
		break;
	case KVM_REG_RISCV_SBI_STA_REG(shmem_hi):
		if (IS_ENABLED(CONFIG_32BIT))
			*value = upper_32_bits(vcpu->arch.sta.shmem);
		else
			*value = 0;
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static int kvm_sbi_ext_sta_set_reg(struct kvm_vcpu *vcpu, unsigned long reg_num,
				   unsigned long reg_size, const void *reg_val)
{
	unsigned long value;

	if (reg_size != sizeof(unsigned long))
		return -EINVAL;
	value = *(const unsigned long *)reg_val;

	switch (reg_num) {
	case KVM_REG_RISCV_SBI_STA_REG(shmem_lo):
		if (IS_ENABLED(CONFIG_32BIT)) {
			gpa_t hi = upper_32_bits(vcpu->arch.sta.shmem);

			vcpu->arch.sta.shmem = value;
			vcpu->arch.sta.shmem |= hi << 32;
		} else {
			vcpu->arch.sta.shmem = value;
		}
		break;
	case KVM_REG_RISCV_SBI_STA_REG(shmem_hi):
		if (IS_ENABLED(CONFIG_32BIT)) {
			gpa_t lo = lower_32_bits(vcpu->arch.sta.shmem);

			vcpu->arch.sta.shmem = ((gpa_t)value << 32);
			vcpu->arch.sta.shmem |= lo;
		} else if (value != 0) {
			return -EINVAL;
		}
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_sta = {
	.extid_start = SBI_EXT_STA,
	.extid_end = SBI_EXT_STA,
	.handler = kvm_sbi_ext_sta_handler,
	.probe = kvm_sbi_ext_sta_probe,
	.reset = kvm_riscv_vcpu_sbi_sta_reset,
	.state_reg_subtype = KVM_REG_RISCV_SBI_STA,
	.get_state_reg_count = kvm_sbi_ext_sta_get_state_reg_count,
	.get_state_reg = kvm_sbi_ext_sta_get_reg,
	.set_state_reg = kvm_sbi_ext_sta_set_reg,
};
