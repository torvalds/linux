/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_LOONGARCH_CURRENT_H
#define __ASM_LOONGARCH_CURRENT_H

#include <linux/compiler.h>

#ifndef __ASSEMBLER__

#include <asm/percpu.h>

struct task_struct;

DECLARE_PER_CPU(struct task_struct *, cpu_tasks);

register struct task_struct *current_thread_pointer __asm__("$tp");

static __always_inline struct task_struct *get_current(void)
{
	return current_thread_pointer;
}

#define current get_current()

static __always_inline void set_current(struct task_struct *task)
{
	__this_cpu_write(cpu_tasks, task);
}

#endif /* __ASSEMBLER__ */

#endif /* __ASM_LOONGARCH_CURRENT_H */
