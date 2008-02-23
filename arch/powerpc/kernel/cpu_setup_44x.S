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
	mflr	r4
	bl	__init_fpu_44x
	bl	__plb_disable_wrp
	bl	__fixup_440A_mcheck
	mtlr	r4
	blr
_GLOBAL(__setup_cpu_440grx)
	mflr	r4
	bl	__plb_disable_wrp
	bl	__fixup_440A_mcheck
	mtlr	r4
	blr
_GLOBAL(__setup_cpu_460ex)
_GLOBAL(__setup_cpu_460gt)
	b	__init_fpu_44x
_GLOBAL(__setup_cpu_440gx)
_GLOBAL(__setup_cpu_440spe)
	b	__fixup_440A_mcheck

 /* Temporary fixup for arch/ppc until we kill the whole thing */
#ifndef CONFIG_PPC_MERGE
_GLOBAL(__fixup_440A_mcheck)
	blr
#endif

/* enable APU between CPU and FPU */
_GLOBAL(__init_fpu_44x)
	mfspr	r3,SPRN_CCR0
	/* Clear DAPUIB flag in CCR0 */
	rlwinm	r3,r3,0,12,10
	mtspr	SPRN_CCR0,r3
	isync
	blr

/*
 * Workaround for the incorrect write to DDR SDRAM errata.
 * The write address can be corrupted during writes to
 * DDR SDRAM when write pipelining is enabled on PLB0.
 * Disable write pipelining here.
 */
#define DCRN_PLB4A0_ACR	0x81

_GLOBAL(__plb_disable_wrp)
	mfdcr	r3,DCRN_PLB4A0_ACR
	/* clear WRP bit in PLB4A0_ACR */
	rlwinm	r3,r3,0,8,6
	mtdcr	DCRN_PLB4A0_ACR,r3
	isync
	blr

