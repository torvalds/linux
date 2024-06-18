/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (C) IBM Corporation, 2010
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <linux/export.h>
#include <asm/processor.h>
#include <asm/ppc_asm.h>
#include <asm/feature-fixups.h>

/* Note: This code relies on -mminimal-toc */

_GLOBAL(__arch_hweight8)
BEGIN_FTR_SECTION
	b CFUNC(__sw_hweight8)
	nop
	nop
FTR_SECTION_ELSE
	PPC_POPCNTB(R3,R3)
	clrldi	r3,r3,64-8
	blr
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_POPCNTB)
EXPORT_SYMBOL(__arch_hweight8)

_GLOBAL(__arch_hweight16)
BEGIN_FTR_SECTION
	b CFUNC(__sw_hweight16)
	nop
	nop
	nop
	nop
FTR_SECTION_ELSE
  BEGIN_FTR_SECTION_NESTED(50)
	PPC_POPCNTB(R3,R3)
	srdi	r4,r3,8
	add	r3,r4,r3
	clrldi	r3,r3,64-8
	blr
  FTR_SECTION_ELSE_NESTED(50)
	clrlwi  r3,r3,16
	PPC_POPCNTW(R3,R3)
	clrldi	r3,r3,64-8
	blr
  ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_POPCNTD, 50)
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_POPCNTB)
EXPORT_SYMBOL(__arch_hweight16)

_GLOBAL(__arch_hweight32)
BEGIN_FTR_SECTION
	b CFUNC(__sw_hweight32)
	nop
	nop
	nop
	nop
	nop
	nop
FTR_SECTION_ELSE
  BEGIN_FTR_SECTION_NESTED(51)
	PPC_POPCNTB(R3,R3)
	srdi	r4,r3,16
	add	r3,r4,r3
	srdi	r4,r3,8
	add	r3,r4,r3
	clrldi	r3,r3,64-8
	blr
  FTR_SECTION_ELSE_NESTED(51)
	PPC_POPCNTW(R3,R3)
	clrldi	r3,r3,64-8
	blr
  ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_POPCNTD, 51)
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_POPCNTB)
EXPORT_SYMBOL(__arch_hweight32)

_GLOBAL(__arch_hweight64)
BEGIN_FTR_SECTION
	b CFUNC(__sw_hweight64)
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
FTR_SECTION_ELSE
  BEGIN_FTR_SECTION_NESTED(52)
	PPC_POPCNTB(R3,R3)
	srdi	r4,r3,32
	add	r3,r4,r3
	srdi	r4,r3,16
	add	r3,r4,r3
	srdi	r4,r3,8
	add	r3,r4,r3
	clrldi	r3,r3,64-8
	blr
  FTR_SECTION_ELSE_NESTED(52)
	PPC_POPCNTD(R3,R3)
	clrldi	r3,r3,64-8
	blr
  ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_POPCNTD, 52)
ALT_FTR_SECTION_END_IFCLR(CPU_FTR_POPCNTB)
EXPORT_SYMBOL(__arch_hweight64)
