#ifndef _ASM_POWERPC_ISERIES_EXCEPTION_H
#define _ASM_POWERPC_ISERIES_EXCEPTION_H
/*
 * Extracted from head_64.S
 *
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen, Peter Bergner, and
 *    Mike Corrigan {engebret|bergner|mikejc}@us.ibm.com
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC-64 platform, including trap and interrupt dispatch.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#include <asm/exception.h>

#define EXCEPTION_PROLOG_ISERIES_1					\
	mfmsr	r10;							\
	ld	r12,PACALPPACAPTR(r13);					\
	ld	r11,LPPACASRR0(r12);					\
	ld	r12,LPPACASRR1(r12);					\
	ori	r10,r10,MSR_RI;						\
	mtmsrd	r10,1

#define STD_EXCEPTION_ISERIES(label, area)				\
	.globl label##_iSeries;						\
label##_iSeries:							\
	HMT_MEDIUM;							\
	mtspr	SPRN_SPRG1,r13;		/* save r13 */			\
	EXCEPTION_PROLOG_1(area);					\
	EXCEPTION_PROLOG_ISERIES_1;					\
	b	label##_common

#define MASKABLE_EXCEPTION_ISERIES(label)				\
	.globl label##_iSeries;						\
label##_iSeries:							\
	HMT_MEDIUM;							\
	mtspr	SPRN_SPRG1,r13;		/* save r13 */			\
	EXCEPTION_PROLOG_1(PACA_EXGEN);					\
	lbz	r10,PACASOFTIRQEN(r13);					\
	cmpwi	0,r10,0;						\
	beq-	label##_iSeries_masked;					\
	EXCEPTION_PROLOG_ISERIES_1;					\
	b	label##_common;						\

#endif	/* _ASM_POWERPC_ISERIES_EXCEPTION_H */
