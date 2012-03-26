/*
 * arch/arm/mach-integrator/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for Integrator platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/irqs.h>

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
/* FIXME: should not be using soo many LDRs here */
		ldr	\base, =IO_ADDRESS(INTEGRATOR_IC_BASE)
		mov	\irqnr, #IRQ_PIC_START
		ldr	\irqstat, [\base, #IRQ_STATUS]		@ get masked status
		ldr	\base, =IO_ADDRESS(INTEGRATOR_HDR_BASE)
		teq	\irqstat, #0
		ldreq	\irqstat, [\base, #(INTEGRATOR_HDR_IC_OFFSET+IRQ_STATUS)]
		moveq	\irqnr, #IRQ_CIC_START

1001:		tst	\irqstat, #15
		bne	1002f
		add	\irqnr, \irqnr, #4
		movs	\irqstat, \irqstat, lsr #4
		bne	1001b
1002:		tst	\irqstat, #1
		bne	1003f
		add	\irqnr, \irqnr, #1
		movs	\irqstat, \irqstat, lsr #1
		bne	1002b
1003:		/* EQ will be set if no irqs pending */
		.endm

