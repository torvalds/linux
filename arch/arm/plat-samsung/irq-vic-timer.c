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
#include <linux/io.h>

#include <mach/map.h>
#include <plat/irq-vic-timer.h>
#include <plat/regs-timer.h>

static void s3c_irq_demux_vic_timer(unsigned int irq, struct irq_desc *desc)
{
	generic_handle_irq((int)desc->handler_data);
}

/* We assume the IRQ_TIMER0..IRQ_TIMER4 range is continuous. */

static void s3c_irq_timer_mask(unsigned int irq)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);

	reg &= 0x1f;  /* mask out pending interrupts */
	reg &= ~(1 << (irq - IRQ_TIMER0));
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static void s3c_irq_timer_unmask(unsigned int irq)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);

	reg &= 0x1f;  /* mask out pending interrupts */
	reg |= 1 << (irq - IRQ_TIMER0);
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static void s3c_irq_timer_ack(unsigned int irq)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);

	reg &= 0x1f;
	reg |= (1 << 5) << (irq - IRQ_TIMER0);
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static struct irq_chip s3c_irq_timer = {
	.name		= "s3c-timer",
	.mask		= s3c_irq_timer_mask,
	.unmask		= s3c_irq_timer_unmask,
	.ack		= s3c_irq_timer_ack,
};

/**
 * s3c_init_vic_timer_irq() - initialise timer irq chanined off VIC.\
 * @parent_irq: The parent IRQ on the VIC for the timer.
 * @timer_irq: The IRQ to be used for the timer.
 *
 * Register the necessary IRQ chaining and support for the timer IRQs
 * chained of the VIC.
 */
void __init s3c_init_vic_timer_irq(unsigned int parent_irq,
				   unsigned int timer_irq)
{
	struct irq_desc *desc = irq_to_desc(parent_irq);

	set_irq_chained_handler(parent_irq, s3c_irq_demux_vic_timer);

	set_irq_chip(timer_irq, &s3c_irq_timer);
	set_irq_handler(timer_irq, handle_level_irq);
	set_irq_flags(timer_irq, IRQF_VALID);

	desc->handler_data = (void *)timer_irq;
}
