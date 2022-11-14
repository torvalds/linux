/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Code to process dynamic relocations in the kernel.
 *
 * Copyright 2008 Paul Mackerras, IBM Corp.
 */

#include <asm/ppc_asm.h>

RELA = 7
RELASZ = 8
RELAENT = 9
R_PPC64_RELATIVE = 22
R_PPC64_UADDR64 = 43

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
	ld	r4,(p_sym - 0b)(r12)
	add	r4,r4,r12	/* r4 has runtime addr of .dynsym */

	/*
	 * Scan the dynamic section for the RELA, RELASZ and RELAENT entries.
	 */
	li	r7,0
	li	r8,0
.Ltags:
	ld	r6,0(r11)	/* get tag */
	cmpdi	r6,0
	beq	.Lend_of_list		/* end of list */
	cmpdi	r6,RELA
	bne	2f
	ld	r7,8(r11)	/* get RELA pointer in r7 */
	b	4f
2:	cmpdi	r6,RELASZ
	bne	3f
	ld	r8,8(r11)	/* get RELASZ value in r8 */
	b	4f
3:	cmpdi	r6,RELAENT
	bne	4f
	ld	r12,8(r11)	/* get RELAENT value in r12 */
4:	addi	r11,r11,16
	b	.Ltags
.Lend_of_list:
	cmpdi	r7,0		/* check we have RELA, RELASZ, RELAENT */
	cmpdi	cr1,r8,0
	beq	.Lout
	beq	cr1,.Lout
	cmpdi	r12,0
	beq	.Lout

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
	 * R_PPC64_RELATIVE and R_PPC64_UADDR64 ones.
	 */
	divd	r8,r8,r12	/* RELASZ / RELAENT */
	mtctr	r8
.Lrels:	ld	r0,8(r9)		/* ELF64_R_TYPE(reloc->r_info) */
	cmpdi	r0,R_PPC64_RELATIVE
	bne	.Luaddr64
	ld	r6,0(r9)	/* reloc->r_offset */
	ld	r0,16(r9)	/* reloc->r_addend */
	b	.Lstore
.Luaddr64:
	srdi	r5,r0,32	/* ELF64_R_SYM(reloc->r_info) */
	clrldi	r0,r0,32
	cmpdi	r0,R_PPC64_UADDR64
	bne	.Lnext
	ld	r6,0(r9)
	ld	r0,16(r9)
	mulli	r5,r5,24	/* 24 == sizeof(elf64_sym) */
	add	r5,r5,r4	/* elf64_sym[ELF64_R_SYM] */
	ld	r5,8(r5)
	add	r0,r0,r5
.Lstore:
	add	r0,r0,r3
	stdx	r0,r7,r6
.Lnext:
	add	r9,r9,r12
	bdnz	.Lrels
.Lout:
	blr

.balign 8
p_dyn:	.8byte	__dynamic_start - 0b
p_rela:	.8byte	__rela_dyn_start - 0b
p_sym:		.8byte __dynamic_symtab - 0b
p_st:	.8byte	_stext - 0b

