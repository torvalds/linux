/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * PowerNV OPAL API wrappers
 *
 * Copyright 2011 IBM Corp.
 */

#include <linux/jump_label.h>
#include <asm/ppc_asm.h>
#include <asm/hvcall.h>
#include <asm/asm-offsets.h>
#include <asm/opal.h>
#include <asm/asm-compat.h>
#include <asm/feature-fixups.h>

	.section ".text"

/*
 * r3-r10		- OPAL call arguments
 * STK_PARAM(R11)	- OPAL opcode
 * STK_PARAM(R12)	- MSR to restore
 */
_GLOBAL_TOC(__opal_call)
	mflr	r0
	std	r0,PPC_LR_STKOFF(r1)
	ld	r12,STK_PARAM(R12)(r1)
	li	r0,MSR_IR|MSR_DR|MSR_LE
	andc	r12,r12,r0
	LOAD_REG_ADDR(r11, opal_return)
	mtlr	r11
	LOAD_REG_ADDR(r11, opal)
	ld	r2,0(r11)
	ld	r11,8(r11)
	mtspr	SPRN_HSRR0,r11
	mtspr	SPRN_HSRR1,r12
	/* set token to r0 */
	ld	r0,STK_PARAM(R11)(r1)
	hrfid
opal_return:
	/*
	 * Restore MSR on OPAL return. The MSR is set to big-endian.
	 */
#ifdef __BIG_ENDIAN__
	ld	r11,STK_PARAM(R12)(r1)
	mtmsrd	r11
#else
	/* Endian can only be switched with rfi, must byte reverse MSR load */
	.short 0x4039	 /* li r10,STK_PARAM(R12)		*/
	.byte (STK_PARAM(R12) >> 8) & 0xff
	.byte STK_PARAM(R12) & 0xff

	.long 0x280c6a7d /* ldbrx r11,r10,r1			*/
	.long 0x05009f42 /* bcl 20,31,$+4			*/
	.long 0xa602487d /* mflr r10				*/
	.long 0x14004a39 /* addi r10,r10,20			*/
	.long 0xa64b5a7d /* mthsrr0 r10				*/
	.long 0xa64b7b7d /* mthsrr1 r11				*/
	.long 0x2402004c /* hrfid				*/
#endif
	ld	r2,PACATOC(r13)
	ld	r0,PPC_LR_STKOFF(r1)
	mtlr	r0
	blr
