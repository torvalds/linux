/*
 * Copyright (C) Paul Mackerras 1997.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * NOTE: this code runs in 32 bit mode, is position-independent,
 * and is packaged as ELF32.
 */

#include "ppc_asm.h"

	.text
	/* A procedure descriptor used when booting this as a COFF file.
	 * When making COFF, this comes first in the link and we're
	 * linked at 0x500000.
	 */
	.globl	_zimage_start_opd
_zimage_start_opd:
	.long	0x500000, 0, 0, 0

p_start:	.long	_start
p_etext:	.long	_etext
p_bss_start:	.long	__bss_start
p_end:		.long	_end

	.weak	_platform_stack_top
p_pstack:	.long	_platform_stack_top

	.weak	_zimage_start
	.globl	_zimage_start
_zimage_start:
	.globl	_zimage_start_lib
_zimage_start_lib:
	/* Work out the offset between the address we were linked at
	   and the address where we're running. */
	bl	.+4
p_base:	mflr	r10		/* r10 now points to runtime addr of p_base */
	/* grab the link address of the dynamic section in r11 */
	addis	r11,r10,(_GLOBAL_OFFSET_TABLE_-p_base)@ha
	lwz	r11,(_GLOBAL_OFFSET_TABLE_-p_base)@l(r11)
	cmpwi	r11,0
	beq	3f		/* if not linked -pie */
	/* get the runtime address of the dynamic section in r12 */
	.weak	__dynamic_start
	addis	r12,r10,(__dynamic_start-p_base)@ha
	addi	r12,r12,(__dynamic_start-p_base)@l
	subf	r11,r11,r12	/* runtime - linktime offset */

	/* The dynamic section contains a series of tagged entries.
	 * We need the RELA and RELACOUNT entries. */
RELA = 7
RELACOUNT = 0x6ffffff9
	li	r9,0
	li	r0,0
9:	lwz	r8,0(r12)	/* get tag */
	cmpwi	r8,0
	beq	10f		/* end of list */
	cmpwi	r8,RELA
	bne	11f
	lwz	r9,4(r12)	/* get RELA pointer in r9 */
	b	12f
11:	addis	r8,r8,(-RELACOUNT)@ha
	cmpwi	r8,RELACOUNT@l
	bne	12f
	lwz	r0,4(r12)	/* get RELACOUNT value in r0 */
12:	addi	r12,r12,8
	b	9b

	/* The relocation section contains a list of relocations.
	 * We now do the R_PPC_RELATIVE ones, which point to words
	 * which need to be initialized with addend + offset.
	 * The R_PPC_RELATIVE ones come first and there are RELACOUNT
	 * of them. */
10:	/* skip relocation if we don't have both */
	cmpwi	r0,0
	beq	3f
	cmpwi	r9,0
	beq	3f

	add	r9,r9,r11	/* Relocate RELA pointer */
	mtctr	r0
2:	lbz	r0,4+3(r9)	/* ELF32_R_INFO(reloc->r_info) */
	cmpwi	r0,22		/* R_PPC_RELATIVE */
	bne	3f
	lwz	r12,0(r9)	/* reloc->r_offset */
	lwz	r0,8(r9)	/* reloc->r_addend */
	add	r0,r0,r11
	stwx	r0,r11,r12
	addi	r9,r9,12
	bdnz	2b

	/* Do a cache flush for our text, in case the loader didn't */
3:	lwz	r9,p_start-p_base(r10)	/* note: these are relocated now */
	lwz	r8,p_etext-p_base(r10)
4:	dcbf	r0,r9
	icbi	r0,r9
	addi	r9,r9,0x20
	cmplw	cr0,r9,r8
	blt	4b
	sync
	isync

	/* Clear the BSS */
	lwz	r9,p_bss_start-p_base(r10)
	lwz	r8,p_end-p_base(r10)
	li	r0,0
5:	stw	r0,0(r9)
	addi	r9,r9,4
	cmplw	cr0,r9,r8
	blt	5b

	/* Possibly set up a custom stack */
	lwz	r8,p_pstack-p_base(r10)
	cmpwi	r8,0
	beq	6f
	lwz	r1,0(r8)
	li	r0,0
	stwu	r0,-16(r1)	/* establish a stack frame */
6:

	/* Call platform_init() */
	bl	platform_init

	/* Call start */
	b	start
