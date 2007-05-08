/*
 * This file contains miscellaneous low-level functions for PPC 44x.
 *    Copyright 2007 David Gibson <dwg@au1.ibm.com>, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 */

#include <asm/reg.h>
#include <asm/ppc_asm.h>

	.text

/*
 * void ppc44x_reset_system(char *cmd)
 *
 * At present, this routine just applies a system reset.
 */
_GLOBAL(ppc44x_reset_system)
	mfspr	r13,SPRN_DBCR0
	oris	r13,r13,DBCR0_RST_SYSTEM@h
	mtspr	SPRN_DBCR0,r13
	b	.			/* Just in case the reset doesn't work */
