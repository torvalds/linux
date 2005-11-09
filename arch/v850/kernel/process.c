/*
 * arch/v850/kernel/process.c -- Arch-dependent process handling
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/reboot.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pgtable.h>

extern void ret_from_fork (void);


/* The idle loop.  */
void default_idle (void)
{
	while (! need_resched ())
		asm ("halt; nop; nop; nop; nop; nop" ::: "cc");
}

void (*idle)(void) = default_idle;

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void cpu_idle (void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched())
			(*idle) ();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

/*
 * This is the mechanism for creating a new kernel thread.
 *
 * NOTE! Only a kernel-only process (ie the swapper or direct descendants who
 * haven't done an "execve()") should use this: it will work within a system
 * call from a "real" process, but the process memory space will not be free'd
 * until both the parent and the child have exited.
 */
int kernel_thread (int (*fn)(void *), void *arg, unsigned long flags)
{
	register mm_segment_t fs = get_fs ();
	register unsigned long syscall asm (SYSCALL_NUM);
	register unsigned long arg0 asm (SYSCALL_ARG0);
	register unsigned long ret asm (SYSCALL_RET);

	set_fs (KERNEL_DS);

	/* Clone this thread.  Note that we don't pass the clone syscall's
	   second argument -- it's ignored for calls from kernel mode (the
	   child's SP is always set to the top of the kernel stack).  */
	arg0 = flags | CLONE_VM;
	syscall = __NR_clone;
	asm volatile ("trap " SYSCALL_SHORT_TRAP
		      : "=r" (ret), "=r" (syscall)
		      : "1" (syscall), "r" (arg0)
		      : SYSCALL_SHORT_CLOBBERS);

	if (ret == 0) {
		/* In child thread, call FN and exit.  */
		arg0 = (*fn) (arg);
		syscall = __NR_exit;
		asm volatile ("trap " SYSCALL_SHORT_TRAP
			      : "=r" (ret), "=r" (syscall)
			      : "1" (syscall), "r" (arg0)
			      : SYSCALL_SHORT_CLOBBERS);
	}

	/* In parent.  */
	set_fs (fs);

	return ret;
}

void flush_thread (void)
{
	set_fs (USER_DS);
}

int copy_thread (int nr, unsigned long clone_flags,
		 unsigned long stack_start, unsigned long stack_size,
		 struct task_struct *p, struct pt_regs *regs)
{
	/* Start pushing stuff from the top of the child's kernel stack.  */
	unsigned long orig_ksp = (unsigned long)p->thread_info + THREAD_SIZE;
	unsigned long ksp = orig_ksp;
	/* We push two `state save' stack fames (see entry.S) on the new
	   kernel stack:
	     1) The innermost one is what switch_thread would have
	        pushed, and is used when we context switch to the child
		thread for the first time.  It's set up to return to
		ret_from_fork in entry.S.
	     2) The outermost one (nearest the top) is what a syscall
	        trap would have pushed, and is set up to return to the
		same location as the parent thread, but with a return
		value of 0. */
	struct pt_regs *child_switch_regs, *child_trap_regs;

	/* Trap frame.  */
	ksp -= STATE_SAVE_SIZE;
	child_trap_regs = (struct pt_regs *)(ksp + STATE_SAVE_PT_OFFSET);
	/* Switch frame.  */
	ksp -= STATE_SAVE_SIZE;
	child_switch_regs = (struct pt_regs *)(ksp + STATE_SAVE_PT_OFFSET);

	/* First copy parent's register state to child.  */
	*child_switch_regs = *regs;
	*child_trap_regs = *regs;

	/* switch_thread returns to the restored value of the lp
	   register (r31), so we make that the place where we want to
	   jump when the child thread begins running.  */
	child_switch_regs->gpr[GPR_LP] = (v850_reg_t)ret_from_fork;

	if (regs->kernel_mode)
		/* Since we're returning to kernel-mode, make sure the child's
		   stored kernel stack pointer agrees with what the actual
		   stack pointer will be at that point (the trap return code
		   always restores the SP, even when returning to
		   kernel-mode).  */
		child_trap_regs->gpr[GPR_SP] = orig_ksp;
	else
		/* Set the child's user-mode stack-pointer (the name
		   `stack_start' is a misnomer, it's just the initial SP
		   value).  */
		child_trap_regs->gpr[GPR_SP] = stack_start;

	/* Thread state for the child (everything else is on the stack).  */
	p->thread.ksp = ksp;

	return 0;
}

/*
 * fill in the user structure for a core dump..
 */
void dump_thread (struct pt_regs *regs, struct user *dump)
{
#if 0  /* Later.  XXX */
	dump->magic = CMAGIC;
	dump->start_code = 0;
	dump->start_stack = regs->gpr[GPR_SP];
	dump->u_tsize = ((unsigned long) current->mm->end_code) >> PAGE_SHIFT;
	dump->u_dsize = ((unsigned long) (current->mm->brk +
					  (PAGE_SIZE-1))) >> PAGE_SHIFT;
	dump->u_dsize -= dump->u_tsize;
	dump->u_ssize = 0;

	if (dump->start_stack < TASK_SIZE)
		dump->u_ssize = ((unsigned long) (TASK_SIZE - dump->start_stack)) >> PAGE_SHIFT;

	dump->u_ar0 = (struct user_regs_struct *)((int)&dump->regs - (int)dump);
	dump->regs = *regs;
	dump->u_fpvalid = 0;
#endif
}

/*
 * sys_execve() executes a new program.
 */
int sys_execve (char *name, char **argv, char **envp, struct pt_regs *regs)
{
	char *filename = getname (name);
	int error = PTR_ERR (filename);

	if (! IS_ERR (filename)) {
		error = do_execve (filename, argv, envp, regs);
		putname (filename);
	}

	return error;
}


/*
 * These bracket the sleeping functions..
 */
#define first_sched	((unsigned long)__sched_text_start)
#define last_sched	((unsigned long)__sched_text_end)

unsigned long get_wchan (struct task_struct *p)
{
#if 0  /* Barf.  Figure out the stack-layout later.  XXX  */
	unsigned long fp, pc;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	pc = thread_saved_pc (p);

	/* This quite disgusting function walks up the stack, following
	   saved return address, until it something that's out of bounds
	   (as defined by `first_sched' and `last_sched').  It then
	   returns the last PC that was in-bounds.  */
	do {
		if (fp < stack_page + sizeof (struct task_struct) ||
		    fp >= 8184+stack_page)
			return 0;
		pc = ((unsigned long *)fp)[1];
		if (pc < first_sched || pc >= last_sched)
			return pc;
		fp = *(unsigned long *) fp;
	} while (count++ < 16);
#endif

	return 0;
}
