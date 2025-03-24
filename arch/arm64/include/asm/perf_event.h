/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */

#ifndef __ASM_PERF_EVENT_H
#define __ASM_PERF_EVENT_H

#include <asm/stack_pointer.h>
#include <asm/ptrace.h>

#ifdef CONFIG_PERF_EVENTS
#define perf_arch_bpf_user_pt_regs(regs) &regs->user_regs
#endif

#define perf_arch_fetch_caller_regs(regs, __ip) { \
	(regs)->pc = (__ip);    \
	(regs)->regs[29] = (unsigned long) __builtin_frame_address(0); \
	(regs)->sp = current_stack_pointer; \
	(regs)->pstate = PSR_MODE_EL1h;	\
}

#endif
