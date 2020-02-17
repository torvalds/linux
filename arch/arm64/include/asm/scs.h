/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCS_H
#define _ASM_SCS_H

#ifndef __ASSEMBLY__

#include <linux/scs.h>

#ifdef CONFIG_SHADOW_CALL_STACK

extern void scs_init_irq(void);

static __always_inline void scs_save(struct task_struct *tsk)
{
	void *s;

	asm volatile("mov %0, x18" : "=r" (s));
	task_set_scs(tsk, s);
}

static inline void scs_overflow_check(struct task_struct *tsk)
{
	if (unlikely(scs_corrupted(tsk)))
		panic("corrupted shadow stack detected inside scheduler\n");
}

#else /* CONFIG_SHADOW_CALL_STACK */

static inline void scs_init_irq(void) {}
static inline void scs_save(struct task_struct *tsk) {}
static inline void scs_overflow_check(struct task_struct *tsk) {}

#endif /* CONFIG_SHADOW_CALL_STACK */

#endif /* __ASSEMBLY __ */

#endif /* _ASM_SCS_H */
