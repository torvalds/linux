/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
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
#include <asm/dsp.h>
#include <asm/fpu.h>

/* Arch specific stuff which needs to be saved per task.
 * However these items are not so important so as to earn a place in
 * struct thread_info
 */
struct thread_struct {
	unsigned long ksp;	/* kernel mode stack pointer */
	unsigned long callee_reg;	/* pointer to callee regs */
	unsigned long fault_address;	/* dbls as brkpt holder as well */
#ifdef CONFIG_ARC_DSP_SAVE_RESTORE_REGS
	struct dsp_callee_regs dsp;
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

#define task_pt_regs(p) \
	((struct pt_regs *)(THREAD_SIZE + (void *)task_stack_page(p)) - 1)

/* Free all resources held by a thread */
#define release_thread(thread) do { } while (0)

/*
 * A lot of busy-wait loops in SMP are based off of non-volatile data otherwise
 * get optimised away by gcc
 */
#define cpu_relax()		barrier()

#define KSTK_EIP(tsk)   (task_pt_regs(tsk)->ret)
#define KSTK_ESP(tsk)   (task_pt_regs(tsk)->sp)

/*
 * Where about of Task's sp, fp, blink when it was last seen in kernel mode.
 * Look in process.c for details of kernel stack layout
 */
#define TSK_K_ESP(tsk)		(tsk->thread.ksp)

#define TSK_K_REG(tsk, off)	(*((unsigned long *)(TSK_K_ESP(tsk) + \
					sizeof(struct callee_regs) + off)))

#define TSK_K_BLINK(tsk)	TSK_K_REG(tsk, 4)
#define TSK_K_FP(tsk)		TSK_K_REG(tsk, 0)

extern void start_thread(struct pt_regs * regs, unsigned long pc,
			 unsigned long usp);

extern unsigned int get_wchan(struct task_struct *p);

#endif /* !__ASSEMBLY__ */

/*
 * Default System Memory Map on ARC
 *
 * ---------------------------- (lower 2G, Translated) -------------------------
 * 0x0000_0000		0x5FFF_FFFF	(user vaddr: TASK_SIZE)
 * 0x6000_0000		0x6FFF_FFFF	(reserved gutter between U/K)
 * 0x7000_0000		0x7FFF_FFFF	(kvaddr: vmalloc/modules/pkmap..)
 *
 * PAGE_OFFSET ---------------- (Upper 2G, Untranslated) -----------------------
 * 0x8000_0000		0xBFFF_FFFF	(kernel direct mapped)
 * 0xC000_0000		0xFFFF_FFFF	(peripheral uncached space)
 * -----------------------------------------------------------------------------
 */

#define TASK_SIZE	0x60000000

#define VMALLOC_START	(PAGE_OFFSET - (CONFIG_ARC_KVADDR_SIZE << 20))

/* 1 PGDIR_SIZE each for fixmap/pkmap, 2 PGDIR_SIZE gutter (see asm/highmem.h) */
#define VMALLOC_SIZE	((CONFIG_ARC_KVADDR_SIZE << 20) - PGDIR_SIZE * 4)

#define VMALLOC_END	(VMALLOC_START + VMALLOC_SIZE)

#define USER_KERNEL_GUTTER    (VMALLOC_START - TASK_SIZE)

#define STACK_TOP       TASK_SIZE
#define STACK_TOP_MAX   STACK_TOP

/* This decides where the kernel will search for a free chunk of vm
 * space during mmap's.
 */
#define TASK_UNMAPPED_BASE      (TASK_SIZE / 3)

#endif /* __ASM_ARC_PROCESSOR_H */
