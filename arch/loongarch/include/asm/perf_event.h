/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#ifndef __LOONGARCH_PERF_EVENT_H__
#define __LOONGARCH_PERF_EVENT_H__

#include <asm/ptrace.h>

#define perf_arch_bpf_user_pt_regs(regs) (struct user_pt_regs *)regs

#define perf_arch_fetch_caller_regs(regs, __ip) { \
	(regs)->csr_era = (__ip); \
	(regs)->regs[3] = current_stack_pointer; \
	(regs)->regs[22] = (unsigned long) __builtin_frame_address(0); \
}

#endif /* __LOONGARCH_PERF_EVENT_H__ */
