/*
 * arch/arm64/include/asm/ftrace.h
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_FTRACE_H
#define __ASM_FTRACE_H

#include <asm/insn.h>

#define MCOUNT_ADDR		((unsigned long)_mcount)
#define MCOUNT_INSN_SIZE	AARCH64_INSN_SIZE

#ifndef __ASSEMBLY__
#include <linux/compat.h>

extern void _mcount(unsigned long);
extern void *return_address(unsigned int);

struct dyn_arch_ftrace {
	/* No extra data needed for arm64 */
};

extern unsigned long ftrace_graph_call;

extern void return_to_handler(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	/*
	 * addr is the address of the mcount call instruction.
	 * recordmcount does the necessary offset calculation.
	 */
	return addr;
}

#define ftrace_return_address(n) return_address(n)

/*
 * Because AArch32 mode does not share the same syscall table with AArch64,
 * tracing compat syscalls may result in reporting bogus syscalls or even
 * hang-up, so just do not trace them.
 * See kernel/trace/trace_syscalls.c
 *
 * x86 code says:
 * If the user really wants these, then they should use the
 * raw syscall tracepoints with filtering.
 */
#define ARCH_TRACE_IGNORE_COMPAT_SYSCALLS
static inline bool arch_trace_is_compat_syscall(struct pt_regs *regs)
{
	return is_compat_task();
}
#endif /* ifndef __ASSEMBLY__ */

#endif /* __ASM_FTRACE_H */
