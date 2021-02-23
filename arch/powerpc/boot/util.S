/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copied from <file:arch/powerpc/kernel/misc_32.S>
 *
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * kexec bits:
 * Copyright (C) 2002-2003 Eric Biederman  <ebiederm@xmission.com>
 * GameCube/ppc32 port Copyright (C) 2004 Albert Herranz
 */
#include "ppc_asm.h"

#define SPRN_PVR        0x11F   /* Processor Version Register */

	.text

/* udelay needs to know the period of the
 * timebase in nanoseconds.  This used to be hardcoded to be 60ns
 * (period of 66MHz/4).  Now a variable is used that is initialized to
 * 60 for backward compatibility, but it can be overridden as necessary
 * with code something like this:
 *    extern unsigned long timebase_period_ns;
 *    timebase_period_ns = 1000000000 / bd->bi_tbfreq;
 */
	.data
	.globl timebase_period_ns
timebase_period_ns:
	.long	60

	.text
/*
 * Delay for a number of microseconds
 */
	.globl	udelay
udelay:
	mulli	r4,r3,1000	/* nanoseconds */
	/*  Change r4 to be the number of ticks using:
	 *	(nanoseconds + (timebase_period_ns - 1 )) / timebase_period_ns
	 *  timebase_period_ns defaults to 60 (16.6MHz) */
	mflr	r5
	bcl	20,31,0f
0:	mflr	r6
	mtlr	r5
	addis	r5,r6,(timebase_period_ns-0b)@ha
	lwz	r5,(timebase_period_ns-0b)@l(r5)
	add	r4,r4,r5
	addi	r4,r4,-1
	divw	r4,r4,r5	/* BUS ticks */
1:	MFTBU(r5)
	MFTBL(r6)
	MFTBU(r7)
	cmpw	0,r5,r7
	bne	1b		/* Get [synced] base time */
	addc	r9,r6,r4	/* Compute end time */
	addze	r8,r5
2:	MFTBU(r5)
	cmpw	0,r5,r8
	blt	2b
	bgt	3f
	MFTBL(r6)
	cmpw	0,r6,r9
	blt	2b
3:	blr
