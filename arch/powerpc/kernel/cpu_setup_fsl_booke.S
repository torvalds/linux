/*
 * This file contains low level CPU setup functions.
 * Kumar Gala <galak@kernel.crashing.org>
 * Copyright 2009 Freescale Semiconductor, Inc.
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

_GLOBAL(__setup_cpu_e200)
	/* enable dedicated debug exception handling resources (Debug APU) */
	mfspr	r3,SPRN_HID0
	ori	r3,r3,HID0_DAPUEN@l
	mtspr	SPRN_HID0,r3
	b	__setup_e200_ivors
_GLOBAL(__setup_cpu_e500v1)
_GLOBAL(__setup_cpu_e500v2)
	b	__setup_e500_ivors
_GLOBAL(__setup_cpu_e500mc)
	b	__setup_e500mc_ivors

