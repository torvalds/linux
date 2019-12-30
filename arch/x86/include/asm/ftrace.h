/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_FTRACE_H
#define _ASM_X86_FTRACE_H

#ifdef CONFIG_FUNCTION_TRACER
#ifndef CC_USING_FENTRY
# error Compiler does not support fentry?
#endif
# define MCOUNT_ADDR		((unsigned long)(__fentry__))
#define MCOUNT_INSN_SIZE	5 /* sizeof mcount call */

#ifdef CONFIG_DYNAMIC_FTRACE
#define ARCH_SUPPORTS_FTRACE_OPS 1
#endif

#define HAVE_FUNCTION_GRAPH_RET_ADDR_PTR

#ifndef __ASSEMBLY__
extern atomic_t modifying_ftrace_code;
extern void __fentry__(void);

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	/*
	 * addr is the address of the mcount call instruction.
	 * recordmcount does the necessary offset calculation.
	 */
	return addr;
}

/*
 * When a ftrace registered caller is tracing a function that is
 * also set by a register_ftrace_direct() call, it needs to be
 * differentiated in the ftrace_caller trampoline. To do this, we
 * place the direct caller in the ORIG_AX part of pt_regs. This
 * tells the ftrace_caller that there's a direct caller.
 */
static inline void arch_ftrace_set_direct_caller(struct pt_regs *regs, unsigned long addr)
{
	/* Emulate a call */
	regs->orig_ax = addr;
}

#ifdef CONFIG_DYNAMIC_FTRACE

struct dyn_arch_ftrace {
	/* No extra data needed for x86 */
};

int ftrace_int3_handler(struct pt_regs *regs);

#define FTRACE_GRAPH_TRAMP_ADDR FTRACE_GRAPH_ADDR

#endif /*  CONFIG_DYNAMIC_FTRACE */
#endif /* __ASSEMBLY__ */
#endif /* CONFIG_FUNCTION_TRACER */


#ifndef __ASSEMBLY__

#define ARCH_HAS_SYSCALL_MATCH_SYM_NAME
static inline bool arch_syscall_match_sym_name(const char *sym, const char *name)
{
	/*
	 * Compare the symbol name with the system call name. Skip the
	 * "__x64_sys", "__ia32_sys" or simple "sys" prefix.
	 */
	return !strcmp(sym + 3, name + 3) ||
		(!strncmp(sym, "__x64_", 6) && !strcmp(sym + 9, name + 3)) ||
		(!strncmp(sym, "__ia32_", 7) && !strcmp(sym + 10, name + 3));
}

#ifndef COMPILE_OFFSETS

#if defined(CONFIG_FTRACE_SYSCALLS) && defined(CONFIG_IA32_EMULATION)
#include <linux/compat.h>

/*
 * Because ia32 syscalls do not map to x86_64 syscall numbers
 * this screws up the trace output when tracing a ia32 task.
 * Instead of reporting bogus syscalls, just do not trace them.
 *
 * If the user really wants these, then they should use the
 * raw syscall tracepoints with filtering.
 */
#define ARCH_TRACE_IGNORE_COMPAT_SYSCALLS 1
static inline bool arch_trace_is_compat_syscall(struct pt_regs *regs)
{
	return in_32bit_syscall();
}
#endif /* CONFIG_FTRACE_SYSCALLS && CONFIG_IA32_EMULATION */
#endif /* !COMPILE_OFFSETS */
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_X86_FTRACE_H */
