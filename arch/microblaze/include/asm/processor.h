/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2008-2009 PetaLogix
 * Copyright (C) 2006 Atmark Techno, Inc.
 */

#ifndef _ASM_MICROBLAZE_PROCESSOR_H
#define _ASM_MICROBLAZE_PROCESSOR_H

#include <asm/ptrace.h>
#include <asm/setup.h>
#include <asm/registers.h>
#include <asm/entry.h>
#include <asm/current.h>

# ifndef __ASSEMBLY__
/* from kernel/cpu/mb.c */
extern const struct seq_operations cpuinfo_op;

# define cpu_relax()		barrier()

#define task_pt_regs(tsk) \
		(((struct pt_regs *)(THREAD_SIZE + task_stack_page(tsk))) - 1)

/* Do necessary setup to start up a newly executed thread. */
void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long usp);

extern void ret_from_fork(void);
extern void ret_from_kernel_thread(void);

# endif /* __ASSEMBLY__ */

/*
 * This is used to define STACK_TOP, and with MMU it must be below
 * kernel base to select the correct PGD when handling MMU exceptions.
 */
# define TASK_SIZE	(CONFIG_KERNEL_START)

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
# define TASK_UNMAPPED_BASE	(TASK_SIZE / 8 * 3)

# define THREAD_KSP	0

#  ifndef __ASSEMBLY__

/* If you change this, you must change the associated assembly-languages
 * constants defined below, THREAD_*.
 */
struct thread_struct {
	/* kernel stack pointer (must be first field in structure) */
	unsigned long	ksp;
	unsigned long	ksp_limit;	/* if ksp <= ksp_limit stack overflow */
	void		*pgdir;		/* root of page-table tree */
	struct pt_regs	*regs;		/* Pointer to saved register state */
};

#  define INIT_THREAD { \
	.ksp   = sizeof init_stack + (unsigned long)init_stack, \
	.pgdir = swapper_pg_dir, \
}

unsigned long __get_wchan(struct task_struct *p);

/* The size allocated for kernel stacks. This _must_ be a power of two! */
# define KERNEL_STACK_SIZE	0x2000

/* Return some info about the user process TASK.  */
#  define task_tos(task)	((unsigned long)(task) + KERNEL_STACK_SIZE)
#  define task_regs(task) ((struct pt_regs *)task_tos(task) - 1)

#  define task_pt_regs_plus_args(tsk) \
	((void *)task_pt_regs(tsk))

#  define task_sp(task)	(task_regs(task)->r1)
#  define task_pc(task)	(task_regs(task)->pc)
/* Grotty old names for some.  */
#  define KSTK_EIP(task)	(task_pc(task))
#  define KSTK_ESP(task)	(task_sp(task))

#  define STACK_TOP	TASK_SIZE
#  define STACK_TOP_MAX	STACK_TOP

#ifdef CONFIG_DEBUG_FS
extern struct dentry *of_debugfs_root;
#endif

#  endif /* __ASSEMBLY__ */
#endif /* _ASM_MICROBLAZE_PROCESSOR_H */
