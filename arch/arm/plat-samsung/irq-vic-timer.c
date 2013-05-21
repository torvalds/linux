/* arch/arm/plat-samsung/irq-vic-timer.c
 *	originally part of arch/arm/plat-s3c64xx/irq.c
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - Interrupt handling
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/io.h>

#include <mach/map.h>
#include <mach/irqs.h>
#include <plat/cpu.h>
#include <plat/irq-vic-timer.h>
#include <plat/regs-timer.h>

static void s3c_irq_demux_vic_timer(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip *chip = irq_get_chip(irq);
	chained_irq_enter(chip, desc);
	generic_handle_irq((int)desc->irq_data.handler_data);
	chained_irq_exit(chip, desc);
}

/* We assume the IRQ_TIMER0..IRQ_TIMER4 range is continuous. */
static void s3c_irq_timer_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	u32 mask = (1 << 5) << (d->irq - gc->irq_base);

	irq_reg_writel(mask | gc->mask_cache, gc->reg_base);
}

/**
 * s3c_init_vic_timer_irq() - initialise timer irq chanined off VIC.\
 * @num: Number of timers to initialize
 * @timer_irq: Base IRQ number to be used for the timers.
 *
 * Register the necessary IRQ chaining and support for the timer IRQs
 * chained of the VIC.
 */
void __init s3c_init_vic_timer_irq(unsigned int num, unsigned int timer_irq)
{
	unsigned int pirq[5] = { IRQ_TIMER0_VIC, IRQ_TIMER1_VIC, IRQ_TIMER2_VIC,
				 IRQ_TIMER3_VIC, IRQ_TIMER4_VIC };
	struct irq_chip_generic *s3c_tgc;
	struct irq_chip_type *ct;
	unsigned int i;

#ifdef CONFIG_ARCH_EXYNOS
	if (soc_is_exynos5250()) {
		pirq[0] = EXYNOS5_IRQ_TIMER0_VIC;
		pirq[1] = EXYNOS5_IRQ_TIMER1_VIC;
		pirq[2] = EXYNOS5_IRQ_TIMER2_VIC;
		pirq[3] = EXYNOS5_IRQ_TIMER3_VIC;
		pirq[4] = EXYNOS5_IRQ_TIMER4_VIC;
	} else {
		pirq[0] = EXYNOS4_IRQ_TIMER0_VIC;
		pirq[1] = EXYNOS4_IRQ_TIMER1_VIC;
		pirq[2] = EXYNOS4_IRQ_TIMER2_VIC;
		pirq[3] = EXYNOS4_IRQ_TIMER3_VIC;
		pirq[4] = EXYNOS4_IRQ_TIMER4_VIC;
	}
#endif
	s3c_tgc = irq_alloc_generic_chip("s3c-timer", 1, timer_irq,
					 S3C64XX_TINT_CSTAT, handle_level_irq);

	if (!s3c_tgc) {
		pr_err("%s: irq_alloc_generic_chip for IRQ %d failed\n",
		       __func__, timer_irq);
		return;
	}

	ct = s3c_tgc->chip_types;
	ct->chip.irq_mask = irq_gc_mask_clr_bit;
	ct->chip.irq_unmask = irq_gc_mask_set_bit;
	ct->chip.irq_ack = s3c_irq_timer_ack;
	irq_setup_generic_chip(s3c_tgc, IRQ_MSK(num), IRQ_GC_INIT_MASK_CACHE,
			       IRQ_NOREQUEST | IRQ_NOPROBE, 0);
	/* Clear the upper bits of the mask_cache*/
	s3c_tgc->mask_cache &= 0x1f;

	for (i = 0; i < num; i++, timer_irq++) {
		irq_set_chained_handler(pirq[i], s3c_irq_demux_vic_timer);
		irq_set_handler_data(pirq[i], (void *)timer_irq);
	}
}
