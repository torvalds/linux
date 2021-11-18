// SPDX-License-Identifier: GPL-2.0
/*
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
#include <asm/kvm_vcpu_sbi.h>

static int kvm_linux_err_map_sbi(int err)
{
	switch (err) {
	case 0:
		return SBI_SUCCESS;
	case -EPERM:
		return SBI_ERR_DENIED;
	case -EINVAL:
		return SBI_ERR_INVALID_PARAM;
	case -EFAULT:
		return SBI_ERR_INVALID_ADDRESS;
	case -EOPNOTSUPP:
		return SBI_ERR_NOT_SUPPORTED;
	default:
		return SBI_ERR_FAILURE;
	};
}

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
	unsigned long i;
	struct kvm_vcpu *tmp;

	kvm_for_each_vcpu(i, tmp, vcpu->kvm)
		tmp->arch.power_off = true;
	kvm_make_all_cpus_request(vcpu->kvm, KVM_REQ_SLEEP);

	memset(&run->system_event, 0, sizeof(run->system_event));
	run->system_event.type = type;
	run->exit_reason = KVM_EXIT_SYSTEM_EVENT;
}

static int kvm_sbi_ext_v01_handler(struct kvm_vcpu *vcpu, struct kvm_run *run,
				      unsigned long *out_val,
				      struct kvm_cpu_trap *utrap,
				      bool *exit)
{
	ulong hmask;
	int i, ret = 0;
	u64 next_cycle;
	struct kvm_vcpu *rvcpu;
	struct cpumask cm, hm;
	struct kvm *kvm = vcpu->kvm;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;

	switch (cp->a7) {
	case SBI_EXT_0_1_CONSOLE_GETCHAR:
	case SBI_EXT_0_1_CONSOLE_PUTCHAR:
		/*
		 * The CONSOLE_GETCHAR/CONSOLE_PUTCHAR SBI calls cannot be
		 * handled in kernel so we forward these to user-space
		 */
		kvm_riscv_vcpu_sbi_forward(vcpu, run);
		*exit = true;
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
			hmask = kvm_riscv_vcpu_unpriv_read(vcpu, false, cp->a0,
							   utrap);
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
		kvm_sbi_system_shutdown(vcpu, run, KVM_SYSTEM_EVENT_SHUTDOWN);
		*exit = true;
		break;
	case SBI_EXT_0_1_REMOTE_FENCE_I:
	case SBI_EXT_0_1_REMOTE_SFENCE_VMA:
	case SBI_EXT_0_1_REMOTE_SFENCE_VMA_ASID:
		if (cp->a0)
			hmask = kvm_riscv_vcpu_unpriv_read(vcpu, false, cp->a0,
							   utrap);
		else
			hmask = (1UL << atomic_read(&kvm->online_vcpus)) - 1;
		if (utrap->scause)
			break;

		cpumask_clear(&cm);
		for_each_set_bit(i, &hmask, BITS_PER_LONG) {
			rvcpu = kvm_get_vcpu_by_id(vcpu->kvm, i);
			if (rvcpu->cpu < 0)
				continue;
			cpumask_set_cpu(rvcpu->cpu, &cm);
		}
		riscv_cpuid_to_hartid_mask(&cm, &hm);
		if (cp->a7 == SBI_EXT_0_1_REMOTE_FENCE_I)
			ret = sbi_remote_fence_i(cpumask_bits(&hm));
		else if (cp->a7 == SBI_EXT_0_1_REMOTE_SFENCE_VMA)
			ret = sbi_remote_hfence_vvma(cpumask_bits(&hm),
						cp->a1, cp->a2);
		else
			ret = sbi_remote_hfence_vvma_asid(cpumask_bits(&hm),
						cp->a1, cp->a2, cp->a3);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

const struct kvm_vcpu_sbi_extension vcpu_sbi_ext_v01 = {
	.extid_start = SBI_EXT_0_1_SET_TIMER,
	.extid_end = SBI_EXT_0_1_SHUTDOWN,
	.handler = kvm_sbi_ext_v01_handler,
};

static const struct kvm_vcpu_sbi_extension *sbi_ext[] = {
	&vcpu_sbi_ext_v01,
};

const struct kvm_vcpu_sbi_extension *kvm_vcpu_sbi_find_ext(unsigned long extid)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(sbi_ext); i++) {
		if (sbi_ext[i]->extid_start <= extid &&
		    sbi_ext[i]->extid_end >= extid)
			return sbi_ext[i];
	}

	return NULL;
}

int kvm_riscv_vcpu_sbi_ecall(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	int ret = 1;
	bool next_sepc = true;
	bool userspace_exit = false;
	struct kvm_cpu_context *cp = &vcpu->arch.guest_context;
	const struct kvm_vcpu_sbi_extension *sbi_ext;
	struct kvm_cpu_trap utrap = { 0 };
	unsigned long out_val = 0;
	bool ext_is_v01 = false;

	sbi_ext = kvm_vcpu_sbi_find_ext(cp->a7);
	if (sbi_ext && sbi_ext->handler) {
		if (cp->a7 >= SBI_EXT_0_1_SET_TIMER &&
		    cp->a7 <= SBI_EXT_0_1_SHUTDOWN)
			ext_is_v01 = true;
		ret = sbi_ext->handler(vcpu, run, &out_val, &utrap, &userspace_exit);
	} else {
		/* Return error for unsupported SBI calls */
		cp->a0 = SBI_ERR_NOT_SUPPORTED;
		goto ecall_done;
	}

	/* Handle special error cases i.e trap, exit or userspace forward */
	if (utrap.scause) {
		/* No need to increment sepc or exit ioctl loop */
		ret = 1;
		utrap.sepc = cp->sepc;
		kvm_riscv_vcpu_trap_redirect(vcpu, &utrap);
		next_sepc = false;
		goto ecall_done;
	}

	/* Exit ioctl loop or Propagate the error code the guest */
	if (userspace_exit) {
		next_sepc = false;
		ret = 0;
	} else {
		/**
		 * SBI extension handler always returns an Linux error code. Convert
		 * it to the SBI specific error code that can be propagated the SBI
		 * caller.
		 */
		ret = kvm_linux_err_map_sbi(ret);
		cp->a0 = ret;
		ret = 1;
	}
ecall_done:
	if (next_sepc)
		cp->sepc += 4;
	if (!ext_is_v01)
		cp->a1 = out_val;

	return ret;
}

#else

int kvm_riscv_vcpu_sbi_ecall(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	kvm_riscv_vcpu_sbi_forward(vcpu, run);
	return 0;
}

#endif
