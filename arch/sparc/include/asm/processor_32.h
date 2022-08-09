/* SPDX-License-Identifier: GPL-2.0 */
/* include/asm/processor.h
 *
 * Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __ASM_SPARC_PROCESSOR_H
#define __ASM_SPARC_PROCESSOR_H

#include <asm/psr.h>
#include <asm/ptrace.h>
#include <asm/head.h>
#include <asm/signal.h>
#include <asm/page.h>

/* Whee, this is STACK_TOP + PAGE_SIZE and the lowest kernel address too...
 * That one page is used to protect kernel from intruders, so that
 * we can make our access_ok test faster
 */
#define TASK_SIZE	PAGE_OFFSET
#ifdef __KERNEL__
#define STACK_TOP	(PAGE_OFFSET - PAGE_SIZE)
#define STACK_TOP_MAX	STACK_TOP
#endif /* __KERNEL__ */

struct task_struct;

#ifdef __KERNEL__
struct fpq {
	unsigned long *insn_addr;
	unsigned long insn;
};
#endif

typedef struct {
	int seg;
} mm_segment_t;

/* The Sparc processor specific thread struct. */
struct thread_struct {
	struct pt_regs *kregs;
	unsigned int _pad1;

	/* Special child fork kpsr/kwim values. */
	unsigned long fork_kpsr __attribute__ ((aligned (8)));
	unsigned long fork_kwim;

	/* Floating point regs */
	unsigned long   float_regs[32] __attribute__ ((aligned (8)));
	unsigned long   fsr;
	unsigned long   fpqdepth;
	struct fpq	fpqueue[16];
	mm_segment_t current_ds;
};

#define INIT_THREAD  { \
	.current_ds = KERNEL_DS, \
	.kregs = (struct pt_regs *)(init_stack+THREAD_SIZE)-1 \
}

/* Do necessary setup to start up a newly executed thread. */
static inline void start_thread(struct pt_regs * regs, unsigned long pc,
				    unsigned long sp)
{
	register unsigned long zero asm("g1");

	regs->psr = (regs->psr & (PSR_CWP)) | PSR_S;
	regs->pc = ((pc & (~3)) - 4);
	regs->npc = regs->pc + 4;
	regs->y = 0;
	zero = 0;
	__asm__ __volatile__("std\t%%g0, [%0 + %3 + 0x00]\n\t"
			     "std\t%%g0, [%0 + %3 + 0x08]\n\t"
			     "std\t%%g0, [%0 + %3 + 0x10]\n\t"
			     "std\t%%g0, [%0 + %3 + 0x18]\n\t"
			     "std\t%%g0, [%0 + %3 + 0x20]\n\t"
			     "std\t%%g0, [%0 + %3 + 0x28]\n\t"
			     "std\t%%g0, [%0 + %3 + 0x30]\n\t"
			     "st\t%1, [%0 + %3 + 0x38]\n\t"
			     "st\t%%g0, [%0 + %3 + 0x3c]"
			     : /* no outputs */
			     : "r" (regs),
			       "r" (sp - sizeof(struct reg_window32)),
			       "r" (zero),
			       "i" ((const unsigned long)(&((struct pt_regs *)0)->u_regs[0]))
			     : "memory");
}

/* Free all resources held by a thread. */
#define release_thread(tsk)		do { } while(0)

unsigned long __get_wchan(struct task_struct *);

#define task_pt_regs(tsk) ((tsk)->thread.kregs)
#define KSTK_EIP(tsk)  ((tsk)->thread.kregs->pc)
#define KSTK_ESP(tsk)  ((tsk)->thread.kregs->u_regs[UREG_FP])

#ifdef __KERNEL__

extern struct task_struct *last_task_used_math;
int do_mathemu(struct pt_regs *regs, struct task_struct *fpt);

#define cpu_relax()	barrier()

extern void (*sparc_idle)(void);

#endif

#endif /* __ASM_SPARC_PROCESSOR_H */
