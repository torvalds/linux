/*
 *  linux/arch/arm/vfp/vfphw.S
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This code is called from the kernel's undefined instruction trap.
 * r9 holds the return address for successful handling.
 * lr holds the return address for unrecognised instructions.
 * r10 points at the start of the private FP workspace in the thread structure
 * sp points to a struct pt_regs (as defined in include/asm/proc/ptrace.h)
 */
#include <asm/thread_info.h>
#include <asm/vfpmacros.h>
#include "../kernel/entry-header.S"

	.macro	DBGSTR, str
#ifdef DEBUG
	stmfd	sp!, {r0-r3, ip, lr}
	add	r0, pc, #4
	bl	printk
	b	1f
	.asciz  "<7>VFP: \str\n"
	.balign 4
1:	ldmfd	sp!, {r0-r3, ip, lr}
#endif
	.endm

	.macro  DBGSTR1, str, arg
#ifdef DEBUG
	stmfd	sp!, {r0-r3, ip, lr}
	mov	r1, \arg
	add	r0, pc, #4
	bl	printk
	b	1f
	.asciz  "<7>VFP: \str\n"
	.balign 4
1:	ldmfd	sp!, {r0-r3, ip, lr}
#endif
	.endm

	.macro  DBGSTR3, str, arg1, arg2, arg3
#ifdef DEBUG
	stmfd	sp!, {r0-r3, ip, lr}
	mov	r3, \arg3
	mov	r2, \arg2
	mov	r1, \arg1
	add	r0, pc, #4
	bl	printk
	b	1f
	.asciz  "<7>VFP: \str\n"
	.balign 4
1:	ldmfd	sp!, {r0-r3, ip, lr}
#endif
	.endm


@ VFP hardware support entry point.
@
@  r0  = faulted instruction
@  r2  = faulted PC+4
@  r9  = successful return
@  r10 = vfp_state union
@  r11 = CPU number
@  lr  = failure return

	.globl	vfp_support_entry
