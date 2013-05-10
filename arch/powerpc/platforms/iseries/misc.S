/*
 * This file contains miscellaneous low-level functions.
 *    Copyright (C) 1995-2005 IBM Corp
 *
 * Largely rewritten by Cort Dougan (cort@cs.nmt.edu)
 * and Paul Mackerras.
 * Adapted for iSeries by Mike Corrigan (mikejc@us.ibm.com)
 * PPC64 updates by Dave Engebretsen (engebret@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/processor.h>
#include <asm/asm-offsets.h>
#include <asm/ppc_asm.h>

	.text

/* Handle pending interrupts in interrupt context */
_GLOBAL(iseries_handle_interrupts)
	li	r0,0x5555
	sc
	blr
