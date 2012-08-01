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
#include <linux/kern_levels.h>
#include "../kernel/entry-header.S"

	.macro	DBGSTR, str
#ifdef DEBUG
	stmfd	sp!, {r0-r3, ip, lr}
	add	r0, pc, #4
	bl	printk
	b	1f
	.asciz  KERN_DEBUG "VFP: \str\n"
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
	.asciz  KERN_DEBUG "VFP: \str\n"
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
	.asciz  KERN_DEBUG "VFP: \str\n"
	.balign 4
1:	ldmfd	sp!, {r0-r3, ip, lr}
#endif
	.endm


@ VFP hardware support entry point.
@
@  r0  = instruction opcode (32-bit ARM or two 16-bit Thumb)
@  r2  = PC value to resume execution after successful emulation
@  r9  = normal "successful" return address
@  r10 = vfp_state union
@  r11 = CPU number
@  lr  = unrecognised instruction return address
@  IRQs enabled.
ENTRY(vfp_support_entry)
	DBGSTR3	"instr %08x pc %08x state %p", r0, r2, r10

	VFPFMRX	r1, FPEXC		@ Is the VFP enabled?
	DBGSTR1	"fpexc %08x", r1
	tst	r1, #FPEXC_EN
	bne	look_for_VFP_exceptions	@ VFP is already enabled

	DBGSTR1 "enable %x", r10
	ldr	r3, vfp_current_hw_state_address
	orr	r1, r1, #FPEXC_EN	@ user FPEXC has the enable bit set
	ldr	r4, [r3, r11, lsl #2]	@ vfp_current_hw_state pointer
	bic	r5, r1, #FPEXC_EX	@ make sure exceptions are disabled
	cmp	r4, r10			@ this thread owns the hw context?
#ifndef CONFIG_SMP
	@ For UP, checking that this thread owns the hw context is
	@ sufficient to determine that the hardware state is valid.
	beq	vfp_hw_state_valid

	@ On UP, we lazily save the VFP context.  As a different
	@ thread wants ownership of the VFP hardware, save the old
	@ state if there was a previous (valid) owner.

	VFPFMXR	FPEXC, r5		@ enable VFP, disable any pending
					@ exceptions, so we can get at the
					@ rest of it

	DBGSTR1	"save old state %p", r4
	cmp	r4, #0			@ if the vfp_current_hw_state is NULL
	beq	vfp_reload_hw		@ then the hw state needs reloading
	VFPFSTMIA r4, r5		@ save the working registers
	VFPFMRX	r5, FPSCR		@ current status
#ifndef CONFIG_CPU_FEROCEON
	tst	r1, #FPEXC_EX		@ is there additional state to save?
	beq	1f
	VFPFMRX	r6, FPINST		@ FPINST (only if FPEXC.EX is set)
	tst	r1, #FPEXC_FP2V		@ is there an FPINST2 to read?
	beq	1f
	VFPFMRX	r8, FPINST2		@ FPINST2 if needed (and present)
1:
#endif
	stmia	r4, {r1, r5, r6, r8}	@ save FPEXC, FPSCR, FPINST, FPINST2
vfp_reload_hw:

#else
	@ For SMP, if this thread does not own the hw context, then we
	@ need to reload it.  No need to save the old state as on SMP,
	@ we always save the state when we switch away from a thread.
	bne	vfp_reload_hw

	@ This thread has ownership of the current hardware context.
	@ However, it may have been migrated to another CPU, in which
	@ case the saved state is newer than the hardware context.
	@ Check this by looking at the CPU number which the state was
	@ last loaded onto.
	ldr	ip, [r10, #VFP_CPU]
	teq	ip, r11
	beq	vfp_hw_state_valid

vfp_reload_hw:
	@ We're loading this threads state into the VFP hardware. Update
	@ the CPU number which contains the most up to date VFP context.
	str	r11, [r10, #VFP_CPU]

	VFPFMXR	FPEXC, r5		@ enable VFP, disable any pending
					@ exceptions, so we can get at the
					@ rest of it
#endif

	DBGSTR1	"load state %p", r10
	str	r10, [r3, r11, lsl #2]	@ update the vfp_current_hw_state pointer
					@ Load the saved state back into the VFP
	VFPFLDMIA r10, r5		@ reload the working registers while
					@ FPEXC is in a safe state
	ldmia	r10, {r1, r5, r6, r8}	@ load FPEXC, FPSCR, FPINST, FPINST2
#ifndef CONFIG_CPU_FEROCEON
	tst	r1, #FPEXC_EX		@ is there additional state to restore?
	beq	1f
	VFPFMXR	FPINST, r6		@ restore FPINST (only if FPEXC.EX is set)
	tst	r1, #FPEXC_FP2V		@ is there an FPINST2 to write?
	beq	1f
	VFPFMXR	FPINST2, r8		@ FPINST2 if needed (and present)
1:
#endif
	VFPFMXR	FPSCR, r5		@ restore status

@ The context stored in the VFP hardware is up to date with this thread
vfp_hw_state_valid:
	tst	r1, #FPEXC_EX
	bne	process_exception	@ might as well handle the pending
					@ exception before retrying branch
					@ out before setting an FPEXC that
					@ stops us reading stuff
	VFPFMXR	FPEXC, r1		@ Restore FPEXC last
	sub	r2, r2, #4		@ Retry current instruction - if Thumb
	str	r2, [sp, #S_PC]		@ mode it's two 16-bit instructions,
					@ else it's one 32-bit instruction, so
					@ always subtract 4 from the following
					@ instruction address.
#ifdef CONFIG_PREEMPT
	get_thread_info	r10
	ldr	r4, [r10, #TI_PREEMPT]	@ get preempt count
	sub	r11, r4, #1		@ decrement it
	str	r11, [r10, #TI_PREEMPT]
#endif
	mov	pc, r9			@ we think we have handled things


look_for_VFP_exceptions:
	@ Check for synchronous or asynchronous exception
	tst	r1, #FPEXC_EX | FPEXC_DEX
	bne	process_exception
	@ On some implementations of the VFP subarch 1, setting FPSCR.IXE
	@ causes all the CDP instructions to be bounced synchronously without
	@ setting the FPEXC.EX bit
	VFPFMRX	r5, FPSCR
	tst	r5, #FPSCR_IXE
	bne	process_exception

	@ Fall into hand on to next handler - appropriate coproc instr
	@ not recognised by VFP

	DBGSTR	"not VFP"
#ifdef CONFIG_PREEMPT
	get_thread_info	r10
	ldr	r4, [r10, #TI_PREEMPT]	@ get preempt count
	sub	r11, r4, #1		@ decrement it
	str	r11, [r10, #TI_PREEMPT]
#endif
	mov	pc, lr

process_exception:
	DBGSTR	"bounce"
	mov	r2, sp			@ nothing stacked - regdump is at TOS
	mov	lr, r9			@ setup for a return to the user code.

	@ Now call the C code to package up the bounce to the support code
	@   r0 holds the trigger instruction
	@   r1 holds the FPEXC value
	@   r2 pointer to register dump
	b	VFP_bounce		@ we have handled this - the support
					@ code will raise an exception if
					@ required. If not, the user code will
					@ retry the faulted instruction
ENDPROC(vfp_support_entry)

ENTRY(vfp_save_state)
	@ Save the current VFP state
	@ r0 - save location
	@ r1 - FPEXC
	DBGSTR1	"save VFP state %p", r0
	VFPFSTMIA r0, r2		@ save the working registers
	VFPFMRX	r2, FPSCR		@ current status
	tst	r1, #FPEXC_EX		@ is there additional state to save?
	beq	1f
	VFPFMRX	r3, FPINST		@ FPINST (only if FPEXC.EX is set)
	tst	r1, #FPEXC_FP2V		@ is there an FPINST2 to read?
	beq	1f
	VFPFMRX	r12, FPINST2		@ FPINST2 if needed (and present)
1:
	stmia	r0, {r1, r2, r3, r12}	@ save FPEXC, FPSCR, FPINST, FPINST2
	mov	pc, lr
ENDPROC(vfp_save_state)

	.align
vfp_current_hw_state_address:
	.word	vfp_current_hw_state

	.macro	tbl_branch, base, tmp, shift
#ifdef CONFIG_THUMB2_KERNEL
	adr	\tmp, 1f
	add	\tmp, \tmp, \base, lsl \shift
	mov	pc, \tmp
#else
	add	pc, pc, \base, lsl \shift
	mov	r0, r0
#endif
1:
	.endm

ENTRY(vfp_get_float)
	tbl_branch r0, r3, #3
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	mrc	p10, 0, r0, c\dr, c0, 0	@ fmrs	r0, s0
	mov	pc, lr
	.org	1b + 8
1:	mrc	p10, 0, r0, c\dr, c0, 4	@ fmrs	r0, s1
	mov	pc, lr
	.org	1b + 8
	.endr
ENDPROC(vfp_get_float)

ENTRY(vfp_put_float)
	tbl_branch r1, r3, #3
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	mcr	p10, 0, r0, c\dr, c0, 0	@ fmsr	r0, s0
	mov	pc, lr
	.org	1b + 8
1:	mcr	p10, 0, r0, c\dr, c0, 4	@ fmsr	r0, s1
	mov	pc, lr
	.org	1b + 8
	.endr
ENDPROC(vfp_put_float)

ENTRY(vfp_get_double)
	tbl_branch r0, r3, #3
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	fmrrd	r0, r1, d\dr
	mov	pc, lr
	.org	1b + 8
	.endr
#ifdef CONFIG_VFPv3
	@ d16 - d31 registers
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	mrrc	p11, 3, r0, r1, c\dr	@ fmrrd	r0, r1, d\dr
	mov	pc, lr
	.org	1b + 8
	.endr
#endif

	@ virtual register 16 (or 32 if VFPv3) for compare with zero
	mov	r0, #0
	mov	r1, #0
	mov	pc, lr
ENDPROC(vfp_get_double)

ENTRY(vfp_put_double)
	tbl_branch r2, r3, #3
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	fmdrr	d\dr, r0, r1
	mov	pc, lr
	.org	1b + 8
	.endr
#ifdef CONFIG_VFPv3
	@ d16 - d31 registers
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	mcrr	p11, 3, r0, r1, c\dr	@ fmdrr	r0, r1, d\dr
	mov	pc, lr
	.org	1b + 8
	.endr
#endif
ENDPROC(vfp_put_double)
