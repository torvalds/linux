/*
 *  linux/arch/arm/common/time-acorn.c
 *
 *  Copyright (c) 1996-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   24-Sep-1996	RMK	Created
 *   10-Oct-1996	RMK	Brought up to date with arch-sa110eval
 *   04-Dec-1997	RMK	Updated for new arch/arm/time.c
 *   13=Jun-2004	DS	Moved to arch/arm/common b/c shared w/CLPS7500
 */
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/hardware/ioc.h>

#include <asm/mach/time.h>

#define RPC_CLOCK_FREQ 2000000
#define RPC_LATCH DIV_ROUND_CLOSEST(RPC_CLOCK_FREQ, HZ)

static u32 ioc_timer_gettimeoffset(void)
{
	unsigned int count1, count2, status;
	long offset;

	ioc_writeb (0, IOC_T0LATCH);
	barrier ();
	count1 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);
	barrier ();
	status = ioc_readb(IOC_IRQREQA);
	barrier ();
	ioc_writeb (0, IOC_T0LATCH);
	barrier ();
	count2 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);

	offset = count2;
	if (count2 < count1) {
		/*
		 * We have not had an interrupt between reading count1
		 * and count2.
		 */
		if (status & (1 << 5))
			offset -= RPC_LATCH;
	} else if (count2 > count1) {
		/*
		 * We have just had another interrupt between reading
		 * count1 and count2.
		 */
		offset -= RPC_LATCH;
	}

	offset = (RPC_LATCH - offset) * (tick_nsec / 1000);
	return DIV_ROUND_CLOSEST(offset, RPC_LATCH) * 1000;
}

void __init ioctime_init(void)
{
	ioc_writeb(RPC_LATCH & 255, IOC_T0LTCHL);
	ioc_writeb(RPC_LATCH >> 8, IOC_T0LTCHH);
	ioc_writeb(0, IOC_T0GO);
}

static irqreturn_t
ioc_timer_interrupt(int irq, void *dev_id)
{
	timer_tick();
	return IRQ_HANDLED;
}

static struct irqaction ioc_timer_irq = {
	.name		= "timer",
	.handler	= ioc_timer_interrupt
};

/*
 * Set up timer interrupt.
 */
void __init ioc_timer_init(void)
{
	arch_gettimeoffset = ioc_timer_gettimeoffset;
	ioctime_init();
	setup_irq(IRQ_TIMER0, &ioc_timer_irq);
}
