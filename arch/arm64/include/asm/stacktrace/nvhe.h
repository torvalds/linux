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

/**
 * kvm_nvhe_unwind_init() - Start an unwind from the given nVHE HYP fp and pc
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

#ifndef __KVM_NVHE_HYPERVISOR__
/*
 * Conventional (non-protected) nVHE HYP stack unwinder
 *
 * In non-protected mode, the unwinding is done from kernel proper context
 * (by the host in EL1).
 */

DECLARE_KVM_NVHE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack);
DECLARE_KVM_NVHE_PER_CPU(struct kvm_nvhe_stacktrace_info, kvm_stacktrace_info);
DECLARE_PER_CPU(unsigned long, kvm_arm_hyp_stack_base);

void kvm_nvhe_dump_backtrace(unsigned long hyp_offset);

#endif	/* __KVM_NVHE_HYPERVISOR__ */
#endif	/* __ASM_STACKTRACE_NVHE_H */
