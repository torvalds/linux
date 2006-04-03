/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999, 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
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
 * ########################################################################
 *
 * Interrupt exception dispatch code.
 *
 */
#include <linux/config.h>

#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/mips-boards/maltaint.h>

/*
 * IRQs on the Malta board look basically (barring software IRQs which we
 * don't use at all and all external interrupt sources are combined together
 * on hardware interrupt 0 (MIPS IRQ 2)) like:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        Combined hardware interrupt (hw0)
 *             3        Hardware (ignored)
 *             4        Hardware (ignored)
 *             5        Hardware (ignored)
 *             6        Hardware (ignored)
 *             7        R4k timer (what we use)
 *
 * We handle the IRQ according to _our_ priority which is:
 *
 * Highest ----     R4k Timer
 * Lowest  ----     Combined hardware interrupt
 *
 * then we just return, if multiple IRQs are pending then we will just take
 * another exception, big deal.
 */

	.text
	.set	noreorder
	.set	noat
	.align	5
	NESTED(mipsIRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI
	.set	at

	mfc0	s0, CP0_CAUSE		# get irq bits
	mfc0	s1, CP0_STATUS		# get irq mask
	andi	s0, ST0_IM		# CAUSE.CE may be non-zero!
	and	s0, s1

#if defined(CONFIG_CPU_MIPS32) || defined(CONFIG_CPU_MIPS64)
 	.set	mips32
	clz	a0, s0
	.set	mips0
	negu	a0
	addu	a0, 31-CAUSEB_IP
	bltz	a0, spurious
#else
	beqz	s0, spurious
	 li	a0, 7

	and	t0, s0, 0xf000
	sltiu	t0, t0, 1
	sll	t0, 2
	subu	a0, t0
	sll	s0, t0

	and	t0, s0, 0xc000
	sltiu	t0, t0, 1
	sll	t0, 1
	subu	a0, t0
	sll	s0, t0

	and	t0, s0, 0x8000
	sltiu	t0, t0, 1
	# sll	t0, 0
	subu	a0, t0
	# sll	s0, t0
#endif

	 li	a1, MIPSCPU_INT_I8259A
	bne	a0, a1, 1f
	 addu	a0, MIPSCPU_INT_BASE

	jal	malta_hw0_irqdispatch
	 move	 a0, sp

	j	ret_from_irq
	 nop
1:

	jal	do_IRQ
	 move	a1, sp

	j	ret_from_irq
	 nop

spurious:
	j	spurious_interrupt
	 nop
	END(mipsIRQ)
