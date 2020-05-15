/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCS_H
#define _ASM_SCS_H

#ifdef __ASSEMBLY__

#include <asm/asm-offsets.h>

#ifdef CONFIG_SHADOW_CALL_STACK
	.macro scs_load tsk, tmp
	ldr	x18, [\tsk, #TSK_TI_SCS_SP]
	.endm

	.macro scs_save tsk, tmp
	str	x18, [\tsk, #TSK_TI_SCS_SP]
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
