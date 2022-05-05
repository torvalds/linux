/*
 * arch/xtensa/kernel/process.c
 *
 * Xtensa Processor version.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Joe Taylor <joe@tensilica.com, joetylr@yahoo.com>
 * Chris Zankel <chris@zankel.net>
 * Marc Gauthier <marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Kevin Chea
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/hw_breakpoint.h>
#include <linux/init.h>
#include <linux/prctl.h>
#include <linux/init_task.h>
#include <linux/module.h>
#include <linux/mqueue.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>

#include <linux/uaccess.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/platform.h>
#include <asm/mmu.h>
#include <asm/irq.h>
#include <linux/atomic.h>
#include <asm/asm-offsets.h>
#include <asm/regs.h>
#include <asm/hw_breakpoint.h>

extern void ret_from_fork(void);
extern void ret_from_kernel_thread(void);

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);


#ifdef CONFIG_STACKPROTECTOR
#include <linux/stackprotector.h>
unsigned long __stack_chk_guard __read_mostly;
EXPORT_SYMBOL(__stack_chk_guard);
#endif

#if XTENSA_HAVE_COPROCESSORS

void coprocessor_release_all(struct thread_info *ti)
{
	unsigned long cpenable;
	int i;

	/* Make sure we don't switch tasks during this operation. */

	preempt_disable();

	/* Walk through all cp owners and release it for the requested one. */

	cpenable = ti->cpenable;

	for (i = 0; i < XCHAL_CP_MAX; i++) {
		if (coprocessor_owner[i] == ti) {
			coprocessor_owner[i] = 0;
			cpenable &= ~(1 << i);
		}
	}

	ti->cpenable = cpenable;
	if (ti == current_thread_info())
		xtensa_set_sr(0, cpenable);

	preempt_enable();
}

void coprocessor_flush_all(struct thread_info *ti)
{
	unsigned long cpenable, old_cpenable;
	int i;

	preempt_disable();

	old_cpenable = xtensa_get_sr(cpenable);
	cpenable = ti->cpenable;
	xtensa_set_sr(cpenable, cpenable);

	for (i = 0; i < XCHAL_CP_MAX; i++) {
		if ((cpenable & 1) != 0 && coprocessor_owner[i] == ti)
			coprocessor_flush(ti, i);
		cpenable >>= 1;
	}
	xtensa_set_sr(old_cpenable, cpenable);

	preempt_enable();
}

#endif


/*
 * Powermanagement idle function, if any is provided by the platform.
 */
void arch_cpu_idle(void)
{
	platform_idle();
}

/*
 * This is called when the thread calls exit().
 */
void exit_thread(struct task_struct *tsk)
{
#if XTENSA_HAVE_COPROCESSORS
	coprocessor_release_all(task_thread_info(tsk));
#endif
}

/*
 * Flush thread state. This is called when a thread does an execve()
 * Note that we flush coprocessor registers for the case execve fails.
 */
void flush_thread(void)
{
#if XTENSA_HAVE_COPROCESSORS
	struct thread_info *ti = current_thread_info();
	coprocessor_flush_all(ti);
	coprocessor_release_all(ti);
#endif
	flush_ptrace_hw_breakpoint(current);
}

/*
 * this gets called so that we can store coprocessor state into memory and
 * copy the current task into the new thread.
 */
int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
#if XTENSA_HAVE_COPROCESSORS
	coprocessor_flush_all(task_thread_info(src));
#endif
	*dst = *src;
	return 0;
}

/*
 * Copy thread.
 *
 * There are two modes in which this function is called:
 * 1) Userspace thread creation,
 *    regs != NULL, usp_thread_fn is userspace stack pointer.
 *    It is expected to copy parent regs (in case CLONE_VM is not set
 *    in the clone_flags) and set up passed usp in the childregs.
 * 2) Kernel thread creation,
 *    regs == NULL, usp_thread_fn is the function to run in the new thread
 *    and thread_fn_arg is its parameter.
 *    childregs are not used for the kernel threads.
 *
 * The stack layout for the new thread looks like this:
 *
 *	+------------------------+
 *	|       childregs        |
 *	+------------------------+ <- thread.sp = sp in dummy-frame
 *	|      dummy-frame       |    (saved in dummy-frame spill-area)
 *	+------------------------+
 *
 * We create a dummy frame to return to either ret_from_fork or
 *   ret_from_kernel_thread:
 *   a0 points to ret_from_fork/ret_from_kernel_thread (simulating a call4)
 *   sp points to itself (thread.sp)
 *   a2, a3 are unused for userspace threads,
 *   a2 points to thread_fn, a3 holds thread_fn arg for kernel threads.
 *
 * Note: This is a pristine frame, so we don't need any spill region on top of
 *       childregs.
 *
 * The fun part:  if we're keeping the same VM (i.e. cloning a thread,
 * not an entire process), we're normally given a new usp, and we CANNOT share
 * any live address register windows.  If we just copy those live frames over,
 * the two threads (parent and child) will overflow the same frames onto the
 * parent stack at different times, likely corrupting the parent stack (esp.
 * if the parent returns from functions that called clone() and calls new
 * ones, before the child overflows its now old copies of its parent windows).
 * One solution is to spill windows to the parent stack, but that's fairly
 * involved.  Much simpler to just not copy those live frames across.
 */

