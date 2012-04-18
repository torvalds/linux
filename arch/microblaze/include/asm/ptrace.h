/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PTRACE_H
#define _ASM_MICROBLAZE_PTRACE_H

#ifndef __ASSEMBLY__

typedef unsigned long microblaze_reg_t;

struct pt_regs {
	microblaze_reg_t r0;
	microblaze_reg_t r1;
	microblaze_reg_t r2;
	microblaze_reg_t r3;
	microblaze_reg_t r4;
	microblaze_reg_t r5;
	microblaze_reg_t r6;
	microblaze_reg_t r7;
	microblaze_reg_t r8;
	microblaze_reg_t r9;
	microblaze_reg_t r10;
	microblaze_reg_t r11;
	microblaze_reg_t r12;
	microblaze_reg_t r13;
	microblaze_reg_t r14;
	microblaze_reg_t r15;
	microblaze_reg_t r16;
	microblaze_reg_t r17;
	microblaze_reg_t r18;
	microblaze_reg_t r19;
	microblaze_reg_t r20;
	microblaze_reg_t r21;
	microblaze_reg_t r22;
	microblaze_reg_t r23;
	microblaze_reg_t r24;
	microblaze_reg_t r25;
	microblaze_reg_t r26;
	microblaze_reg_t r27;
	microblaze_reg_t r28;
	microblaze_reg_t r29;
	microblaze_reg_t r30;
	microblaze_reg_t r31;
	microblaze_reg_t pc;
	microblaze_reg_t msr;
	microblaze_reg_t ear;
	microblaze_reg_t esr;
	microblaze_reg_t fsr;
	int pt_mode;
};

#ifdef __KERNEL__
#define kernel_mode(regs)		((regs)->pt_mode)
#define user_mode(regs)			(!kernel_mode(regs))

#define instruction_pointer(regs)	((regs)->pc)
#define profile_pc(regs)		instruction_pointer(regs)

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->r3;
}

#else /* __KERNEL__ */

/* pt_regs offsets used by gdbserver etc in ptrace syscalls */
#define PT_GPR(n)	((n) * sizeof(microblaze_reg_t))
#define PT_PC		(32 * sizeof(microblaze_reg_t))
#define PT_MSR		(33 * sizeof(microblaze_reg_t))
#define PT_EAR		(34 * sizeof(microblaze_reg_t))
#define PT_ESR		(35 * sizeof(microblaze_reg_t))
#define PT_FSR		(36 * sizeof(microblaze_reg_t))
#define PT_KERNEL_MODE	(37 * sizeof(microblaze_reg_t))

#endif /* __KERNEL */

#endif /* __ASSEMBLY__ */

#endif /* _ASM_MICROBLAZE_PTRACE_H */
