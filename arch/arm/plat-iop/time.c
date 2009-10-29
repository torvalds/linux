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
#include <linux/io.h>
#include <linux/clocksource.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <mach/time.h>

/*
 * IOP clocksource (free-running timer 1).
 */
static cycle_t iop_clocksource_read(struct clocksource *unused)
{
	return 0xffffffffu - read_tcr1();
}

static struct clocksource iop_clocksource = {
	.name 		= "iop_timer1",
	.rating		= 300,
	.read		= iop_clocksource_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init iop_clocksource_set_hz(struct clocksource *cs, unsigned int hz)
{
	u64 temp;
	u32 shift;

	/* Find shift and mult values for hz. */
	shift = 32;
	do {
		temp = (u64) NSEC_PER_SEC << shift;
		do_div(temp, hz);
		if ((temp >> 32) == 0)
			break;
	} while (--shift != 0);

	cs->shift = shift;
	cs->mult = (u32) temp;

	printk(KERN_INFO "clocksource: %s uses shift %u mult %#x\n",
	       cs->name, cs->shift, cs->mult);
}

static unsigned long ticks_per_jiffy;
static unsigned long ticks_per_usec;
static unsigned long next_jiffy_time;

unsigned long iop_gettimeoffset(void)
{
	unsigned long offset, temp;

	/* enable cp6, if necessary, to avoid taking the overhead of an
	 * undefined instruction trap
	 */
	asm volatile (
	"mrc	p15, 0, %0, c15, c1, 0\n\t"
	"tst	%0, #(1 << 6)\n\t"
	"orreq	%0, %0, #(1 << 6)\n\t"
	"mcreq	p15, 0, %0, c15, c1, 0\n\t"
#ifdef CONFIG_CPU_XSCALE
	"mrceq	p15, 0, %0, c15, c1, 0\n\t"
	"moveq	%0, %0\n\t"
	"subeq	pc, pc, #4\n\t"
#endif
	: "=r"(temp) : : "cc");

	offset = next_jiffy_time - read_tcr1();

	return offset / ticks_per_usec;
}

static irqreturn_t
iop_timer_interrupt(int irq, void *dev_id)
{
	write_tisr(1);

	while ((signed long)(next_jiffy_time - read_tcr1())
		>= ticks_per_jiffy) {
		timer_tick();
		next_jiffy_time -= ticks_per_jiffy;
	}

	return IRQ_HANDLED;
}

static struct irqaction iop_timer_irq = {
	.name		= "IOP Timer Tick",
	.handler	= iop_timer_interrupt,
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
};

static unsigned long iop_tick_rate;
unsigned long get_iop_tick_rate(void)
{
	return iop_tick_rate;
}
EXPORT_SYMBOL(get_iop_tick_rate);

void __init iop_init_time(unsigned long tick_rate)
{
	u32 timer_ctl;

	ticks_per_jiffy = DIV_ROUND_CLOSEST(tick_rate, HZ);
	ticks_per_usec = tick_rate / 1000000;
	next_jiffy_time = 0xffffffff;
	iop_tick_rate = tick_rate;

	timer_ctl = IOP_TMR_EN | IOP_TMR_PRIVILEGED |
			IOP_TMR_RELOAD | IOP_TMR_RATIO_1_1;

	/*
	 * We use timer 0 for our timer interrupt, and timer 1 as
	 * monotonic counter for tracking missed jiffies.
	 */
	write_trr0(ticks_per_jiffy - 1);
	write_tmr0(timer_ctl);

	/*
	 * Set up free-running clocksource timer 1.
	 */
	write_trr1(0xffffffff);
	write_tcr1(0xffffffff);
	write_tmr1(timer_ctl);
	iop_clocksource_set_hz(&iop_clocksource, tick_rate);
	clocksource_register(&iop_clocksource);

	setup_irq(IRQ_IOP_TIMER0, &iop_timer_irq);
}
