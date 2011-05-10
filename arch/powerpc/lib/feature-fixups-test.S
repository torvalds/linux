/*
 * Copyright 2008 Michael Ellerman, IBM Corporation.
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/feature-fixups.h>
#include <asm/ppc_asm.h>
#include <asm/synch.h>

	.text

#define globl(x)		\
	.globl x;	\
x:

globl(ftr_fixup_test1)
	or	1,1,1
	or	2,2,2	/* fixup will nop out this instruction */
	or	3,3,3

globl(end_ftr_fixup_test1)

globl(ftr_fixup_test1_orig)
	or	1,1,1
	or	2,2,2
	or	3,3,3

globl(ftr_fixup_test1_expected)
	or	1,1,1
	nop
	or	3,3,3

globl(ftr_fixup_test2)
	or	1,1,1
	or	2,2,2	/* fixup will replace this with ftr_fixup_test2_alt */
	or	3,3,3

globl(end_ftr_fixup_test2)

globl(ftr_fixup_test2_orig)
	or	1,1,1
	or	2,2,2
	or	3,3,3

globl(ftr_fixup_test2_alt)
	or	31,31,31

globl(ftr_fixup_test2_expected)
	or	1,1,1
	or	31,31,31
	or	3,3,3

globl(ftr_fixup_test3)
	or	1,1,1
	or	2,2,2	/* fixup will fail to replace this */
	or	3,3,3

globl(end_ftr_fixup_test3)

globl(ftr_fixup_test3_orig)
	or	1,1,1
	or	2,2,2
	or	3,3,3

globl(ftr_fixup_test3_alt)
	or	31,31,31
	or	31,31,31

globl(ftr_fixup_test4)
	or	1,1,1
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	3,3,3

globl(end_ftr_fixup_test4)

globl(ftr_fixup_test4_expected)
	or	1,1,1
	or	31,31,31
	or	31,31,31
	nop
	nop
	or	3,3,3

globl(ftr_fixup_test4_orig)
	or	1,1,1
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	3,3,3

globl(ftr_fixup_test4_alt)
	or	31,31,31
	or	31,31,31


globl(ftr_fixup_test5)
	or	1,1,1
BEGIN_FTR_SECTION
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	2,2,2
	or	2,2,2
FTR_SECTION_ELSE
2:	b	3f
3:	or	5,5,5
	beq	3b
	b	1f
	or	6,6,6
	b	2b
1:	bdnz	3b
ALT_FTR_SECTION_END(0, 1)
	or	1,1,1

globl(end_ftr_fixup_test5)

globl(ftr_fixup_test5_expected)
	or	1,1,1
2:	b	3f
3:	or	5,5,5
	beq	3b
	b	1f
	or	6,6,6
	b	2b
1:	bdnz	3b
	or	1,1,1

globl(ftr_fixup_test6)
1:	or	1,1,1
BEGIN_FTR_SECTION
	or	5,5,5
2:	PPC_LCMPI	r3,0
	beq	4f
	blt	2b
	b	1b
	b	4f
FTR_SECTION_ELSE
2:	or	2,2,2
	PPC_LCMPI	r3,1
	beq	3f
	blt	2b
	b	3f
	b	1b
ALT_FTR_SECTION_END(0, 1)
3:	or	1,1,1
	or	2,2,2
4:	or	3,3,3

globl(end_ftr_fixup_test6)

globl(ftr_fixup_test6_expected)
1:	or	1,1,1
2:	or	2,2,2
	PPC_LCMPI	r3,1
	beq	3f
	blt	2b
	b	3f
	b	1b
2:	or	1,1,1
	or	2,2,2
3:	or	3,3,3


#if 0
/* Test that if we have a larger else case the assembler spots it and
 * reports an error. #if 0'ed so as not to break the build normally.
 */
ftr_fixup_test7:
	or	1,1,1
BEGIN_FTR_SECTION
	or	2,2,2
	or	2,2,2
	or	2,2,2
FTR_SECTION_ELSE
	or	3,3,3
	or	3,3,3
	or	3,3,3
	or	3,3,3
ALT_FTR_SECTION_END(0, 1)
	or	1,1,1
#endif

