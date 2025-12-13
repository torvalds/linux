/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 2002, 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <linux/bits.h>
#include <vdso/page.h>

/*
 * General size of kernel stacks
 */
#if defined(CONFIG_KASAN) || defined(CONFIG_KMSAN)
#define THREAD_SIZE_ORDER 4
#else
#define THREAD_SIZE_ORDER 2
#endif
#define BOOT_STACK_SIZE (PAGE_SIZE << 2)
#define THREAD_SIZE (PAGE_SIZE << THREAD_SIZE_ORDER)

#define STACK_INIT_OFFSET (THREAD_SIZE - STACK_FRAME_OVERHEAD - __PT_SIZE)

#ifndef __ASSEMBLER__

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must also be changed
 */
struct thread_info {
	unsigned long		flags;		/* low level flags */
	unsigned long		syscall_work;	/* SYSCALL_WORK_ flags */
	unsigned int		cpu;		/* current CPU */
	unsigned char		sie;		/* running in SIE context */
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
}

struct task_struct;

void arch_setup_new_exec(void);
#define arch_setup_new_exec arch_setup_new_exec

#endif

/*
 * thread information flags bit numbers
 *
 * Tell the generic TIF infrastructure which special bits s390 supports
 */
#define HAVE_TIF_NEED_RESCHED_LAZY
#define HAVE_TIF_RESTORE_SIGMASK

#include <asm-generic/thread_info_tif.h>

/* Architecture specific bits */
#define TIF_ASCE_PRIMARY	16	/* primary asce is kernel asce */
#define TIF_GUARDED_STORAGE	17	/* load guarded storage control block */
#define TIF_ISOLATE_BP_GUEST	18	/* Run KVM guests with isolated BP */
#define TIF_PER_TRAP		19	/* Need to handle PER trap on exit to usermode */
#define TIF_31BIT		20	/* 32bit process */
#define TIF_SINGLE_STEP		21	/* This task is single stepped */
#define TIF_BLOCK_STEP		22	/* This task is block stepped */
#define TIF_UPROBE_SINGLESTEP	23	/* This task is uprobe single stepped */

#define _TIF_ASCE_PRIMARY	BIT(TIF_ASCE_PRIMARY)
#define _TIF_GUARDED_STORAGE	BIT(TIF_GUARDED_STORAGE)
#define _TIF_ISOLATE_BP_GUEST	BIT(TIF_ISOLATE_BP_GUEST)
#define _TIF_PER_TRAP		BIT(TIF_PER_TRAP)
#define _TIF_31BIT		BIT(TIF_31BIT)
#define _TIF_SINGLE_STEP	BIT(TIF_SINGLE_STEP)
#define _TIF_BLOCK_STEP		BIT(TIF_BLOCK_STEP)
#define _TIF_UPROBE_SINGLESTEP	BIT(TIF_UPROBE_SINGLESTEP)

#endif /* _ASM_THREAD_INFO_H */
