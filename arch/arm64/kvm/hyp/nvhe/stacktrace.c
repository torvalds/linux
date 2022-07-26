// SPDX-License-Identifier: GPL-2.0-only
/*
 * KVM nVHE hypervisor stack tracing support.
 *
 * Copyright (C) 2022 Google LLC
 */
#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>
#include <asm/memory.h>
#include <asm/percpu.h>

DEFINE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack)
	__aligned(16);

DEFINE_PER_CPU(struct kvm_nvhe_stacktrace_info, kvm_stacktrace_info);

/*
 * hyp_prepare_backtrace - Prepare non-protected nVHE backtrace.
 *
 * @fp : frame pointer at which to start the unwinding.
 * @pc : program counter at which to start the unwinding.
 *
 * Save the information needed by the host to unwind the non-protected
 * nVHE hypervisor stack in EL1.
 */
static void hyp_prepare_backtrace(unsigned long fp, unsigned long pc)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info = this_cpu_ptr(&kvm_stacktrace_info);
	struct kvm_nvhe_init_params *params = this_cpu_ptr(&kvm_init_params);

	stacktrace_info->stack_base = (unsigned long)(params->stack_hyp_va - PAGE_SIZE);
	stacktrace_info->overflow_stack_base = (unsigned long)this_cpu_ptr(overflow_stack);
	stacktrace_info->fp = fp;
	stacktrace_info->pc = pc;
}

#ifdef CONFIG_PROTECTED_NVHE_STACKTRACE
DEFINE_PER_CPU(unsigned long [NVHE_STACKTRACE_SIZE/sizeof(long)], pkvm_stacktrace);
#endif /* CONFIG_PROTECTED_NVHE_STACKTRACE */

/*
 * kvm_nvhe_prepare_backtrace - prepare to dump the nVHE backtrace
 *
 * @fp : frame pointer at which to start the unwinding.
 * @pc : program counter at which to start the unwinding.
 *
 * Saves the information needed by the host to dump the nVHE hypervisor
 * backtrace.
 */
void kvm_nvhe_prepare_backtrace(unsigned long fp, unsigned long pc)
{
	if (is_protected_kvm_enabled())
		return;
	else
		hyp_prepare_backtrace(fp, pc);
}
