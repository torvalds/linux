/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 *
 * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/ppc_asm.h>

	.text

/*
 * Returns (address we are running at) - (address we were linked at)
 * for use before the text and data are mapped to KERNELBASE.
 */

_GLOBAL(reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r3
	LOAD_REG_IMMEDIATE(r4,1b)
	subf	r3,r4,r3
	mtlr	r0
	blr

/*
 * add_reloc_offset(x) returns x + reloc_offset().
 */
_GLOBAL(add_reloc_offset)
	mflr	r0
	bl	1f
1:	mflr	r5
	LOAD_REG_IMMEDIATE(r4,1b)
	subf	r5,r4,r5
	add	r3,r3,r5
	mtlr	r0
	blr
