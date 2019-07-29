/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/ptrace.h
 *
 * Copyright (C) 1996-2003 Russell King
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_PTRACE_H
#define __ASM_PTRACE_H

#include <asm/cpufeature.h>

#include <uapi/asm/ptrace.h>

/* Current Exception Level values, as contained in CurrentEL */
#define CurrentEL_EL1		(1 << 2)
#define CurrentEL_EL2		(2 << 2)

/*
 * PMR values used to mask/unmask interrupts.
 *
 * GIC priority masking works as follows: if an IRQ's priority is a higher value
 * than the value held in PMR, that IRQ is masked. Lowering the value of PMR
 * means masking more IRQs (or at least that the same IRQs remain masked).
 *
 * To mask interrupts, we clear the most significant bit of PMR.
 *
 * Some code sections either automatically switch back to PSR.I or explicitly
 * require to not use priority masking. If bit GIC_PRIO_PSR_I_SET is included
 * in the  the priority mask, it indicates that PSR.I should be set and
 * interrupt disabling temporarily does not rely on IRQ priorities.
 */
#define GIC_PRIO_IRQON			0xe0
#define GIC_PRIO_IRQOFF			(GIC_PRIO_IRQON & ~0x80)
#define GIC_PRIO_PSR_I_SET		(1 << 4)

/* Additional SPSR bits not exposed in the UABI */
#define PSR_IL_BIT		(1 << 20)

/* AArch32-specific ptrace requests */
#define COMPAT_PTRACE_GETREGS		12
#define COMPAT_PTRACE_SETREGS		13
#define COMPAT_PTRACE_GET_THREAD_AREA	22
#define COMPAT_PTRACE_SET_SYSCALL	23
#define COMPAT_PTRACE_GETVFPREGS	27
#define COMPAT_PTRACE_SETVFPREGS	28
#define COMPAT_PTRACE_GETHBPREGS	29
#define COMPAT_PTRACE_SETHBPREGS	30

/* SPSR_ELx bits for exceptions taken from AArch32 */
#define PSR_AA32_MODE_MASK	0x0000001f
#define PSR_AA32_MODE_USR	0x00000010
#define PSR_AA32_MODE_FIQ	0x00000011
#define PSR_AA32_MODE_IRQ	0x00000012
#define PSR_AA32_MODE_SVC	0x00000013
#define PSR_AA32_MODE_ABT	0x00000017
#define PSR_AA32_MODE_HYP	0x0000001a
#define PSR_AA32_MODE_UND	0x0000001b
#define PSR_AA32_MODE_SYS	0x0000001f
#define PSR_AA32_T_BIT		0x00000020
#define PSR_AA32_F_BIT		0x00000040
#define PSR_AA32_I_BIT		0x00000080
#define PSR_AA32_A_BIT		0x00000100
#define PSR_AA32_E_BIT		0x00000200
#define PSR_AA32_SSBS_BIT	0x00800000
#define PSR_AA32_DIT_BIT	0x01000000
#define PSR_AA32_Q_BIT		0x08000000
#define PSR_AA32_V_BIT		0x10000000
#define PSR_AA32_C_BIT		0x20000000
#define PSR_AA32_Z_BIT		0x40000000
#define PSR_AA32_N_BIT		0x80000000
#define PSR_AA32_IT_MASK	0x0600fc00	/* If-Then execution state mask */
#define PSR_AA32_GE_MASK	0x000f0000

#ifdef CONFIG_CPU_BIG_ENDIAN
#define PSR_AA32_ENDSTATE	PSR_AA32_E_BIT
#else
#define PSR_AA32_ENDSTATE	0
#endif

/* AArch32 CPSR bits, as seen in AArch32 */
#define COMPAT_PSR_DIT_BIT	0x00200000

/*
 * These are 'magic' values for PTRACE_PEEKUSR that return info about where a
 * process is located in memory.
 */
#define COMPAT_PT_TEXT_ADDR		0x10000
#define COMPAT_PT_DATA_ADDR		0x10004
#define COMPAT_PT_TEXT_END_ADDR		0x10008

/*
 * If pt_regs.syscallno == NO_SYSCALL, then the thread is not executing
 * a syscall -- i.e., its most recent entry into the kernel from
 * userspace was not via SVC, or otherwise a tracer cancelled the syscall.
 *
 * This must have the value -1, for ABI compatibility with ptrace etc.
 */
#define NO_SYSCALL (-1)

#ifndef __ASSEMBLY__
#include <linux/bug.h>
#include <linux/types.h>

/* sizeof(struct user) for AArch32 */
#define COMPAT_USER_SZ	296

/* Architecturally defined mapping between AArch32 and AArch64 registers */
#define compat_usr(x)	regs[(x)]
#define compat_fp	regs[11]
#define compat_sp	regs[13]
#define compat_lr	regs[14]
#define compat_sp_hyp	regs[15]
#define compat_lr_irq	regs[16]
#define compat_sp_irq	regs[17]
#define compat_lr_svc	regs[18]
#define compat_sp_svc	regs[19]
#define compat_lr_abt	regs[20]
#define compat_sp_abt	regs[21]
#define compat_lr_und	regs[22]
#define compat_sp_und	regs[23]
#define compat_r8_fiq	regs[24]
#define compat_r9_fiq	regs[25]
#define compat_r10_fiq	regs[26]
#define compat_r11_fiq	regs[27]
#define compat_r12_fiq	regs[28]
#define compat_sp_fiq	regs[29]
#define compat_lr_fiq	regs[30]

