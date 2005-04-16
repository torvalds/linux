/*
 *  linux/arch/arm/mach-footbridge/isa-timer.c
 *
 *  Copyright (C) 1998 Russell King.
 *  Copyright (C) 1998 Phil Blundell
 */
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/irq.h>

#include <asm/mach/time.h>

#include "common.h"

/*
 * ISA timer tick support
 */
#define mSEC_10_from_14 ((14318180 + 100) / 200)

static unsigned long isa_gettimeoffset(void)
{
	int count;

	static int count_p = (mSEC_10_from_14/6);    /* for the first call after boot */
	static unsigned long jiffies_p = 0;

	/*
	 * cache volatile jiffies temporarily; we have IRQs turned off. 
	 */
	unsigned long jiffies_t;

	/* timer count may underflow right here */
	outb_p(0x00, 0x43);	/* latch the count ASAP */

	count = inb_p(0x40);	/* read the latched count */

	/*
	 * We do this guaranteed double memory access instead of a _p 
	 * postfix in the previous port access. Wheee, hackady hack
	 */
 	jiffies_t = jiffies;

	count |= inb_p(0x40) << 8;

	/* Detect timer underflows.  If we haven't had a timer tick since 
	   the last time we were called, and time is apparently going
	   backwards, the counter must have wrapped during this routine. */
	if ((jiffies_t == jiffies_p) && (count > count_p))
		count -= (mSEC_10_from_14/6);
	else
		jiffies_p = jiffies_t;

	count_p = count;

	count = (((mSEC_10_from_14/6)-1) - count) * (tick_nsec / 1000);
	count = (count + (mSEC_10_from_14/6)/2) / (mSEC_10_from_14/6);

	return count;
}

static irqreturn_t
isa_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);
	timer_tick(regs);
	write_sequnlock(&xtime_lock);
	return IRQ_HANDLED;
}

static struct irqaction isa_timer_irq = {
	.name		= "ISA timer tick",
	.handler	= isa_timer_interrupt,
	.flags		= SA_INTERRUPT,
};

static void __init isa_timer_init(void)
{
	isa_rtc_init();

	/* enable PIT timer */
	/* set for periodic (4) and LSB/MSB write (0x30) */
	outb(0x34, 0x43);
	outb((mSEC_10_from_14/6) & 0xFF, 0x40);
	outb((mSEC_10_from_14/6) >> 8, 0x40);

	setup_irq(IRQ_ISA_TIMER, &isa_timer_irq);
}

struct sys_timer isa_timer = {
	.init		= isa_timer_init,
	.offset		= isa_gettimeoffset,
};
