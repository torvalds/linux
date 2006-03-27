/*
 *  This file contains the power_save function for 970-family CPUs.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/threads.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/cputable.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

#undef DEBUG

	.text

_GLOBAL(power4_idle)
BEGIN_FTR_SECTION
	blr
END_FTR_SECTION_IFCLR(CPU_FTR_CAN_NAP)
	/* Now check if user or arch enabled NAP mode */
	LOAD_REG_ADDRBASE(r3,powersave_nap)
	lwz	r4,ADDROFF(powersave_nap)(r3)
	cmpwi	0,r4,0
	beqlr

	/* Go to NAP now */
BEGIN_FTR_SECTION
	DSSALL
	sync
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
	mfmsr	r7
	ori	r7,r7,MSR_EE
	oris	r7,r7,MSR_POW@h
	sync
	isync
	mtmsrd	r7
	isync
	sync
	blr
