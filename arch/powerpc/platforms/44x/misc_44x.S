/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains miscellaneous low-level functions for PPC 44x.
 *    Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 */

#include <asm/reg.h>
#include <asm/ppc_asm.h>

	.text

/*
 * Do an IO access in AS1
 */
_GLOBAL(as1_readb)
	mfmsr	r7
	ori	r0,r7,MSR_DS
	sync
	mtmsr	r0
	sync
	isync
	lbz	r3,0(r3)
	sync
	mtmsr	r7
	sync
	isync
	blr

_GLOBAL(as1_writeb)
	mfmsr	r7
	ori	r0,r7,MSR_DS
	sync
	mtmsr	r0
	sync
	isync
	stb	r3,0(r4)
	sync
	mtmsr	r7
	sync
	isync
	blr
