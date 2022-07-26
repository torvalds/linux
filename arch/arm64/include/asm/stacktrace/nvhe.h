/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * KVM nVHE hypervisor stack tracing support.
 *
 * The unwinder implementation depends on the nVHE mode:
 *
 *   1) Non-protected nVHE mode - the host can directly access the
 *      HYP stack pages and unwind the HYP stack in EL1. This saves having
 *      to allocate shared buffers for the host to read the unwinded
 *      stacktrace.
 *
 *   2) pKVM (protected nVHE) mode - the host cannot directly access
 *      the HYP memory. The stack is unwinded in EL2 and dumped to a shared
 *      buffer where the host can read and print the stacktrace.
 *
 * Copyright (C) 2022 Google LLC
 */
#ifndef __ASM_STACKTRACE_NVHE_H
#define __ASM_STACKTRACE_NVHE_H

#include <asm/stacktrace/common.h>

/*
 * kvm_nvhe_unwind_init - Start an unwind from the given nVHE HYP fp and pc
 *
 * @state : unwind_state to initialize
 * @fp    : frame pointer at which to start the unwinding.
 * @pc    : program counter at which to start the unwinding.
 */
static inline void kvm_nvhe_unwind_init(struct unwind_state *state,
					unsigned long fp,
					unsigned long pc)
{
	unwind_init_common(state, NULL);

	state->fp = fp;
	state->pc = pc;
}

static inline bool on_hyp_stack(unsigned long sp, unsigned long size,
				struct stack_info *info);

static inline bool on_accessible_stack(const struct task_struct *tsk,
				       unsigned long sp, unsigned long size,
				       struct stack_info *info)
{
	if (on_accessible_stack_common(tsk, sp, size, info))
		return true;

	if (on_hyp_stack(sp, size, info))
		return true;

	return false;
}

#ifdef __KVM_NVHE_HYPERVISOR__
/*
 * Protected nVHE HYP stack unwinder
 *
 * In protected mode, the unwinding is done by the hypervisor in EL2.
 */

#ifdef CONFIG_PROTECTED_NVHE_STACKTRACE
static inline bool on_overflow_stack(unsigned long sp, unsigned long size,
				     struct stack_info *info)
{
	return false;
}

static inline bool on_hyp_stack(unsigned long sp, unsigned long size,
				struct stack_info *info)
{
	return false;
}

static inline int notrace unwind_next(struct unwind_state *state)
{
	return 0;
}
NOKPROBE_SYMBOL(unwind_next);
#endif	/* CONFIG_PROTECTED_NVHE_STACKTRACE */

#else	/* !__KVM_NVHE_HYPERVISOR__ */
/*
 * Conventional (non-protected) nVHE HYP stack unwinder
 *
 * In non-protected mode, the unwinding is done from kernel proper context
 * (by the host in EL1).
 */

DECLARE_KVM_NVHE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack);
DECLARE_KVM_NVHE_PER_CPU(struct kvm_nvhe_stacktrace_info, kvm_stacktrace_info);
DECLARE_PER_CPU(unsigned long, kvm_arm_hyp_stack_page);

/*
 * kvm_nvhe_stack_kern_va - Convert KVM nVHE HYP stack addresses to a kernel VAs
 *
 * The nVHE hypervisor stack is mapped in the flexible 'private' VA range, to
 * allow for guard pages below the stack. Consequently, the fixed offset address
 * translation macros won't work here.
 *
 * The kernel VA is calculated as an offset from the kernel VA of the hypervisor
 * stack base.
 *
 * Returns true on success and updates @addr to its corresponding kernel VA;
 * otherwise returns false.
 */
static inline bool kvm_nvhe_stack_kern_va(unsigned long *addr,
					  enum stack_type type)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info;
	unsigned long hyp_base, kern_base, hyp_offset;

	stacktrace_info = this_cpu_ptr_nvhe_sym(kvm_stacktrace_info);

	switch (type) {
	case STACK_TYPE_HYP:
		kern_base = (unsigned long)*this_cpu_ptr(&kvm_arm_hyp_stack_page);
		hyp_base = (unsigned long)stacktrace_info->stack_base;
		break;
	case STACK_TYPE_OVERFLOW:
		kern_base = (unsigned long)this_cpu_ptr_nvhe_sym(overflow_stack);
		hyp_base = (unsigned long)stacktrace_info->overflow_stack_base;
		break;
	default:
		return false;
	}

	hyp_offset = *addr - hyp_base;

	*addr = kern_base + hyp_offset;

	return true;
}

static inline bool on_overflow_stack(unsigned long sp, unsigned long size,
				     struct stack_info *info)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info
				= this_cpu_ptr_nvhe_sym(kvm_stacktrace_info);
	unsigned long low = (unsigned long)stacktrace_info->overflow_stack_base;
	unsigned long high = low + OVERFLOW_STACK_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_OVERFLOW, info);
}

static inline bool on_hyp_stack(unsigned long sp, unsigned long size,
				struct stack_info *info)
{
	struct kvm_nvhe_stacktrace_info *stacktrace_info
				= this_cpu_ptr_nvhe_sym(kvm_stacktrace_info);
	unsigned long low = (unsigned long)stacktrace_info->stack_base;
	unsigned long high = low + PAGE_SIZE;

	return on_stack(sp, size, low, high, STACK_TYPE_HYP, info);
}

static inline int notrace unwind_next(struct unwind_state *state)
{
	struct stack_info info;

	return unwind_next_common(state, &info, kvm_nvhe_stack_kern_va);
}
NOKPROBE_SYMBOL(unwind_next);

#endif	/* __KVM_NVHE_HYPERVISOR__ */
#endif	/* __ASM_STACKTRACE_NVHE_H */
