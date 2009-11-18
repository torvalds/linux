/*
 * arch/arm/mach-pxa/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for PXA-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#include <mach/irqs.h>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mrc	p15, 0, \tmp, c0, c0, 0		@ CPUID
		mov	\tmp, \tmp, lsr #13
		and	\tmp, \tmp, #0x7		@ Core G
		cmp	\tmp, #1
		bhi	1002f

		@ Core Generation 1 (PXA25x)
		mov	\base, #io_p2v(0x40000000)	@ IIR Ctl = 0x40d00000
		add	\base, \base, #0x00d00000
		ldr	\irqstat, [\base, #0]		@ ICIP
		ldr	\irqnr, [\base, #4]		@ ICMR

		ands	\irqnr, \irqstat, \irqnr
		beq	1001f
		rsb	\irqstat, \irqnr, #0
		and	\irqstat, \irqstat, \irqnr
		clz	\irqnr, \irqstat
		rsb	\irqnr, \irqnr, #(31 + PXA_IRQ(0))
		b	1001f
1002:
		@ Core Generation 2 (PXA27x) or Core Generation 3 (PXA3xx)
		mrc	p6, 0, \irqstat, c5, c0, 0	@ ICHP
		tst	\irqstat, #0x80000000
		beq	1001f
		bic	\irqstat, \irqstat, #0x80000000
		mov	\irqnr, \irqstat, lsr #16
1001:
		.endm
