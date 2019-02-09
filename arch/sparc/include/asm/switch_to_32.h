/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SPARC_SWITCH_TO_H
#define __SPARC_SWITCH_TO_H

#include <asm/smp.h>

extern struct thread_info *current_set[NR_CPUS];

/*
 * Flush windows so that the VM switch which follows
 * would not pull the stack from under us.
 *
 * SWITCH_ENTER and SWITCH_DO_LAZY_FPU do not work yet (e.g. SMP does not work)
 * XXX WTF is the above comment? Found in late teen 2.4.x.
 */
#ifdef CONFIG_SMP
#define SWITCH_ENTER(prv) \
	do {			\
	if (test_tsk_thread_flag(prv, TIF_USEDFPU)) { \
		put_psr(get_psr() | PSR_EF); \
		fpsave(&(prv)->thread.float_regs[0], &(prv)->thread.fsr, \
		       &(prv)->thread.fpqueue[0], &(prv)->thread.fpqdepth); \
		clear_tsk_thread_flag(prv, TIF_USEDFPU); \
		(prv)->thread.kregs->psr &= ~PSR_EF; \
	} \
	} while(0)

#define SWITCH_DO_LAZY_FPU(next)	/* */
#else
#define SWITCH_ENTER(prv)		/* */
#define SWITCH_DO_LAZY_FPU(nxt)	\
	do {			\
	if (last_task_used_math != (nxt))		\
		(nxt)->thread.kregs->psr&=~PSR_EF;	\
	} while(0)
#endif

#define prepare_arch_switch(next) do { \
	__asm__ __volatile__( \
	".globl\tflush_patch_switch\nflush_patch_switch:\n\t" \
	"save %sp, -0x40, %sp; save %sp, -0x40, %sp; save %sp, -0x40, %sp\n\t" \
	"save %sp, -0x40, %sp; save %sp, -0x40, %sp; save %sp, -0x40, %sp\n\t" \
	"save %sp, -0x40, %sp\n\t" \
	"restore; restore; restore; restore; restore; restore; restore"); \
} while(0)

	/* Much care has gone into this code, do not touch it.
	 *
	 * We need to loadup regs l0/l1 for the newly forked child
	 * case because the trap return path relies on those registers
	 * holding certain values, gcc is told that they are clobbered.
	 * Gcc needs registers for 3 values in and 1 value out, so we
	 * clobber every non-fixed-usage register besides l2/l3/o4/o5.  -DaveM
	 *
	 * Hey Dave, that do not touch sign is too much of an incentive
	 * - Anton & Pete
	 */
#define switch_to(prev, next, last) do {						\
	SWITCH_ENTER(prev);								\
	SWITCH_DO_LAZY_FPU(next);							\
	cpumask_set_cpu(smp_processor_id(), mm_cpumask(next->active_mm));		\
	__asm__ __volatile__(								\
	"sethi	%%hi(here - 0x8), %%o7\n\t"						\
	"mov	%%g6, %%g3\n\t"								\
	"or	%%o7, %%lo(here - 0x8), %%o7\n\t"					\
	"rd	%%psr, %%g4\n\t"							\
	"std	%%sp, [%%g6 + %4]\n\t"							\
	"rd	%%wim, %%g5\n\t"							\
	"wr	%%g4, 0x20, %%psr\n\t"							\
	"nop\n\t"									\
	"std	%%g4, [%%g6 + %3]\n\t"							\
	"ldd	[%2 + %3], %%g4\n\t"							\
	"mov	%2, %%g6\n\t"								\
	".globl	patchme_store_new_current\n"						\
"patchme_store_new_current:\n\t"							\
	"st	%2, [%1]\n\t"								\
	"wr	%%g4, 0x20, %%psr\n\t"							\
	"nop\n\t"									\
	"nop\n\t"									\
	"nop\n\t"	/* LEON needs all 3 nops: load to %sp depends on CWP. */		\
	"ldd	[%%g6 + %4], %%sp\n\t"							\
	"wr	%%g5, 0x0, %%wim\n\t"							\
	"ldd	[%%sp + 0x00], %%l0\n\t"						\
	"ldd	[%%sp + 0x38], %%i6\n\t"						\
	"wr	%%g4, 0x0, %%psr\n\t"							\
	"nop\n\t"									\
	"nop\n\t"									\
	"jmpl	%%o7 + 0x8, %%g0\n\t"							\
	" ld	[%%g3 + %5], %0\n\t"							\
	"here:\n"									\
        : "=&r" (last)									\
        : "r" (&(current_set[hard_smp_processor_id()])),	\
	  "r" (task_thread_info(next)),				\
	  "i" (TI_KPSR),					\
	  "i" (TI_KSP),						\
	  "i" (TI_TASK)						\
	:       "g1", "g2", "g3", "g4", "g5",       "g7",	\
	  "l0", "l1",       "l3", "l4", "l5", "l6", "l7",	\
	  "i0", "i1", "i2", "i3", "i4", "i5",			\
	  "o0", "o1", "o2", "o3",                   "o7");	\
	} while(0)

void fpsave(unsigned long *fpregs, unsigned long *fsr,
	    void *fpqueue, unsigned long *fpqdepth);
void synchronize_user_stack(void);

#endif /* __SPARC_SWITCH_TO_H */
