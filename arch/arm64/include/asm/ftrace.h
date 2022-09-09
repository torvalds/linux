/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm64/include/asm/ftrace.h
 *
 * Copyright (C) 2013 Linaro Limited
 * Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */
#ifndef __ASM_FTRACE_H
#define __ASM_FTRACE_H

#include <asm/insn.h>

#define HAVE_FUNCTION_GRAPH_FP_TEST

/*
 * HAVE_FUNCTION_GRAPH_RET_ADDR_PTR means that the architecture can provide a
 * "return address pointer" which can be used to uniquely identify a return
 * address which has been overwritten.
 *
 * On arm64 we use the address of the caller's frame record, which remains the
 * same for the lifetime of the instrumented function, unlike the return
 * address in the LR.
 */
#define HAVE_FUNCTION_GRAPH_RET_ADDR_PTR

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
#define ARCH_SUPPORTS_FTRACE_OPS 1
#else
#define MCOUNT_ADDR		((unsigned long)function_nocfi(_mcount))
#endif

/* The BL at the callsite's adjusted rec->ip */
#define MCOUNT_INSN_SIZE	AARCH64_INSN_SIZE

#define FTRACE_PLT_IDX		0
#define FTRACE_REGS_PLT_IDX	1
#define NR_FTRACE_PLTS		2

/*
 * Currently, gcc tends to save the link register after the local variables
 * on the stack. This causes the max stack tracer to report the function
 * frame sizes for the wrong functions. By defining
 * ARCH_FTRACE_SHIFT_STACK_TRACER, it will tell the stack tracer to expect
 * to find the return address on the stack after the local variables have
 * been set up.
 *
 * Note, this may change in the future, and we will need to deal with that
 * if it were to happen.
 */
#define ARCH_FTRACE_SHIFT_STACK_TRACER 1

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
	 * Adjust addr to point at the BL in the callsite.
	 * See ftrace_init_nop() for the callsite sequence.
	 */
	if (IS_ENABLED(CONFIG_DYNAMIC_FTRACE_WITH_REGS))
		return addr + AARCH64_INSN_SIZE;
	/*
	 * addr is the address of the mcount call instruction.
	 * recordmcount does the necessary offset calculation.
	 */
	return addr;
}

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
struct dyn_ftrace;
struct ftrace_ops;
struct ftrace_regs;

int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec);
#define ftrace_init_nop ftrace_init_nop

void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs);
#define ftrace_graph_func ftrace_graph_func
#endif

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

#define ARCH_HAS_SYSCALL_MATCH_SYM_NAME

static inline bool arch_syscall_match_sym_name(const char *sym,
					       const char *name)
{
	/*
	 * Since all syscall functions have __arm64_ prefix, we must skip it.
	 * However, as we described above, we decided to ignore compat
	 * syscalls, so we don't care about __arm64_compat_ prefix here.
	 */
	return !strcmp(sym + 8, name);
}
#endif /* ifndef __ASSEMBLY__ */

#endif /* __ASM_FTRACE_H */
