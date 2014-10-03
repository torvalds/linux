/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  Updated for 2.6.34: Mark Salter <msalter@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_PROCESSOR_H
#define _ASM_C6X_PROCESSOR_H

#include <asm/ptrace.h>
#include <asm/page.h>
#include <asm/current.h>

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr()			\
({						\
	void *__pc;				\
	asm("mvc .S2 pce1,%0\n" : "=b"(__pc));	\
	__pc;					\
})

/*
 * User space process size. This is mostly meaningless for NOMMU
 * but some C6X processors may have RAM addresses up to 0xFFFFFFFF.
 * Since calls like mmap() can return an address or an error, we
 * have to allow room for error returns when code does something
 * like:
 *
 *       addr = do_mmap(...)
 *       if ((unsigned long)addr >= TASK_SIZE)
 *            ... its an error code, not an address ...
 *
 * Here, we allow for 4096 error codes which means we really can't
 * use the last 4K page on systems with RAM extending all the way
 * to the end of the 32-bit address space.
 */
#define TASK_SIZE	0xFFFFF000

/*
 * This decides where the kernel will search for a free chunk of vm
 * space during mmap's. We won't be using it
 */
#define TASK_UNMAPPED_BASE	0

struct thread_struct {
	unsigned long long b15_14;
	unsigned long long a15_14;
	unsigned long long b13_12;
	unsigned long long a13_12;
	unsigned long long b11_10;
	unsigned long long a11_10;
	unsigned long long ricl_icl;
	unsigned long  usp;		/* user stack pointer */
	unsigned long  pc;		/* kernel pc */
	unsigned long  wchan;
};

#define INIT_THREAD					\
{							\
	.usp = 0,					\
	.wchan = 0,					\
}

#define INIT_MMAP { \
	&init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, \
	NULL, NULL }

#define task_pt_regs(task) \
	((struct pt_regs *)(THREAD_START_SP + task_stack_page(task)) - 1)

#define alloc_kernel_stack()	__get_free_page(GFP_KERNEL)
#define free_kernel_stack(page) free_page((page))


/* Forward declaration, a strange C thing */
struct task_struct;

extern void start_thread(struct pt_regs *regs, unsigned int pc,
			 unsigned long usp);

/* Free all resources held by a thread. */
static inline void release_thread(struct task_struct *dead_task)
{
}

#define copy_segments(tsk, mm)		do { } while (0)
#define release_segments(mm)		do { } while (0)

/*
 * saved PC of a blocked thread.
 */
#define thread_saved_pc(tsk) (task_pt_regs(tsk)->pc)

/*
 * saved kernel SP and DP of a blocked thread.
 */
#ifdef _BIG_ENDIAN
#define thread_saved_ksp(tsk) \
	(*(unsigned long *)&(tsk)->thread.b15_14)
#define thread_saved_dp(tsk) \
	(*(((unsigned long *)&(tsk)->thread.b15_14) + 1))
#else
#define thread_saved_ksp(tsk) \
	(*(((unsigned long *)&(tsk)->thread.b15_14) + 1))
#define thread_saved_dp(tsk) \
	(*(unsigned long *)&(tsk)->thread.b15_14)
#endif

extern unsigned long get_wchan(struct task_struct *p);

#define KSTK_EIP(task)	(task_pt_regs(task)->pc)
#define KSTK_ESP(task)	(task_pt_regs(task)->sp)

#define cpu_relax()		do { } while (0)
#define cpu_relax_lowlatency()        cpu_relax()

extern const struct seq_operations cpuinfo_op;

/* Reset the board */
#define HARD_RESET_NOW()

extern unsigned int c6x_core_freq;


extern void (*c6x_restart)(void);
extern void (*c6x_halt)(void);

#endif /* ASM_C6X_PROCESSOR_H */
