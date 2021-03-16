/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_SWITCH_TO_H
#define __ASM_CSKY_SWITCH_TO_H

#include <linux/thread_info.h>
#ifdef CONFIG_CPU_HAS_FPU
#include <abi/fpu.h>
static inline void __switch_to_fpu(struct task_struct *prev,
				   struct task_struct *next)
{
	save_to_user_fp(&prev->thread.user_fp);
	restore_from_user_fp(&next->thread.user_fp);
}
#else
static inline void __switch_to_fpu(struct task_struct *prev,
				   struct task_struct *next)
{}
#endif

/*
 * Context switching is now performed out-of-line in switch_to.S
 */
extern struct task_struct *__switch_to(struct task_struct *,
				       struct task_struct *);

#define switch_to(prev, next, last)					\
	do {								\
		struct task_struct *__prev = (prev);			\
		struct task_struct *__next = (next);			\
		__switch_to_fpu(__prev, __next);			\
		((last) = __switch_to((prev), (next)));			\
	} while (0)

#endif /* __ASM_CSKY_SWITCH_TO_H */
