/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  S390 version
 *    Copyright IBM Corp. 2002, 2006
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#ifndef _ASM_THREAD_INFO_H
#define _ASM_THREAD_INFO_H

#include <linux/const.h>

/*
 * Size of kernel stack for each process
 */
#define THREAD_SIZE_ORDER 2
#define ASYNC_ORDER  2

#define THREAD_SIZE (PAGE_SIZE << THREAD_SIZE_ORDER)
#define ASYNC_SIZE  (PAGE_SIZE << ASYNC_ORDER)

#ifndef __ASSEMBLY__
#include <asm/lowcore.h>
#include <asm/page.h>
#include <asm/processor.h>

/*
 * low level task data that entry.S needs immediate access to
 * - this struct should fit entirely inside of one cache line
 * - this struct shares the supervisor stack pages
 * - if the contents of this structure are changed, the assembly constants must also be changed
 */
struct thread_info {
	unsigned long		flags;		/* low level flags */
};

/*
 * macros/functions for gaining access to the thread information structure
 */
#define INIT_THREAD_INFO(tsk)			\
{						\
	.flags		= 0,			\
}

#define init_stack		(init_thread_union.stack)

void arch_release_task_struct(struct task_struct *tsk);
int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src);

#endif

/*
 * thread information flags bit numbers
 */
/* _TIF_WORK bits */
#define TIF_NOTIFY_RESUME	0	/* callback before returning to user */
#define TIF_SIGPENDING		1	/* signal pending */
#define TIF_NEED_RESCHED	2	/* rescheduling necessary */
#define TIF_UPROBE		3	/* breakpointed or single-stepping */
#define TIF_GUARDED_STORAGE	4	/* load guarded storage control block */
#define TIF_PATCH_PENDING	5	/* pending live patching update */
#define TIF_PGSTE		6	/* New mm's will use 4K page tables */
#define TIF_ISOLATE_BP		8	/* Run process with isolated BP */
#define TIF_ISOLATE_BP_GUEST	9	/* Run KVM guests with isolated BP */

#define TIF_31BIT		16	/* 32bit process */
#define TIF_MEMDIE		17	/* is terminating due to OOM killer */
#define TIF_RESTORE_SIGMASK	18	/* restore signal mask in do_signal() */
#define TIF_SINGLE_STEP		19	/* This task is single stepped */
#define TIF_BLOCK_STEP		20	/* This task is block stepped */
#define TIF_UPROBE_SINGLESTEP	21	/* This task is uprobe single stepped */

/* _TIF_TRACE bits */
#define TIF_SYSCALL_TRACE	24	/* syscall trace active */
#define TIF_SYSCALL_AUDIT	25	/* syscall auditing active */
#define TIF_SECCOMP		26	/* secure computing */
#define TIF_SYSCALL_TRACEPOINT	27	/* syscall tracepoint instrumentation */

#define _TIF_NOTIFY_RESUME	_BITUL(TIF_NOTIFY_RESUME)
#define _TIF_SIGPENDING		_BITUL(TIF_SIGPENDING)
#define _TIF_NEED_RESCHED	_BITUL(TIF_NEED_RESCHED)
#define _TIF_UPROBE		_BITUL(TIF_UPROBE)
#define _TIF_GUARDED_STORAGE	_BITUL(TIF_GUARDED_STORAGE)
#define _TIF_PATCH_PENDING	_BITUL(TIF_PATCH_PENDING)
#define _TIF_ISOLATE_BP		_BITUL(TIF_ISOLATE_BP)
#define _TIF_ISOLATE_BP_GUEST	_BITUL(TIF_ISOLATE_BP_GUEST)

#define _TIF_31BIT		_BITUL(TIF_31BIT)
#define _TIF_SINGLE_STEP	_BITUL(TIF_SINGLE_STEP)

#define _TIF_SYSCALL_TRACE	_BITUL(TIF_SYSCALL_TRACE)
#define _TIF_SYSCALL_AUDIT	_BITUL(TIF_SYSCALL_AUDIT)
#define _TIF_SECCOMP		_BITUL(TIF_SECCOMP)
#define _TIF_SYSCALL_TRACEPOINT	_BITUL(TIF_SYSCALL_TRACEPOINT)

#endif /* _ASM_THREAD_INFO_H */
