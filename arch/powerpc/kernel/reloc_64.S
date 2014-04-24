/*
 * Code to process dynamic relocations in the kernel.
 *
 * Copyright 2008 Paul Mackerras, IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <asm/ppc_asm.h>

RELA = 7
RELACOUNT = 0x6ffffff9
R_PPC64_RELATIVE = 22

/*
 * r3 = desired final address of kernel
 */
_GLOBAL(relocate)
	mflr	r0
	bcl	20,31,$+4
0:	mflr	r12		/* r12 has runtime addr of label 0 */
	mtlr	r0
	ld	r11,(p_dyn - 0b)(r12)
	add	r11,r11,r12	/* r11 has runtime addr of .dynamic section */
	ld	r9,(p_rela - 0b)(r12)
	add	r9,r9,r12	/* r9 has runtime addr of .rela.dyn section */
	ld	r10,(p_st - 0b)(r12)
	add	r10,r10,r12	/* r10 has runtime addr of _stext */

	/*
	 * Scan the dynamic section for the RELA and RELACOUNT entries.
	 */
	li	r7,0
	li	r8,0
1:	ld	r6,0(r11)	/* get tag */
	cmpdi	r6,0
	beq	4f		/* end of list */
	cmpdi	r6,RELA
	bne	2f
	ld	r7,8(r11)	/* get RELA pointer in r7 */
	b	3f
2:	addis	r6,r6,(-RELACOUNT)@ha
	cmpdi	r6,RELACOUNT@l
	bne	3f
	ld	r8,8(r11)	/* get RELACOUNT value in r8 */
3:	addi	r11,r11,16
	b	1b
4:	cmpdi	r7,0		/* check we have both RELA and RELACOUNT */
	cmpdi	cr1,r8,0
	beq	6f
	beq	cr1,6f

	/*
	 * Work out linktime address of _stext and hence the
	 * relocation offset to be applied.
	 * cur_offset [r7] = rela.run [r9] - rela.link [r7]
	 * _stext.link [r10] = _stext.run [r10] - cur_offset [r7]
	 * final_offset [r3] = _stext.final [r3] - _stext.link [r10]
	 */
	subf	r7,r7,r9	/* cur_offset */
	subf	r10,r7,r10
	subf	r3,r10,r3	/* final_offset */

	/*
	 * Run through the list of relocations and process the
	 * R_PPC64_RELATIVE ones.
	 */
	mtctr	r8
5:	lwz	r0,12(9)	/* ELF64_R_TYPE(reloc->r_info) */
	cmpwi	r0,R_PPC64_RELATIVE
	bne	6f
	ld	r6,0(r9)	/* reloc->r_offset */
	ld	r0,16(r9)	/* reloc->r_addend */
	add	r0,r0,r3
	stdx	r0,r7,r6
	addi	r9,r9,24
	bdnz	5b

6:	blr

.balign 8
p_dyn:	.llong	__dynamic_start - 0b
p_rela:	.llong	__rela_dyn_start - 0b
p_st:	.llong	_stext - 0b

