/*
 * include/asm-m68k/processor.h
 *
 * Copyright (C) 1995 Hamish Macdonald
 */

#ifndef __ASM_M68K_PROCESSOR_H
#define __ASM_M68K_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#include <linux/thread_info.h>
#include <asm/segment.h>
#include <asm/fpu.h>
#include <asm/ptrace.h>

static inline unsigned long rdusp(void)
{
#ifdef CONFIG_COLDFIRE_SW_A7
	extern unsigned int sw_usp;
	return sw_usp;
#else
	register unsigned long usp __asm__("a0");
	/* move %usp,%a0 */
	__asm__ __volatile__(".word 0x4e68" : "=a" (usp));
	return usp;
#endif
}

static inline void wrusp(unsigned long usp)
{
#ifdef CONFIG_COLDFIRE_SW_A7
	extern unsigned int sw_usp;
	sw_usp = usp;
#else
	register unsigned long a0 __asm__("a0") = usp;
	/* move %a0,%usp */
	__asm__ __volatile__(".word 0x4e60" : : "a" (a0) );
#endif
}

/*
 * User space process size: 3.75GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */
#ifdef CONFIG_MMU
#if defined(CONFIG_COLDFIRE)
#define TASK_SIZE	(0xC0000000UL)
#elif defined(CONFIG_SUN3)
#define TASK_SIZE	(0x0E000000UL)
#else
#define TASK_SIZE	(0xF0000000UL)
#endif
#else
#define TASK_SIZE	(0xFFFFFFFFUL)
#endif

#ifdef __KERNEL__
#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP
#endif

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#ifdef CONFIG_MMU
#if defined(CONFIG_COLDFIRE)
#define TASK_UNMAPPED_BASE	0x60000000UL
#elif defined(CONFIG_SUN3)
#define TASK_UNMAPPED_BASE	0x0A000000UL
#else
#define TASK_UNMAPPED_BASE	0xC0000000UL
#endif
#define TASK_UNMAPPED_ALIGN(addr, off)	PAGE_ALIGN(addr)
#else
#define TASK_UNMAPPED_BASE	0
#endif

struct thread_struct {
	unsigned long  ksp;		/* kernel stack pointer */
	unsigned long  usp;		/* user stack pointer */
	unsigned short sr;		/* saved status register */
	unsigned short fs;		/* saved fs (sfc, dfc) */
	unsigned long  crp[2];		/* cpu root pointer */
	unsigned long  esp0;		/* points to SR of stack frame */
	unsigned long  faddr;		/* info about last fault */
	int            signo, code;
	unsigned long  fp[8*3];
	unsigned long  fpcntl[3];	/* fp control regs */
	unsigned char  fpstate[FPSTATESIZE];  /* floating point state */
};

#define INIT_THREAD  {							\
	.ksp	= sizeof(init_stack) + (unsigned long) init_stack,	\
	.sr	= PS_S,							\
	.fs	= __KERNEL_DS,						\
}

/*
 * ColdFire stack format sbould be 0x4 for an aligned usp (will always be
 * true on thread creation). We need to set this explicitly.
 */
#ifdef CONFIG_COLDFIRE
#define setframeformat(_regs)	do { (_regs)->format = 0x4; } while(0)
#else
#define setframeformat(_regs)	do { } while (0)
#endif

/*
 * Do necessary setup to start up a newly executed thread.
 */
static inline void start_thread(struct pt_regs * regs, unsigned long pc,
				unsigned long usp)
{
	regs->pc = pc;
	regs->sr &= ~0x2000;
	setframeformat(regs);
	wrusp(usp);
}

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

unsigned long get_wchan(struct task_struct *p);

#define	KSTK_EIP(tsk)	\
    ({			\
	unsigned long eip = 0;	 \
	if ((tsk)->thread.esp0 > PAGE_SIZE && \
	    (virt_addr_valid((tsk)->thread.esp0))) \
	      eip = ((struct pt_regs *) (tsk)->thread.esp0)->pc; \
	eip; })
#define	KSTK_ESP(tsk)	((tsk) == current ? rdusp() : (tsk)->thread.usp)

#define task_pt_regs(tsk)	((struct pt_regs *) ((tsk)->thread.esp0))

#define cpu_relax()	barrier()

#endif
