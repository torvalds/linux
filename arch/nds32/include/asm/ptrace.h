/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef __ASM_NDS32_PTRACE_H
#define __ASM_NDS32_PTRACE_H

#include <uapi/asm/ptrace.h>

/*
 * If pt_regs.syscallno == NO_SYSCALL, then the thread is not executing
 * a syscall -- i.e., its most recent entry into the kernel from
 * userspace was not via syscall, or otherwise a tracer cancelled the
 * syscall.
 *
 * This must have the value -1, for ABI compatibility with ptrace etc.
 */
#define NO_SYSCALL (-1)
#ifndef __ASSEMBLY__
#include <linux/types.h>

struct pt_regs {
	union {
		struct user_pt_regs user_regs;
		struct {
			long uregs[26];
			long fp;
			long gp;
			long lp;
			long sp;
			long ipc;
#if defined(CONFIG_HWZOL)
			long lb;
			long le;
			long lc;
#else
			long dummy[3];
#endif
			long syscallno;
		};
	};
	long orig_r0;
	long ir0;
	long ipsw;
	long pipsw;
	long pipc;
	long pp0;
	long pp1;
	long fucop_ctl;
	long osp;
};

static inline bool in_syscall(struct pt_regs const *regs)
{
	return regs->syscallno != NO_SYSCALL;
}

static inline void forget_syscall(struct pt_regs *regs)
{
	regs->syscallno = NO_SYSCALL;
}
static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->uregs[0];
}
extern void show_regs(struct pt_regs *);
/* Avoid circular header include via sched.h */
struct task_struct;

#define arch_has_single_step()		(1)
#define user_mode(regs)			(((regs)->ipsw & PSW_mskPOM) == 0)
#define interrupts_enabled(regs)	(!!((regs)->ipsw & PSW_mskGIE))
#define user_stack_pointer(regs)	((regs)->sp)
#define instruction_pointer(regs)	((regs)->ipc)
#define profile_pc(regs) 		instruction_pointer(regs)

#endif /* __ASSEMBLY__ */
#endif
