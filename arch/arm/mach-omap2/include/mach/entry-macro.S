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
 * We use __glue to avoid errors with multiple definitions of
 * .globl omap_irq_base as it's included from entry-armv.S but not
 * from entry-common.S.
 */
#ifdef __glue
		.pushsection .data
		.globl	omap_irq_base
omap_irq_base:
		.word	0
		.popsection
#endif

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
9998:
		ldrne	\irqnr, [\base, #INTCPS_SIR_IRQ_OFFSET]
		and	\irqnr, \irqnr, #ACTIVEIRQ_MASK /* Clear spurious bits */
		b	9999f

		/* Handle omap4 */
4401:		ldr     \irqstat, [\base, #GIC_CPU_INTACK]
		ldr     \tmp, =1021
		bic     \irqnr, \irqstat, #0x1c00
		cmp     \irqnr, #29
		cmpcc   \irqnr, \irqnr
		cmpne   \irqnr, \tmp
		cmpcs   \irqnr, \irqnr
9999:
		.endm


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
9999:
		ldrne	\irqnr, [\base, #INTCPS_SIR_IRQ_OFFSET]
		and	\irqnr, \irqnr, #ACTIVEIRQ_MASK /* Clear spurious bits */

		.endm
#endif


#ifdef CONFIG_ARCH_OMAP4

		.macro  get_irqnr_preamble, base, tmp
		ldr     \base, =OMAP4_IRQ_BASE
		.endm

		/*
		 * The interrupt numbering scheme is defined in the
		 * interrupt controller spec.  To wit:
		 *
		 * Interrupts 0-15 are IPI
		 * 16-28 are reserved
		 * 29-31 are local.  We allow 30 to be used for the watchdog.
		 * 32-1020 are global
		 * 1021-1022 are reserved
		 * 1023 is "spurious" (no interrupt)
		 *
		 * For now, we ignore all local interrupts so only return an
		 * interrupt if it's between 30 and 1020.  The test_for_ipi
		 * routine below will pick up on IPIs.
		 * A simple read from the controller will tell us the number
		 * of the highest priority enabled interrupt.
		 * We then just need to check whether it is in the
		 * valid range for an IRQ (30-1020 inclusive).
		 */
		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr     \irqstat, [\base, #GIC_CPU_INTACK]

		ldr     \tmp, =1021

		bic     \irqnr, \irqstat, #0x1c00

		cmp     \irqnr, #29
		cmpcc   \irqnr, \irqnr
		cmpne   \irqnr, \tmp
		cmpcs   \irqnr, \irqnr
		.endm
#endif
#endif	/* MULTI_OMAP2 */

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

		/* As above, this assumes that irqstat and base are preserved */

		.macro test_for_ltirq, irqnr, irqstat, base, tmp
		bic	\irqnr, \irqstat, #0x1c00
		mov 	\tmp, #0
		cmp	\irqnr, #29
		itt	eq
		moveq	\tmp, #1
		streq	\irqstat, [\base, #GIC_CPU_EOI]
		cmp	\tmp, #0
		.endm
#endif	/* CONFIG_SMP */

		.macro	irq_prio_table
		.endm
