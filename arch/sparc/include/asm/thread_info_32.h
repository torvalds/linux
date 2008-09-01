/*
 * thread_info.h: sparc low-level thread information
 * adapted from the ppc version by Pete Zaitcev, which was
 * adapted from the i386 version by Paul Mackerras
 *
 * Copyright (C) 2002  David Howells (dhowells@redhat.com)
 * Copyright (c) 2002  Pete Zaitcev (zaitcev@yahoo.com)
 * - Incorporating suggestions made by Linus Torvalds and Dave Miller
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <asm/btfixup.h>
#include <asm/ptrace.h>
#include <asm/page.h>

/*
 * Low level task data.
 *
 * If you change this, change the TI_* offsets below to match.
 */
#define NSWINS 8
struct thread_info {
	unsigned long		uwinmask;
	struct task_struct	*task;		/* main task structure */
	struct exec_domain	*exec_domain;	/* execution domain */
	unsigned long		flags;		/* low level flags */
	int			cpu;		/* cpu we're on */
	int			preempt_count;	/* 0 => preemptable,
						   <0 => BUG */
	int			softirq_count;
	int			hardirq_count;

	/* Context switch saved kernel state. */
	unsigned long ksp;	/* ... ksp __attribute__ ((aligned (8))); */
	unsigned long kpc;
	unsigned long kpsr;
	unsigned long kwim;

	/* A place to store user windows and stack pointers
	 * when the stack needs inspection.
	 */
	struct reg_window	reg_window[NSWINS];	/* align for ldd! */
	unsigned long		rwbuf_stkptrs[NSWINS];
	unsigned long		w_saved;

	struct restart_block	restart_block;
};

/*
 * macros/functions for gaining access to the thread information structure
 *
 * preempt_count needs to be 1 initially, until the scheduler is functional.
 */
#define INIT_THREAD_INFO(tsk)				\
{							\
	.uwinmask	=	0,			\
	.task		=	&tsk,			\
	.exec_domain	=	&default_exec_domain,	\
	.flags		=	0,			\
	.cpu		=	0,			\
	.preempt_count	=	1,			\
	.restart_block	= {				\
		.fn	=	do_no_restart_syscall,	\
	},						\
}

#define init_thread_info	(init_thread_union.thread_info)
#define init_stack		(init_thread_union.stack)

/* how to get the thread information struct from C */
register struct thread_info *current_thread_info_reg asm("g6");
#define current_thread_info()   (current_thread_info_reg)

/*
 * thread information allocation
 */
#define THREAD_INFO_ORDER  1

#define __HAVE_ARCH_THREAD_INFO_ALLOCATOR

BTFIXUPDEF_CALL(struct thread_info *, alloc_thread_info, void)
#define alloc_thread_info(tsk) BTFIXUP_CALL(alloc_thread_info)()

BTFIXUPDEF_CALL(void, free_thread_info, struct thread_info *)
#define free_thread_info(ti) BTFIXUP_CALL(free_thread_info)(ti)

#endif /* __ASSEMBLY__ */

/*
 * Size of kernel stack for each process.
 * Observe the order of get_free_pages() in alloc_thread_info().
 * The sun4 has 8K stack too, because it's short on memory, and 16K is a waste.
 */
#define THREAD_SIZE		8192

/*
 * Offsets in thread_info structure, used in assembly code
 * The "#define REGWIN_SZ 0x40" was abolished, so no multiplications.
 */
#define TI_UWINMASK	0x00	/* uwinmask */
#define TI_TASK		0x04
#define TI_EXECDOMAIN	0x08	/* exec_domain */
#define TI_FLAGS	0x0c
#define TI_CPU		0x10
#define TI_PREEMPT	0x14	/* preempt_count */
#define TI_SOFTIRQ	0x18	/* softirq_count */
#define TI_HARDIRQ	0x1c	/* hardirq_count */
#define TI_KSP		0x20	/* ksp */
#define TI_KPC		0x24	/* kpc (ldd'ed with kpc) */
#define TI_KPSR		0x28	/* kpsr */
#define TI_KWIM		0x2c	/* kwim (ldd'ed with kpsr) */
#define TI_REG_WINDOW	0x30
#define TI_RWIN_SPTRS	0x230
#define TI_W_SAVED	0x250
/* #define TI_RESTART_BLOCK 0x25n */ /* Nobody cares */

#define PREEMPT_ACTIVE		0x4000000

/*
 * thread information flag bit numbers
 */
#define TIF_SYSCALL_TRACE	0	/* syscall trace active */
#define TIF_NOTIFY_RESUME	1	/* callback before returning to user */
#define TIF_SIGPENDING		2	/* signal pending */
#define TIF_NEED_RESCHED	3	/* rescheduling necessary */
#define TIF_RESTORE_SIGMASK	4	/* restore signal mask in do_signal() */
#define TIF_USEDFPU		8	/* FPU was used by this task
					 * this quantum (SMP) */
#define TIF_POLLING_NRFLAG	9	/* true if poll_idle() is polling
					 * TIF_NEED_RESCHED */
#define TIF_MEMDIE		10

/* as above, but as bit values */
#define _TIF_SYSCALL_TRACE	(1<<TIF_SYSCALL_TRACE)
#define _TIF_NOTIFY_RESUME	(1<<TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		(1<<TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	(1<<TIF_NEED_RESCHED)
#define _TIF_RESTORE_SIGMASK	(1<<TIF_RESTORE_SIGMASK)
#define _TIF_USEDFPU		(1<<TIF_USEDFPU)
#define _TIF_POLLING_NRFLAG	(1<<TIF_POLLING_NRFLAG)

#define _TIF_DO_NOTIFY_RESUME_MASK	(_TIF_NOTIFY_RESUME | \
					 _TIF_SIGPENDING | \
					 _TIF_RESTORE_SIGMASK)

#endif /* __KERNEL__ */

#endif /* _ASM_THREAD_INFO_H */
