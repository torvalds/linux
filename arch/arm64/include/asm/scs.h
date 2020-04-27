/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCS_H
#define _ASM_SCS_H

#ifdef __ASSEMBLY__

#ifdef CONFIG_SHADOW_CALL_STACK
	.macro scs_load tsk, tmp
	ldp	x18, \tmp, [\tsk, #TSK_TI_SCS_BASE]
	add	x18, x18, \tmp
	.endm

	.macro scs_save tsk, tmp
	ldr	\tmp, [\tsk, #TSK_TI_SCS_BASE]
	sub	\tmp, x18, \tmp
	str	\tmp, [\tsk, #TSK_TI_SCS_OFFSET]
	.endm
#else
	.macro scs_load tsk, tmp
	.endm

	.macro scs_save tsk, tmp
	.endm
#endif /* CONFIG_SHADOW_CALL_STACK */

#else /* __ASSEMBLY__ */

#include <linux/scs.h>

#ifdef CONFIG_SHADOW_CALL_STACK

static inline void scs_overflow_check(struct task_struct *tsk)
{
	if (unlikely(scs_corrupted(tsk)))
		panic("corrupted shadow stack detected inside scheduler\n");
}

#else /* CONFIG_SHADOW_CALL_STACK */

static inline void scs_overflow_check(struct task_struct *tsk) {}

#endif /* CONFIG_SHADOW_CALL_STACK */

#endif /* __ASSEMBLY __ */

#endif /* _ASM_SCS_H */
