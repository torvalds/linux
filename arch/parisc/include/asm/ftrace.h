/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_PARISC_FTRACE_H
#define _ASM_PARISC_FTRACE_H

#ifndef __ASSEMBLY__
extern void mcount(void);

#define MCOUNT_ADDR		((unsigned long)mcount)
#define MCOUNT_INSN_SIZE	4
#define CC_USING_NOP_MCOUNT
#define ARCH_SUPPORTS_FTRACE_OPS 1
extern unsigned long sys_call_table[];

extern unsigned long return_address(unsigned int);
struct ftrace_regs;
extern void ftrace_function_trampoline(unsigned long parent,
		unsigned long self_addr, unsigned long org_sp_gr3,
		struct ftrace_regs *fregs);

#ifdef CONFIG_DYNAMIC_FTRACE
extern void ftrace_caller(void);

struct dyn_arch_ftrace {
};

unsigned long ftrace_call_adjust(unsigned long addr);

#endif

#define ftrace_return_address(n) return_address(n)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_PARISC_FTRACE_H */
