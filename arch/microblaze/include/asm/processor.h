/*
 * Copyright (C) 2008 Michal Simek
 * Copyright (C) 2008 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_PROCESSOR_H
#define _ASM_MICROBLAZE_PROCESSOR_H

#include <asm/ptrace.h>
#include <asm/setup.h>
#include <asm/registers.h>
#include <asm/segment.h>

# ifndef __ASSEMBLY__
/* from kernel/cpu/mb.c */
extern const struct seq_operations cpuinfo_op;

# define cpu_relax()		barrier()
# define cpu_sleep()		do {} while (0)
# define prepare_to_copy(tsk)	do {} while (0)

# endif /* __ASSEMBLY__ */

/*
 * User space process size: memory size
 *
 * TASK_SIZE on MMU cpu is usually 1GB. However, on no-MMU arch, both
 * user processes and the kernel is on the same memory region. They
 * both share the memory space and that is limited by the amount of
 * physical memory. thus, we set TASK_SIZE == amount of total memory.
 */
# define TASK_SIZE	(0x81000000 - 0x80000000)

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
# define current_text_addr() ({ __label__ _l; _l: &&_l; })

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's. We won't be using it
 */
# define TASK_UNMAPPED_BASE	0

/* definition in include/linux/sched.h */
struct task_struct;

/* thread_struct is gone. use thread_info instead. */
struct thread_struct { };
# define INIT_THREAD	{ }

/* Do necessary setup to start up a newly executed thread. */
static inline void start_thread(struct pt_regs *regs,
				unsigned long pc,
				unsigned long usp)
{
	regs->pc = pc;
	regs->r1 = usp;
	regs->kernel_mode = 0;
}

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

/* Free all resources held by a thread. */
static inline void exit_thread(void)
{
}

extern unsigned long thread_saved_pc(struct task_struct *t);

extern unsigned long get_wchan(struct task_struct *p);

/*
 * create a kernel thread without removing it from tasklists
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

# define task_pt_regs(tsk) \
		(((struct pt_regs *)(THREAD_SIZE + task_stack_page(tsk))) - 1)

# define KSTK_EIP(tsk)	(0)
# define KSTK_ESP(tsk)	(0)

#endif /* _ASM_MICROBLAZE_PROCESSOR_H */
