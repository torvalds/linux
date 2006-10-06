/*
 * arch/arm/plat-iop/time.c
 *
 * Timer code for IOP32x and IOP33x based systems
 *
 * Author: Deepak Saxena <dsaxena@mvista.com>
 *
 * Copyright 2002-2003 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

#ifdef CONFIG_ARCH_IOP32X
#define IRQ_IOP3XX_TIMER0	IRQ_IOP32X_TIMER0
#else
#ifdef CONFIG_ARCH_IOP33X
#define IRQ_IOP3XX_TIMER0	IRQ_IOP33X_TIMER0
#endif
#endif

static unsigned long ticks_per_jiffy;
static unsigned long ticks_per_usec;
static unsigned long next_jiffy_time;

unsigned long iop3xx_gettimeoffset(void)
{
	unsigned long offset;

	offset = next_jiffy_time - *IOP3XX_TU_TCR1;

	return offset / ticks_per_usec;
}

static irqreturn_t
iop3xx_timer_interrupt(int irq, void *dev_id)
{
	write_seqlock(&xtime_lock);

	iop3xx_cp6_enable();
	asm volatile("mcr p6, 0, %0, c6, c1, 0" : : "r" (1));
	iop3xx_cp6_disable();

	while ((signed long)(next_jiffy_time - *IOP3XX_TU_TCR1)
							>= ticks_per_jiffy) {
		timer_tick();
		next_jiffy_time -= ticks_per_jiffy;
	}

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction iop3xx_timer_irq = {
	.name		= "IOP3XX Timer Tick",
	.handler	= iop3xx_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
};

void __init iop3xx_init_time(unsigned long tick_rate)
{
	u32 timer_ctl;

	ticks_per_jiffy = (tick_rate + HZ/2) / HZ;
	ticks_per_usec = tick_rate / 1000000;
	next_jiffy_time = 0xffffffff;

	timer_ctl = IOP3XX_TMR_EN | IOP3XX_TMR_PRIVILEGED |
			IOP3XX_TMR_RELOAD | IOP3XX_TMR_RATIO_1_1;

	/*
	 * We use timer 0 for our timer interrupt, and timer 1 as
	 * monotonic counter for tracking missed jiffies.
	 */
	iop3xx_cp6_enable();
	asm volatile("mcr p6, 0, %0, c4, c1, 0" : : "r" (ticks_per_jiffy - 1));
	asm volatile("mcr p6, 0, %0, c0, c1, 0" : : "r" (timer_ctl));
	asm volatile("mcr p6, 0, %0, c5, c1, 0" : : "r" (0xffffffff));
	asm volatile("mcr p6, 0, %0, c1, c1, 0" : : "r" (timer_ctl));
	iop3xx_cp6_disable();

	setup_irq(IRQ_IOP3XX_TIMER0, &iop3xx_timer_irq);
}
