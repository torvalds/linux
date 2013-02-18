/* arch/arm/mach-exynos/include/mach/entry-macro.S
 *
 * Cloned from arch/arm/mach-realview/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for EXYNOS4 platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
*/

#include <mach/hardware.h>
#include <asm/hardware/gic.h>
#include <mach/map.h>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
#ifdef CONFIG_ARCH_EXYNOS4
		mov	\tmp, #0
		mrc	p15, 0, \base, c0, c0, 5
		and	\base, \base, #3
		cmp	\base, #0
		beq	1f
		ldr	\tmp, =gic_bank_offset
		ldr	\tmp, [\tmp]
		cmp	\base, #1
		beq	1f
		cmp	\base, #2
		addeq	\tmp, \tmp, \tmp
		addne	\tmp, \tmp, \tmp, LSL #1
#endif
1:		ldr	\base, =gic_cpu_base_addr
		ldr	\base, [\base]

#ifdef CONFIG_ARCH_EXYNOS4
		add	\base, \base, \tmp
#endif
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
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
		 * For now, we ignore all local interrupts so only return an interrupt if it's
		 * between 30 and 1020.  The test_for_ipi routine below will pick up on IPIs.
		 *
		 * A simple read from the controller will tell us the number of the highest
                 * priority enabled interrupt.  We then just need to check whether it is in the
		 * valid range for an IRQ (30-1020 inclusive).
		 */

		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp

		ldr     \irqstat, [\base, #GIC_CPU_INTACK] /* bits 12-10 = src CPU, 9-0 = int # */

		ldr	\tmp, =1021

		bic     \irqnr, \irqstat, #0x1c00

		cmp     \irqnr, #29
		cmpcc	\irqnr, \irqnr
		cmpne	\irqnr, \tmp
		cmpcs	\irqnr, \irqnr
		addne	\irqnr, \irqnr, #32
#if defined(CONFIG_ARM_TRUSTZONE)
		bne	101f

		mrc	p15, 0, \tmp, c0, c0, 5
		and	\tmp, \tmp, #0x3
		cmp	\tmp, #1
		cmpcs	\tmp, \tmp
		beq	101f

		cmp	\irqnr, #7
		cmpcc	\irqnr, \irqnr
		cmpne	\irqnr, #16
		cmpcs	\irqnr, \irqnr
		addne	\irqnr, \irqnr, #32
101:
#endif

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
		bic     \irqnr, \irqstat, #0x1c00
		mov     \tmp, #0
		cmp     \irqnr, #28
		moveq   \tmp, #1
		streq   \irqstat, [\base, #GIC_CPU_EOI]
		cmp     \tmp, #0
		.endm