#define	MAKE_MACRO_TEST(TYPE)						\
globl(ftr_fixup_test_ ##TYPE##_macros)					\
	or	1,1,1;							\
	/* Basic test, this section should all be nop'ed */		\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
	or	2,2,2;							\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Basic test, this section should NOT be nop'ed */		\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
	or	2,2,2;							\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nesting test, inner section should be nop'ed */		\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(80)					\
	or	3,3,3;							\
	or	3,3,3;							\
END_##TYPE##_SECTION_NESTED(0, 1, 80)					\
	or	2,2,2;							\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nesting test, whole section should be nop'ed */		\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(80)					\
	or	3,3,3;							\
	or	3,3,3;							\
END_##TYPE##_SECTION_NESTED(0, 0, 80)					\
	or	2,2,2;							\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nesting test, none should be nop'ed */			\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(80)					\
	or	3,3,3;							\
	or	3,3,3;							\
END_##TYPE##_SECTION_NESTED(0, 0, 80)					\
	or	2,2,2;							\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Basic alt section test, default case should be taken */	\
BEGIN_##TYPE##_SECTION							\
	or	3,3,3;							\
	or	3,3,3;							\
	or	3,3,3;							\
##TYPE##_SECTION_ELSE							\
	or	5,5,5;							\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Basic alt section test, else case should be taken */		\
BEGIN_##TYPE##_SECTION							\
	or	3,3,3;							\
	or	3,3,3;							\
	or	3,3,3;							\
##TYPE##_SECTION_ELSE							\
	or	31,31,31;						\
	or	31,31,31;						\
	or	31,31,31;						\
ALT_##TYPE##_SECTION_END(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt with smaller else case, should be padded with nops */	\
BEGIN_##TYPE##_SECTION							\
	or	3,3,3;							\
	or	3,3,3;							\
	or	3,3,3;							\
##TYPE##_SECTION_ELSE							\
	or	31,31,31;						\
ALT_##TYPE##_SECTION_END(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt section with nested section in default case */		\
	/* Default case should be taken, with nop'ed inner section */	\
BEGIN_##TYPE##_SECTION							\
	or	3,3,3;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	3,3,3;							\
	or	3,3,3;							\
END_##TYPE##_SECTION_NESTED(0, 1, 95)					\
	or	3,3,3;							\
##TYPE##_SECTION_ELSE							\
	or	2,2,2;							\
	or	2,2,2;							\
ALT_##TYPE##_SECTION_END(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt section with nested section in else, default taken */	\
BEGIN_##TYPE##_SECTION							\
	or	3,3,3;							\
	or	3,3,3;							\
	or	3,3,3;							\
##TYPE##_SECTION_ELSE							\
	or	5,5,5;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	3,3,3;							\
END_##TYPE##_SECTION_NESTED(0, 1, 95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt section with nested section in else, else taken & nop */	\
BEGIN_##TYPE##_SECTION							\
	or	3,3,3;							\
	or	3,3,3;							\
	or	3,3,3;							\
##TYPE##_SECTION_ELSE							\
	or	5,5,5;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	3,3,3;							\
END_##TYPE##_SECTION_NESTED(0, 1, 95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Feature section with nested alt section, default taken */	\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	1,1,1;							\
##TYPE##_SECTION_ELSE_NESTED(95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 0, 95)				\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Feature section with nested alt section, else taken */	\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	1,1,1;							\
##TYPE##_SECTION_ELSE_NESTED(95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95)				\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Feature section with nested alt section, all nop'ed */	\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	1,1,1;							\
##TYPE##_SECTION_ELSE_NESTED(95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 0, 95)				\
	or	2,2,2;							\
END_##TYPE##_SECTION(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, default with inner default taken */	\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	1,1,1;							\
##TYPE##_SECTION_ELSE_NESTED(95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 0, 95)				\
	or	2,2,2;							\
##TYPE##_SECTION_ELSE							\
	or	31,31,31;						\
BEGIN_##TYPE##_SECTION_NESTED(94)					\
	or	5,5,5;							\
##TYPE##_SECTION_ELSE_NESTED(94)					\
	or	1,1,1;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 0, 94)				\
	or	31,31,31;						\
