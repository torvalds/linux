// SPDX-License-Identifier: GPL-2.0-only
/*
 * Process creation support for Hexagon
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 */

#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/tick.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/resume_user_mode.h>

/*
 * Program thread launch.  Often defined as a macro in processor.h,
 * but we're shooting for a small footprint and it's not an inner-loop
 * performance-critical operation.
 *
 * The Hexagon ABI specifies that R28 is zero'ed before program launch,
 * so that gets automatically done here.  If we ever stop doing that here,
 * we'll probably want to define the ELF_PLAT_INIT macro.
 */
void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	/* We want to zero all data-containing registers. Is this overkill? */
	memset(regs, 0, sizeof(*regs));
	/* We might want to also zero all Processor registers here */
	pt_set_usermode(regs);
	pt_set_elr(regs, pc);
	pt_set_rte_sp(regs, sp);
}

/*
 *  Spin, or better still, do a hardware or VM wait instruction
 *  If hardware or VM offer wait termination even though interrupts
 *  are disabled.
 */
void arch_cpu_idle(void)
{
	__vmwait();
	/*  interrupts wake us up, but irqs are still disabled */
	raw_local_irq_enable();
}

/*
 * Copy architecture-specific thread state
 */
int copy_thread(struct task_struct *p, const struct kernel_clone_args *args)
{
	unsigned long clone_flags = args->flags;
	unsigned long usp = args->stack;
	unsigned long tls = args->tls;
	struct thread_info *ti = task_thread_info(p);
	struct hexagon_switch_stack *ss;
	struct pt_regs *childregs;
	asmlinkage void ret_from_fork(void);

	childregs = (struct pt_regs *) (((unsigned long) ti + THREAD_SIZE) -
					sizeof(*childregs));

	ti->regs = childregs;

	/*
	 * Establish kernel stack pointer and initial PC for new thread
	 * Note that unlike the usual situation, we do not copy the
	 * parent's callee-saved here; those are in pt_regs and whatever
	 * we leave here will be overridden on return to userland.
	 */
	ss = (struct hexagon_switch_stack *) ((unsigned long) childregs -
						    sizeof(*ss));
	ss->lr = (unsigned long)ret_from_fork;
	p->thread.switch_sp = ss;
	if (unlikely(args->fn)) {
		memset(childregs, 0, sizeof(struct pt_regs));
		/* r24 <- fn, r25 <- arg */
		ss->r24 = (unsigned long)args->fn;
		ss->r25 = (unsigned long)args->fn_arg;
		pt_set_kmode(childregs);
		return 0;
	}
	memcpy(childregs, current_pt_regs(), sizeof(*childregs));
	ss->r2524 = 0;

	if (usp)
		pt_set_rte_sp(childregs, usp);

	/* Child sees zero return value */
	childregs->r00 = 0;

	/*
	 * The clone syscall has the C signature:
	 * int [r0] clone(int flags [r0],
	 *           void *child_frame [r1],
	 *           void *parent_tid [r2],
	 *           void *child_tid [r3],
	 *           void *thread_control_block [r4]);
	 * ugp is used to provide TLS support.
	 */
	if (clone_flags & CLONE_SETTLS)
		childregs->ugp = tls;

	/*
	 * Parent sees new pid -- not necessary, not even possible at
	 * this point in the fork process
	 */

	return 0;
}

/*
 * Some archs flush debug and FPU info here
 */
void flush_thread(void)
{
}

/*
 * The "wait channel" terminology is archaic, but what we want
 * is an identification of the point at which the scheduler
 * was invoked by a blocked thread.
 */
unsigned long __get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;

	stack_page = (unsigned long)task_stack_page(p);
	fp = ((struct hexagon_switch_stack *)p->thread.switch_sp)->fp;
	do {
		if (fp < (stack_page + sizeof(struct thread_info)) ||
			fp >= (THREAD_SIZE - 8 + stack_page))
			return 0;
		pc = ((unsigned long *)fp)[1];
		if (!in_sched_functions(pc))
			return pc;
		fp = *(unsigned long *) fp;
	} while (count++ < 16);

	return 0;
}

/*
 * Called on the exit path of event entry; see vm_entry.S
 *
 * Interrupts will already be disabled.
 *
 * Returns 0 if there's no need to re-check for more work.
 */

int do_work_pending(struct pt_regs *regs, u32 thread_info_flags)
{
	if (!(thread_info_flags & _TIF_WORK_MASK)) {
		return 0;
	}  /* shortcut -- no work to be done */

	local_irq_enable();

	if (thread_info_flags & _TIF_NEED_RESCHED) {
		schedule();
		return 1;
	}

	if (thread_info_flags & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL)) {
		do_signal(regs);
		return 1;
	}

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		resume_user_mode_work(regs);
		return 1;
	}

	/* Should not even reach here */
	panic("%s: bad thread_info flags 0x%08x\n", __func__,
		thread_info_flags);
}
