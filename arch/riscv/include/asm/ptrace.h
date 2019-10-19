/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_PTRACE_H
#define _ASM_RISCV_PTRACE_H

#include <uapi/asm/ptrace.h>
#include <asm/csr.h>

#ifndef __ASSEMBLY__

struct pt_regs {
	unsigned long sepc;
	unsigned long ra;
	unsigned long sp;
	unsigned long gp;
	unsigned long tp;
	unsigned long t0;
	unsigned long t1;
	unsigned long t2;
	unsigned long s0;
	unsigned long s1;
	unsigned long a0;
	unsigned long a1;
	unsigned long a2;
	unsigned long a3;
	unsigned long a4;
	unsigned long a5;
	unsigned long a6;
	unsigned long a7;
	unsigned long s2;
	unsigned long s3;
	unsigned long s4;
	unsigned long s5;
	unsigned long s6;
	unsigned long s7;
	unsigned long s8;
	unsigned long s9;
	unsigned long s10;
	unsigned long s11;
	unsigned long t3;
	unsigned long t4;
	unsigned long t5;
	unsigned long t6;
	/* Supervisor CSRs */
	unsigned long sstatus;
	unsigned long sbadaddr;
	unsigned long scause;
	/* a0 value before the syscall */
	unsigned long orig_a0;
};

#ifdef CONFIG_64BIT
#define REG_FMT "%016lx"
#else
#define REG_FMT "%08lx"
#endif

#define user_mode(regs) (((regs)->sstatus & SR_SPP) == 0)


/* Helpers for working with the instruction pointer */
static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return regs->sepc;
}
static inline void instruction_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	regs->sepc = val;
}

#define profile_pc(regs) instruction_pointer(regs)

/* Helpers for working with the user stack pointer */
static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->sp;
}
static inline void user_stack_pointer_set(struct pt_regs *regs,
					  unsigned long val)
{
	regs->sp =  val;
}

/* Helpers for working with the frame pointer */
static inline unsigned long frame_pointer(struct pt_regs *regs)
{
	return regs->s0;
}
static inline void frame_pointer_set(struct pt_regs *regs,
				     unsigned long val)
{
	regs->s0 = val;
}

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->a0;
}

#endif /* __ASSEMBLY__ */

#endif /* _ASM_RISCV_PTRACE_H */
