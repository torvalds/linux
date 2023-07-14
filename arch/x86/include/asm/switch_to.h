/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SWITCH_TO_H
#define _ASM_X86_SWITCH_TO_H

#include <linux/sched/task_stack.h>

struct task_struct; /* one of the stranger aspects of C forward declarations */

struct task_struct *__switch_to_asm(struct task_struct *prev,
				    struct task_struct *next);

__visible struct task_struct *__switch_to(struct task_struct *prev,
					  struct task_struct *next);

asmlinkage void ret_from_fork(void);

/*
 * This is the structure pointed to by thread.sp for an inactive task.  The
 * order of the fields must match the code in __switch_to_asm().
 */
struct inactive_task_frame {
#ifdef CONFIG_X86_64
	unsigned long r15;
	unsigned long r14;
	unsigned long r13;
	unsigned long r12;
#else
	unsigned long flags;
	unsigned long si;
	unsigned long di;
#endif
	unsigned long bx;

	/*
	 * These two fields must be together.  They form a stack frame header,
	 * needed by get_frame_pointer().
	 */
	unsigned long bp;
	unsigned long ret_addr;
};

struct fork_frame {
	struct inactive_task_frame frame;
	struct pt_regs regs;
};

#define switch_to(prev, next, last)					\
do {									\
	((last) = __switch_to_asm((prev), (next)));			\
} while (0)

#ifdef CONFIG_X86_32
static inline void refresh_sysenter_cs(struct thread_struct *thread)
{
	/* Only happens when SEP is enabled, no need to test "SEP"arately: */
	if (unlikely(this_cpu_read(cpu_tss_rw.x86_tss.ss1) == thread->sysenter_cs))
		return;

	this_cpu_write(cpu_tss_rw.x86_tss.ss1, thread->sysenter_cs);
	wrmsr(MSR_IA32_SYSENTER_CS, thread->sysenter_cs, 0);
}
#endif

/* This is used when switching tasks or entering/exiting vm86 mode. */
static inline void update_task_stack(struct task_struct *task)
{
	/* sp0 always points to the entry trampoline stack, which is constant: */
#ifdef CONFIG_X86_32
	this_cpu_write(cpu_tss_rw.x86_tss.sp1, task->thread.sp0);
#else
	/* Xen PV enters the kernel on the thread stack. */
	if (cpu_feature_enabled(X86_FEATURE_XENPV))
		load_sp0(task_top_of_stack(task));
#endif
}

static inline void kthread_frame_init(struct inactive_task_frame *frame,
				      int (*fun)(void *), void *arg)
{
	frame->bx = (unsigned long)fun;
#ifdef CONFIG_X86_32
	frame->di = (unsigned long)arg;
#else
	frame->r12 = (unsigned long)arg;
#endif
}

#endif /* _ASM_X86_SWITCH_TO_H */
