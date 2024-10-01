// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/common/time-acorn.c
 *
 *  Copyright (c) 1996-2000 Russell King.
 *
 *  Changelog:
 *   24-Sep-1996	RMK	Created
 *   10-Oct-1996	RMK	Brought up to date with arch-sa110eval
 *   04-Dec-1997	RMK	Updated for new arch/arm/time.c
 *   13=Jun-2004	DS	Moved to arch/arm/common b/c shared w/CLPS7500
 */
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/hardware/ioc.h>

#include <asm/mach/time.h>

#define RPC_CLOCK_FREQ 2000000
#define RPC_LATCH DIV_ROUND_CLOSEST(RPC_CLOCK_FREQ, HZ)

static u32 ioc_time;

static u64 ioc_timer_read(struct clocksource *cs)
{
	unsigned int count1, count2, status;
	unsigned long flags;
	u32 ticks;

	local_irq_save(flags);
	ioc_writeb (0, IOC_T0LATCH);
	barrier ();
	count1 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);
	barrier ();
	status = ioc_readb(IOC_IRQREQA);
	barrier ();
	ioc_writeb (0, IOC_T0LATCH);
	barrier ();
	count2 = ioc_readb(IOC_T0CNTL) | (ioc_readb(IOC_T0CNTH) << 8);
	ticks = ioc_time + RPC_LATCH - count2;
	local_irq_restore(flags);

	if (count2 < count1) {
		/*
		 * The timer has not reloaded between reading count1 and
		 * count2, check whether an interrupt was actually pending.
		 */
		if (status & (1 << 5))
			ticks += RPC_LATCH;
	} else if (count2 > count1) {
		/*
		 * The timer has reloaded, so count2 indicates the new
		 * count since the wrap.  The interrupt would not have
		 * been processed, so add the missed ticks.
		 */
		ticks += RPC_LATCH;
	}

	return ticks;
}

static struct clocksource ioctime_clocksource = {
	.read = ioc_timer_read,
	.mask = CLOCKSOURCE_MASK(32),
	.rating = 100,
};

void __init ioctime_init(void)
{
	ioc_writeb(RPC_LATCH & 255, IOC_T0LTCHL);
	ioc_writeb(RPC_LATCH >> 8, IOC_T0LTCHH);
	ioc_writeb(0, IOC_T0GO);
}

static irqreturn_t
ioc_timer_interrupt(int irq, void *dev_id)
{
	ioc_time += RPC_LATCH;
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

/*
 * Set up timer interrupt.
 */
void __init ioc_timer_init(void)
{
	WARN_ON(clocksource_register_hz(&ioctime_clocksource, RPC_CLOCK_FREQ));
	ioctime_init();
	if (request_irq(IRQ_TIMER0, ioc_timer_interrupt, 0, "timer", NULL))
		pr_err("Failed to request irq %d (timer)\n", IRQ_TIMER0);
}