static inline unsigned long compat_psr_to_pstate(const unsigned long psr)
{
	unsigned long pstate;

	pstate = psr & ~COMPAT_PSR_DIT_BIT;

	if (psr & COMPAT_PSR_DIT_BIT)
		pstate |= PSR_AA32_DIT_BIT;

	return pstate;
}

static inline unsigned long pstate_to_compat_psr(const unsigned long pstate)
{
	unsigned long psr;

	psr = pstate & ~PSR_AA32_DIT_BIT;

	if (pstate & PSR_AA32_DIT_BIT)
		psr |= COMPAT_PSR_DIT_BIT;

	return psr;
}

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
#ifdef __AARCH64EB__
	u32 unused2;
	s32 syscallno;
#else
	s32 syscallno;
	u32 unused2;
#endif

	u64 orig_addr_limit;
	/* Only valid when ARM64_HAS_IRQ_PRIO_MASKING is enabled. */
	u64 pmr_save;
	u64 stackframe[2];
};

static inline bool in_syscall(struct pt_regs const *regs)
{
	return regs->syscallno != NO_SYSCALL;
}

static inline void forget_syscall(struct pt_regs *regs)
{
	regs->syscallno = NO_SYSCALL;
}

#define MAX_REG_OFFSET offsetof(struct pt_regs, pstate)

#define arch_has_single_step()	(1)

#ifdef CONFIG_COMPAT
#define compat_thumb_mode(regs) \
	(((regs)->pstate & PSR_AA32_T_BIT))
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

#define irqs_priority_unmasked(regs)					\
	(system_uses_irq_prio_masking() ?				\
		(regs)->pmr_save == GIC_PRIO_IRQON :			\
		true)

#define interrupts_enabled(regs)			\
	(!((regs)->pstate & PSR_I_BIT) && irqs_priority_unmasked(regs))

#define fast_interrupts_enabled(regs) \
	(!((regs)->pstate & PSR_F_BIT))

#define GET_USP(regs) \
	(!compat_user_mode(regs) ? (regs)->sp : (regs)->compat_sp)

#define SET_USP(ptregs, value) \
	(!compat_user_mode(regs) ? ((regs)->sp = value) : ((regs)->compat_sp = value))

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
static inline u64 regs_get_register(struct pt_regs *regs, unsigned int offset)
{
	u64 val = 0;

	WARN_ON(offset & 7);

	offset >>= 3;
	switch (offset) {
	case 0 ... 30:
		val = regs->regs[offset];
		break;
	case offsetof(struct pt_regs, sp) >> 3:
		val = regs->sp;
		break;
	case offsetof(struct pt_regs, pc) >> 3:
		val = regs->pc;
		break;
	case offsetof(struct pt_regs, pstate) >> 3:
		val = regs->pstate;
		break;
	default:
		val = 0;
	}

	return val;
}

/*
 * Read a register given an architectural register index r.
 * This handles the common case where 31 means XZR, not SP.
 */
static inline unsigned long pt_regs_read_reg(const struct pt_regs *regs, int r)
{
	return (r == 31) ? 0 : regs->regs[r];
}

/*
 * Write a register given an architectural register index r.
 * This handles the common case where 31 means XZR, not SP.
 */
static inline void pt_regs_write_reg(struct pt_regs *regs, int r,
				     unsigned long val)
{
	if (r != 31)
		regs->regs[r] = val;
}

/* Valid only for Kernel mode traps. */
static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->sp;
}

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->regs[0];
}

/**
 * regs_get_kernel_argument() - get Nth function argument in kernel
 * @regs:	pt_regs of that context
 * @n:		function argument number (start from 0)
 *
 * regs_get_argument() returns @n th argument of the function call.
 *
 * Note that this chooses the most likely register mapping. In very rare
 * cases this may not return correct data, for example, if one of the
 * function parameters is 16 bytes or bigger. In such cases, we cannot
 * get access the parameter correctly and the register assignment of
 * subsequent parameters will be shifted.
 */
static inline unsigned long regs_get_kernel_argument(struct pt_regs *regs,
						     unsigned int n)
{
#define NR_REG_ARGUMENTS 8
	if (n < NR_REG_ARGUMENTS)
		return pt_regs_read_reg(regs, n);
	return 0;
}

/* We must avoid circular header include via sched.h */
struct task_struct;
int valid_user_regs(struct user_pt_regs *regs, struct task_struct *task);

#define GET_IP(regs)		((unsigned long)(regs)->pc)
#define SET_IP(regs, value)	((regs)->pc = ((u64) (value)))

#define GET_FP(ptregs)		((unsigned long)(ptregs)->regs[29])
#define SET_FP(ptregs, value)	((ptregs)->regs[29] = ((u64) (value)))

#include <asm-generic/ptrace.h>

#define procedure_link_pointer(regs)	((regs)->regs[30])

static inline void procedure_link_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	procedure_link_pointer(regs) = val;
}

#undef profile_pc
extern unsigned long profile_pc(struct pt_regs *regs);

#endif /* __ASSEMBLY__ */
#endif