ALT_##TYPE##_SECTION_END(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, default with inner else taken */	\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	1,1,1;							\
##TYPE##_SECTION_ELSE_NESTED(95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95)				\
	or	2,2,2;							\
##TYPE##_SECTION_ELSE							\
	or	31,31,31;						\
BEGIN_##TYPE##_SECTION_NESTED(94)					\
	or	5,5,5;							\
##TYPE##_SECTION_ELSE_NESTED(94)					\
	or	1,1,1;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 0, 94)				\
	or	31,31,31;						\
ALT_##TYPE##_SECTION_END(0, 0)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, else with inner default taken */	\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	1,1,1;							\
##TYPE##_SECTION_ELSE_NESTED(95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95)				\
	or	2,2,2;							\
##TYPE##_SECTION_ELSE							\
	or	31,31,31;						\
BEGIN_##TYPE##_SECTION_NESTED(94)					\
	or	5,5,5;							\
##TYPE##_SECTION_ELSE_NESTED(94)					\
	or	1,1,1;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 0, 94)				\
	or	31,31,31;						\
ALT_##TYPE##_SECTION_END(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, else with inner else taken */		\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
BEGIN_##TYPE##_SECTION_NESTED(95)					\
	or	1,1,1;							\
##TYPE##_SECTION_ELSE_NESTED(95)					\
	or	5,5,5;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95)				\
	or	2,2,2;							\
##TYPE##_SECTION_ELSE							\
	or	31,31,31;						\
BEGIN_##TYPE##_SECTION_NESTED(94)					\
	or	5,5,5;							\
##TYPE##_SECTION_ELSE_NESTED(94)					\
	or	1,1,1;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 1, 94)				\
	or	31,31,31;						\
ALT_##TYPE##_SECTION_END(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, else can have large else case */	\
BEGIN_##TYPE##_SECTION							\
	or	2,2,2;							\
	or	2,2,2;							\
	or	2,2,2;							\
	or	2,2,2;							\
##TYPE##_SECTION_ELSE 							\
BEGIN_##TYPE##_SECTION_NESTED(94) 					\
	or	5,5,5;							\
	or	5,5,5;							\
	or	5,5,5;							\
	or	5,5,5;							\
##TYPE##_SECTION_ELSE_NESTED(94) 					\
	or	1,1,1;							\
	or	1,1,1;							\
	or	1,1,1;							\
	or	1,1,1;							\
ALT_##TYPE##_SECTION_END_NESTED(0, 1, 94)				\
ALT_##TYPE##_SECTION_END(0, 1)						\
	or	1,1,1;							\
	or	1,1,1;

