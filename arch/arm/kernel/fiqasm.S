/*
 *  linux/arch/arm/kernel/fiqasm.S
 *
 *  Derived from code originally in linux/arch/arm/kernel/fiq.c:
 *
 *  Copyright (C) 1998 Russell King
 *  Copyright (C) 1998, 1999 Phil Blundell
 *  Copyright (C) 2011, Linaro Limited
 *
 *  FIQ support written by Philip Blundell <philb@gnu.org>, 1998.
 *
 *  FIQ support re-written by Russell King to be more generic
 *
 *  v7/Thumb-2 compatibility modifications by Linaro Limited, 2011.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Taking an interrupt in FIQ mode is death, so both these functions
 * disable irqs for the duration.
 */

ENTRY(__set_fiq_regs)
	mov	r2, #PSR_I_BIT | PSR_F_BIT | FIQ_MODE
	mrs	r1, cpsr
	msr	cpsr_c, r2	@ select FIQ mode
	mov	r0, r0		@ avoid hazard prior to ARMv4
	ldmia	r0!, {r8 - r12}
	ldr	sp, [r0], #4
	ldr	lr, [r0]
	msr	cpsr_c, r1	@ return to SVC mode
	mov	r0, r0		@ avoid hazard prior to ARMv4
	mov	pc, lr
ENDPROC(__set_fiq_regs)

ENTRY(__get_fiq_regs)
	mov	r2, #PSR_I_BIT | PSR_F_BIT | FIQ_MODE
	mrs	r1, cpsr
	msr	cpsr_c, r2	@ select FIQ mode
	mov	r0, r0		@ avoid hazard prior to ARMv4
	stmia	r0!, {r8 - r12}
	str	sp, [r0], #4
	str	lr, [r0]
	msr	cpsr_c, r1	@ return to SVC mode
	mov	r0, r0		@ avoid hazard prior to ARMv4
	mov	pc, lr
ENDPROC(__get_fiq_regs)
