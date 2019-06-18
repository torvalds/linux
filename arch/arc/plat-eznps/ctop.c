// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2015 EZchip Technologies.
 */

#include <linux/sched.h>
#include <asm/processor.h>
#include <plat/ctop.h>

void dp_save_restore(struct task_struct *prev, struct task_struct *next)
{
	struct eznps_dp *prev_task_dp = &prev->thread.dp;
	struct eznps_dp *next_task_dp = &next->thread.dp;

	/* Here we save all Data Plane related auxiliary registers */
	prev_task_dp->eflags = read_aux_reg(CTOP_AUX_EFLAGS);
	write_aux_reg(CTOP_AUX_EFLAGS, next_task_dp->eflags);

	prev_task_dp->gpa1 = read_aux_reg(CTOP_AUX_GPA1);
	write_aux_reg(CTOP_AUX_GPA1, next_task_dp->gpa1);
}
