/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * vineetg: March 2009
 *  -Implemented task_pt_regs( )
 *
 * Amit Bhor, Sameer Dhavale, Ashwin Chaugule: Codito Technologies 2004
 */

#ifndef __ASM_ARC_PROCESSOR_H
#define __ASM_ARC_PROCESSOR_H

#ifndef __ASSEMBLY__

#include <asm/ptrace.h>

/* Arch specific stuff which needs to be saved per task.
 * However these items are not so important so as to earn a place in
 * struct thread_info
 */
struct thread_struct {
	unsigned long ksp;	/* kernel mode stack pointer */
	unsigned long callee_reg;	/* pointer to callee regs */
	unsigned long fault_address;	/* dbls as brkpt holder as well */
#ifdef CONFIG_ARC_FPU_SAVE_RESTORE
	struct arc_fpu fpu;
#endif
};

#define INIT_THREAD  {                          \
	.ksp = sizeof(init_stack) + (unsigned long) init_stack, \
}

/* Forward declaration, a strange C thing */
struct task_struct;

/* Return saved PC of a blocked thread  */
unsigned long thread_saved_pc(struct task_struct *t);

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + (void *)task_stack_page(p)) - 1)

/* Free all resources held by a thread */
#define release_thread(thread) do { } while (0)

/* Prepare to copy thread state - unlazy all lazy status */
#define prepare_to_copy(tsk)    do { } while (0)

/*
 * A lot of busy-wait loops in SMP are based off of non-volatile data otherwise
 * get optimised away by gcc
 */
#ifdef CONFIG_SMP
#define cpu_relax()	__asm__ __volatile__ ("" : : : "memory")
#else
#define cpu_relax()	do { } while (0)
#endif

#define cpu_relax_lowlatency() cpu_relax()

#define copy_segments(tsk, mm)      do { } while (0)
#define release_segments(mm)        do { } while (0)

#define KSTK_EIP(tsk)   (task_pt_regs(tsk)->ret)

/*
 * Where abouts of Task's sp, fp, blink when it was last seen in kernel mode.
 * Look in process.c for details of kernel stack layout
 */
#define KSTK_ESP(tsk)   (tsk->thread.ksp)

#define KSTK_REG(tsk, off)	(*((unsigned int *)(KSTK_ESP(tsk) + \
					sizeof(struct callee_regs) + off)))

#define KSTK_BLINK(tsk) KSTK_REG(tsk, 4)
#define KSTK_FP(tsk)    KSTK_REG(tsk, 0)

extern void start_thread(struct pt_regs * regs, unsigned long pc,
			 unsigned long usp);

extern unsigned int get_wchan(struct task_struct *p);

/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 * Should the PC register be read instead ? This macro does not seem to
 * be used in many places so this wont be all that bad.
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l; })

#endif /* !__ASSEMBLY__ */

/* Kernels Virtual memory area.
 * Unlike other architectures(MIPS, sh, cris ) ARC 700 does not have a
 * "kernel translated" region (like KSEG2 in MIPS). So we use a upper part
 * of the translated bottom 2GB for kernel virtual memory and protect
 * these pages from user accesses by disabling Ru, Eu and Wu.
 */
#define VMALLOC_SIZE	(0x10000000)	/* 256M */
#define VMALLOC_START	(PAGE_OFFSET - VMALLOC_SIZE)
#define VMALLOC_END	(PAGE_OFFSET)

/* Most of the architectures seem to be keeping some kind of padding between
 * userspace TASK_SIZE and PAGE_OFFSET. i.e TASK_SIZE != PAGE_OFFSET.
 */
#define USER_KERNEL_GUTTER    0x10000000

/* User address space:
 * On ARC700, CPU allows the entire lower half of 32 bit address space to be
 * translated. Thus potentially 2G (0:0x7FFF_FFFF) could be User vaddr space.
 * However we steal 256M for kernel addr (0x7000_0000:0x7FFF_FFFF) and another
 * 256M (0x6000_0000:0x6FFF_FFFF) is gutter between user/kernel spaces
 * Thus total User vaddr space is (0:0x5FFF_FFFF)
 */
#define TASK_SIZE	(PAGE_OFFSET - VMALLOC_SIZE - USER_KERNEL_GUTTER)

#define STACK_TOP       TASK_SIZE
#define STACK_TOP_MAX   STACK_TOP

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE      (TASK_SIZE / 3)

#endif /* __ASM_ARC_PROCESSOR_H */
