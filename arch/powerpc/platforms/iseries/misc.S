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

/* unsigned long local_save_flags(void) */
_GLOBAL(local_get_flags)
	lbz	r3,PACAPROCENABLED(r13)
	blr

/* unsigned long local_irq_disable(void) */
_GLOBAL(local_irq_disable)
	lbz	r3,PACAPROCENABLED(r13)
	li	r4,0
	stb	r4,PACAPROCENABLED(r13)
	blr			/* Done */

/* void local_irq_restore(unsigned long flags) */
_GLOBAL(local_irq_restore)
	lbz	r5,PACAPROCENABLED(r13)
	 /* Check if things are setup the way we want _already_. */
	cmpw	0,r3,r5
	beqlr
	/* are we enabling interrupts? */
	cmpdi	0,r3,0
	stb	r3,PACAPROCENABLED(r13)
	beqlr
	/* Check pending interrupts */
	/*   A decrementer, IPI or PMC interrupt may have occurred
	 *   while we were in the hypervisor (which enables) */
	ld	r4,PACALPPACAPTR(r13)
	ld	r4,LPPACAANYINT(r4)
	cmpdi	r4,0
	beqlr

	/*
	 * Handle pending interrupts in interrupt context
	 */
	li	r0,0x5555
	sc
	blr
