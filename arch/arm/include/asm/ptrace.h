/*
 *  arch/arm/include/asm/ptrace.h
 *
 *  Copyright (C) 1996-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_PTRACE_H
#define __ASM_ARM_PTRACE_H

#include <asm/hwcap.h>

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15
/* PTRACE_ATTACH is 16 */
/* PTRACE_DETACH is 17 */
#define PTRACE_GETWMMXREGS	18
#define PTRACE_SETWMMXREGS	19
/* 20 is unused */
#define PTRACE_OLDSETOPTIONS	21
#define PTRACE_GET_THREAD_AREA	22
#define PTRACE_SET_SYSCALL	23
/* PTRACE_SYSCALL is 24 */
#define PTRACE_GETCRUNCHREGS	25
#define PTRACE_SETCRUNCHREGS	26
#define PTRACE_GETVFPREGS	27
#define PTRACE_SETVFPREGS	28
#define PTRACE_GETHBPREGS	29
#define PTRACE_SETHBPREGS	30

/*
 * PSR bits
 */
#define USR26_MODE	0x00000000
#define FIQ26_MODE	0x00000001
#define IRQ26_MODE	0x00000002
#define SVC26_MODE	0x00000003
#define USR_MODE	0x00000010
#define FIQ_MODE	0x00000011
#define IRQ_MODE	0x00000012
#define SVC_MODE	0x00000013
#define ABT_MODE	0x00000017
#define UND_MODE	0x0000001b
#define SYSTEM_MODE	0x0000001f
#define MODE32_BIT	0x00000010
#define MODE_MASK	0x0000001f
#define PSR_T_BIT	0x00000020
#define PSR_F_BIT	0x00000040
#define PSR_I_BIT	0x00000080
#define PSR_A_BIT	0x00000100
#define PSR_E_BIT	0x00000200
#define PSR_J_BIT	0x01000000
#define PSR_Q_BIT	0x08000000
#define PSR_V_BIT	0x10000000
#define PSR_C_BIT	0x20000000
#define PSR_Z_BIT	0x40000000
#define PSR_N_BIT	0x80000000

/*
 * Groups of PSR bits
 */
#define PSR_f		0xff000000	/* Flags		*/
#define PSR_s		0x00ff0000	/* Status		*/
#define PSR_x		0x0000ff00	/* Extension		*/
#define PSR_c		0x000000ff	/* Control		*/

/*
 * ARMv7 groups of PSR bits
 */
#define APSR_MASK	0xf80f0000	/* N, Z, C, V, Q and GE flags */
#define PSR_ISET_MASK	0x01000010	/* ISA state (J, T) mask */
#define PSR_IT_MASK	0x0600fc00	/* If-Then execution state mask */
#define PSR_ENDIAN_MASK	0x00000200	/* Endianness state mask */

/*
 * Default endianness state
 */
#ifdef CONFIG_CPU_ENDIAN_BE8
#define PSR_ENDSTATE	PSR_E_BIT
#else
#define PSR_ENDSTATE	0
#endif

/* 
 * These are 'magic' values for PTRACE_PEEKUSR that return info about where a
 * process is located in memory.
 */
#define PT_TEXT_ADDR		0x10000
#define PT_DATA_ADDR		0x10004
#define PT_TEXT_END_ADDR	0x10008

#ifndef __ASSEMBLY__

/*
 * This struct defines the way the registers are stored on the
 * stack during a system call.  Note that sizeof(struct pt_regs)
 * has to be a multiple of 8.
 */
#ifndef __KERNEL__
struct pt_regs {
	long uregs[18];
};
#else /* __KERNEL__ */
struct pt_regs {
	unsigned long uregs[18];
};
#endif /* __KERNEL__ */

