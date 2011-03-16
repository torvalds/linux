/*
 * linux/arch/arm/plat-omap/sram-fn.S
 *
 * Functions that need to be run in internal SRAM
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>
#include <mach/io.h>
#include <mach/hardware.h>

	.text

/*
 * Reprograms ULPD and CKCTL.
 */
	.align	3
ENTRY(omap1_sram_reprogram_clock)
	stmfd	sp!, {r0 - r12, lr}		@ save registers on stack

	mov	r2, #OMAP1_IO_ADDRESS(DPLL_CTL) & 0xff000000
	orr	r2, r2, #OMAP1_IO_ADDRESS(DPLL_CTL) & 0x00ff0000
	orr	r2, r2, #OMAP1_IO_ADDRESS(DPLL_CTL) & 0x0000ff00

	mov	r3, #OMAP1_IO_ADDRESS(ARM_CKCTL) & 0xff000000
	orr	r3, r3, #OMAP1_IO_ADDRESS(ARM_CKCTL) & 0x00ff0000
	orr	r3, r3, #OMAP1_IO_ADDRESS(ARM_CKCTL) & 0x0000ff00

	tst	r0, #1 << 4			@ want lock mode?
	beq	newck				@ nope
	bic	r0, r0, #1 << 4			@ else clear lock bit
	strh	r0, [r2]			@ set dpll into bypass mode
	orr	r0, r0, #1 << 4			@ set lock bit again

newck:
	strh	r1, [r3]			@ write new ckctl value
	strh	r0, [r2]			@ write new dpll value

	mov	r4, #0x0700			@ let the clocks settle
	orr	r4, r4, #0x00ff
delay:	sub	r4, r4, #1
	cmp	r4, #0
	bne	delay

lock:	ldrh	r4, [r2], #0			@ read back dpll value
	tst	r0, #1 << 4			@ want lock mode?
	beq	out				@ nope
	tst	r4, #1 << 0			@ dpll rate locked?
	beq	lock				@ try again

out:
	ldmfd	sp!, {r0 - r12, pc}		@ restore regs and return
ENTRY(omap1_sram_reprogram_clock_sz)
	.word	. - omap1_sram_reprogram_clock
