/*
 * Based on arch/arm/include/asm/ptrace.h
 *
 * Copyright (C) 1996-2003 Russell King
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_PTRACE_H
#define __ASM_PTRACE_H

#include <uapi/asm/ptrace.h>

/* AArch32-specific ptrace requests */
#define COMPAT_PTRACE_GETREGS		12
#define COMPAT_PTRACE_SETREGS		13
#define COMPAT_PTRACE_GET_THREAD_AREA	22
#define COMPAT_PTRACE_SET_SYSCALL	23
#define COMPAT_PTRACE_GETVFPREGS	27
#define COMPAT_PTRACE_SETVFPREGS	28
#define COMPAT_PTRACE_GETHBPREGS	29
#define COMPAT_PTRACE_SETHBPREGS	30

/* AArch32 CPSR bits */
#define COMPAT_PSR_MODE_MASK	0x0000001f
#define COMPAT_PSR_MODE_USR	0x00000010
#define COMPAT_PSR_MODE_FIQ	0x00000011
#define COMPAT_PSR_MODE_IRQ	0x00000012
#define COMPAT_PSR_MODE_SVC	0x00000013
#define COMPAT_PSR_MODE_ABT	0x00000017
#define COMPAT_PSR_MODE_HYP	0x0000001a
#define COMPAT_PSR_MODE_UND	0x0000001b
#define COMPAT_PSR_MODE_SYS	0x0000001f
#define COMPAT_PSR_T_BIT	0x00000020
#define COMPAT_PSR_F_BIT	0x00000040
#define COMPAT_PSR_I_BIT	0x00000080
#define COMPAT_PSR_A_BIT	0x00000100
#define COMPAT_PSR_E_BIT	0x00000200
#define COMPAT_PSR_J_BIT	0x01000000
#define COMPAT_PSR_Q_BIT	0x08000000
#define COMPAT_PSR_V_BIT	0x10000000
#define COMPAT_PSR_C_BIT	0x20000000
#define COMPAT_PSR_Z_BIT	0x40000000
#define COMPAT_PSR_N_BIT	0x80000000
#define COMPAT_PSR_IT_MASK	0x0600fc00	/* If-Then execution state mask */
/*
 * These are 'magic' values for PTRACE_PEEKUSR that return info about where a
 * process is located in memory.
 */
#define COMPAT_PT_TEXT_ADDR		0x10000
#define COMPAT_PT_DATA_ADDR		0x10004
#define COMPAT_PT_TEXT_END_ADDR		0x10008
#ifndef __ASSEMBLY__

/* sizeof(struct user) for AArch32 */
#define COMPAT_USER_SZ	296

/* Architecturally defined mapping between AArch32 and AArch64 registers */
#define compat_usr(x)	regs[(x)]
#define compat_sp	regs[13]
#define compat_lr	regs[14]
#define compat_sp_hyp	regs[15]
#define compat_sp_irq	regs[16]
#define compat_lr_irq	regs[17]
#define compat_sp_svc	regs[18]
#define compat_lr_svc	regs[19]
#define compat_sp_abt	regs[20]
#define compat_lr_abt	regs[21]
#define compat_sp_und	regs[22]
#define compat_lr_und	regs[23]
#define compat_r8_fiq	regs[24]
#define compat_r9_fiq	regs[25]
#define compat_r10_fiq	regs[26]
#define compat_r11_fiq	regs[27]
#define compat_r12_fiq	regs[28]
#define compat_sp_fiq	regs[29]
#define compat_lr_fiq	regs[30]

/*
 * This struct defines the way the registers are stored on the stack during an
 * exception. Note that sizeof(struct pt_regs) has to be a multiple of 16 (for
 * stack alignment). struct user_pt_regs must form a prefix of struct pt_regs.
 */
struct pt_regs {
	union {
		struct user_pt_regs user_regs;
		struct {
			u64 regs[31];
			u64 sp;
			u64 pc;
			u64 pstate;
		};
	};
	u64 orig_x0;
	u64 syscallno;
};

#define arch_has_single_step()	(1)

#ifdef CONFIG_COMPAT
#define compat_thumb_mode(regs) \
	(((regs)->pstate & COMPAT_PSR_T_BIT))
#else
#define compat_thumb_mode(regs) (0)
#endif

#define user_mode(regs)	\
	(((regs)->pstate & PSR_MODE_MASK) == PSR_MODE_EL0t)

#define compat_user_mode(regs)	\
	(((regs)->pstate & (PSR_MODE32_BIT | PSR_MODE_MASK)) == \
	 (PSR_MODE32_BIT | PSR_MODE_EL0t))

#define processor_mode(regs) \
	((regs)->pstate & PSR_MODE_MASK)

#define interrupts_enabled(regs) \
	(!((regs)->pstate & PSR_I_BIT))

#define fast_interrupts_enabled(regs) \
	(!((regs)->pstate & PSR_F_BIT))

#define user_stack_pointer(regs) \
	((regs)->sp)

/*
 * Are the current registers suitable for user mode? (used to maintain
 * security in signal handlers)
 */
static inline int valid_user_regs(struct user_pt_regs *regs)
{
	if (user_mode(regs) && (regs->pstate & PSR_I_BIT) == 0) {
		regs->pstate &= ~(PSR_F_BIT | PSR_A_BIT);

		/* The T bit is reserved for AArch64 */
		if (!(regs->pstate & PSR_MODE32_BIT))
			regs->pstate &= ~COMPAT_PSR_T_BIT;

		return 1;
	}

	/*
	 * Force PSR to something logical...
	 */
	regs->pstate &= PSR_f | PSR_s | (PSR_x & ~PSR_A_BIT) | \
			COMPAT_PSR_T_BIT | PSR_MODE32_BIT;

	if (!(regs->pstate & PSR_MODE32_BIT)) {
		regs->pstate &= ~COMPAT_PSR_T_BIT;
		regs->pstate |= PSR_MODE_EL0t;
	}

	return 0;
}

#define instruction_pointer(regs)	((unsigned long)(regs)->pc)

#ifdef CONFIG_SMP
extern unsigned long profile_pc(struct pt_regs *regs);
#else
#define profile_pc(regs) instruction_pointer(regs)
#endif

#endif /* __ASSEMBLY__ */
#endif