#define ARM_cpsr	uregs[16]
#define ARM_pc		uregs[15]
#define ARM_lr		uregs[14]
#define ARM_sp		uregs[13]
#define ARM_ip		uregs[12]
#define ARM_fp		uregs[11]
#define ARM_r10		uregs[10]
#define ARM_r9		uregs[9]
#define ARM_r8		uregs[8]
#define ARM_r7		uregs[7]
#define ARM_r6		uregs[6]
#define ARM_r5		uregs[5]
#define ARM_r4		uregs[4]
#define ARM_r3		uregs[3]
#define ARM_r2		uregs[2]
#define ARM_r1		uregs[1]
#define ARM_r0		uregs[0]
#define ARM_ORIG_r0	uregs[17]

/*
 * The size of the user-visible VFP state as seen by PTRACE_GET/SETVFPREGS
 * and core dumps.
 */
#define ARM_VFPREGS_SIZE ( 32 * 8 /*fpregs*/ + 4 /*fpscr*/ )

#ifdef __KERNEL__

#define user_mode(regs)	\
	(((regs)->ARM_cpsr & 0xf) == 0)

#ifdef CONFIG_ARM_THUMB
#define thumb_mode(regs) \
	(((regs)->ARM_cpsr & PSR_T_BIT))
#else
#define thumb_mode(regs) (0)
#endif

#define isa_mode(regs) \
	((((regs)->ARM_cpsr & PSR_J_BIT) >> 23) | \
	 (((regs)->ARM_cpsr & PSR_T_BIT) >> 5))

#define processor_mode(regs) \
	((regs)->ARM_cpsr & MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->ARM_cpsr & PSR_F_BIT))

/* Are the current registers suitable for user mode?
 * (used to maintain security in signal handlers)
 */
static inline int valid_user_regs(struct pt_regs *regs)
{
	unsigned long mode = regs->ARM_cpsr & MODE_MASK;

	/*
	 * Always clear the F (FIQ) and A (delayed abort) bits
	 */
	regs->ARM_cpsr &= ~(PSR_F_BIT | PSR_A_BIT);

	if ((regs->ARM_cpsr & PSR_I_BIT) == 0) {
		if (mode == USR_MODE)
			return 1;
		if (elf_hwcap & HWCAP_26BIT && mode == USR26_MODE)
			return 1;
	}

	/*
	 * Force CPSR to something logical...
	 */
	regs->ARM_cpsr &= PSR_f | PSR_s | PSR_x | PSR_T_BIT | MODE32_BIT;
	if (!(elf_hwcap & HWCAP_26BIT))
		regs->ARM_cpsr |= USR_MODE;

	return 0;
}

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->ARM_r0;
}

#define instruction_pointer(regs)	(regs)->ARM_pc

#ifdef CONFIG_SMP
extern unsigned long profile_pc(struct pt_regs *regs);
#else
#define profile_pc(regs) instruction_pointer(regs)
#endif

#define predicate(x)		((x) & 0xf0000000)
#define PREDICATE_ALWAYS	0xe0000000

/*
 * True if instr is a 32-bit thumb instruction. This works if instr
 * is the first or only half-word of a thumb instruction. It also works
 * when instr holds all 32-bits of a wide thumb instruction if stored
 * in the form (first_half<<16)|(second_half)
 */
#define is_wide_instruction(instr)	((unsigned)(instr) >= 0xe800)

/*
 * kprobe-based event tracer support
 */
#include <linux/stddef.h>
#include <linux/types.h>
#define MAX_REG_OFFSET (offsetof(struct pt_regs, ARM_ORIG_r0))

extern int regs_query_register_offset(const char *name);
extern const char *regs_query_register_name(unsigned int offset);
extern bool regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr);
extern unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs,
					       unsigned int n);

/**
 * regs_get_register() - get register value from its offset
 * @regs:	   pt_regs from which register value is gotten
 * @offset:    offset number of the register.
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

/* Valid only for Kernel mode traps. */
static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->ARM_sp;
}

#endif /* __KERNEL__ */

#endif /* __ASSEMBLY__ */

#endif

