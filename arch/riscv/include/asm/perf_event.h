/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 SiFive
 * Copyright (C) 2018 Andes Technology Corporation
 *
 */

#ifndef _ASM_RISCV_PERF_EVENT_H
#define _ASM_RISCV_PERF_EVENT_H

#ifdef CONFIG_PERF_EVENTS
#include <linux/perf_event.h>
#define perf_arch_bpf_user_pt_regs(regs) (struct user_regs_struct *)regs

#define perf_arch_fetch_caller_regs(regs, __ip) { \
	(regs)->epc = (__ip); \
	(regs)->s0 = (unsigned long) __builtin_frame_address(0); \
	(regs)->sp = current_stack_pointer; \
	(regs)->status = SR_PP; \
}
#endif

#endif /* _ASM_RISCV_PERF_EVENT_H */
