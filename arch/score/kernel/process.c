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
#include <linux/rcupdate.h>
#include <linux/sched/task.h>

void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

/* If or when software machine-restart is implemented, add code here. */
void machine_restart(char *command) {}

/* If or when software machine-halt is implemented, add code here. */
void machine_halt(void) {}

/* If or when software machine-power-off is implemented, add code here. */
void machine_power_off(void) {}

void ret_from_fork(void);
void ret_from_kernel_thread(void);

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
		unsigned long arg, struct task_struct *p)
{
	struct thread_info *ti = task_thread_info(p);
	struct pt_regs *childregs = task_pt_regs(p);
	struct pt_regs *regs = current_pt_regs();

	p->thread.reg0 = (unsigned long) childregs;
	if (unlikely(p->flags & PF_KTHREAD)) {
		memset(childregs, 0, sizeof(struct pt_regs));
		p->thread.reg12 = usp;
		p->thread.reg13 = arg;
		p->thread.reg3 = (unsigned long) ret_from_kernel_thread;
	} else {
		*childregs = *current_pt_regs();
		childregs->regs[7] = 0;		/* Clear error flag */
		childregs->regs[4] = 0;		/* Child gets zero as return value */
		if (usp)
			childregs->regs[0] = usp;	/* user fork */
		p->thread.reg3 = (unsigned long) ret_from_fork;
	}

	p->thread.cp0_psr = 0;

	return 0;
}

/* Fill in the fpu structure for a core dump. */
int dump_fpu(struct pt_regs *regs, elf_fpregset_t *r)
{
	return 1;
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
