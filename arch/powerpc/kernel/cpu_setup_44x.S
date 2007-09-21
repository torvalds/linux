/*
 * This file contains low level CPU setup functions.
 * Valentine Barshak <vbarshak@ru.mvista.com>
 * MontaVista Software, Inc (c) 2007
 *
 * Based on cpu_setup_6xx code by 
 * Benjamin Herrenschmidt <benh@kernel.crashing.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <asm/processor.h>
#include <asm/cputable.h>
#include <asm/ppc_asm.h>

_GLOBAL(__setup_cpu_440ep)
	b	__init_fpu_44x
_GLOBAL(__setup_cpu_440epx)
	b	__init_fpu_44x

/* enable APU between CPU and FPU */
_GLOBAL(__init_fpu_44x)
	mfspr	r3,SPRN_CCR0
	/* Clear DAPUIB flag in CCR0 */
	rlwinm	r3,r3,0,12,10
	mtspr	SPRN_CCR0,r3
	isync
	blr

