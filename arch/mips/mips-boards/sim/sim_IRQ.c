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
#include <linux/config.h>

#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

/* A lot of complication here is taken away because:
 *
 * 1) We handle one interrupt and return, sitting in a loop and moving across
 *    all the pending IRQ bits in the cause register is _NOT_ the answer, the
 *    common case is one pending IRQ so optimize in that direction.
 *
 * 2) We need not check against bits in the status register IRQ mask, that
 *    would make this routine slow as hell.
 *
 * 3) Linux only thinks in terms of all IRQs on or all IRQs off, nothing in
 *    between like BSD spl() brain-damage.
 *
 * Furthermore, the IRQs on the MIPS board look basically (barring software
 * IRQs which we don't use at all and all external interrupt sources are
 * combined together on hardware interrupt 0 (MIPS IRQ 2)) like:
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
 * Note: On the SEAD board thing are a little bit different.
 *       Here IRQ 2 (hw0) is wired to the UART0 and IRQ 3 (hw1) is wired
 *       wired to UART1.
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
	and	s0, s1

	/* First we check for r4k counter/timer IRQ. */
	andi	a0, s0, CAUSEF_IP7
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP2	# delay slot, check hw0 interrupt

	/* Wheee, a timer interrupt. */
	move	a0, sp
	jal	mips_timer_interrupt
	 nop

	j	ret_from_irq
	 nop

1:
#if defined(CONFIG_MIPS_SEAD)
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP3	# delay slot, check hw1 interrupt
#else
	beq	a0, zero, 1f		# delay slot, check hw3 interrupt
	 andi	a0, s0, CAUSEF_IP5
#endif

	/* Wheee, combined hardware level zero interrupt. */
#if defined(CONFIG_MIPS_ATLAS)
	jal	atlas_hw0_irqdispatch
#elif defined(CONFIG_MIPS_MALTA)
	jal	malta_hw0_irqdispatch
#elif defined(CONFIG_MIPS_SEAD)
	jal	sead_hw0_irqdispatch
#else
#error "MIPS board not supported\n"
#endif
	 move	a0, sp			# delay slot

	j	ret_from_irq
	 nop				# delay slot

1:
#if defined(CONFIG_MIPS_SEAD)
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP5	# delay slot, check hw3 interrupt
	jal	sead_hw1_irqdispatch
	 move	a0, sp			# delay slot
	j	ret_from_irq
	 nop				# delay slot
1:
#endif
#if defined(CONFIG_MIPS_MALTA)
	beq	a0, zero, 1f            # check hw3 (coreHI) interrupt
	 nop
	jal	corehi_irqdispatch
	 move	a0, sp
	j	ret_from_irq
	 nop
1:
#endif
	/*
	 * Here by mistake?  This is possible, what can happen is that by the
	 * time we take the exception the IRQ pin goes low, so just leave if
	 * this is the case.
	 */
	move	a1,s0
	PRINT("Got interrupt: c0_cause = %08x\n")
	mfc0	a1, CP0_EPC
	PRINT("c0_epc = %08x\n")

	j	ret_from_irq
	 nop
	END(mipsIRQ)
