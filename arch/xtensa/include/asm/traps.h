/*
 * arch/xtensa/include/asm/traps.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Tensilica Inc.
 */
#ifndef _XTENSA_TRAPS_H
#define _XTENSA_TRAPS_H

#include <asm/ptrace.h>

typedef void xtensa_exception_handler(struct pt_regs *regs);

/*
 * Per-CPU exception handling data structure.
 * EXCSAVE1 points to it.
 */
struct exc_table {
	/* Kernel Stack */
	void *kstk;
	/* Double exception save area for a0 */
	unsigned long double_save;
	/* Fixup handler */
	void *fixup;
	/* For passing a parameter to fixup */
	void *fixup_param;
#if XTENSA_HAVE_COPROCESSORS
	/* Pointers to owner struct thread_info */
	struct thread_info *coprocessor_owner[XCHAL_CP_MAX];
#endif
	/* Fast user exception handlers */
	void *fast_user_handler[EXCCAUSE_N];
	/* Fast kernel exception handlers */
	void *fast_kernel_handler[EXCCAUSE_N];
	/* Default C-Handlers */
	xtensa_exception_handler *default_handler[EXCCAUSE_N];
};

DECLARE_PER_CPU(struct exc_table, exc_table);

xtensa_exception_handler *
__init trap_set_handler(int cause, xtensa_exception_handler *handler);

asmlinkage void fast_illegal_instruction_user(void);
asmlinkage void fast_syscall_user(void);
asmlinkage void fast_alloca(void);
asmlinkage void fast_unaligned(void);
asmlinkage void fast_second_level_miss(void);
asmlinkage void fast_store_prohibited(void);
asmlinkage void fast_coprocessor(void);

asmlinkage void kernel_exception(void);
asmlinkage void user_exception(void);
asmlinkage void system_call(struct pt_regs *regs);

void do_IRQ(int hwirq, struct pt_regs *regs);
void do_page_fault(struct pt_regs *regs);
void do_unhandled(struct pt_regs *regs);

/* Initialize minimal exc_table structure sufficient for basic paging */
static inline void __init early_trap_init(void)
{
	static struct exc_table init_exc_table __initdata = {
		.fast_kernel_handler[EXCCAUSE_DTLB_MISS] =
			fast_second_level_miss,
	};
	xtensa_set_sr(&init_exc_table, excsave1);
}

void secondary_trap_init(void);

static inline void spill_registers(void)
{
#if defined(__XTENSA_WINDOWED_ABI__)
#if XCHAL_NUM_AREGS > 16
	__asm__ __volatile__ (
		"	call8	1f\n"
		"	_j	2f\n"
		"	retw\n"
		"	.align	4\n"
		"1:\n"
#if XCHAL_NUM_AREGS == 32
		"	_entry	a1, 32\n"
		"	addi	a8, a0, 3\n"
		"	_entry	a1, 16\n"
		"	mov	a12, a12\n"
		"	retw\n"
#else
		"	_entry	a1, 48\n"
		"	call12	1f\n"
		"	retw\n"
		"	.align	4\n"
		"1:\n"
		"	.rept	(" __stringify(XCHAL_NUM_AREGS) " - 16) / 12\n"
		"	_entry	a1, 48\n"
		"	mov	a12, a0\n"
		"	.endr\n"
		"	_entry	a1, 16\n"
#if XCHAL_NUM_AREGS % 12 == 0
		"	mov	a12, a12\n"
#elif XCHAL_NUM_AREGS % 12 == 4
		"	mov	a4, a4\n"
#elif XCHAL_NUM_AREGS % 12 == 8
		"	mov	a8, a8\n"
#endif
		"	retw\n"
#endif
		"2:\n"
		: : : "a8", "a9", "memory");
#else
	__asm__ __volatile__ (
		"	mov	a12, a12\n"
		: : : "memory");
#endif
#endif
}

struct debug_table {
	/* Pointer to debug exception handler */
	void (*debug_exception)(void);
	/* Temporary register save area */
	unsigned long debug_save[1];
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	/* Save area for DBREAKC registers */
	unsigned long dbreakc_save[XCHAL_NUM_DBREAK];
	/* Saved ICOUNT register */
	unsigned long icount_save;
	/* Saved ICOUNTLEVEL register */
	unsigned long icount_level_save;
#endif
};

void debug_exception(void);

#endif /* _XTENSA_TRAPS_H */
