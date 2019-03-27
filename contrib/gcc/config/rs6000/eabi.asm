/*
 * Special support for eabi and SVR4
 *
 *   Copyright (C) 1995, 1996, 1998, 2000, 2001 Free Software Foundation, Inc.
 *   Written By Michael Meissner
 * 
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 * 
 * In addition to the permissions in the GNU General Public License, the
 * Free Software Foundation gives you unlimited permission to link the
 * compiled version of this file with other programs, and to distribute
 * those programs without any restriction coming from the use of this
 * file.  (The General Public License restrictions do apply in other
 * respects; for example, they cover modification of the file, and
 * distribution when not linked into another program.)
 * 
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 * 
 *    As a special exception, if you link this library with files
 *    compiled with GCC to produce an executable, this does not cause
 *    the resulting executable to be covered by the GNU General Public License.
 *    This exception does not however invalidate any other reasons why
 *    the executable file might be covered by the GNU General Public License.
 */ 

/* Do any initializations needed for the eabi environment */

	.file	"eabi.asm"
	.section ".text"
	#include "ppc-asm.h"

#ifndef __powerpc64__

	 .section ".got2","aw"
	.align	2
.LCTOC1 = . /* +32768 */

/* Table of addresses */
.Ltable = .-.LCTOC1
	.long	.LCTOC1				/* address we are really at */

.Lsda = .-.LCTOC1
	.long	_SDA_BASE_			/* address of the first small data area */

.Lsdas = .-.LCTOC1
	.long	__SDATA_START__			/* start of .sdata/.sbss section */

.Lsdae = .-.LCTOC1
	.long	__SBSS_END__			/* end of .sdata/.sbss section */

.Lsda2 = .-.LCTOC1
	.long	_SDA2_BASE_			/* address of the second small data area */

.Lsda2s = .-.LCTOC1
	.long	__SDATA2_START__		/* start of .sdata2/.sbss2 section */

.Lsda2e = .-.LCTOC1
	.long	__SBSS2_END__			/* end of .sdata2/.sbss2 section */

#ifdef _RELOCATABLE
.Lgots = .-.LCTOC1
	.long	__GOT_START__			/* Global offset table start */

.Lgotm1 = .-.LCTOC1
	.long	_GLOBAL_OFFSET_TABLE_-4		/* end of GOT ptrs before BLCL + 3 reserved words */

.Lgotm2 = .-.LCTOC1
	.long	_GLOBAL_OFFSET_TABLE_+12	/* start of GOT ptrs after BLCL + 3 reserved words */

.Lgote = .-.LCTOC1
	.long	__GOT_END__			/* Global offset table end */

.Lgot2s = .-.LCTOC1
	.long	__GOT2_START__			/* -mrelocatable GOT pointers start */

.Lgot2e = .-.LCTOC1
	.long	__GOT2_END__			/* -mrelocatable GOT pointers end */

.Lfixups = .-.LCTOC1
	.long	__FIXUP_START__			/* start of .fixup section */

.Lfixupe = .-.LCTOC1
	.long	__FIXUP_END__			/* end of .fixup section */

.Lctors = .-.LCTOC1
	.long	__CTOR_LIST__			/* start of .ctor section */

.Lctore = .-.LCTOC1
	.long	__CTOR_END__			/* end of .ctor section */

.Ldtors = .-.LCTOC1
	.long	__DTOR_LIST__			/* start of .dtor section */

.Ldtore = .-.LCTOC1
	.long	__DTOR_END__			/* end of .dtor section */

.Lexcepts = .-.LCTOC1
	.long	__EXCEPT_START__		/* start of .gcc_except_table section */

.Lexcepte = .-.LCTOC1
	.long	__EXCEPT_END__			/* end of .gcc_except_table section */

.Linit = .-.LCTOC1
	.long	.Linit_p			/* address of variable to say we've been called */

	.text
	.align	2
.Lptr:
	.long	.LCTOC1-.Laddr			/* PC relative pointer to .got2 */
#endif

	.data
	.align	2
.Linit_p:
	.long	0

	.text

FUNC_START(__eabi)

/* Eliminate -mrelocatable code if not -mrelocatable, so that this file can
   be assembled with other assemblers than GAS.  */

#ifndef _RELOCATABLE
	addis	10,0,.Linit_p@ha		/* init flag */
	addis	11,0,.LCTOC1@ha			/* load address of .LCTOC1 */
	lwz	9,.Linit_p@l(10)		/* init flag */
	addi	11,11,.LCTOC1@l
	cmplwi	2,9,0				/* init flag != 0? */
	bnelr	2				/* return now, if we've been called already */
	stw	1,.Linit_p@l(10)		/* store a nonzero value in the done flag */

#else /* -mrelocatable */
	mflr	0
	bl	.Laddr				/* get current address */
.Laddr:
	mflr	12				/* real address of .Laddr */
	lwz	11,(.Lptr-.Laddr)(12)		/* linker generated address of .LCTOC1 */
	add	11,11,12			/* correct to real pointer */
	lwz	12,.Ltable(11)			/* get linker's idea of where .Laddr is */
	lwz	10,.Linit(11)			/* address of init flag */
	subf.	12,12,11			/* calculate difference */
	lwzx	9,10,12				/* done flag */
	cmplwi	2,9,0				/* init flag != 0? */
	mtlr	0				/* restore in case branch was taken */
	bnelr	2				/* return now, if we've been called already */
	stwx	1,10,12				/* store a nonzero value in the done flag */
	beq+	0,.Lsdata			/* skip if we don't need to relocate */

