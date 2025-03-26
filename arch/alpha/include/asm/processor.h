/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/asm-alpha/processor.h
 *
 * Copyright (C) 1994 Linus Torvalds
 */

#ifndef __ASM_ALPHA_PROCESSOR_H
#define __ASM_ALPHA_PROCESSOR_H

/*
 * We have a 42-bit user address space: 4TB user VM...
 */
#define TASK_SIZE (0x40000000000UL)

#define STACK_TOP (0x00120000000UL)

#define STACK_TOP_MAX	0x00120000000UL

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE (TASK_SIZE / 2)

/* This is dead.  Everything has been moved to thread_info.  */
struct thread_struct { };
#define INIT_THREAD  { }

/* Do necessary setup to start up a newly executed thread.  */
struct pt_regs;
extern void start_thread(struct pt_regs *, unsigned long, unsigned long);

/* Free all resources held by a thread. */
struct task_struct;
unsigned long __get_wchan(struct task_struct *p);

#define KSTK_EIP(tsk) (task_pt_regs(tsk)->pc)

#define KSTK_ESP(tsk) \
  ((tsk) == current ? rdusp() : task_thread_info(tsk)->pcb.usp)

#define cpu_relax()	barrier()

#define ARCH_HAS_PREFETCH
#define ARCH_HAS_PREFETCHW

extern inline void prefetch(const void *ptr)  
{ 
	__builtin_prefetch(ptr, 0, 3);
}

extern inline void prefetchw(const void *ptr)  
{
	__builtin_prefetch(ptr, 1, 3);
}

#endif /* __ASM_ALPHA_PROCESSOR_H */
