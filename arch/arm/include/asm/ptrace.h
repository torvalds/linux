/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/ptrace.h
 *
 *  Copyright (C) 1996-2003 Russell King
 */
#ifndef __ASM_ARM_PTRACE_H
#define __ASM_ARM_PTRACE_H

#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLY__
#include <linux/types.h>

struct pt_regs {
	unsigned long uregs[18];
};

struct svc_pt_regs {
	struct pt_regs regs;
	u32 dacr;
};

#define to_svc_pt_regs(r) container_of(r, struct svc_pt_regs, regs)

#define user_mode(regs)	\
	(((regs)->ARM_cpsr & 0xf) == 0)

#ifdef CONFIG_ARM_THUMB
#define thumb_mode(regs) \
	(((regs)->ARM_cpsr & PSR_T_BIT))
#else
#define thumb_mode(regs) (0)
#endif

#ifndef CONFIG_CPU_V7M
#define isa_mode(regs) \
	((((regs)->ARM_cpsr & PSR_J_BIT) >> (__ffs(PSR_J_BIT) - 1)) | \
	 (((regs)->ARM_cpsr & PSR_T_BIT) >> (__ffs(PSR_T_BIT))))
#else
#define isa_mode(regs) 1 /* Thumb */
#endif

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
#ifndef CONFIG_CPU_V7M
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
#else /* ifndef CONFIG_CPU_V7M */
	return 1;
#endif
}

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->ARM_r0;
}

#define instruction_pointer(regs)	(regs)->ARM_pc

#ifdef CONFIG_THUMB2_KERNEL
#define frame_pointer(regs) (regs)->ARM_r7
#else
#define frame_pointer(regs) (regs)->ARM_fp
#endif

static inline void instruction_pointer_set(struct pt_regs *regs,
					   unsigned long val)
{
	instruction_pointer(regs) = val;
}

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
#include <linux/compiler.h>
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

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->ARM_sp;
}

#define current_pt_regs(void) ({ (struct pt_regs *)			\
		((current_stack_pointer | (THREAD_SIZE - 1)) - 7) - 1;	\
})


/*
 * Update ITSTATE after normal execution of an IT block instruction.
 *
 * The 8 IT state bits are split into two parts in CPSR:
 *	ITSTATE<1:0> are in CPSR<26:25>
 *	ITSTATE<7:2> are in CPSR<15:10>
 */
static inline unsigned long it_advance(unsigned long cpsr)
{
	if ((cpsr & 0x06000400) == 0) {
		/* ITSTATE<2:0> == 0 means end of IT block, so clear IT state */
		cpsr &= ~PSR_IT_MASK;
	} else {
		/* We need to shift left ITSTATE<4:0> */
		const unsigned long mask = 0x06001c00;  /* Mask ITSTATE<4:0> */
		unsigned long it = cpsr & mask;
		it <<= 1;
		it |= it >> (27 - 10);  /* Carry ITSTATE<2> to correct place */
		it &= mask;
		cpsr &= ~mask;
		cpsr |= it;
	}
	return cpsr;
}

#endif /* __ASSEMBLY__ */
#endif
