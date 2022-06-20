/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ALTERNATIVE_ASM_H
#define _ASM_S390_ALTERNATIVE_ASM_H

#ifdef __ASSEMBLY__

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
	.org	. - ( \orig_end - \orig_start ) + ( \alt_end - \alt_start )
	.org	. - ( \alt_end - \alt_start ) + ( \orig_end - \orig_start )
.endm

/*
 * Define an alternative between two instructions. If @feature is
 * present, early code in apply_alternatives() replaces @oldinstr with
 * @newinstr.
 */
.macro ALTERNATIVE oldinstr, newinstr, feature
	.pushsection .altinstr_replacement,"ax"
770:	\newinstr
771:	.popsection
772:	\oldinstr
773:	.pushsection .altinstructions,"a"
	alt_entry 772b, 773b, 770b, 771b, \feature
	.popsection
.endm

/*
 * Define an alternative between two instructions. If @feature is
 * present, early code in apply_alternatives() replaces @oldinstr with
 * @newinstr.
 */
.macro ALTERNATIVE_2 oldinstr, newinstr1, feature1, newinstr2, feature2
	.pushsection .altinstr_replacement,"ax"
770:	\newinstr1
771:	\newinstr2
772:	.popsection
773:	\oldinstr
774:	.pushsection .altinstructions,"a"
	alt_entry 773b, 774b, 770b, 771b,\feature1
	alt_entry 773b, 774b, 771b, 772b,\feature2
	.popsection
.endm

#endif	/*  __ASSEMBLY__  */

#endif /* _ASM_S390_ALTERNATIVE_ASM_H */
