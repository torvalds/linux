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
	generic_handle_irq((int)desc->irq_data.handler_data);
}

/* We assume the IRQ_TIMER0..IRQ_TIMER4 range is continuous. */

static void s3c_irq_timer_mask(struct irq_data *data)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);
	u32 mask = (u32)data->chip_data;

	reg &= 0x1f;  /* mask out pending interrupts */
	reg &= ~mask;
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static void s3c_irq_timer_unmask(struct irq_data *data)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);
	u32 mask = (u32)data->chip_data;

	reg &= 0x1f;  /* mask out pending interrupts */
	reg |= mask;
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static void s3c_irq_timer_ack(struct irq_data *data)
{
	u32 reg = __raw_readl(S3C64XX_TINT_CSTAT);
	u32 mask = (u32)data->chip_data;

	reg &= 0x1f;
	reg |= mask << 5;
	__raw_writel(reg, S3C64XX_TINT_CSTAT);
}

static struct irq_chip s3c_irq_timer = {
	.name		= "s3c-timer",
	.irq_mask	= s3c_irq_timer_mask,
	.irq_unmask	= s3c_irq_timer_unmask,
	.irq_ack	= s3c_irq_timer_ack,
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

	irq_set_chained_handler(parent_irq, s3c_irq_demux_vic_timer);
	irq_set_handler_data(parent_irq, (void *)timer_irq);

	irq_set_chip_and_handler(timer_irq, &s3c_irq_timer, handle_level_irq);
	irq_set_chip_data(timer_irq, (void *)(1 << (timer_irq - IRQ_TIMER0)));
	set_irq_flags(timer_irq, IRQF_VALID);
}
