/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_STACKTRACE_H
#define _ASM_STACKTRACE_H

#include <asm/ptrace.h>
#include <asm/asm.h>
#include <linux/stringify.h>

#ifdef CONFIG_KALLSYMS
extern int raw_show_trace;
extern unsigned long unwind_stack(struct task_struct *task, unsigned long *sp,
				  unsigned long pc, unsigned long *ra);
extern unsigned long unwind_stack_by_address(unsigned long stack_page,
					     unsigned long *sp,
					     unsigned long pc,
					     unsigned long *ra);
#else
#define raw_show_trace 1
static inline unsigned long unwind_stack(struct task_struct *task,
	unsigned long *sp, unsigned long pc, unsigned long *ra)
{
	return 0;
}
#endif

#define STR_PTR_LA    __stringify(PTR_LA)
#define STR_LONG_S    __stringify(LONG_S)
#define STR_LONG_L    __stringify(LONG_L)
#define STR_LONGSIZE  __stringify(LONGSIZE)

#define STORE_ONE_REG(r) \
    STR_LONG_S   " $" __stringify(r)",("STR_LONGSIZE"*"__stringify(r)")(%1)\n\t"

static __always_inline void prepare_frametrace(struct pt_regs *regs)
{
#ifndef CONFIG_KALLSYMS
	/*
	 * Remove any garbage that may be in regs (specially func
	 * addresses) to avoid show_raw_backtrace() to report them
	 */
	memset(regs, 0, sizeof(*regs));
#endif
	__asm__ __volatile__(
		".set push\n\t"
		".set noat\n\t"
		/* Store $1 so we can use it */
		STR_LONG_S " $1,"STR_LONGSIZE"(%1)\n\t"
		/* Store the PC */
		"1: " STR_PTR_LA " $1, 1b\n\t"
		STR_LONG_S " $1,%0\n\t"
		STORE_ONE_REG(2)
		STORE_ONE_REG(3)
		STORE_ONE_REG(4)
		STORE_ONE_REG(5)
		STORE_ONE_REG(6)
		STORE_ONE_REG(7)
		STORE_ONE_REG(8)
		STORE_ONE_REG(9)
		STORE_ONE_REG(10)
		STORE_ONE_REG(11)
		STORE_ONE_REG(12)
		STORE_ONE_REG(13)
		STORE_ONE_REG(14)
		STORE_ONE_REG(15)
		STORE_ONE_REG(16)
		STORE_ONE_REG(17)
		STORE_ONE_REG(18)
		STORE_ONE_REG(19)
		STORE_ONE_REG(20)
		STORE_ONE_REG(21)
		STORE_ONE_REG(22)
		STORE_ONE_REG(23)
		STORE_ONE_REG(24)
		STORE_ONE_REG(25)
		STORE_ONE_REG(26)
		STORE_ONE_REG(27)
		STORE_ONE_REG(28)
		STORE_ONE_REG(29)
		STORE_ONE_REG(30)
		STORE_ONE_REG(31)
		/* Restore $1 */
		STR_LONG_L " $1,"STR_LONGSIZE"(%1)\n\t"
		".set pop\n\t"
		: "=m" (regs->cp0_epc)
		: "r" (regs->regs)
		: "memory");
}

#endif /* _ASM_STACKTRACE_H */
