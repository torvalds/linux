/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999, 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Interrupt exception dispatch code.
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

	.text
	.set	noreorder
	.align	5
	NESTED(lasatIRQ, PT_SIZE, sp)
	.set	noat
	SAVE_ALL
	CLI
	.set	at
	.set	noreorder

	mfc0	s0, CP0_CAUSE		# get irq mask

	/* First we check for r4k counter/timer IRQ. */
	andi	a0, s0, CAUSEF_IP7
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP2	# delay slot, check hw0 interrupt

	/* Wheee, a timer interrupt. */
	li	a0, 7
	jal	ll_timer_interrupt
	 move	a1, sp

	j	ret_from_irq
	 nop

1:
	/* Wheee, combined hardware level zero interrupt. */
	jal	lasat_hw0_irqdispatch
	 move	a0, sp			# delay slot

	j	ret_from_irq
	 nop				# delay slot

1:
	/*
	 * Here by mistake?  This is possible, what can happen is that by the
	 * time we take the exception the IRQ pin goes low, so just leave if
	 * this is the case.
	 */
	move	a1,s0
	mfc0	a1, CP0_EPC

	j	ret_from_irq
	 nop
	END(lasatIRQ)
