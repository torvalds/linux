/*
 *  linux/arch/arm/mach-epxa10db/time.c
 *
 *  Copyright (C) 2000 Deep Blue Solutions
 *  Copyright (C) 2001 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>

#include <asm/hardware.h>
#include <asm/system.h>
#include <asm/leds.h>

#include <asm/mach/time.h>

#define TIMER00_TYPE (volatile unsigned int*)
#include <asm/arch/timer00.h>

static int epxa10db_set_rtc(void)
{
	return 1;
}

static int epxa10db_rtc_init(void)
{
	set_rtc = epxa10db_set_rtc;

	return 0;
}

__initcall(epxa10db_rtc_init);


/*
 * IRQ handler for the timer
 */
static irqreturn_t
epxa10db_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	// ...clear the interrupt
	*TIMER0_CR(IO_ADDRESS(EXC_TIMER00_BASE))|=TIMER0_CR_CI_MSK;

	timer_tick(regs);
	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction epxa10db_timer_irq = {
	.name		= "Excalibur Timer Tick",
	.flags		= SA_INTERRUPT,
	.handler	= epxa10db_timer_interrupt
};

/*
 * Set up timer interrupt, and return the current time in seconds.
 */
static void __init epxa10db_timer_init(void)
{
	/* Start the timer */
	*TIMER0_LIMIT(IO_ADDRESS(EXC_TIMER00_BASE))=(unsigned int)(EXC_AHB2_CLK_FREQUENCY/200);
	*TIMER0_PRESCALE(IO_ADDRESS(EXC_TIMER00_BASE))=1;
	*TIMER0_CR(IO_ADDRESS(EXC_TIMER00_BASE))=TIMER0_CR_IE_MSK | TIMER0_CR_S_MSK;

	setup_irq(IRQ_TIMER0, &epxa10db_timer_irq);
}

struct sys_timer epxa10db_timer = {
	.init		= epxa10db_timer_init,
};
