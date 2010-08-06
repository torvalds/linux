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
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/prctl.h>
#include <linux/init_task.h>
#include <linux/module.h>
#include <linux/mqueue.h>
#include <linux/fs.h>
#include <linux/slab.h>

#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/platform.h>
#include <asm/mmu.h>
#include <asm/irq.h>
#include <asm/atomic.h>
#include <asm/asm-offsets.h>
#include <asm/regs.h>

extern void ret_from_fork(void);

struct task_struct *current_set[NR_CPUS] = {&init_task, };

void (*pm_power_off)(void) = NULL;
EXPORT_SYMBOL(pm_power_off);


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
	coprocessor_clear_cpenable();

	preempt_enable();
}

void coprocessor_flush_all(struct thread_info *ti)
{
	unsigned long cpenable;
	int i;

	preempt_disable();

	cpenable = ti->cpenable;

	for (i = 0; i < XCHAL_CP_MAX; i++) {
		if ((cpenable & 1) != 0 && coprocessor_owner[i] == ti)
			coprocessor_flush(ti, i);
		cpenable >>= 1;
	}

	preempt_enable();
}

#endif


/*
 * Powermanagement idle function, if any is provided by the platform.
 */

void cpu_idle(void)
{
  	local_irq_enable();

	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched())
			platform_idle();
		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

/*
 * This is called when the thread calls exit().
 */
void exit_thread(void)
{
#if XTENSA_HAVE_COPROCESSORS
	coprocessor_release_all(current_thread_info());
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
}

/*
 * This is called before the thread is copied. 
 */
void prepare_to_copy(struct task_struct *tsk)
{
#if XTENSA_HAVE_COPROCESSORS
	coprocessor_flush_all(task_thread_info(tsk));
#endif
}

/*
 * Copy thread.
 *
 * The stack layout for the new thread looks like this:
 *
 *	+------------------------+ <- sp in childregs (= tos)
 *	|       childregs        |
 *	+------------------------+ <- thread.sp = sp in dummy-frame
 *	|      dummy-frame       |    (saved in dummy-frame spill-area)
 *	+------------------------+
 *
 * We create a dummy frame to return to ret_from_fork:
 *   a0 points to ret_from_fork (simulating a call4)
 *   sp points to itself (thread.sp)
 *   a2, a3 are unused.
 *
 * Note: This is a pristine frame, so we don't need any spill region on top of
 *       childregs.
 */

int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
                struct task_struct * p, struct pt_regs * regs)
{
	struct pt_regs *childregs;
	struct thread_info *ti;
	unsigned long tos;
	int user_mode = user_mode(regs);

	/* Set up new TSS. */
	tos = (unsigned long)task_stack_page(p) + THREAD_SIZE;
	if (user_mode)
		childregs = (struct pt_regs*)(tos - PT_USER_SIZE);
	else
		childregs = (struct pt_regs*)tos - 1;

	*childregs = *regs;

	/* Create a call4 dummy-frame: a0 = 0, a1 = childregs. */
	*((int*)childregs - 3) = (unsigned long)childregs;
	*((int*)childregs - 4) = 0;

	childregs->areg[1] = tos;
	childregs->areg[2] = 0;
	p->set_child_tid = p->clear_child_tid = NULL;
	p->thread.ra = MAKE_RA_FOR_CALL((unsigned long)ret_from_fork, 0x1);
	p->thread.sp = (unsigned long)childregs;

	if (user_mode(regs)) {

		int len = childregs->wmask & ~0xf;
		childregs->areg[1] = usp;
		memcpy(&childregs->areg[XCHAL_NUM_AREGS - len/4],
		       &regs->areg[XCHAL_NUM_AREGS - len/4], len);
// FIXME: we need to set THREADPTR in thread_info...
		if (clone_flags & CLONE_SETTLS)
			childregs->areg[2] = childregs->areg[6];

	} else {
		/* In kernel space, we start a new thread with a new stack. */
		childregs->wmask = 1;
	}

#if (XTENSA_HAVE_COPROCESSORS || XTENSA_HAVE_IO_PORTS)
	ti = task_thread_info(p);
	ti->cpenable = 0;
#endif

	return 0;
}


/*
 * These bracket the sleeping functions..
 */

unsigned long get_wchan(struct task_struct *p)
{
	unsigned long sp, pc;
	unsigned long stack_page = (unsigned long) task_stack_page(p);
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

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

		pc = MAKE_PC_FROM_RA(*(unsigned long*)sp - 4, sp);
		sp = *(unsigned long *)sp - 3;
	} while (count++ < 16);
	return 0;
}

/*
 * xtensa_gregset_t and 'struct pt_regs' are vastly different formats
 * of processor registers.  Besides different ordering,
 * xtensa_gregset_t contains non-live register information that
 * 'struct pt_regs' does not.  Exception handling (primarily) uses
 * 'struct pt_regs'.  Core files and ptrace use xtensa_gregset_t.
 *
 */

void xtensa_elf_core_copy_regs (xtensa_gregset_t *elfregs, struct pt_regs *regs)
{
	unsigned long wb, ws, wm;
	int live, last;

	wb = regs->windowbase;
	ws = regs->windowstart;
	wm = regs->wmask;
	ws = ((ws >> wb) | (ws << (WSBITS - wb))) & ((1 << WSBITS) - 1);

	/* Don't leak any random bits. */

	memset(elfregs, 0, sizeof (elfregs));

	/* Note:  PS.EXCM is not set while user task is running; its
	 * being set in regs->ps is for exception handling convenience.
	 */

	elfregs->pc		= regs->pc;
	elfregs->ps		= (regs->ps & ~(1 << PS_EXCM_BIT));
	elfregs->lbeg		= regs->lbeg;
	elfregs->lend		= regs->lend;
	elfregs->lcount		= regs->lcount;
	elfregs->sar		= regs->sar;
	elfregs->windowstart	= ws;

	live = (wm & 2) ? 4 : (wm & 4) ? 8 : (wm & 8) ? 12 : 16;
	last = XCHAL_NUM_AREGS - (wm >> 4) * 4;
	memcpy(elfregs->a, regs->areg, live * 4);
	memcpy(elfregs->a + last, regs->areg + last, (wm >> 4) * 16);
}

int dump_fpu(void)
{
	return 0;
}

asmlinkage
long xtensa_clone(unsigned long clone_flags, unsigned long newsp,
                  void __user *parent_tid, void *child_tls,
                  void __user *child_tid, long a5,
                  struct pt_regs *regs)
{
        if (!newsp)
                newsp = regs->areg[1];
        return do_fork(clone_flags, newsp, regs, 0, parent_tid, child_tid);
}

/*
 * xtensa_execve() executes a new program.
 */

asmlinkage
long xtensa_execve(char __user *name, char __user * __user *argv,
                   char __user * __user *envp,
                   long a3, long a4, long a5,
                   struct pt_regs *regs)
{
	long error;
	char * filename;

	filename = getname(name);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	return error;
}

