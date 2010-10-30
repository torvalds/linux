#ifndef __ASM_SH_PTRACE_64_H
#define __ASM_SH_PTRACE_64_H

struct pt_regs {
	unsigned long long pc;
	unsigned long long sr;
	long long syscall_nr;
	unsigned long long regs[63];
	unsigned long long tregs[8];
	unsigned long long pad[2];
};

#ifdef __KERNEL__

#define MAX_REG_OFFSET		offsetof(struct pt_regs, tregs[7])
#define regs_return_value(regs)	((regs)->regs[3])

#endif /* __KERNEL__ */

#endif /* __ASM_SH_PTRACE_64_H */
