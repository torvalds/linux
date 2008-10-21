/* arch/arm/plat-s3c64xx/irq.c
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

#include <asm/hardware/vic.h>

#include <mach/map.h>
#include <plat/regs-timer.h>
#include <plat/cpu.h>

/* Timer interrupt handling */

static void s3c_irq_demux_timer(unsigned int base_irq, unsigned int sub_irq)
{
	generic_handle_irq(sub_irq);
}

static void s3c_irq_demux_timer0(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER0);
}

static void s3c_irq_demux_timer1(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER1);
}

static void s3c_irq_demux_timer2(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER2);
}

static void s3c_irq_demux_timer3(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER3);
}

static void s3c_irq_demux_timer4(unsigned int irq, struct irq_desc *desc)
{
	s3c_irq_demux_timer(irq, IRQ_TIMER4);
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

void __init s3c64xx_init_irq(u32 vic0_valid, u32 vic1_valid)
{
	int irq;

	printk(KERN_INFO "%s: initialising interrupts\n", __func__);

	/* initialise the pair of VICs */
	vic_init(S3C_VA_VIC0, S3C_VIC0_BASE, vic0_valid);
	vic_init(S3C_VA_VIC1, S3C_VIC1_BASE, vic1_valid);

	/* add the timer sub-irqs */

	set_irq_chained_handler(IRQ_TIMER0_VIC, s3c_irq_demux_timer0);
	set_irq_chained_handler(IRQ_TIMER1_VIC, s3c_irq_demux_timer1);
	set_irq_chained_handler(IRQ_TIMER2_VIC, s3c_irq_demux_timer2);
	set_irq_chained_handler(IRQ_TIMER3_VIC, s3c_irq_demux_timer3);
	set_irq_chained_handler(IRQ_TIMER4_VIC, s3c_irq_demux_timer4);

	for (irq = IRQ_TIMER0; irq <= IRQ_TIMER4; irq++) {
		set_irq_chip(irq, &s3c_irq_timer);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
}


