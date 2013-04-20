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

#ifdef __KERNEL__

#ifndef __ASSEMBLY__

#include <asm/arcregs.h>	/* for STATUS_E1_MASK et all */

/* Arch specific stuff which needs to be saved per task.
 * However these items are not so important so as to earn a place in
 * struct thread_info
 */
struct thread_struct {
	unsigned long ksp;	/* kernel mode stack pointer */
	unsigned long callee_reg;	/* pointer to callee regs */
	unsigned long fault_address;	/* dbls as brkpt holder as well */
	unsigned long cause_code;	/* Exception Cause Code (ECR) */
#ifdef CONFIG_ARC_CURR_IN_REG
	unsigned long user_r25;
#endif
#ifdef CONFIG_ARC_FPU_SAVE_RESTORE
	struct arc_fpu fpu;
#endif
};

#define INIT_THREAD  {                          \
	.ksp = sizeof(init_stack) + (unsigned long) init_stack, \
}

/* Forward declaration, a strange C thing */
struct task_struct;

/*
 * Return saved PC of a blocked thread.
 */
unsigned long thread_saved_pc(struct task_struct *t);

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE - 4 + (void *)task_stack_page(p)) - 1)

/* Free all resources held by a thread. */
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

#define copy_segments(tsk, mm)      do { } while (0)
#define release_segments(mm)        do { } while (0)

#define KSTK_EIP(tsk)   (task_pt_regs(tsk)->ret)

/*
 * Where abouts of Task's sp, fp, blink when it was last seen in kernel mode.
 * These can't be derived from pt_regs as that would give correp user-mode val
 */
#define KSTK_ESP(tsk)   (tsk->thread.ksp)
#define KSTK_BLINK(tsk) (*((unsigned int *)((KSTK_ESP(tsk)) + (13+1+1)*4)))
#define KSTK_FP(tsk)    (*((unsigned int *)((KSTK_ESP(tsk)) + (13+1)*4)))

/*
 * Do necessary setup to start up a newly executed thread.
 *
 * E1,E2 so that Interrupts are enabled in user mode
 * L set, so Loop inhibited to begin with
 * lp_start and lp_end seeded with bogus non-zero values so to easily catch
 * the ARC700 sr to lp_start hardware bug
 */
#define start_thread(_regs, _pc, _usp)				\
do {								\
	set_fs(USER_DS); /* reads from user space */		\
	(_regs)->ret = (_pc);					\
	/* Interrupts enabled in User Mode */			\
	(_regs)->status32 = STATUS_U_MASK | STATUS_L_MASK	\
		| STATUS_E1_MASK | STATUS_E2_MASK;		\
	(_regs)->sp = (_usp);					\
	/* bogus seed values for debugging */			\
	(_regs)->lp_start = 0x10;				\
	(_regs)->lp_end = 0x80;					\
} while (0)

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

#endif /* __KERNEL__ */

#endif /* __ASM_ARC_PROCESSOR_H */
