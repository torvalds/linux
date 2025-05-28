/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_PTRACE_H
#define _ASM_PTRACE_H

#include <asm/page.h>
#include <asm/irqflags.h>
#include <asm/thread_info.h>
#include <uapi/asm/ptrace.h>

/*
 * This struct defines the way the registers are stored on the stack during
 * a system call/exception. If you add a register here, please also add it to
 * regoffset_table[] in arch/loongarch/kernel/ptrace.c.
 */
struct pt_regs {
	/* Main processor registers. */
	unsigned long regs[32];

	/* Original syscall arg0. */
	unsigned long orig_a0;

	/* Special CSR registers. */
	unsigned long csr_era;
	unsigned long csr_badvaddr;
	unsigned long csr_crmd;
	unsigned long csr_prmd;
	unsigned long csr_euen;
	unsigned long csr_ecfg;
	unsigned long csr_estat;
	unsigned long __last[];
} __aligned(8);

static __always_inline bool regs_irqs_disabled(struct pt_regs *regs)
{
	return !(regs->csr_prmd & CSR_PRMD_PIE);
}

static inline unsigned long kernel_stack_pointer(struct pt_regs *regs)
{
	return regs->regs[3];
}

/*
 * Don't use asm-generic/ptrace.h it defines FP accessors that don't make
 * sense on LoongArch.  We rather want an error if they get invoked.
 */

static inline void instruction_pointer_set(struct pt_regs *regs, unsigned long val)
{
	regs->csr_era = val;
}

/* Query offset/name of register from its name/offset */
extern int regs_query_register_offset(const char *name);
#define MAX_REG_OFFSET (offsetof(struct pt_regs, __last))

/**
 * regs_get_register() - get register value from its offset
 * @regs:       pt_regs from which register value is gotten.
 * @offset:     offset number of the register.
 *
 * regs_get_register returns the value of a register. The @offset is the
 * offset of the register in struct pt_regs address which specified by @regs.
 * If @offset is bigger than MAX_REG_OFFSET, this returns 0.
 */
static inline unsigned long regs_get_register(struct pt_regs *regs, unsigned int offset)
{
	if (unlikely(offset > MAX_REG_OFFSET))
		return 0;

	return *(unsigned long *)((unsigned long)regs + offset);
}

/**
 * regs_within_kernel_stack() - check the address in the stack
 * @regs:       pt_regs which contains kernel stack pointer.
 * @addr:       address which is checked.
 *
 * regs_within_kernel_stack() checks @addr is within the kernel stack page(s).
 * If @addr is within the kernel stack, it returns true. If not, returns false.
 */
static inline int regs_within_kernel_stack(struct pt_regs *regs, unsigned long addr)
{
	return ((addr & ~(THREAD_SIZE - 1))  ==
		(kernel_stack_pointer(regs) & ~(THREAD_SIZE - 1)));
}

/**
 * regs_get_kernel_stack_nth() - get Nth entry of the stack
 * @regs:       pt_regs which contains kernel stack pointer.
 * @n:          stack entry number.
 *
 * regs_get_kernel_stack_nth() returns @n th entry of the kernel stack which
 * is specified by @regs. If the @n th entry is NOT in the kernel stack,
 * this returns 0.
 */
static inline unsigned long regs_get_kernel_stack_nth(struct pt_regs *regs, unsigned int n)
{
	unsigned long *addr = (unsigned long *)kernel_stack_pointer(regs);

	addr += n;
	if (regs_within_kernel_stack(regs, (unsigned long)addr))
		return *addr;
	else
		return 0;
}

struct task_struct;

/**
 * regs_get_kernel_argument() - get Nth function argument in kernel
 * @regs:       pt_regs of that context
 * @n:          function argument number (start from 0)
 *
 * regs_get_argument() returns @n th argument of the function call.
 * Note that this chooses most probably assignment, in some case
 * it can be incorrect.
 * This is expected to be called from kprobes or ftrace with regs
 * where the top of stack is the return address.
 */
static inline unsigned long regs_get_kernel_argument(struct pt_regs *regs,
						     unsigned int n)
{
#define NR_REG_ARGUMENTS 8
	static const unsigned int args[] = {
		offsetof(struct pt_regs, regs[4]),
		offsetof(struct pt_regs, regs[5]),
		offsetof(struct pt_regs, regs[6]),
		offsetof(struct pt_regs, regs[7]),
		offsetof(struct pt_regs, regs[8]),
		offsetof(struct pt_regs, regs[9]),
		offsetof(struct pt_regs, regs[10]),
		offsetof(struct pt_regs, regs[11]),
	};

	if (n < NR_REG_ARGUMENTS)
		return regs_get_register(regs, args[n]);
	else {
		n -= NR_REG_ARGUMENTS;
		return regs_get_kernel_stack_nth(regs, n);
	}
}

/*
 * Does the process account for user or for system time?
 */
#define user_mode(regs) (((regs)->csr_prmd & PLV_MASK) == PLV_USER)

static inline long regs_return_value(struct pt_regs *regs)
{
	return regs->regs[4];
}

static inline void regs_set_return_value(struct pt_regs *regs, unsigned long val)
{
	regs->regs[4] = val;
}

#define instruction_pointer(regs) ((regs)->csr_era)
#define profile_pc(regs) instruction_pointer(regs)

extern void die(const char *str, struct pt_regs *regs);

static inline void die_if_kernel(const char *str, struct pt_regs *regs)
{
	if (unlikely(!user_mode(regs)))
		die(str, regs);
}

#define current_pt_regs()						\
({									\
	unsigned long sp = (unsigned long)__builtin_frame_address(0);	\
	(struct pt_regs *)((sp | (THREAD_SIZE - 1)) + 1) - 1;		\
})

/* Helpers for working with the user stack pointer */

static inline unsigned long user_stack_pointer(struct pt_regs *regs)
{
	return regs->regs[3];
}

static inline void user_stack_pointer_set(struct pt_regs *regs,
	unsigned long val)
{
	regs->regs[3] = val;
}

#ifdef CONFIG_HAVE_HW_BREAKPOINT
#define arch_has_single_step()		(1)
#endif

#endif /* _ASM_PTRACE_H */
