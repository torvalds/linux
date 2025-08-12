/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_CURRENT_H
#define __ASM_CURRENT_H

#include <linux/compiler.h>
#include <linux/threads.h>

#ifndef __ASSEMBLER__

struct task_struct;
extern struct task_struct *cpu_tasks[NR_CPUS];

static __always_inline struct task_struct *get_current(void)
{
	return cpu_tasks[0];
}


#define current get_current()

#endif /* __ASSEMBLER__ */

#endif /* __ASM_CURRENT_H */
