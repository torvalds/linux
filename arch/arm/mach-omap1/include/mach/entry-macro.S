/*
 * arch/arm/mach-omap1/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for OMAP-based platforms
 *
 * Copyright (C) 2009 Texas Instruments
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/irqs.h>
#include <asm/hardware/gic.h>

#if (defined(CONFIG_ARCH_OMAP730)||defined(CONFIG_ARCH_OMAP850)) && \
	(defined(CONFIG_ARCH_OMAP15XX) || defined(CONFIG_ARCH_OMAP16XX))
#error "FIXME: OMAP7XX doesn't support multiple-OMAP"
#elif defined(CONFIG_ARCH_OMAP730) || defined(CONFIG_ARCH_OMAP850)
#define INT_IH2_IRQ		INT_7XX_IH2_IRQ
#elif defined(CONFIG_ARCH_OMAP15XX)
#define INT_IH2_IRQ		INT_1510_IH2_IRQ
#elif defined(CONFIG_ARCH_OMAP16XX)
#define INT_IH2_IRQ		INT_1610_IH2_IRQ
#else
#warning "IH2 IRQ defaulted"
#define INT_IH2_IRQ		INT_1510_IH2_IRQ
#endif

 		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =OMAP1_IO_ADDRESS(OMAP_IH1_BASE)
		ldr	\irqnr, [\base, #IRQ_ITR_REG_OFFSET]
		ldr	\tmp, [\base, #IRQ_MIR_REG_OFFSET]
		mov	\irqstat, #0xffffffff
		bic	\tmp, \irqstat, \tmp
		tst	\irqnr, \tmp
		beq	1510f

		ldr	\irqnr, [\base, #IRQ_SIR_FIQ_REG_OFFSET]
		cmp	\irqnr, #0
		ldreq	\irqnr, [\base, #IRQ_SIR_IRQ_REG_OFFSET]
		cmpeq	\irqnr, #INT_IH2_IRQ
		ldreq	\base, =OMAP1_IO_ADDRESS(OMAP_IH2_BASE)
		ldreq	\irqnr, [\base, #IRQ_SIR_IRQ_REG_OFFSET]
		addeqs	\irqnr, \irqnr, #32
1510:
		.endm