#define	MAKE_MACRO_TEST_EXPECTED(TYPE)					\
globl(ftr_fixup_test_ ##TYPE##_macros_expected)				\
	or	1,1,1;							\
	/* Basic test, this section should all be nop'ed */		\
/* BEGIN_##TYPE##_SECTION */						\
	nop;								\
	nop;								\
	nop;								\
/* END_##TYPE##_SECTION(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Basic test, this section should NOT be nop'ed */		\
/* BEGIN_##TYPE##_SECTION */						\
	or	2,2,2;							\
	or	2,2,2;							\
	or	2,2,2;							\
/* END_##TYPE##_SECTION(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nesting test, inner section should be nop'ed */		\
/* BEGIN_##TYPE##_SECTION */						\
	or	2,2,2;							\
	or	2,2,2;							\
/* BEGIN_##TYPE##_SECTION_NESTED(80) */					\
	nop;								\
	nop;								\
/* END_##TYPE##_SECTION_NESTED(0, 1, 80) */				\
	or	2,2,2;							\
	or	2,2,2;							\
/* END_##TYPE##_SECTION(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nesting test, whole section should be nop'ed */		\
	/* NB. inner section is not nop'ed, but then entire outer is */	\
/* BEGIN_##TYPE##_SECTION */						\
	nop;								\
	nop;								\
/* BEGIN_##TYPE##_SECTION_NESTED(80) */					\
	nop;								\
	nop;								\
/* END_##TYPE##_SECTION_NESTED(0, 0, 80) */				\
	nop;								\
	nop;								\
/* END_##TYPE##_SECTION(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nesting test, none should be nop'ed */			\
/* BEGIN_##TYPE##_SECTION */						\
	or	2,2,2;							\
	or	2,2,2;							\
/* BEGIN_##TYPE##_SECTION_NESTED(80) */					\
	or	3,3,3;							\
	or	3,3,3;							\
/* END_##TYPE##_SECTION_NESTED(0, 0, 80) */				\
	or	2,2,2;							\
	or	2,2,2;							\
/* END_##TYPE##_SECTION(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Basic alt section test, default case should be taken */	\
/* BEGIN_##TYPE##_SECTION */						\
	or	3,3,3;							\
	or	3,3,3;							\
	or	3,3,3;							\
/* ##TYPE##_SECTION_ELSE */						\
	/* or	5,5,5; */						\
	/* or	5,5,5; */						\
/* ALT_##TYPE##_SECTION_END(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Basic alt section test, else case should be taken */		\
/* BEGIN_##TYPE##_SECTION */						\
	/* or	3,3,3; */						\
	/* or	3,3,3; */						\
	/* or	3,3,3; */						\
/* ##TYPE##_SECTION_ELSE */						\
	or	31,31,31;						\
	or	31,31,31;						\
	or	31,31,31;						\
/* ALT_##TYPE##_SECTION_END(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt with smaller else case, should be padded with nops */	\
/* BEGIN_##TYPE##_SECTION */						\
	/* or	3,3,3; */						\
	/* or	3,3,3; */						\
	/* or	3,3,3; */						\
/* ##TYPE##_SECTION_ELSE */						\
	or	31,31,31;						\
	nop;								\
	nop;								\
/* ALT_##TYPE##_SECTION_END(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt section with nested section in default case */		\
	/* Default case should be taken, with nop'ed inner section */	\
/* BEGIN_##TYPE##_SECTION */						\
	or	3,3,3;							\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	nop;								\
	nop;								\
/* END_##TYPE##_SECTION_NESTED(0, 1, 95) */				\
	or	3,3,3;							\
/* ##TYPE##_SECTION_ELSE */						\
	/* or	2,2,2; */						\
	/* or	2,2,2; */						\
/* ALT_##TYPE##_SECTION_END(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt section with nested section in else, default taken */	\
/* BEGIN_##TYPE##_SECTION */						\
	or	3,3,3;							\
	or	3,3,3;							\
	or	3,3,3;							\
/* ##TYPE##_SECTION_ELSE */						\
	/* or	5,5,5; */						\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	/* or	3,3,3; */						\
/* END_##TYPE##_SECTION_NESTED(0, 1, 95) */				\
	/* or	5,5,5; */						\
/* ALT_##TYPE##_SECTION_END(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Alt section with nested section in else, else taken & nop */	\
/* BEGIN_##TYPE##_SECTION */						\
	/* or	3,3,3; */						\
	/* or	3,3,3; */						\
	/* or	3,3,3; */						\
/* ##TYPE##_SECTION_ELSE */						\
	or	5,5,5;							\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	nop;								\
/* END_##TYPE##_SECTION_NESTED(0, 1, 95) */				\
	or	5,5,5;							\
/* ALT_##TYPE##_SECTION_END(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Feature section with nested alt section, default taken */	\
/* BEGIN_##TYPE##_SECTION */						\
	or	2,2,2;							\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	or	1,1,1;							\
/* ##TYPE##_SECTION_ELSE_NESTED(95) */					\
	/* or	5,5,5; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 0, 95) */				\
	or	2,2,2;							\
/* END_##TYPE##_SECTION(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Feature section with nested alt section, else taken */	\
/* BEGIN_##TYPE##_SECTION */						\
	or	2,2,2;							\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	/* or	1,1,1; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(95) */					\
	or	5,5,5;							\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95) */				\
	or	2,2,2;							\
/* END_##TYPE##_SECTION(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Feature section with nested alt section, all nop'ed */	\
/* BEGIN_##TYPE##_SECTION */						\
	nop;								\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	nop;								\
/* ##TYPE##_SECTION_ELSE_NESTED(95) */					\
	/* or	5,5,5; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 0, 95) */				\
	nop;								\
/* END_##TYPE##_SECTION(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, default with inner default taken */	\
/* BEGIN_##TYPE##_SECTION */						\
	or	2,2,2;							\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	or	1,1,1;							\
