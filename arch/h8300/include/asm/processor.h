/*
 * include/asm-h8300/processor.h
 *
 * Copyright (C) 2002 Yoshinori Sato
 *
 * Based on: linux/asm-m68nommu/processor.h
 *
 * Copyright (C) 1995 Hamish Macdonald
 */

#ifndef __ASM_H8300_PROCESSOR_H
#define __ASM_H8300_PROCESSOR_H

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#include <linux/compiler.h>
#include <asm/segment.h>
#include <asm/fpu.h>
#include <asm/ptrace.h>
#include <asm/current.h>

static inline unsigned long rdusp(void) {
	extern unsigned int	sw_usp;
	return(sw_usp);
}

static inline void wrusp(unsigned long usp) {
	extern unsigned int	sw_usp;
	sw_usp = usp;
}

/*
 * User space process size: 3.75GB. This is hardcoded into a few places,
 * so don't change it unless you know what you are doing.
 */
#define TASK_SIZE	(0xFFFFFFFFUL)

#ifdef __KERNEL__
#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP
#endif

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's. We won't be using it
 */
#define TASK_UNMAPPED_BASE	0

struct thread_struct {
	unsigned long  ksp;		/* kernel stack pointer */
	unsigned long  usp;		/* user stack pointer */
	unsigned long  ccr;		/* saved status register */
	unsigned long  esp0;            /* points to SR of stack frame */
	struct {
		unsigned short *addr;
		unsigned short inst;
	} breakinfo;
};

#define INIT_THREAD  {						\
	.ksp  = sizeof(init_stack) + (unsigned long)init_stack, \
	.usp  = 0,						\
	.ccr  = PS_S,						\
	.esp0 = 0,						\
	.breakinfo = {						\
		.addr = (unsigned short *)-1,			\
		.inst = 0					\
	}							\
}

/*
 * Do necessary setup to start up a newly executed thread.
 *
 * pass the data segment into user programs if it exists,
 * it can't hurt anything as far as I can tell
 */
#if defined(__H8300H__)
#define start_thread(_regs, _pc, _usp)			        \
do {							        \
	set_fs(USER_DS);           /* reads from user space */  \
  	(_regs)->pc = (_pc);				        \
	(_regs)->ccr = 0x00;	   /* clear all flags */        \
	(_regs)->er5 = current->mm->start_data;	/* GOT base */  \
	wrusp((unsigned long)(_usp) - sizeof(unsigned long)*3);	\
} while(0)
#endif
#if defined(__H8300S__)
#define start_thread(_regs, _pc, _usp)			        \
do {							        \
	set_fs(USER_DS);           /* reads from user space */  \
	(_regs)->pc = (_pc);				        \
	(_regs)->ccr = 0x00;	   /* clear kernel flag */      \
	(_regs)->exr = 0x78;       /* enable all interrupts */  \
	(_regs)->er5 = current->mm->start_data;	/* GOT base */  \
	/* 14 = space for retaddr(4), vector(4), er0(4) and ext(2) on stack */ \
	wrusp(((unsigned long)(_usp)) - 14);                    \
} while(0)
#endif

/* Forward declaration, a strange C thing */
struct task_struct;

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

extern int kernel_thread(int (*fn)(void *), void * arg, unsigned long flags);

#define prepare_to_copy(tsk)	do { } while (0)

/*
 * Free current thread data structures etc..
 */
static inline void exit_thread(void)
{
}

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *tsk);
unsigned long get_wchan(struct task_struct *p);

#define	KSTK_EIP(tsk)	\
    ({			\
	unsigned long eip = 0;	 \
	if ((tsk)->thread.esp0 > PAGE_SIZE && \
	    MAP_NR((tsk)->thread.esp0) < max_mapnr) \
	      eip = ((struct pt_regs *) (tsk)->thread.esp0)->pc; \
	eip; })
#define	KSTK_ESP(tsk)	((tsk) == current ? rdusp() : (tsk)->thread.usp)

#define cpu_relax()    barrier()

#endif
