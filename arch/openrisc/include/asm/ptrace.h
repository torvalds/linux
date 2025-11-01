/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 */
#ifndef __ASM_OPENRISC_PTRACE_H
#define __ASM_OPENRISC_PTRACE_H


#include <asm/spr_defs.h>
#include <uapi/asm/ptrace.h>
#include <linux/compiler.h>

/*
 * Make kernel PTrace/register structures opaque to userspace... userspace can
 * access thread state via the regset mechanism.  This allows us a bit of
 * flexibility in how we order the registers on the stack, permitting some
 * optimizations like packing call-clobbered registers together so that
 * they share a cacheline (not done yet, though... future optimization).
 */

#ifndef __ASSEMBLER__
/*
 * This struct describes how the registers are laid out on the kernel stack
 * during a syscall or other kernel entry.
 *
 * This structure should always be cacheline aligned on the stack.
 * FIXME: I don't think that's the case right now.  The alignment is
 * taken care of elsewhere... head.S, process.c, etc.
 */

struct pt_regs {
	union {
		struct {
			/* Named registers */
			long  sr;	/* Stored in place of r0 */
			long  sp;	/* r1 */
			long  gpr2;
			long  gpr3;
			long  gpr4;
			long  gpr5;
			long  gpr6;
			long  gpr7;
			long  gpr8;
			long  gpr9;
			long  gpr10;
			long  gpr11;
			long  gpr12;
			long  gpr13;
			long  gpr14;
			long  gpr15;
			long  gpr16;
			long  gpr17;
			long  gpr18;
			long  gpr19;
			long  gpr20;
			long  gpr21;
			long  gpr22;
			long  gpr23;
			long  gpr24;
			long  gpr25;
			long  gpr26;
			long  gpr27;
			long  gpr28;
			long  gpr29;
			long  gpr30;
			long  gpr31;
		};
		struct {
			/* Old style */
			long offset[2];
			long gprs[30];
		};
		struct {
			/* New style */
			long gpr[32];
		};
	};
	long  pc;
	/* For restarting system calls:
	 * Set to syscall number for syscall exceptions,
	 * -1 for all other exceptions.
	 */
	long  orig_gpr11;	/* For restarting system calls */
	long dummy;		/* Cheap alignment fix */
	long dummy2;		/* Cheap alignment fix */
};

/* TODO: Rename this to REDZONE because that's what it is */
#define STACK_FRAME_OVERHEAD  128  /* size of minimum stack frame */

#define MAX_REG_OFFSET offsetof(struct pt_regs, orig_gpr11)

/* Helpers for working with the instruction pointer */
static inline unsigned long instruction_pointer(struct pt_regs *regs)
{
	return (unsigned long)regs->pc;
}
static inline void instruction_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	regs->pc = val;
}

#define user_mode(regs)			(((regs)->sr & SPR_SR_SM) == 0)
#define user_stack_pointer(regs)	((unsigned long)(regs)->sp)
#define profile_pc(regs)		instruction_pointer(regs)

/* Valid only for Kernel mode traps. */
static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return (unsigned long)regs->sp;
}

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->gpr[11];
}

extern int regs_query_register_offset(const char *name);
extern unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
					       unsigned int n);

/**
 * regs_get_register() - get register value from its offset
 * @regs:	pt_regs from which register value is gotten
 * @offset:	offset of the register.
 *
 * regs_get_register returns the value of a register whose offset from @regs.
 * The @offset is the offset of the register in struct pt_regs.
 * If @offset is bigger than MAX_REG_OFFSET, this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs,
					      unsigned int offset)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return 0;

	return *(unsigned long *)((unsigned long)regs + offset);
}

#endif /* __ASSEMBLER__ */

/*
 * Offsets used by 'ptrace' system call interface.
 */
#define PT_SR         0
#define PT_SP         4
#define PT_GPR2       8
#define PT_GPR3       12
#define PT_GPR4       16
#define PT_GPR5       20
#define PT_GPR6       24
#define PT_GPR7       28
#define PT_GPR8       32
#define PT_GPR9       36
#define PT_GPR10      40
#define PT_GPR11      44
#define PT_GPR12      48
#define PT_GPR13      52
#define PT_GPR14      56
#define PT_GPR15      60
#define PT_GPR16      64
#define PT_GPR17      68
#define PT_GPR18      72
#define PT_GPR19      76
#define PT_GPR20      80
#define PT_GPR21      84
#define PT_GPR22      88
#define PT_GPR23      92
#define PT_GPR24      96
#define PT_GPR25      100
#define PT_GPR26      104
#define PT_GPR27      108
#define PT_GPR28      112
#define PT_GPR29      116
#define PT_GPR30      120
#define PT_GPR31      124
#define PT_PC	      128
#define PT_ORIG_GPR11 132

#endif /* __ASM_OPENRISC_PTRACE_H */
