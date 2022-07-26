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
 * Copyright (C) 2022 Google LLC
 */
#ifndef __ASM_STACKTRACE_NVHE_H
#define __ASM_STACKTRACE_NVHE_H

#include <asm/stacktrace/common.h>

static inline bool on_accessible_stack(const struct task_struct *tsk,
				       unsigned long sp, unsigned long size,
				       struct stack_info *info)
{
	return false;
}

#ifndef __KVM_NVHE_HYPERVISOR__
/*
 * Conventional (non-protected) nVHE HYP stack unwinder
 *
 * In non-protected mode, the unwinding is done from kernel proper context
 * (by the host in EL1).
 */

static inline bool on_overflow_stack(unsigned long sp, unsigned long size,
				     struct stack_info *info)
{
	return false;
}

static inline int notrace unwind_next(struct unwind_state *state)
{
	return 0;
}
NOKPROBE_SYMBOL(unwind_next);

#endif	/* !__KVM_NVHE_HYPERVISOR__ */
#endif	/* __ASM_STACKTRACE_NVHE_H */
