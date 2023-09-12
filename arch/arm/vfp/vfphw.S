/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/vfp/vfphw.S
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 */
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/thread_info.h>
#include <asm/vfpmacros.h>
#include <linux/kern_levels.h>
#include <asm/assembler.h>
#include <asm/asm-offsets.h>

	.macro  DBGSTR1, str, arg
#ifdef DEBUG
	stmfd	sp!, {r0-r3, ip, lr}
	mov	r1, \arg
	ldr	r0, =1f
	bl	_printk
	ldmfd	sp!, {r0-r3, ip, lr}

	.pushsection .rodata, "a"
1:	.ascii	KERN_DEBUG "VFP: \str\n"
	.byte	0
	.previous
#endif
	.endm

ENTRY(vfp_load_state)
	@ Load the current VFP state
	@ r0 - load location
	@ returns FPEXC
	DBGSTR1	"load VFP state %p", r0
					@ Load the saved state back into the VFP
	VFPFLDMIA r0, r1		@ reload the working registers while
					@ FPEXC is in a safe state
	ldmia	r0, {r0-r3}		@ load FPEXC, FPSCR, FPINST, FPINST2
	tst	r0, #FPEXC_EX		@ is there additional state to restore?
	beq	1f
	VFPFMXR	FPINST, r2		@ restore FPINST (only if FPEXC.EX is set)
	tst	r0, #FPEXC_FP2V		@ is there an FPINST2 to write?
	beq	1f
	VFPFMXR	FPINST2, r3		@ FPINST2 if needed (and present)
1:
	VFPFMXR	FPSCR, r1		@ restore status
	ret	lr
ENDPROC(vfp_load_state)

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
	ret	lr
ENDPROC(vfp_save_state)

	.macro	tbl_branch, base, tmp, shift
#ifdef CONFIG_THUMB2_KERNEL
	adr	\tmp, 1f
	add	\tmp, \tmp, \base, lsl \shift
	ret	\tmp
#else
	add	pc, pc, \base, lsl \shift
	mov	r0, r0
#endif
1:
	.endm

ENTRY(vfp_get_float)
	tbl_branch r0, r3, #3
	.fpu	vfpv2
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	vmov	r0, s\dr
	ret	lr
	.org	1b + 8
	.endr
	.irp	dr,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
1:	vmov	r0, s\dr
	ret	lr
	.org	1b + 8
	.endr
ENDPROC(vfp_get_float)

ENTRY(vfp_put_float)
	tbl_branch r1, r3, #3
	.fpu	vfpv2
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	vmov	s\dr, r0
	ret	lr
	.org	1b + 8
	.endr
	.irp	dr,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
1:	vmov	s\dr, r0
	ret	lr
	.org	1b + 8
	.endr
ENDPROC(vfp_put_float)

ENTRY(vfp_get_double)
	tbl_branch r0, r3, #3
	.fpu	vfpv2
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	vmov	r0, r1, d\dr
	ret	lr
	.org	1b + 8
	.endr
#ifdef CONFIG_VFPv3
	@ d16 - d31 registers
	.fpu	vfpv3
	.irp	dr,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
1:	vmov	r0, r1, d\dr
	ret	lr
	.org	1b + 8
	.endr
#endif

	@ virtual register 16 (or 32 if VFPv3) for compare with zero
	mov	r0, #0
	mov	r1, #0
	ret	lr
ENDPROC(vfp_get_double)

ENTRY(vfp_put_double)
	tbl_branch r2, r3, #3
	.fpu	vfpv2
	.irp	dr,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
1:	vmov	d\dr, r0, r1
	ret	lr
	.org	1b + 8
	.endr
#ifdef CONFIG_VFPv3
	.fpu	vfpv3
	@ d16 - d31 registers
	.irp	dr,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
1:	vmov	d\dr, r0, r1
	ret	lr
	.org	1b + 8
	.endr
#endif
ENDPROC(vfp_put_double)
