/*
 * arch/score/kernel/process.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/elfcore.h>
#include <linux/pm.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

/* If or when software machine-restart is implemented, add code here. */
void machine_restart(char *command) {}

/* If or when software machine-halt is implemented, add code here. */
void machine_halt(void) {}

/* If or when software machine-power-off is implemented, add code here. */
void machine_power_off(void) {}

/*
 * The idle thread. There's no useful work to be
 * done, so just try to conserve power and have a
 * low exit latency (ie sit in a loop waiting for
 * somebody to say that they'd like to reschedule)
 */
void __noreturn cpu_idle(void)
{
	/* endless idle loop with no priority at all */
	while (1) {
		while (!need_resched())
			barrier();

		preempt_enable_no_resched();
		schedule();
		preempt_disable();
	}
}

void ret_from_fork(void);

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	unsigned long status;

	/* New thread loses kernel privileges. */
	status = regs->cp0_psr & ~(KU_MASK);
	status |= KU_USER;
	regs->cp0_psr = status;
	regs->cp0_epc = pc;
	regs->regs[0] = sp;
}

void exit_thread(void) {}

/*
 * When a process does an "exec", machine state like FPU and debug
 * registers need to be reset.  This is a hook function for that.
 * Currently we don't have any such state to reset, so this is empty.
 */
void flush_thread(void) {}

/*
 * set up the kernel stack and exception frames for a new process
 */
int copy_thread(unsigned long clone_flags, unsigned long usp,
		unsigned long unused,
		struct task_struct *p, struct pt_regs *regs)
{
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *childregs = task_pt_regs(p);

	p->set_child_tid = NULL;
	p->clear_child_tid = NULL;

	*childregs = *regs;
	childregs->regs[7] = 0;		/* Clear error flag */
	childregs->regs[4] = 0;		/* Child gets zero as return value */
	regs->regs[4] = p->pid;

	if (childregs->cp0_psr & 0x8) {	/* test kernel fork or user fork */
		childregs->regs[0] = usp;		/* user fork */
	} else {
		childregs->regs[28] = (unsigned long) ti; /* kernel fork */
		childregs->regs[0] = (unsigned long) childregs;
	}

	p->thread.reg0 = (unsigned long) childregs;
	p->thread.reg3 = (unsigned long) ret_from_fork;
	p->thread.cp0_psr = 0;

	return 0;
}

/* Fill in the fpu structure for a core dump. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	return 1;
}

static void __noreturn
kernel_thread_helper(void *unused0, int (*fn)(void *),
		 void *arg, void *unused1)
{
	do_exit(fn(arg));
}

/*
 * Create a kernel thread.
 */
long kernel_thread(int (*fn)(void *), void *arg, unsigned long flags)
{
	struct pt_regs regs;

	memset(&regs, 0, sizeof(regs));

	regs.regs[6] = (unsigned long) arg;
	regs.regs[5] = (unsigned long) fn;
	regs.cp0_epc = (unsigned long) kernel_thread_helper;
	regs.cp0_psr = (regs.cp0_psr & ~(0x1|0x4|0x8)) | \
			((regs.cp0_psr & 0x3) << 2);

	return do_fork(flags | CLONE_VM | CLONE_UNTRACED, \
			0, &regs, 0, NULL, NULL);
}

unsigned long thread_saved_pc(struct task_struct *tsk)
{
	return task_pt_regs(tsk)->cp0_epc;
}

unsigned long get_wchan(struct task_struct *task)
{
	if (!task || task == current || task->state == TASK_RUNNING)
		return 0;

	if (!task_stack_page(task))
		return 0;

	return task_pt_regs(task)->cp0_epc;
}

unsigned long arch_align_stack(unsigned long sp)
{
	return sp;
}
