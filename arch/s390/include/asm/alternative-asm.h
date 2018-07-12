/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ALTERNATIVE_ASM_H
#define _ASM_S390_ALTERNATIVE_ASM_H

#ifdef __ASSEMBLY__

/*
 * Check the length of an instruction sequence. The length may not be larger
 * than 254 bytes and it has to be divisible by 2.
 */
.macro alt_len_check start,end
	.if ( \end - \start ) > 254
	.error "cpu alternatives does not support instructions blocks > 254 bytes\n"
	.endif
	.if ( \end - \start ) % 2
	.error "cpu alternatives instructions length is odd\n"
	.endif
.endm

/*
 * Issue one struct alt_instr descriptor entry (need to put it into
 * the section .altinstructions, see below). This entry contains
 * enough information for the alternatives patching code to patch an
 * instruction. See apply_alternatives().
 */
.macro alt_entry orig_start, orig_end, alt_start, alt_end, feature
	.long	\orig_start - .
	.long	\alt_start - .
	.word	\feature
	.byte	\orig_end - \orig_start
	.byte	\alt_end - \alt_start
.endm

/*
 * Fill up @bytes with nops. The macro emits 6-byte nop instructions
 * for the bulk of the area, possibly followed by a 4-byte and/or
 * a 2-byte nop if the size of the area is not divisible by 6.
 */
.macro alt_pad_fill bytes
	.fill	( \bytes ) / 6, 6, 0xc0040000
	.fill	( \bytes ) % 6 / 4, 4, 0x47000000
	.fill	( \bytes ) % 6 % 4 / 2, 2, 0x0700
.endm

/*
 * Fill up @bytes with nops. If the number of bytes is larger
 * than 6, emit a jg instruction to branch over all nops, then
 * fill an area of size (@bytes - 6) with nop instructions.
 */
.macro alt_pad bytes
	.if ( \bytes > 0 )
	.if ( \bytes > 6 )
	jg	. + \bytes
	alt_pad_fill \bytes - 6
	.else
	alt_pad_fill \bytes
	.endif
	.endif
.endm

/*
 * Define an alternative between two instructions. If @feature is
 * present, early code in apply_alternatives() replaces @oldinstr with
 * @newinstr. ".skip" directive takes care of proper instruction padding
 * in case @newinstr is longer than @oldinstr.
 */
.macro ALTERNATIVE oldinstr, newinstr, feature
	.pushsection .altinstr_replacement,"ax"
770:	\newinstr
771:	.popsection
772:	\oldinstr
773:	alt_len_check 770b, 771b
	alt_len_check 772b, 773b
	alt_pad ( ( 771b - 770b ) - ( 773b - 772b ) )
774:	.pushsection .altinstructions,"a"
	alt_entry 772b, 774b, 770b, 771b, \feature
	.popsection
.endm

/*
 * Define an alternative between two instructions. If @feature is
 * present, early code in apply_alternatives() replaces @oldinstr with
 * @newinstr. ".skip" directive takes care of proper instruction padding
 * in case @newinstr is longer than @oldinstr.
 */
.macro ALTERNATIVE_2 oldinstr, newinstr1, feature1, newinstr2, feature2
	.pushsection .altinstr_replacement,"ax"
770:	\newinstr1
771:	\newinstr2
772:	.popsection
773:	\oldinstr
774:	alt_len_check 770b, 771b
	alt_len_check 771b, 772b
	alt_len_check 773b, 774b
	.if ( 771b - 770b > 772b - 771b )
	alt_pad ( ( 771b - 770b ) - ( 774b - 773b ) )
	.else
	alt_pad ( ( 772b - 771b ) - ( 774b - 773b ) )
	.endif
775:	.pushsection .altinstructions,"a"
	alt_entry 773b, 775b, 770b, 771b,\feature1
	alt_entry 773b, 775b, 771b, 772b,\feature2
	.popsection
.endm

#endif	/*  __ASSEMBLY__  */

#endif /* _ASM_S390_ALTERNATIVE_ASM_H */
