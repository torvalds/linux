/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__ASM_SH_PTRACE_64_H
#define _UAPI__ASM_SH_PTRACE_64_H

struct pt_regs {
	unsigned long long pc;
	unsigned long long sr;
	long long syscall_nr;
	unsigned long long regs[63];
	unsigned long long tregs[8];
	unsigned long long pad[2];
};


#endif /* _UAPI__ASM_SH_PTRACE_64_H */
