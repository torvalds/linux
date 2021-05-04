/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_PERF_EVENT_H
#define __ASM_CSKY_PERF_EVENT_H

#include <abi/regdef.h>

#define perf_arch_fetch_caller_regs(regs, __ip) { \
	(regs)->pc = (__ip); \
	regs_fp(regs) = (unsigned long) __builtin_frame_address(0); \
	asm volatile("mov %0, sp\n":"=r"((regs)->usp)); \
}

#endif /* __ASM_PERF_EVENT_ELF_H */
