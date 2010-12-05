/*
 * arch/arm/include/asm/hardware/entry-macro-gic.S
 *
 * Low-level IRQ helper macros for GIC
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/hardware/gic.h>

#ifndef HAVE_GET_IRQNR_PREAMBLE
	.macro	get_irqnr_preamble, base, tmp
	ldr	\base, =gic_cpu_base_addr
	ldr	\base, [\base]
	.endm
#endif

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
 * For now, we ignore all local interrupts so only return an interrupt if it's
 * between 30 and 1020.  The test_for_ipi routine below will pick up on IPIs.
 *
 * A simple read from the controller will tell us the number of the highest
 * priority enabled interrupt.  We then just need to check whether it is in the
 * valid range for an IRQ (30-1020 inclusive).
 */

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp

	ldr     \irqstat, [\base, #GIC_CPU_INTACK]
	/* bits 12-10 = src CPU, 9-0 = int # */

	ldr	\tmp, =1021
	bic     \irqnr, \irqstat, #0x1c00
	cmp     \irqnr, #29
	cmpcc	\irqnr, \irqnr
	cmpne	\irqnr, \tmp
	cmpcs	\irqnr, \irqnr
	.endm

/* We assume that irqstat (the raw value of the IRQ acknowledge
 * register) is preserved from the macro above.
 * If there is an IPI, we immediately signal end of interrupt on the
 * controller, since this requires the original irqstat value which
 * we won't easily be able to recreate later.
 */

	.macro test_for_ipi, irqnr, irqstat, base, tmp
	bic	\irqnr, \irqstat, #0x1c00
	cmp	\irqnr, #16
	strcc	\irqstat, [\base, #GIC_CPU_EOI]
	cmpcs	\irqnr, \irqnr
	.endm

/* As above, this assumes that irqstat and base are preserved.. */

	.macro test_for_ltirq, irqnr, irqstat, base, tmp
	bic	\irqnr, \irqstat, #0x1c00
	mov 	\tmp, #0
	cmp	\irqnr, #29
	moveq	\tmp, #1
	streq	\irqstat, [\base, #GIC_CPU_EOI]
	cmp	\tmp, #0
	.endm
