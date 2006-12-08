/*
 * arch/arm/mach-iop13xx/time.c
 *
 * Timer code for IOP13xx (copied from IOP32x/IOP33x implementation)
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
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>

static unsigned long ticks_per_jiffy;
static unsigned long ticks_per_usec;
static unsigned long next_jiffy_time;

static inline u32 read_tcr1(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c3, c9, 0" : "=r" (val));
	return val;
}

unsigned long iop13xx_gettimeoffset(void)
{
	unsigned long offset;
	u32 cp_flags;

	cp_flags = iop13xx_cp6_save();
	offset = next_jiffy_time - read_tcr1();
	iop13xx_cp6_restore(cp_flags);

	return offset / ticks_per_usec;
}

static irqreturn_t
iop13xx_timer_interrupt(int irq, void *dev_id)
{
	u32 cp_flags = iop13xx_cp6_save();

	write_seqlock(&xtime_lock);

	asm volatile("mcr p6, 0, %0, c6, c9, 0" : : "r" (1));

	while ((signed long)(next_jiffy_time - read_tcr1())
							>= ticks_per_jiffy) {
		timer_tick();
		next_jiffy_time -= ticks_per_jiffy;
	}

	write_sequnlock(&xtime_lock);

	iop13xx_cp6_restore(cp_flags);

	return IRQ_HANDLED;
}

static struct irqaction iop13xx_timer_irq = {
	.name		= "IOP13XX Timer Tick",
	.handler	= iop13xx_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
};

void __init iop13xx_init_time(unsigned long tick_rate)
{
	u32 timer_ctl;
	u32 cp_flags;

	ticks_per_jiffy = (tick_rate + HZ/2) / HZ;
	ticks_per_usec = tick_rate / 1000000;
	next_jiffy_time = 0xffffffff;

	timer_ctl = IOP13XX_TMR_EN | IOP13XX_TMR_PRIVILEGED |
			IOP13XX_TMR_RELOAD | IOP13XX_TMR_RATIO_1_1;

	/*
	 * We use timer 0 for our timer interrupt, and timer 1 as
	 * monotonic counter for tracking missed jiffies.
	 */
	cp_flags = iop13xx_cp6_save();
	asm volatile("mcr p6, 0, %0, c4, c9, 0" : : "r" (ticks_per_jiffy - 1));
	asm volatile("mcr p6, 0, %0, c0, c9, 0" : : "r" (timer_ctl));
	asm volatile("mcr p6, 0, %0, c5, c9, 0" : : "r" (0xffffffff));
	asm volatile("mcr p6, 0, %0, c1, c9, 0" : : "r" (timer_ctl));
	iop13xx_cp6_restore(cp_flags);

	setup_irq(IRQ_IOP13XX_TIMER0, &iop13xx_timer_irq);
}
