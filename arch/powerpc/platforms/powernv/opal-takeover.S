/*
 * PowerNV OPAL takeover assembly code, for use by prom_init.c
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/ppc_asm.h>
#include <asm/hvcall.h>
#include <asm/asm-offsets.h>
#include <asm/opal.h>

#define H_HAL_TAKEOVER			0x5124
#define H_HAL_TAKEOVER_QUERY_MAGIC	-1

	.text
_GLOBAL(opal_query_takeover)
	mfcr	r0
	stw	r0,8(r1)
	stdu	r1,-STACKFRAMESIZE(r1)
	std	r3,STK_PARAM(R3)(r1)
	std	r4,STK_PARAM(R4)(r1)
	li	r3,H_HAL_TAKEOVER
	li	r4,H_HAL_TAKEOVER_QUERY_MAGIC
	HVSC
	addi	r1,r1,STACKFRAMESIZE
	ld	r10,STK_PARAM(R3)(r1)
	std	r4,0(r10)
	ld	r10,STK_PARAM(R4)(r1)
	std	r5,0(r10)
	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr

_GLOBAL(opal_do_takeover)
	mfcr	r0
	stw	r0,8(r1)
	mflr	r0
	std	r0,16(r1)
	bl	__opal_do_takeover
	ld	r0,16(r1)
	mtlr	r0
	lwz	r0,8(r1)
	mtcrf	0xff,r0
	blr

__opal_do_takeover:
	ld	r4,0(r3)
	ld	r5,0x8(r3)
	ld	r6,0x10(r3)
	ld	r7,0x18(r3)
	ld	r8,0x20(r3)
	ld	r9,0x28(r3)
	ld	r10,0x30(r3)
	ld	r11,0x38(r3)
	li	r3,H_HAL_TAKEOVER
	HVSC
	blr

	.globl opal_secondary_entry
opal_secondary_entry:
	mr	r31,r3
	mfmsr	r11
	li	r12,(MSR_SF | MSR_ISF)@highest
	sldi	r12,r12,48
	or	r11,r11,r12
	mtmsrd	r11
	isync
	mfspr	r4,SPRN_PIR
	std	r4,0(r3)
1:	HMT_LOW
	ld	r4,8(r3)
	cmpli	cr0,r4,0
	beq	1b
	HMT_MEDIUM
1:	addi	r3,r31,16
	bl	__opal_do_takeover
	b	1b

_GLOBAL(opal_enter_rtas)
	mflr	r0
	std	r0,16(r1)
        stdu	r1,-PROM_FRAME_SIZE(r1)	/* Save SP and create stack space */

	/* Because PROM is running in 32b mode, it clobbers the high order half
	 * of all registers that it saves.  We therefore save those registers
	 * PROM might touch to the stack.  (r0, r3-r13 are caller saved)
	*/
	SAVE_GPR(2, r1)
	SAVE_GPR(13, r1)
	SAVE_8GPRS(14, r1)
	SAVE_10GPRS(22, r1)
	mfcr	r10
	mfmsr	r11
	std	r10,_CCR(r1)
	std	r11,_MSR(r1)

	/* Get the PROM entrypoint */
	mtlr	r5

	/* Switch MSR to 32 bits mode
	 */
        li      r12,1
        rldicr  r12,r12,MSR_SF_LG,(63-MSR_SF_LG)
        andc    r11,r11,r12
        li      r12,1
        rldicr  r12,r12,MSR_ISF_LG,(63-MSR_ISF_LG)
        andc    r11,r11,r12
        mtmsrd  r11
        isync

	/* Enter RTAS here... */
	blrl

	/* Just make sure that r1 top 32 bits didn't get
	 * corrupt by OF
	 */
	rldicl	r1,r1,0,32

	/* Restore the MSR (back to 64 bits) */
	ld	r0,_MSR(r1)
	MTMSRD(r0)
        isync

	/* Restore other registers */
	REST_GPR(2, r1)
	REST_GPR(13, r1)
	REST_8GPRS(14, r1)
	REST_10GPRS(22, r1)
	ld	r4,_CCR(r1)
	mtcr	r4

        addi	r1,r1,PROM_FRAME_SIZE
	ld	r0,16(r1)
	mtlr    r0
	blr
