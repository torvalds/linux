// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <asm/sbi.h>
#include <asm/kvm_vcpu_timer.h>
#include <asm/kvm_vcpu_sbi.h>

static int kvm_sbi_ext_time_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				    unsigned long *out_val,
				    struct kvm_cpu_trap *utrap, bool *exit)
{
	int ret = 0;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	u64 next_cycle;

	if (cp->a6 != SBI_EXT_TIME_SET_TIMER)
		return -EINVAL;

#if __riscv_xlen == 32
	next_cycle = ((u64)cp->a1 << 32) | (u64)cp->a0;
#else
	next_cycle = (u64)cp->a0;
#endif
	kvm_riscv_vcpu_timer_next_event(vcpu, next_cycle);

	return ret;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_time = {
	.extid_start = SBI_EXT_TIME,
	.extid_end = SBI_EXT_TIME,
	.handler = kvm_sbi_ext_time_handler,
};

static int kvm_sbi_ext_ipi_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				   unsigned long *out_val,
				   struct kvm_cpu_trap *utrap, bool *exit)
{
	int ret = 0;
	unsigned long i;
	struct kvm_vcpu *tmp;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	unsigned long hmask = cp->a0;
	unsigned long hbase = cp->a1;

	if (cp->a6 != SBI_EXT_IPI_SEND_IPI)
		return -EINVAL;

	kvm_for_each_vcpu(i, tmp, vcpu->kvm) {
		if (hbase != -1UL) {
			if (tmp->vcpu_id < hbase)
				continue;
			if (!(hmask & (1UL << (tmp->vcpu_id - hbase))))
				continue;
		}
		ret = kvm_riscv_vcpu_set_interrupt(tmp, IRQ_VS_SOFT);
		if (ret < 0)
			break;
	}

	return ret;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_ipi = {
	.extid_start = SBI_EXT_IPI,
	.extid_end = SBI_EXT_IPI,
	.handler = kvm_sbi_ext_ipi_handler,
};

static int kvm_sbi_ext_rfence_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				      unsigned long *out_val,
				      struct kvm_cpu_trap *utrap, bool *exit)
{
	int ret = 0;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	unsigned long hmask = cp->a0;
	unsigned long hbase = cp->a1;
	unsigned long funcid = cp->a6;

	switch (funcid) {
	case SBI_EXT_RFENCE_REMOTE_FENCE_I:
		kvm_riscv_fence_i(vcpu->kvm, hbase, hmask);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA:
		if (cp->a2 == 0 && cp->a3 == 0)
			kvm_riscv_hfence_vvma_all(vcpu->kvm, hbase, hmask);
		else
			kvm_riscv_hfence_vvma_gva(vcpu->kvm, hbase, hmask,
						  cp->a2, cp->a3, PAGE_SHIFT);
		break;
	case SBI_EXT_RFENCE_REMOTE_SFENCE_VMA_ASID:
		if (cp->a2 == 0 && cp->a3 == 0)
			kvm_riscv_hfence_vvma_asid_all(vcpu->kvm,
						       hbase, hmask, cp->a4);
		else
			kvm_riscv_hfence_vvma_asid_gva(vcpu->kvm,
						       hbase, hmask,
						       cp->a2, cp->a3,
						       PAGE_SHIFT, cp->a4);
		break;
	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA:
	case SBI_EXT_RFENCE_REMOTE_HFENCE_GVMA_VMID:
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA:
	case SBI_EXT_RFENCE_REMOTE_HFENCE_VVMA_ASID:
		/*
		 * Until nested virtualization is implemented, the
		 * SBI HFENCE calls should be treated as NOPs
		 */
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_rfence = {
	.extid_start = SBI_EXT_RFENCE,
	.extid_end = SBI_EXT_RFENCE,
	.handler = kvm_sbi_ext_rfence_handler,
};

static int kvm_sbi_ext_srst_handler(struct kvm_vcpu *vcpu,
				    struct kvm_run *run,
				    unsigned long *out_val,
				    struct kvm_cpu_trap *utrap, bool *exit)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	unsigned long funcid = cp->a6;
	u32 reason = cp->a1;
	u32 type = cp->a0;
	int ret = 0;

	switch (funcid) {
	case SBI_EXT_SRST_RESET:
		switch (type) {
		case SBI_SRST_RESET_TYPE_SHUTDOWN:
			kvm_riscv_vcpu_sbi_system_reset(vcpu, run,
						KVM_SYSTEM_EVENT_SHUTDOWN,
						reason);
			*exit = true;
			break;
		case SBI_SRST_RESET_TYPE_COLD_REBOOT:
		case SBI_SRST_RESET_TYPE_WARM_REBOOT:
			kvm_riscv_vcpu_sbi_system_reset(vcpu, run,
						KVM_SYSTEM_EVENT_RESET,
						reason);
			*exit = true;
			break;
		default:
			ret = -EOPNOTSUPP;
		}
		break;
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_srst = {
	.extid_start = SBI_EXT_SRST,
	.extid_end = SBI_EXT_SRST,
	.handler = kvm_sbi_ext_srst_handler,
};
