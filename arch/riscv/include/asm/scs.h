/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCS_H
#define _ASM_SCS_H

#ifdef __ASSEMBLY__
#include <asm/asm-offsets.h>

#ifdef CONFIG_SHADOW_CALL_STACK

/* Load init_shadow_call_stack to gp. */
.macro scs_load_init_stack
	la	gp, init_shadow_call_stack
	XIP_FIXUP_OFFSET gp
.endm

/* Load the per-CPU IRQ shadow call stack to gp. */
.macro scs_load_irq_stack tmp
	load_per_cpu gp, irq_shadow_call_stack_ptr, \tmp
.endm

/* Load task_scs_sp(current) to gp. */
.macro scs_load_current
	REG_L	gp, TASK_TI_SCS_SP(tp)
.endm

/* Load task_scs_sp(current) to gp, but only if tp has changed. */
.macro scs_load_current_if_task_changed prev
	beq	\prev, tp, _skip_scs
	scs_load_current
_skip_scs:
.endm

/* Save gp to task_scs_sp(current). */
.macro scs_save_current
	REG_S	gp, TASK_TI_SCS_SP(tp)
.endm

#else /* CONFIG_SHADOW_CALL_STACK */

.macro scs_load_init_stack
.endm
.macro scs_load_irq_stack tmp
.endm
.macro scs_load_current
.endm
.macro scs_load_current_if_task_changed prev
.endm
.macro scs_save_current
.endm

#endif /* CONFIG_SHADOW_CALL_STACK */
#endif /* __ASSEMBLY__ */

#endif /* _ASM_SCS_H */
