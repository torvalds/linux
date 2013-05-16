/*
 * Copyright (C) 2013 Altera Corporation
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2004 Microtronix Datacom Ltd
 * Copyright (C) 2001 Ken Hill (khill@microtronix.com)
 *                    Vic Phillips (vic@microtronix.com)
 *
 * based on SPARC asm/processor_32.h which is:
 *
 * Copyright (C) 1994 David S. Miller
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_NIOS2_PROCESSOR_H
#define _ASM_NIOS2_PROCESSOR_H

#include <asm/ptrace.h>
#include <asm/registers.h>

#define NIOS2_FLAG_KTHREAD	0x00000001	/* task is a kernel thread */

#define NIOS2_OP_NOP		0x1883a
#define NIOS2_OP_BREAK		0x3da03a

#ifdef CONFIG_MMU
#ifdef __KERNEL__

#define STACK_TOP	TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

#endif /* __KERNEL__ */
#endif /* CONFIG_MMU */

#ifndef __ASSEMBLY__

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l; })

#ifdef CONFIG_MMU
# define TASK_SIZE		0x7FFF0000UL
# define TASK_UNMAPPED_BASE	(PAGE_ALIGN(TASK_SIZE / 3))
#else
/*
 * User space process size: 1st byte beyond user address space.
 * Fairly meaningless on nommu.  Parts of user programs can be scattered
 * in a lot of places, so just disable this by setting it to 0xFFFFFFFF.
 */
# define TASK_SIZE		0xFFFFFFFFUL
/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's. We won't be using it.
 */
# define TASK_UNMAPPED_BASE	0
#endif /* CONFIG_MMU */

/* The Nios processor specific thread struct. */
struct thread_struct {
	struct pt_regs *kregs;

	/* Context switch saved kernel state. */
	unsigned long ksp;
	unsigned long kpsr;
#ifndef CONFIG_MMU
	unsigned long kesr;
#endif
};

#define INIT_MMAP \
	{ &init_mm, (0), (0), __pgprot(0x0), VM_READ | VM_WRITE | VM_EXEC }

#ifdef CONFIG_MMU
# define INIT_THREAD {			\
	.kregs	= NULL,			\
	.ksp	= 0,			\
	.kpsr	= 0,			\
}
#else
# define INIT_THREAD {			\
	.kregs	= NULL,			\
	.ksp	= sizeof(init_stack) + (unsigned long) init_stack, \
	.kpsr	= 0,			\
}
#endif /* CONFIG_MMU */

extern void start_thread(struct pt_regs *regs, unsigned long pc,
			unsigned long sp);

struct task_struct;

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

/* Free current thread data structures etc.. */
static inline void exit_thread(void)
{
}

/* Return saved PC of a blocked thread. */
#define thread_saved_pc(tsk)	((tsk)->thread.kregs->ea)

extern unsigned long get_wchan(struct task_struct *p);

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)	do { } while (0)

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + task_stack_page(p)) - 1)

/* Used by procfs */
#define KSTK_EIP(tsk)	((tsk)->thread.kregs->ea)
#define KSTK_ESP(tsk)	((tsk)->thread.kregs->sp)

#define cpu_relax()	barrier()

#endif /* __ASSEMBLY__ */

#endif /* _ASM_NIOS2_PROCESSOR_H */
