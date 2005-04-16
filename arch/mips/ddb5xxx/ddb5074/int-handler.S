/*
 *  arch/mips/ddb5074/int-handler.S -- NEC DDB Vrc-5074 interrupt handler
 *
 *  Based on arch/mips/sgi/kernel/indyIRQ.S
 *
 *  Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
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
 * Furthermore, the IRQs on the INDY look basically (barring software IRQs
 * which we don't use at all) like:
 *
 *	MIPS IRQ	Source
 *      --------        ------
 *             0	Software (ignored)
 *             1        Software (ignored)
 *             2        Local IRQ level zero
 *             3        Local IRQ level one
 *             4        8254 Timer zero
 *             5        8254 Timer one
 *             6        Bus Error
 *             7        R4k timer (what we use)
 *
 * We handle the IRQ according to _our_ priority which is:
 *
 * Highest ----     R4k Timer
 *                  Local IRQ zero
 *                  Local IRQ one
 *                  Bus Error
 *                  8254 Timer zero
 * Lowest  ----     8254 Timer one
 *
 * then we just return, if multiple IRQs are pending then we will just take
 * another exception, big deal.
 */

	.text
	.set	noreorder
	.set	noat
	.align	5
	NESTED(ddbIRQ, PT_SIZE, sp)
	SAVE_ALL
	CLI
	.set	at
	mfc0	s0, CP0_CAUSE		# get irq mask

#if 1
	mfc0	t2,CP0_STATUS		# get enabled interrupts
	and	s0,t2			# isolate allowed ones
#endif
	/* First we check for r4k counter/timer IRQ. */
	andi	a0, s0, CAUSEF_IP2	# delay slot, check local level zero
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP3	# delay slot, check local level one

	/* Wheee, local level zero interrupt. */
	jal	ddb_local0_irqdispatch
	 move	a0, sp			# delay slot

	j	ret_from_irq
	 nop				# delay slot

1:
	beq	a0, zero, 1f
	 andi	a0, s0, CAUSEF_IP6	# delay slot, check bus error

	/* Wheee, local level one interrupt. */
	move	a0, sp
	jal	ddb_local1_irqdispatch
	 nop

	j	ret_from_irq
	 nop

1:
	beq	a0, zero, 1f
	 nop

	/* Wheee, an asynchronous bus error... */
	move	a0, sp
	jal	ddb_buserror_irq
	 nop

	j	ret_from_irq
	 nop

1:
	/* Here by mistake?  This is possible, what can happen
	 * is that by the time we take the exception the IRQ
	 * pin goes low, so just leave if this is the case.
	 */
	andi	a0, s0, (CAUSEF_IP4 | CAUSEF_IP5)
	beq	a0, zero, 1f

	/* Must be one of the 8254 timers... */
	move	a0, sp
	jal	ddb_8254timer_irq
	 nop
1:
	j	ret_from_irq
	 nop
	END(ddbIRQ)
