#ifndef _ASM_LKL_PTRACE_H
#define _ASM_LKL_PTRACE_H

#include <linux/errno.h>

struct task_struct;

#define user_mode(regs) 0
#define kernel_mode(regs) 1
#define profile_pc(regs) 0
#define instruction_pointer(regs) 0
#define user_stack_pointer(regs) 0

static inline long arch_ptrace(struct task_struct *child,
			       long request, unsigned long addr,
			       unsigned long data)
{
	return -EINVAL;
}

static inline void ptrace_disable(struct task_struct *child)
{
}

#endif
