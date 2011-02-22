/*
 * Low-level IRQ helper macros
 *
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <mach/hardware.h>
#include <asm/hardware/gic.h>

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =gic_cpu_base_addr
	ldr	\base, [\base]
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	/*
	 * The interrupt numbering scheme is defined in the
	 * interrupt controller spec.  To wit:
	 *
	 * Migrated the code from ARM MP port to be more consistent
	 * with interrupt processing , the following still holds true
	 * however, all interrupts are treated the same regardless of
	 * if they are local IPI or PPI
	 *
	 * Interrupts 0-15 are IPI
	 * 16-31 are PPI
	 *   (16-18 are the timers)
	 * 32-1020 are global
	 * 1021-1022 are reserved
	 * 1023 is "spurious" (no interrupt)
	 *
	 * A simple read from the controller will tell us the number of the
	 * highest priority enabled interrupt.  We then just need to check
	 * whether it is in the valid range for an IRQ (0-1020 inclusive).
	 *
	 * Base ARM code assumes that the local (private) peripheral interrupts
	 * are not valid, we treat them differently, in that the privates are
	 * handled like normal shared interrupts with the exception that only
	 * one processor can register the interrupt and the handler must be
	 * the same for all processors.
	 */

	.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp

	ldr  \irqstat, [\base, #GIC_CPU_INTACK] /* bits 12-10 =srcCPU,
						   9-0 =int # */

	bic     \irqnr, \irqstat, #0x1c00	@mask src
	cmp     \irqnr, #15
	ldr		\tmp, =1021
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
    bic \irqnr, \irqstat, #0x1c00
    cmp \irqnr, #16
    strcc   \irqstat, [\base, #GIC_CPU_EOI]
    cmpcs   \irqnr, \irqnr
	.endm

	/* As above, this assumes that irqstat and base are preserved.. */

	.macro test_for_ltirq, irqnr, irqstat, base, tmp
    bic \irqnr, \irqstat, #0x1c00
    mov     \tmp, #0
    cmp \irqnr, #16
    moveq   \tmp, #1
    streq   \irqstat, [\base, #GIC_CPU_EOI]
    cmp \tmp, #0
	.endm
