/*
 * arch/arm/mach-sa1100/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for SA1100-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

		.macro  get_irqnr_preamble, base, tmp
		mov	\base, #0xfa000000		@ ICIP = 0xfa050000
		add	\base, \base, #0x00050000
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, [\base]		@ get irqs
		ldr	\irqnr, [\base, #4]		@ ICMR = 0xfa050004
		ands	\irqstat, \irqstat, \irqnr
		mov	\irqnr, #0
		beq	1001f
		tst	\irqstat, #0xff
		moveq	\irqstat, \irqstat, lsr #8
		addeq	\irqnr, \irqnr, #8
		tsteq	\irqstat, #0xff
		moveq	\irqstat, \irqstat, lsr #8
		addeq	\irqnr, \irqnr, #8
		tsteq	\irqstat, #0xff
		moveq	\irqstat, \irqstat, lsr #8
		addeq	\irqnr, \irqnr, #8
		tst	\irqstat, #0x0f
		moveq	\irqstat, \irqstat, lsr #4
		addeq	\irqnr, \irqnr, #4
		tst	\irqstat, #0x03
		moveq	\irqstat, \irqstat, lsr #2
		addeq	\irqnr, \irqnr, #2
		tst	\irqstat, #0x01
		addeqs	\irqnr, \irqnr, #1
1001:
		.endm