vfp_support_entry:
	DBGSTR3	"instr %08x pc %08x state %p", r0, r2, r10

	VFPFMRX	r1, FPEXC		@ Is the VFP enabled?
	DBGSTR1	"fpexc %08x", r1
	tst	r1, #FPEXC_ENABLE
	bne	look_for_VFP_exceptions	@ VFP is already enabled

	DBGSTR1 "enable %x", r10
	ldr	r3, last_VFP_context_address
	orr	r1, r1, #FPEXC_ENABLE	@ user FPEXC has the enable bit set
	ldr	r4, [r3, r11, lsl #2]	@ last_VFP_context pointer
	bic	r5, r1, #FPEXC_EXCEPTION @ make sure exceptions are disabled
	cmp	r4, r10
	beq	check_for_exception	@ we are returning to the same
					@ process, so the registers are
					@ still there.  In this case, we do
					@ not want to drop a pending exception.

	VFPFMXR	FPEXC, r5		@ enable VFP, disable any pending
					@ exceptions, so we can get at the
					@ rest of it

#ifndef CONFIG_SMP
	@ Save out the current registers to the old thread state
	@ No need for SMP since this is not done lazily

	DBGSTR1	"save old state %p", r4
	cmp	r4, #0
	beq	no_old_VFP_process
	VFPFMRX	r5, FPSCR		@ current status
	VFPFMRX	r6, FPINST		@ FPINST (always there, rev0 onwards)
	tst	r1, #FPEXC_FPV2		@ is there an FPINST2 to read?
	VFPFMRX	r8, FPINST2, NE		@ FPINST2 if needed - avoids reading
					@ nonexistant reg on rev0
	VFPFSTMIA r4 			@ save the working registers
	stmia	r4, {r1, r5, r6, r8}	@ save FPEXC, FPSCR, FPINST, FPINST2
					@ and point r4 at the word at the
					@ start of the register dump
#endif

no_old_VFP_process:
	DBGSTR1	"load state %p", r10
	str	r10, [r3, r11, lsl #2]	@ update the last_VFP_context pointer
					@ Load the saved state back into the VFP
	VFPFLDMIA r10	 		@ reload the working registers while
					@ FPEXC is in a safe state
	ldmia	r10, {r1, r5, r6, r8}	@ load FPEXC, FPSCR, FPINST, FPINST2
	tst	r1, #FPEXC_FPV2		@ is there an FPINST2 to write?
	VFPFMXR	FPINST2, r8, NE		@ FPINST2 if needed - avoids writing
					@ nonexistant reg on rev0
	VFPFMXR	FPINST, r6
	VFPFMXR	FPSCR, r5		@ restore status

check_for_exception:
	tst	r1, #FPEXC_EXCEPTION
	bne	process_exception	@ might as well handle the pending
					@ exception before retrying branch
					@ out before setting an FPEXC that
					@ stops us reading stuff
	VFPFMXR	FPEXC, r1		@ restore FPEXC last
	sub	r2, r2, #4
	str	r2, [sp, #S_PC]		@ retry the instruction
	mov	pc, r9			@ we think we have handled things


look_for_VFP_exceptions:
	tst	r1, #FPEXC_EXCEPTION
	bne	process_exception
	VFPFMRX	r5, FPSCR
	tst	r5, #FPSCR_IXE		@ IXE doesn't set FPEXC_EXCEPTION !
	bne	process_exception

	@ Fall into hand on to next handler - appropriate coproc instr
	@ not recognised by VFP

	DBGSTR	"not VFP"
	mov	pc, lr

process_exception:
	DBGSTR	"bounce"
	sub	r2, r2, #4
	str	r2, [sp, #S_PC]		@ retry the instruction on exit from
					@ the imprecise exception handling in
					@ the support code
	mov	r2, sp			@ nothing stacked - regdump is at TOS
	mov	lr, r9			@ setup for a return to the user code.

	@ Now call the C code to package up the bounce to the support code
	@   r0 holds the trigger instruction
	@   r1 holds the FPEXC value
	@   r2 pointer to register dump
	b	VFP9_bounce		@ we have handled this - the support
					@ code will raise an exception if
					@ required. If not, the user code will
					@ retry the faulted instruction

#ifdef CONFIG_SMP
	.globl	vfp_save_state
	.type	vfp_save_state, %function
vfp_save_state:
	@ Save the current VFP state
	@ r0 - save location
	@ r1 - FPEXC
	DBGSTR1	"save VFP state %p", r0
	VFPFMRX	r2, FPSCR		@ current status
	VFPFMRX	r3, FPINST		@ FPINST (always there, rev0 onwards)
	tst	r1, #FPEXC_FPV2		@ is there an FPINST2 to read?
	VFPFMRX	r12, FPINST2, NE	@ FPINST2 if needed - avoids reading
					@ nonexistant reg on rev0
	VFPFSTMIA r0 			@ save the working registers
	stmia	r0, {r1, r2, r3, r12}	@ save FPEXC, FPSCR, FPINST, FPINST2
	mov	pc, lr
#endif

last_VFP_context_address:
	.word	last_VFP_context

	.globl	vfp_get_float
vfp_get_float:
	add	pc, pc, r0, lsl #3
	mov	r0, r0
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	mrc	p10, 0, r0, c\dr, c0, 0	@ fmrs	r0, s0
	mov	pc, lr
	mrc	p10, 0, r0, c\dr, c0, 4	@ fmrs	r0, s1
	mov	pc, lr
	.endr

	.globl	vfp_put_float
vfp_put_float:
	add	pc, pc, r1, lsl #3
	mov	r0, r0
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	mcr	p10, 0, r0, c\dr, c0, 0	@ fmsr	r0, s0
	mov	pc, lr
	mcr	p10, 0, r0, c\dr, c0, 4	@ fmsr	r0, s1
	mov	pc, lr
	.endr

	.globl	vfp_get_double
vfp_get_double:
	add	pc, pc, r0, lsl #3
	mov	r0, r0
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	fmrrd	r0, r1, d\dr
	mov	pc, lr
	.endr

	@ virtual register 16 for compare with zero
	mov	r0, #0
	mov	r1, #0
	mov	pc, lr

	.globl	vfp_put_double
vfp_put_double:
	add	pc, pc, r2, lsl #3
	mov	r0, r0
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	fmdrr	d\dr, r0, r1
	mov	pc, lr
	.endr
