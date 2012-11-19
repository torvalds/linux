#ifndef __ASM_SH_PTRACE_32_H
#define __ASM_SH_PTRACE_32_H

#include <uapi/asm/ptrace_32.h>


#define MAX_REG_OFFSET		offsetof(struct pt_regs, tra)
static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->regs[0];
}

#endif /* __ASM_SH_PTRACE_32_H */
