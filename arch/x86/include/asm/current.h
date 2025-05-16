/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_CURRENT_H
#define _ASM_X86_CURRENT_H

#include <linux/build_bug.h>
#include <linux/compiler.h>

#ifndef __ASSEMBLER__

#include <linux/cache.h>
#include <asm/percpu.h>

struct task_struct;

DECLARE_PER_CPU_CACHE_HOT(struct task_struct *, current_task);
/* const-qualified alias provided by the linker. */
DECLARE_PER_CPU_CACHE_HOT(struct task_struct * const __percpu_seg_override,
			  const_current_task);

static __always_inline struct task_struct *get_current(void)
{
	if (IS_ENABLED(CONFIG_USE_X86_SEG_SUPPORT))
		return this_cpu_read_const(const_current_task);

	return this_cpu_read_stable(current_task);
}

#define current get_current()

#endif /* __ASSEMBLER__ */

#endif /* _ASM_X86_CURRENT_H */
