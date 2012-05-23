/*
 * arch/arm/mach-versatile/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for Versatile platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/hardware/vic.h>

		.macro getioaddr, rp, rv
		ldr	\rp, =SW_IO_PHYS
		ldr	\rv, =SW_IO_VIRT
		.endm

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		ldr	\base, =SW_VA_INT_IO_BASE
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, [\base, #0x08]	@ get pending status
		bic     \irqstat, \irqstat, #1
		mov	\irqnr, #0
		teq	\irqstat, #0
		beq	1003f

1001:		tst	\irqstat, #15
		bne	1002f
		add	\irqnr, \irqnr, #4
		movs	\irqstat, \irqstat, lsr #4
		bne	1001b
1002:		tst	\irqstat, #1
		bne	3000f
		add	\irqnr, \irqnr, #1
		movs	\irqstat, \irqstat, lsr #1
		bne	1002b
1003:		/* EQ will be set if no irqs pending */

		ldr	\irqstat, [\base, #0x0c]	@ get pending status
		teq	\irqstat, #0
		beq	3000f

		mov	\irqnr, #32
2001:		tst	\irqstat, #15
		bne	2002f
		add	\irqnr, \irqnr, #4
		movs	\irqstat, \irqstat, lsr #4
		bne	2001b
2002:		tst	\irqstat, #1
		bne	3000f
		add	\irqnr, \irqnr, #1
		movs	\irqstat, \irqstat, lsr #1
		bne	2002b

3000:
		.endm

