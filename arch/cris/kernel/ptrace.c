/*
 *  linux/arch/cris/kernel/ptrace.c
 *
 * Parts taken from the m68k port.
 *
 * Copyright (c) 2000, 2001, 2002 Axis Communications AB
 *
 * Authors:   Bjorn Wesen
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/tracehook.h>

#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>


/* notification of userspace execution resumption
 * - triggered by current->work.notify_resume
 */
extern int do_signal(int canrestart, struct pt_regs *regs);


void do_notify_resume(int canrestart, struct pt_regs *regs,
		      __u32 thread_info_flags)
{
	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(canrestart,regs);

	if (thread_info_flags & _TIF_NOTIFY_RESUME) {
		clear_thread_flag(TIF_NOTIFY_RESUME);
		tracehook_notify_resume(regs);
	}
}

void do_work_pending(int syscall, struct pt_regs *regs,
		     unsigned int thread_flags)
{
	do {
		if (likely(thread_flags & _TIF_NEED_RESCHED)) {
			schedule();
		} else {
			if (unlikely(!user_mode(regs)))
				return;
			local_irq_enable();
			if (thread_flags & _TIF_SIGPENDING) {
				do_signal(syscall, regs);
				syscall = 0;
			} else {
				clear_thread_flag(TIF_NOTIFY_RESUME);
				tracehook_notify_resume(regs);
			}
		}
		local_irq_disable();
		thread_flags = current_thread_info()->flags;
	} while (thread_flags & _TIF_WORK_MASK);
}
