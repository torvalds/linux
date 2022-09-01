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
#include <asm/stacktrace/nvhe.h>

DEFINE_PER_CPU(unsigned long [NVHE_STACKTRACE_SIZE/sizeof(long)], pkvm_stacktrace);

static struct stack_info stackinfo_get_overflow(void)
{
	unsigned long low = (unsigned long)this_cpu_ptr(overflow_stack);
	unsigned long high = low + OVERFLOW_STACK_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}

static struct stack_info stackinfo_get_hyp(void)
{
	struct kvm_nvhe_init_params *params = this_cpu_ptr(&kvm_init_params);
	unsigned long high = params->stack_hyp_va;
	unsigned long low = high - PAGE_SIZE;

	return (struct stack_info) {
		.low = low,
		.high = high,
	};
}

static int unwind_next(struct unwind_state *state)
{
	return unwind_next_frame_record(state);
}

static void notrace unwind(struct unwind_state *state,
			   stack_trace_consume_fn consume_entry,
			   void *cookie)
{
	while (1) {
		int ret;

		if (!consume_entry(cookie, state->pc))
			break;
		ret = unwind_next(state);
		if (ret < 0)
			break;
	}
}

/*
 * pkvm_save_backtrace_entry - Saves a protected nVHE HYP stacktrace entry
 *
 * @arg    : index of the entry in the stacktrace buffer
 * @where  : the program counter corresponding to the stack frame
 *
 * Save the return address of a stack frame to the shared stacktrace buffer.
 * The host can access this shared buffer from EL1 to dump the backtrace.
 */
static bool pkvm_save_backtrace_entry(void *arg, unsigned long where)
{
	unsigned long *stacktrace = this_cpu_ptr(pkvm_stacktrace);
	int *idx = (int *)arg;

	/*
	 * Need 2 free slots: 1 for current entry and 1 for the
	 * delimiter.
	 */
	if (*idx > ARRAY_SIZE(pkvm_stacktrace) - 2)
		return false;

	stacktrace[*idx] = where;
	stacktrace[++*idx] = 0UL;

	return true;
}

/*
 * pkvm_save_backtrace - Saves the protected nVHE HYP stacktrace
 *
 * @fp : frame pointer at which to start the unwinding.
 * @pc : program counter at which to start the unwinding.
 *
 * Save the unwinded stack addresses to the shared stacktrace buffer.
 * The host can access this shared buffer from EL1 to dump the backtrace.
 */
static void pkvm_save_backtrace(unsigned long fp, unsigned long pc)
{
	struct stack_info stacks[] = {
		stackinfo_get_overflow(),
		stackinfo_get_hyp(),
	};
	struct unwind_state state = {
		.stacks = stacks,
		.nr_stacks = ARRAY_SIZE(stacks),
	};
	int idx = 0;

	kvm_nvhe_unwind_init(&state, fp, pc);

	unwind(&state, pkvm_save_backtrace_entry, &idx);
}
#else /* !CONFIG_PROTECTED_NVHE_STACKTRACE */
static void pkvm_save_backtrace(unsigned long fp, unsigned long pc)
{
}
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
		pkvm_save_backtrace(fp, pc);
	else
		hyp_prepare_backtrace(fp, pc);
}
