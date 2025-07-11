// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/ptrace.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <asm/current.h>
#include <asm/processor.h>
#include <linux/uaccess.h>
#include <as-layout.h>
#include <mem_user.h>
#include <registers.h>
#include <skas.h>
#include <os.h>

void flush_thread(void)
{
	arch_flush_thread(&current->thread.arch);

	get_safe_registers(current_pt_regs()->regs.gp,
			   current_pt_regs()->regs.fp);
}

void start_thread(struct pt_regs *regs, unsigned long eip, unsigned long esp)
{
	PT_REGS_IP(regs) = eip;
	PT_REGS_SP(regs) = esp;
	clear_thread_flag(TIF_SINGLESTEP);
}
EXPORT_SYMBOL(start_thread);
