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

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
#define ARCH_SUPPORTS_FTRACE_OPS 1
#else
#define MCOUNT_ADDR		((unsigned long)_mcount)
#endif

/* The BL at the callsite's adjusted rec->ip */
#define MCOUNT_INSN_SIZE	AARCH64_INSN_SIZE

#define FTRACE_PLT_IDX		0
#define NR_FTRACE_PLTS		1

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

unsigned long ftrace_call_adjust(unsigned long addr);

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_ARGS
#define HAVE_ARCH_FTRACE_REGS
struct dyn_ftrace;
struct ftrace_ops;
struct ftrace_regs;
#define arch_ftrace_regs(fregs) ((struct __arch_ftrace_regs *)(fregs))

#define arch_ftrace_get_regs(regs) NULL

/*
 * Note: sizeof(struct ftrace_regs) must be a multiple of 16 to ensure correct
 * stack alignment
 */
struct __arch_ftrace_regs {
	/* x0 - x8 */
	unsigned long regs[9];

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
	unsigned long direct_tramp;
#else
	unsigned long __unused;
#endif

	unsigned long fp;
	unsigned long lr;

	unsigned long sp;
	unsigned long pc;
};

static __always_inline unsigned long
ftrace_regs_get_instruction_pointer(const struct ftrace_regs *fregs)
{
	return arch_ftrace_regs(fregs)->pc;
}

static __always_inline void
ftrace_regs_set_instruction_pointer(struct ftrace_regs *fregs,
				    unsigned long pc)
{
	arch_ftrace_regs(fregs)->pc = pc;
}

static __always_inline unsigned long
ftrace_regs_get_stack_pointer(const struct ftrace_regs *fregs)
{
	return arch_ftrace_regs(fregs)->sp;
}

static __always_inline unsigned long
ftrace_regs_get_argument(struct ftrace_regs *fregs, unsigned int n)
{
	if (n < 8)
		return arch_ftrace_regs(fregs)->regs[n];
	return 0;
}

static __always_inline unsigned long
ftrace_regs_get_return_value(const struct ftrace_regs *fregs)
{
	return arch_ftrace_regs(fregs)->regs[0];
}

static __always_inline void
ftrace_regs_set_return_value(struct ftrace_regs *fregs,
			     unsigned long ret)
{
	arch_ftrace_regs(fregs)->regs[0] = ret;
}

static __always_inline void
ftrace_override_function_with_return(struct ftrace_regs *fregs)
{
	arch_ftrace_regs(fregs)->pc = arch_ftrace_regs(fregs)->lr;
}

int ftrace_regs_query_register_offset(const char *name);

int ftrace_init_nop(struct module *mod, struct dyn_ftrace *rec);
#define ftrace_init_nop ftrace_init_nop

void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs);
#define ftrace_graph_func ftrace_graph_func

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS
static inline void arch_ftrace_set_direct_caller(struct ftrace_regs *fregs,
						 unsigned long addr)
{
	/*
	 * The ftrace trampoline will return to this address instead of the
	 * instrumented function.
	 */
	arch_ftrace_regs(fregs)->direct_tramp = addr;
}
#endif /* CONFIG_DYNAMIC_FTRACE_WITH_DIRECT_CALLS */

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

#ifndef __ASSEMBLY__
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
struct fgraph_ret_regs {
	/* x0 - x7 */
	unsigned long regs[8];

	unsigned long fp;
	unsigned long __unused;
};

static inline unsigned long fgraph_ret_regs_return_value(struct fgraph_ret_regs *ret_regs)
{
	return ret_regs->regs[0];
}

static inline unsigned long fgraph_ret_regs_frame_pointer(struct fgraph_ret_regs *ret_regs)
{
	return ret_regs->fp;
}

void prepare_ftrace_return(unsigned long self_addr, unsigned long *parent,
			   unsigned long frame_pointer);

#endif /* ifdef CONFIG_FUNCTION_GRAPH_TRACER  */
#endif

#endif /* __ASM_FTRACE_H */
