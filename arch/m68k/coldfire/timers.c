// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	timers.c -- generic ColdFire hardware timer support.
 *
 *	Copyright (C) 1999-2008, Greg Ungerer <gerg@snapgear.com>
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/profile.h>
#include <linux/clocksource.h>
#include <asm/io.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcftimer.h>
#include <asm/mcfsim.h>

/***************************************************************************/

/*
 *	By default use timer1 as the system clock timer.
 */
#define	FREQ	(MCF_BUSCLK / 16)
#define	TA(a)	(MCFTIMER_BASE1 + (a))

/*
 *	These provide the underlying interrupt vector support.
 *	Unfortunately it is a little different on each ColdFire.
 */
void coldfire_profile_init(void);

#if defined(CONFIG_M53xx) || defined(CONFIG_M5441x)
#define	__raw_readtrr	__raw_readl
#define	__raw_writetrr	__raw_writel
#else
#define	__raw_readtrr	__raw_readw
#define	__raw_writetrr	__raw_writew
#endif

static u32 mcftmr_cycles_per_jiffy;
static u32 mcftmr_cnt;

static irq_handler_t timer_interrupt;

/***************************************************************************/

static void init_timer_irq(void)
{
#ifdef MCFSIM_ICR_AUTOVEC
	/* Timer1 is always used as system timer */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI3,
		MCFSIM_TIMER1ICR);
	mcf_mapirq2imr(MCF_IRQ_TIMER, MCFINTC_TIMER1);

#ifdef CONFIG_HIGHPROFILE
	/* Timer2 is to be used as a high speed profile timer  */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL7 | MCFSIM_ICR_PRI3,
		MCFSIM_TIMER2ICR);
	mcf_mapirq2imr(MCF_IRQ_PROFILER, MCFINTC_TIMER2);
#endif
#endif /* MCFSIM_ICR_AUTOVEC */
}

/***************************************************************************/

static irqreturn_t mcftmr_tick(int irq, void *dummy)
{
	/* Reset the ColdFire timer */
	__raw_writeb(MCFTIMER_TER_CAP | MCFTIMER_TER_REF, TA(MCFTIMER_TER));

	mcftmr_cnt += mcftmr_cycles_per_jiffy;
	return timer_interrupt(irq, dummy);
}

/***************************************************************************/

static struct irqaction mcftmr_timer_irq = {
	.name	 = "timer",
	.flags	 = IRQF_TIMER,
	.handler = mcftmr_tick,
};

/***************************************************************************/

static u64 mcftmr_read_clk(struct clocksource *cs)
{
	unsigned long flags;
	u32 cycles;
	u16 tcn;

	local_irq_save(flags);
	tcn = __raw_readw(TA(MCFTIMER_TCN));
	cycles = mcftmr_cnt;
	local_irq_restore(flags);

	return cycles + tcn;
}

/***************************************************************************/

static struct clocksource mcftmr_clk = {
	.name	= "tmr",
	.rating	= 250,
	.read	= mcftmr_read_clk,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

/***************************************************************************/

void hw_timer_init(irq_handler_t handler)
{
	__raw_writew(MCFTIMER_TMR_DISABLE, TA(MCFTIMER_TMR));
	mcftmr_cycles_per_jiffy = FREQ / HZ;
	/*
	 *	The coldfire timer runs from 0 to TRR included, then 0
	 *	again and so on.  It counts thus actually TRR + 1 steps
	 *	for 1 tick, not TRR.  So if you want n cycles,
	 *	initialize TRR with n - 1.
	 */
	__raw_writetrr(mcftmr_cycles_per_jiffy - 1, TA(MCFTIMER_TRR));
	__raw_writew(MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE, TA(MCFTIMER_TMR));

	clocksource_register_hz(&mcftmr_clk, FREQ);

	timer_interrupt = handler;
	init_timer_irq();
	setup_irq(MCF_IRQ_TIMER, &mcftmr_timer_irq);

#ifdef CONFIG_HIGHPROFILE
	coldfire_profile_init();
#endif
}

/***************************************************************************/
#ifdef CONFIG_HIGHPROFILE
/***************************************************************************/

/*
 *	By default use timer2 as the profiler clock timer.
 */
#define	PA(a)	(MCFTIMER_BASE2 + (a))

/*
 *	Choose a reasonably fast profile timer. Make it an odd value to
 *	try and get good coverage of kernel operations.
 */
#define	PROFILEHZ	1013

/*
 *	Use the other timer to provide high accuracy profiling info.
 */
irqreturn_t coldfire_profile_tick(int irq, void *dummy)
{
	/* Reset ColdFire timer2 */
	__raw_writeb(MCFTIMER_TER_CAP | MCFTIMER_TER_REF, PA(MCFTIMER_TER));
	if (current->pid)
		profile_tick(CPU_PROFILING);
	return IRQ_HANDLED;
}

/***************************************************************************/

static struct irqaction coldfire_profile_irq = {
	.name	 = "profile timer",
	.flags	 = IRQF_TIMER,
	.handler = coldfire_profile_tick,
};

void coldfire_profile_init(void)
{
	printk(KERN_INFO "PROFILE: lodging TIMER2 @ %dHz as profile timer\n",
	       PROFILEHZ);

	/* Set up TIMER 2 as high speed profile clock */
	__raw_writew(MCFTIMER_TMR_DISABLE, PA(MCFTIMER_TMR));

	__raw_writetrr(((MCF_BUSCLK / 16) / PROFILEHZ), PA(MCFTIMER_TRR));
	__raw_writew(MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE, PA(MCFTIMER_TMR));

	setup_irq(MCF_IRQ_PROFILER, &coldfire_profile_irq);
}

/***************************************************************************/
#endif	/* CONFIG_HIGHPROFILE */
/***************************************************************************/