/* We need to relocate the .got2 pointers.  */

	lwz	3,.Lgot2s(11)			/* GOT2 pointers start */
	lwz	4,.Lgot2e(11)			/* GOT2 pointers end */
	add	3,12,3				/* adjust pointers */
	add	4,12,4
	bl	FUNC_NAME(__eabi_convert)	/* convert pointers in .got2 section */

/* Fixup the .ctor section for static constructors */

	lwz	3,.Lctors(11)			/* constructors pointers start */
	lwz	4,.Lctore(11)			/* constructors pointers end */
	bl	FUNC_NAME(__eabi_convert)	/* convert constructors */

/* Fixup the .dtor section for static destructors */

	lwz	3,.Ldtors(11)			/* destructors pointers start */
	lwz	4,.Ldtore(11)			/* destructors pointers end */
	bl	FUNC_NAME(__eabi_convert)	/* convert destructors */

/* Fixup the .gcc_except_table section for G++ exceptions */

	lwz	3,.Lexcepts(11)			/* exception table pointers start */
	lwz	4,.Lexcepte(11)			/* exception table pointers end */
	bl	FUNC_NAME(__eabi_convert)	/* convert exceptions */

/* Fixup the addresses in the GOT below _GLOBAL_OFFSET_TABLE_-4 */

	lwz	3,.Lgots(11)			/* GOT table pointers start */
	lwz	4,.Lgotm1(11)			/* GOT table pointers below _GLOBAL_OFFSET_TABLE-4 */
	bl	FUNC_NAME(__eabi_convert)	/* convert lower GOT */

/* Fixup the addresses in the GOT above _GLOBAL_OFFSET_TABLE_+12 */

	lwz	3,.Lgotm2(11)			/* GOT table pointers above _GLOBAL_OFFSET_TABLE+12 */
	lwz	4,.Lgote(11)			/* GOT table pointers end */
	bl	FUNC_NAME(__eabi_convert)	/* convert lower GOT */

/* Fixup any user initialized pointers now (the compiler drops pointers to */
/* each of the relocs that it does in the .fixup section).  */

.Lfix:
	lwz	3,.Lfixups(11)			/* fixup pointers start */
	lwz	4,.Lfixupe(11)			/* fixup pointers end */
	bl	FUNC_NAME(__eabi_uconvert)	/* convert user initialized pointers */

.Lsdata:
	mtlr	0				/* restore link register */
#endif /* _RELOCATABLE */

/* Only load up register 13 if there is a .sdata and/or .sbss section */
	lwz	3,.Lsdas(11)			/* start of .sdata/.sbss section */
	lwz	4,.Lsdae(11)			/* end of .sdata/.sbss section */
	cmpw	1,3,4				/* .sdata/.sbss section non-empty? */
	beq-	1,.Lsda2l			/* skip loading r13 */

	lwz	13,.Lsda(11)			/* load r13 with _SDA_BASE_ address */

/* Only load up register 2 if there is a .sdata2 and/or .sbss2 section */

.Lsda2l:	
	lwz	3,.Lsda2s(11)			/* start of .sdata/.sbss section */
	lwz	4,.Lsda2e(11)			/* end of .sdata/.sbss section */
	cmpw	1,3,4				/* .sdata/.sbss section non-empty? */
	beq+	1,.Ldone			/* skip loading r2 */

	lwz	2,.Lsda2(11)			/* load r2 with _SDA2_BASE_ address */

/* Done adjusting pointers, return by way of doing the C++ global constructors.  */

.Ldone:
	b	FUNC_NAME(__init)	/* do any C++ global constructors (which returns to caller) */
FUNC_END(__eabi)

/* Special subroutine to convert a bunch of pointers directly.
   r0		has original link register
   r3		has low pointer to convert
   r4		has high pointer to convert
   r5 .. r10	are scratch registers
   r11		has the address of .LCTOC1 in it.
   r12		has the value to add to each pointer
   r13 .. r31	are unchanged */
	
FUNC_START(__eabi_convert)
        cmplw	1,3,4				/* any pointers to convert? */
        subf	5,3,4				/* calculate number of words to convert */
        bclr	4,4				/* return if no pointers */

        srawi	5,5,2
	addi	3,3,-4				/* start-4 for use with lwzu */
        mtctr	5

.Lcvt:
	lwzu	6,4(3)				/* pointer to convert */
	cmpwi	0,6,0
	beq-	.Lcvt2				/* if pointer is null, don't convert */

        add	6,6,12				/* convert pointer */
        stw	6,0(3)
.Lcvt2:
        bdnz+	.Lcvt
        blr

FUNC_END(__eabi_convert)

/* Special subroutine to convert the pointers the user has initialized.  The
   compiler has placed the address of the initialized pointer into the .fixup
   section.

   r0		has original link register
   r3		has low pointer to convert
   r4		has high pointer to convert
   r5 .. r10	are scratch registers
   r11		has the address of .LCTOC1 in it.
   r12		has the value to add to each pointer
   r13 .. r31	are unchanged */
	
FUNC_START(__eabi_uconvert)
        cmplw	1,3,4				/* any pointers to convert? */
        subf	5,3,4				/* calculate number of words to convert */
        bclr	4,4				/* return if no pointers */

        srawi	5,5,2
	addi	3,3,-4				/* start-4 for use with lwzu */
        mtctr	5

.Lucvt:
	lwzu	6,4(3)				/* next pointer to pointer to convert */
	add	6,6,12				/* adjust pointer */
	lwz	7,0(6)				/* get the pointer it points to */
	stw	6,0(3)				/* store adjusted pointer */
	add	7,7,12				/* adjust */
	stw	7,0(6)
        bdnz+	.Lucvt
        blr

FUNC_END(__eabi_uconvert)

#endif
