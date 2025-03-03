/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CURRENT_H
#define _ASM_X86_CURRENT_H

#include <linux/build_bug.h>
#include <linux/compiler.h>

#ifndef __ASSEMBLY__

#include <linux/cache.h>
#include <asm/percpu.h>

struct task_struct;

struct pcpu_hot {
	struct task_struct	*current_task;
	unsigned long		top_of_stack;
	void			*hardirq_stack_ptr;
#ifdef CONFIG_X86_64
	bool			hardirq_stack_inuse;
#else
	void			*softirq_stack_ptr;
#endif
};

DECLARE_PER_CPU_CACHE_HOT(struct pcpu_hot, pcpu_hot);

/* const-qualified alias to pcpu_hot, aliased by linker. */
DECLARE_PER_CPU_CACHE_HOT(const struct pcpu_hot __percpu_seg_override,
			const_pcpu_hot);

static __always_inline struct task_struct *get_current(void)
{
	if (IS_ENABLED(CONFIG_USE_X86_SEG_SUPPORT))
		return this_cpu_read_const(const_pcpu_hot.current_task);

	return this_cpu_read_stable(pcpu_hot.current_task);
}

#define current get_current()

#endif /* __ASSEMBLY__ */

#endif /* _ASM_X86_CURRENT_H */
