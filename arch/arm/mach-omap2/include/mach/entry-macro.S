/*
 * arch/arm/plat-omap/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for OMAP-based platforms
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <mach/hardware.h>
#include <mach/io.h>
#include <mach/irqs.h>
#include <asm/hardware/gic.h>

#include <plat/omap24xx.h>
#include <plat/omap34xx.h>
#include <plat/omap44xx.h>

#include <plat/multi.h>

#define OMAP2_IRQ_BASE		OMAP2_L4_IO_ADDRESS(OMAP24XX_IC_BASE)
#define OMAP3_IRQ_BASE		OMAP2_L4_IO_ADDRESS(OMAP34XX_IC_BASE)
#define OMAP4_IRQ_BASE		OMAP2_L4_IO_ADDRESS(OMAP44XX_GIC_CPU_BASE)
#define INTCPS_SIR_IRQ_OFFSET	0x0040	/* omap2/3 active interrupt offset */
#define	ACTIVEIRQ_MASK		0x7f	/* omap2/3 active interrupt bits */

		.macro	disable_fiq
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

/*
 * Unoptimized irq functions for multi-omap2, 3 and 4
 */

#ifdef MULTI_OMAP2
		/*
		 * Configure the interrupt base on the first interrupt.
		 * See also omap_irq_base_init for setting omap_irq_base.
		 */
		.macro  get_irqnr_preamble, base, tmp
		ldr	\base, =omap_irq_base	@ irq base address
		ldr	\base, [\base, #0]	@ irq base value
		.endm

		/* Check the pending interrupts. Note that base already set */
		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		tst	\base, #0x100		@ gic address?
		bne	4401f			@ found gic

		/* Handle omap2 and omap3 */
		ldr	\irqnr, [\base, #0x98] /* IRQ pending reg 1 */
		cmp	\irqnr, #0x0
		bne	9998f
		ldr	\irqnr, [\base, #0xb8] /* IRQ pending reg 2 */
		cmp	\irqnr, #0x0
		bne	9998f
		ldr	\irqnr, [\base, #0xd8] /* IRQ pending reg 3 */
		cmp	\irqnr, #0x0
		bne	9998f

		/*
		 * ti816x has additional IRQ pending register. Checking this
		 * register on omap2 & omap3 has no effect (read as 0).
		 */
		ldr	\irqnr, [\base, #0xf8] /* IRQ pending reg 4 */
		cmp	\irqnr, #0x0
9998:
		ldrne	\irqnr, [\base, #INTCPS_SIR_IRQ_OFFSET]
		and	\irqnr, \irqnr, #ACTIVEIRQ_MASK /* Clear spurious bits */
		b	9999f

		/* Handle omap4 */
4401:		ldr     \irqstat, [\base, #GIC_CPU_INTACK]
		ldr     \tmp, =1021
		bic     \irqnr, \irqstat, #0x1c00
		cmp     \irqnr, #15
		cmpcc   \irqnr, \irqnr
		cmpne   \irqnr, \tmp
		cmpcs   \irqnr, \irqnr
9999:
		.endm

#ifdef CONFIG_SMP
		/* We assume that irqstat (the raw value of the IRQ acknowledge
		 * register) is preserved from the macro above.
		 * If there is an IPI, we immediately signal end of interrupt
		 * on the controller, since this requires the original irqstat
		 * value which we won't easily be able to recreate later.
		 */

		.macro test_for_ipi, irqnr, irqstat, base, tmp
		bic	\irqnr, \irqstat, #0x1c00
		cmp	\irqnr, #16
		it	cc
		strcc	\irqstat, [\base, #GIC_CPU_EOI]
		it	cs
		cmpcs	\irqnr, \irqnr
		.endm
#endif	/* CONFIG_SMP */

#else	/* MULTI_OMAP2 */


/*
 * Optimized irq functions for omap2, 3 and 4
 */

#if defined(CONFIG_ARCH_OMAP2) || defined(CONFIG_ARCH_OMAP3)
		.macro  get_irqnr_preamble, base, tmp
#ifdef CONFIG_ARCH_OMAP2
		ldr	\base, =OMAP2_IRQ_BASE
#else
		ldr	\base, =OMAP3_IRQ_BASE
#endif
		.endm

		/* Check the pending interrupts. Note that base already set */
		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqnr, [\base, #0x98] /* IRQ pending reg 1 */
		cmp	\irqnr, #0x0
		bne	9999f
		ldr	\irqnr, [\base, #0xb8] /* IRQ pending reg 2 */
		cmp	\irqnr, #0x0
		bne	9999f
		ldr	\irqnr, [\base, #0xd8] /* IRQ pending reg 3 */
		cmp	\irqnr, #0x0
#ifdef CONFIG_SOC_OMAPTI816X
		bne	9999f
		ldr	\irqnr, [\base, #0xf8] /* IRQ pending reg 4 */
		cmp	\irqnr, #0x0
#endif
9999:
		ldrne	\irqnr, [\base, #INTCPS_SIR_IRQ_OFFSET]
		and	\irqnr, \irqnr, #ACTIVEIRQ_MASK /* Clear spurious bits */

		.endm
#endif


#ifdef CONFIG_ARCH_OMAP4
#define HAVE_GET_IRQNR_PREAMBLE
#include <asm/hardware/entry-macro-gic.S>

		.macro  get_irqnr_preamble, base, tmp
		ldr     \base, =OMAP4_IRQ_BASE
		.endm

#endif

#endif	/* MULTI_OMAP2 */
