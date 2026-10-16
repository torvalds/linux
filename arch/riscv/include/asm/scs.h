/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SCS_H
#define _ASM_SCS_H

#ifdef __ASSEMBLER__
#include <asm/asm-offsets.h>
#include <asm/csr.h>

#ifdef CONFIG_SHADOW_CALL_STACK

/* Load init_shadow_call_stack to gp. */
.macro scs_load_init_stack
	la	gp, init_shadow_call_stack
.endm

/* Load the per-CPU IRQ shadow call stack to gp. */
.macro scs_load_irq_stack tmp
	load_per_cpu gp, irq_shadow_call_stack_ptr, \tmp
.endm

/* Load task_scs_sp(current) to gp. */
.macro scs_load_current
	REG_L	gp, TASK_TI_SCS_SP(tp)
.endm

/*
 * Load task_scs_sp(current) to gp, but only when the trap came from U-mode.
 * The source privilege level is taken from the saved sstatus.SPP bit (written
 * by hardware on trap entry), not by comparing tp.  tp is a user-writable
 * register, so gating the shadow call stack reload on it let a user task that
 * set tp == &current skip the reload and run the kernel with an attacker
 * controlled gp (the shadow call stack pointer).
 */
.macro scs_load_current_if_from_user status, tmp
	andi	\tmp, \status, SR_SPP
	bnez	\tmp, _skip_scs
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
.macro scs_load_current_if_from_user status, tmp
.endm
.macro scs_save_current
.endm

#endif /* CONFIG_SHADOW_CALL_STACK */
#endif /* __ASSEMBLER__ */

#endif /* _ASM_SCS_H */