/* ##TYPE##_SECTION_ELSE_NESTED(95) */					\
	/* or	5,5,5; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 0, 95) */				\
	or	2,2,2;							\
/* ##TYPE##_SECTION_ELSE */						\
	/* or	31,31,31; */						\
/* BEGIN_##TYPE##_SECTION_NESTED(94) */					\
	/* or	5,5,5; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(94) */					\
	/* or	1,1,1; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 0, 94) */				\
	/* or	31,31,31; */						\
/* ALT_##TYPE##_SECTION_END(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, default with inner else taken */	\
/* BEGIN_##TYPE##_SECTION */						\
	or	2,2,2;							\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	/* or	1,1,1; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(95) */					\
	or	5,5,5;							\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95) */				\
	or	2,2,2;							\
/* ##TYPE##_SECTION_ELSE */						\
	/* or	31,31,31; */						\
/* BEGIN_##TYPE##_SECTION_NESTED(94) */					\
	/* or	5,5,5; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(94) */					\
	/* or	1,1,1; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 0, 94) */				\
	/* or	31,31,31; */						\
/* ALT_##TYPE##_SECTION_END(0, 0) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, else with inner default taken */	\
/* BEGIN_##TYPE##_SECTION */						\
	/* or	2,2,2; */						\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	/* or	1,1,1; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(95) */					\
	/* or	5,5,5; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95) */				\
	/* or	2,2,2; */						\
/* ##TYPE##_SECTION_ELSE */						\
	or	31,31,31;						\
/* BEGIN_##TYPE##_SECTION_NESTED(94) */					\
	or	5,5,5;							\
/* ##TYPE##_SECTION_ELSE_NESTED(94) */					\
	/* or	1,1,1; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 0, 94) */				\
	or	31,31,31;						\
/* ALT_##TYPE##_SECTION_END(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, else with inner else taken */		\
/* BEGIN_##TYPE##_SECTION */						\
	/* or	2,2,2; */						\
/* BEGIN_##TYPE##_SECTION_NESTED(95) */					\
	/* or	1,1,1; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(95) */					\
	/* or	5,5,5; */						\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 1, 95) */				\
	/* or	2,2,2; */						\
/* ##TYPE##_SECTION_ELSE */						\
	or	31,31,31;						\
/* BEGIN_##TYPE##_SECTION_NESTED(94) */					\
	/* or	5,5,5; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(94) */					\
	or	1,1,1;							\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 1, 94) */				\
	or	31,31,31;						\
/* ALT_##TYPE##_SECTION_END(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	/* Nested alt sections, else can have large else case */	\
/* BEGIN_##TYPE##_SECTION */						\
	/* or	2,2,2; */						\
	/* or	2,2,2; */						\
	/* or	2,2,2; */						\
	/* or	2,2,2; */						\
/* ##TYPE##_SECTION_ELSE */						\
/* BEGIN_##TYPE##_SECTION_NESTED(94) */					\
	/* or	5,5,5; */						\
	/* or	5,5,5; */						\
	/* or	5,5,5; */						\
	/* or	5,5,5; */						\
/* ##TYPE##_SECTION_ELSE_NESTED(94) */					\
	or	1,1,1;							\
	or	1,1,1;							\
	or	1,1,1;							\
	or	1,1,1;							\
/* ALT_##TYPE##_SECTION_END_NESTED(0, 1, 94) */				\
/* ALT_##TYPE##_SECTION_END(0, 1) */					\
	or	1,1,1;							\
	or	1,1,1;

MAKE_MACRO_TEST(FTR);
MAKE_MACRO_TEST_EXPECTED(FTR);

#ifdef CONFIG_PPC64
MAKE_MACRO_TEST(FW_FTR);
MAKE_MACRO_TEST_EXPECTED(FW_FTR);
#endif

globl(lwsync_fixup_test)
1:	or	1,1,1
	LWSYNC
globl(end_lwsync_fixup_test)

globl(lwsync_fixup_test_expected_LWSYNC)
1:	or	1,1,1
	lwsync

globl(lwsync_fixup_test_expected_SYNC)
1:	or	1,1,1
	sync

