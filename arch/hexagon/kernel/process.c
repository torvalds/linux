/*
 * Process creation support for Hexagon
 *
 * Copyright (c) 2010-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include <linux/sched.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/tick.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

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
	/* Set to run with user-mode data segmentation */
	set_fs(USER_DS);
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
static void default_idle(void)
{
	__vmwait();
}

void (*idle_sleep)(void) = default_idle;

void cpu_idle(void)
{
	while (1) {
		tick_nohz_idle_enter();
		local_irq_disable();
		while (!need_resched()) {
			idle_sleep();
			/*  interrupts wake us up, but aren't serviced  */
			local_irq_enable();	/* service interrupt   */
			local_irq_disable();
		}
		local_irq_enable();
		tick_nohz_idle_exit();
		schedule();
	}
}

/*
 *  Return saved PC of a blocked thread
 */
unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return 0;
}

/*
 * Copy architecture-specific thread state
 */
int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long arg, struct task_struct *p,
		struct pt_regs *regs)
{
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
	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(childregs, 0, sizeof(struct pt_regs));
		/* r24 <- fn, r25 <- arg */
		ss->r2524 = usp | ((u64)arg << 32);
		pt_set_kmode(childregs);
		return 0;
	}
	memcpy(childregs, regs, sizeof(*childregs));
	ss->r2524 = 0;

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
		childregs->ugp = childregs->r04;

	/*
	 * Parent sees new pid -- not necessary, not even possible at
	 * this point in the fork process
	 * Might also want to set things like ti->addr_limit
	 */

	return 0;
}

/*
 * Release any architecture-specific resources locked by thread
 */
void release_thread(struct task_struct *dead_task)
{
}

/*
 * Free any architecture-specific thread data structures, etc.
 */
void exit_thread(void)
{
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
unsigned long get_wchan(struct task_struct *p)
{
	unsigned long fp, pc;
	unsigned long stack_page;
	int count = 0;
	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

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
 * Required placeholder.
 */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *fpu)
{
	return 0;
}
