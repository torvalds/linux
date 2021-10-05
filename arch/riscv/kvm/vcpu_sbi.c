// SPDX-License-Identifier: GPL-2.0
/**
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *     Atish Patra <atish.patra@wdc.com>
 */

#include <linux/errno.h>
#include <linux/err.h>
#include <linux/kvm_host.h>
#include <asm/csr.h>
#include <asm/sbi.h>
#include <asm/kvm_vcpu_timer.h>

#define SBI_VERSION_MAJOR			0
#define SBI_VERSION_MINOR			1

static void kvm_riscv_vcpu_sbi_forward(struct kvm_vcpu *vcpu,
				       struct kvm_run *run)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;

	vcpu->arch.sbi_context.return_handled = 0;
	vcpu->stat.ecall_exit_stat++;
	run->exit_reason = KVM_EXIT_RISCV_SBI;
	run->riscv_sbi.extension_id = cp->a7;
	run->riscv_sbi.function_id = cp->a6;
	run->riscv_sbi.args[0] = cp->a0;
	run->riscv_sbi.args[1] = cp->a1;
	run->riscv_sbi.args[2] = cp->a2;
	run->riscv_sbi.args[3] = cp->a3;
	run->riscv_sbi.args[4] = cp->a4;
	run->riscv_sbi.args[5] = cp->a5;
	run->riscv_sbi.ret[0] = cp->a0;
	run->riscv_sbi.ret[1] = cp->a1;
}

int kvm_riscv_vcpu_sbi_return(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;

	/* Handle SBI return only once */
	if (vcpu->arch.sbi_context.return_handled)
		return 0;
	vcpu->arch.sbi_context.return_handled = 1;

	/* Update return values */
	cp->a0 = run->riscv_sbi.ret[0];
	cp->a1 = run->riscv_sbi.ret[1];

	/* Move to next instruction */
	vcpu->arch.guest_context.sepc += 4;

	return 0;
}

#ifdef CONFIG_RISCV_SBI_V01

static void kvm_sbi_system_shutdown(struct kvm_vcpu *vcpu,
				    struct kvm_run *run, u32 type)
{
	int i;
	struct kvm_vcpu *tmp;

	kvm_for_each_vcpu(i, tmp, vcpu->kvm)
		tmp->arch.power_off = true;
	kvm_make_all_cpus_request(vcpu->kvm, KVM_REQ_SLEEP);

	memset(&run->system_event, 0, sizeof(run->system_event));
	run->system_event.type = type;
	run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
}

int kvm_riscv_vcpu_sbi_ecall(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	ulong hmask;
	int i, ret = 1;
	u64 next_cycle;
	struct kvm_vcpu *rvcpu;
	bool next_sepc = true;
	struct cpumask cm, hm;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_cpu_trap utrap = { 0 };
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;

	if (!cp)
		return -EINVAL;

	switch (cp->a7) {
	case SBI_EXT_0_1_CONSOLE_GETCHAR:
	case SBI_EXT_0_1_CONSOLE_PUTCHAR:
		/*
		 * The CONSOLE_GETCHAR/CONSOLE_PUTCHAR SBI calls cannot be
		 * handled in kernel so we forward these to user-space
		 */
		kvm_riscv_vcpu_sbi_forward(vcpu, run);
		next_sepc = false;
		ret = 0;
		break;
	case SBI_EXT_0_1_SET_TIMER:
#if __riscv_xlen == 32
		next_cycle = ((u64)cp->a1 << 32) | (u64)cp->a0;
#else
		next_cycle = (u64)cp->a0;
#endif
		kvm_riscv_vcpu_timer_next_event(vcpu, next_cycle);
		break;
	case SBI_EXT_0_1_CLEAR_IPI:
		kvm_riscv_vcpu_unset_interrupt(vcpu, IRQ_VS_SOFT);
		break;
	case SBI_EXT_0_1_SEND_IPI:
		if (cp->a0)
			hmask = kvm_riscv_vcpu_unpriv_read(vcpu, false, cp->a0,
							   &utrap);
		else
			hmask = (1UL << atomic_read(&kvm->online_vcpus)) - 1;
		if (utrap.scause) {
			utrap.sepc = cp->sepc;
			kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);
			next_sepc = false;
			break;
		}
		for_each_set_bit(i, &hmask, BITS_PER_LONG) {
			rvcpu = kvm_get_vcpu_by_id(vcpu->kvm, i);
			kvm_riscv_vcpu_set_interrupt(rvcpu, IRQ_VS_SOFT);
		}
		break;
	case SBI_EXT_0_1_SHUTDOWN:
		kvm_sbi_system_shutdown(vcpu, run, KVM_SYSTEM_EVENT_SHUTDOWN);
		next_sepc = false;
		ret = 0;
		break;
	case SBI_EXT_0_1_REMOTE_FENCE_I:
	case SBI_EXT_0_1_REMOTE_SFENCE_VMA:
	case SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID:
		if (cp->a0)
			hmask = kvm_riscv_vcpu_unpriv_read(vcpu, false, cp->a0,
							   &utrap);
		else
			hmask = (1UL << atomic_read(&kvm->online_vcpus)) - 1;
		if (utrap.scause) {
			utrap.sepc = cp->sepc;
			kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);
			next_sepc = false;
			break;
		}
		cpumask_clear(&cm);
		for_each_set_bit(i, &hmask, BITS_PER_LONG) {
			rvcpu = kvm_get_vcpu_by_id(vcpu->kvm, i);
			if (rvcpu->cpu < 0)
				continue;
			cpumask_set_cpu(rvcpu->cpu, &cm);
		}
		riscv_cpuid_to_hartid_mask(&cm, &hm);
		if (cp->a7 == SBI_EXT_0_1_REMOTE_FENCE_I)
			sbi_remote_fence_i(cpumask_bits(&hm));
		else if (cp->a7 == SBI_EXT_0_1_REMOTE_SFENCE_VMA)
			sbi_remote_hfence_vvma(cpumask_bits(&hm),
						cp->a1, cp->a2);
		else
			sbi_remote_hfence_vvma_asid(cpumask_bits(&hm),
						cp->a1, cp->a2, cp->a3);
		break;
	default:
		/* Return error for unsupported SBI calls */
		cp->a0 = SBI_ERR_NOT_SUPPORTED;
		break;
	};

	if (next_sepc)
		cp->sepc += 4;

	return ret;
}

#else

int kvm_riscv_vcpu_sbi_ecall(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	kvm_riscv_vcpu_sbi_forward(vcpu, run);
	return 0;
}

#endif