int copy_thread(unsigned long clone_flags, unsigned long usp_thread_fn,
		unsigned long thread_fn_arg, struct task_struct *p,
		unsigned long tls)
{
	struct pt_regs *childregs = task_pt_regs(p);

#if (XTENSA_HAVE_COPROCESSORS || XTENSA_HAVE_IO_PORTS)
	struct thread_info *ti;
#endif

#if defined(__XTENSA_WINDOWED_ABI__)
	/* Create a call4 dummy-frame: a0 = 0, a1 = childregs. */
	SPILL_SLOT(childregs, 1) = (unsigned long)childregs;
	SPILL_SLOT(childregs, 0) = 0;

	p->thread.sp = (unsigned long)childregs;
#elif defined(__XTENSA_CALL0_ABI__)
	/* Reserve 16 bytes for the _switch_to stack frame. */
	p->thread.sp = (unsigned long)childregs - 16;
#else
#error Unsupported Xtensa ABI
#endif

	if (!(p->flags & (PF_KTHREAD | PF_IO_WORKER))) {
		struct pt_regs *regs = current_pt_regs();
		unsigned long usp = usp_thread_fn ?
			usp_thread_fn : regs->areg[1];

		p->thread.ra = MAKE_RA_FOR_CALL(
				(unsigned long)ret_from_fork, 0x1);

		*childregs = *regs;
		childregs->areg[1] = usp;
		childregs->areg[2] = 0;

		/* When sharing memory with the parent thread, the child
		   usually starts on a pristine stack, so we have to reset
		   windowbase, windowstart and wmask.
		   (Note that such a new thread is required to always create
		   an initial call4 frame)
		   The exception is vfork, where the new thread continues to
		   run on the parent's stack until it calls execve. This could
		   be a call8 or call12, which requires a legal stack frame
		   of the previous caller for the overflow handlers to work.
		   (Note that it's always legal to overflow live registers).
		   In this case, ensure to spill at least the stack pointer
		   of that frame. */

		if (clone_flags & CLONE_VM) {
			/* check that caller window is live and same stack */
			int len = childregs->wmask & ~0xf;
			if (regs->areg[1] == usp && len != 0) {
				int callinc = (regs->areg[0] >> 30) & 3;
				int caller_ars = XCHAL_NUM_AREGS - callinc * 4;
				put_user(regs->areg[caller_ars+1],
					 (unsigned __user*)(usp - 12));
			}
			childregs->wmask = 1;
			childregs->windowstart = 1;
			childregs->windowbase = 0;
		}

		if (clone_flags & CLONE_SETTLS)
			childregs->threadptr = tls;
	} else {
		p->thread.ra = MAKE_RA_FOR_CALL(
				(unsigned long)ret_from_kernel_thread, 1);

		/* pass parameters to ret_from_kernel_thread: */
#if defined(__XTENSA_WINDOWED_ABI__)
		/*
		 * a2 = thread_fn, a3 = thread_fn arg.
		 * Window underflow will load registers from the
		 * spill slots on the stack on return from _switch_to.
		 */
		SPILL_SLOT(childregs, 2) = usp_thread_fn;
		SPILL_SLOT(childregs, 3) = thread_fn_arg;
#elif defined(__XTENSA_CALL0_ABI__)
		/*
		 * a12 = thread_fn, a13 = thread_fn arg.
		 * _switch_to epilogue will load registers from the stack.
		 */
		((unsigned long *)p->thread.sp)[0] = usp_thread_fn;
		((unsigned long *)p->thread.sp)[1] = thread_fn_arg;
#else
#error Unsupported Xtensa ABI
#endif

		/* Childregs are only used when we're going to userspace
		 * in which case start_thread will set them up.
		 */
	}

#if (XTENSA_HAVE_COPROCESSORS || XTENSA_HAVE_IO_PORTS)
	ti = task_thread_info(p);
	ti->cpenable = 0;
#endif

	clear_ptrace_hw_breakpoint(p);

	return 0;
}


/*
 * These bracket the sleeping functions..
 */

unsigned long __get_wchan(struct task_struct *p)
{
	unsigned long sp, pc;
	unsigned long stack_page = (unsigned long) task_stack_page(p);
	int count = 0;

	sp = p->thread.sp;
	pc = MAKE_PC_FROM_RA(p->thread.ra, p->thread.sp);

	do {
		if (sp < stack_page + sizeof(struct task_struct) ||
		    sp >= (stack_page + THREAD_SIZE) ||
		    pc == 0)
			return 0;
		if (!in_sched_functions(pc))
			return pc;

		/* Stack layout: sp-4: ra, sp-3: sp' */

		pc = MAKE_PC_FROM_RA(SPILL_SLOT(sp, 0), sp);
		sp = SPILL_SLOT(sp, 1);
	} while (count++ < 16);
	return 0;
}
