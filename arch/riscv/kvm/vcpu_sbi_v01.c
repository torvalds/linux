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

static int kvm_sbi_ext_v01_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				   struct kvm_vcpu_sbi_return *retdata)
{
	ulong hmask;
	int i, ret = 0;
	u64 next_cycle;
	struct kvm_vcpu *rvcpu;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	struct kvm_cpu_trap *utrap = retdata->utrap;
	unsigned long vmid;

	switch (cp->a7) {
	case SBI_EXT_0_1_CONSOLE_GETCHAR:
	case SBI_EXT_0_1_CONSOLE_PUTCHAR:
		/*
		 * The CONSOLE_GETCHAR/CONSOLE_PUTCHAR SBI calls cannot be
		 * handled in kernel so we forward these to user-space
		 */
		kvm_riscv_vcpu_sbi_forward(vcpu, run);
		retdata->uexit = true;
		break;
	case SBI_EXT_0_1_SET_TIMER:
#if __riscv_xlen == 32
		next_cycle = ((u64)cp->a1 << 32) | (u64)cp->a0;
#else
		next_cycle = (u64)cp->a0;
#endif
		ret = kvm_riscv_vcpu_timer_next_event(vcpu, next_cycle);
		break;
	case SBI_EXT_0_1_CLEAR_IPI:
		ret = kvm_riscv_vcpu_unset_interrupt(vcpu, IRQ_VS_SOFT);
		break;
	case SBI_EXT_0_1_SEND_IPI:
		if (cp->a0)
			hmask = kvm_riscv_vcpu_unpriv_read(vcpu, false, cp->a0, utrap);
		else
			hmask = (1UL << atomic_read(&kvm->online_vcpus)) - 1;
		if (utrap->scause)
			break;

		for_each_set_bit(i, &hmask, BITS_PER_LONG) {
			rvcpu = kvm_get_vcpu_by_id(vcpu->kvm, i);
			ret = kvm_riscv_vcpu_set_interrupt(rvcpu, IRQ_VS_SOFT);
			if (ret < 0)
				break;
		}
		break;
	case SBI_EXT_0_1_SHUTDOWN:
		kvm_riscv_vcpu_sbi_system_reset(vcpu, run,
						KVM_SYSTEM_EVENT_SHUTDOWN, 0);
		retdata->uexit = true;
		break;
	case SBI_EXT_0_1_REMOTE_FENCE_I:
	case SBI_EXT_0_1_REMOTE_SFENCE_VMA:
	case SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID:
		if (cp->a0)
			hmask = kvm_riscv_vcpu_unpriv_read(vcpu, false, cp->a0, utrap);
		else
			hmask = (1UL << atomic_read(&kvm->online_vcpus)) - 1;
		if (utrap->scause)
			break;

		if (cp->a7 == SBI_EXT_0_1_REMOTE_FENCE_I)
			kvm_riscv_fence_i(vcpu->kvm, 0, hmask);
		else if (cp->a7 == SBI_EXT_0_1_REMOTE_SFENCE_VMA) {
			vmid = READ_ONCE(vcpu->kvm->arch.vmid.vmid);
			if (cp->a1 == 0 && cp->a2 == 0)
				kvm_riscv_hfence_vvma_all(vcpu->kvm, 0, hmask, vmid);
			else
				kvm_riscv_hfence_vvma_gva(vcpu->kvm, 0, hmask, cp->a1,
							  cp->a2, PAGE_SHIFT, vmid);
		} else {
			vmid = READ_ONCE(vcpu->kvm->arch.vmid.vmid);
			if (cp->a1 == 0 && cp->a2 == 0)
				kvm_riscv_hfence_vvma_asid_all(vcpu->kvm, 0, hmask,
							       cp->a3, vmid);
			else
				kvm_riscv_hfence_vvma_asid_gva(vcpu->kvm, 0, hmask,
							       cp->a1, cp->a2, PAGE_SHIFT,
							       cp->a3, vmid);
		}
		break;
	default:
		retdata->err_val = SBI_ERR_NOT_SUPPORTED;
		break;
	}

	return ret;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_v01 = {
	.extid_start = SBI_EXT_0_1_SET_TIMER,
	.extid_end = SBI_EXT_0_1_SHUTDOWN,
	.handler = kvm_sbi_ext_v01_handler,
};
