/*
 *  linux/arch/cris/kernel/ptrace.c
 *
 * Parts taken from the m68k port.
 * 
 * Copyright (c) 2000, 2001, 2002 Axis Communications AB
 *
 * Authors:   Bjorn Wesen
 *
 * $Log: ptrace.c,v $
 * Revision 1.9  2003/07/04 12:56:11  tobiasa
 * Moved arch-specific code to arch-specific files.
 *
 * Revision 1.8  2003/04/09 05:20:47  starvik
 * Merge of Linux 2.5.67
 *
 * Revision 1.7  2002/11/27 08:42:34  starvik
 * Argument to user_regs() is thread_info*
 *
 * Revision 1.6  2002/11/20 11:56:11  starvik
 * Merge of Linux 2.5.48
 *
 * Revision 1.5  2002/11/18 07:41:19  starvik
 * Removed warning
 *
 * Revision 1.4  2002/11/11 12:47:28  starvik
 * SYSCALL_TRACE has been moved to thread flags
 *
 * Revision 1.3  2002/02/05 15:37:18  bjornw
 * * Add do_notify_resume (replaces do_signal in the callchain)
 * * syscall_trace is now do_syscall_trace
 * * current->ptrace flag PT_TRACESYS -> PT_SYSCALLTRACE
 * * Keep track of the current->work.syscall_trace counter
 *
 * Revision 1.2  2001/12/18 13:35:20  bjornw
 * Applied the 2.4.13->2.4.16 CRIS patch to 2.5.1 (is a copy of 2.4.15).
 *
 * Revision 1.8  2001/11/12 18:26:21  pkj
 * Fixed compiler warnings.
 *
 * Revision 1.7  2001/09/26 11:53:49  bjornw
 * PTRACE_DETACH works more simple in 2.4.10
 *
 * Revision 1.6  2001/07/25 16:08:47  bjornw
 * PTRACE_ATTACH bulk moved into arch-independent code in 2.4.7
 *
 * Revision 1.5  2001/03/26 14:24:28  orjanf
 * * Changed loop condition.
 * * Added comment documenting non-standard ptrace behaviour.
 *
 * Revision 1.4  2001/03/20 19:44:41  bjornw
 * Use the user_regs macro instead of thread.esp0
 *
 * Revision 1.3  2000/12/18 23:45:25  bjornw
 * Linux/CRIS first version
 *
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>

#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/processor.h>

/*
 * Get contents of register REGNO in task TASK.
 */
inline long get_reg(struct task_struct *task, unsigned int regno)
{
	/* USP is a special case, it's not in the pt_regs struct but
	 * in the tasks thread struct
	 */

	if (regno == PT_USP)
		return task->thread.usp;
	else if (regno < PT_MAX)
		return ((unsigned long *)user_regs(task->thread_info))[regno];
	else
		return 0;
}

/*
 * Write contents of register REGNO in task TASK.
 */
inline int put_reg(struct task_struct *task, unsigned int regno,
			  unsigned long data)
{
	if (regno == PT_USP)
		task->thread.usp = data;
	else if (regno < PT_MAX)
		((unsigned long *)user_regs(task->thread_info))[regno] = data;
	else
		return -1;
	return 0;
}

/* notification of userspace execution resumption
 * - triggered by current->work.notify_resume
 */
extern int do_signal(int canrestart, sigset_t *oldset, struct pt_regs *regs);


void do_notify_resume(int canrestart, sigset_t *oldset, struct pt_regs *regs, 
		      __u32 thread_info_flags  )
{
	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(canrestart,oldset,regs);
}
