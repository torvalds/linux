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
#define	TA(a)	(MCF_MBAR + MCFTIMER_BASE1 + (a))

/*
 *	Default the timer and vector to use for ColdFire. Some ColdFire
 *	CPU's and some boards may want different. Their sub-architecture
 *	startup code (in config.c) can change these if they want.
 */
unsigned int	mcf_timervector = 29;
unsigned int	mcf_profilevector = 31;
unsigned int	mcf_timerlevel = 5;

/*
 *	These provide the underlying interrupt vector support.
 *	Unfortunately it is a little different on each ColdFire.
 */
extern void mcf_settimericr(int timer, int level);
void coldfire_profile_init(void);

#if defined(CONFIG_M532x)
#define	__raw_readtrr	__raw_readl
#define	__raw_writetrr	__raw_writel
#else
#define	__raw_readtrr	__raw_readw
#define	__raw_writetrr	__raw_writew
#endif

static u32 mcftmr_cycles_per_jiffy;
static u32 mcftmr_cnt;

/***************************************************************************/

static irqreturn_t mcftmr_tick(int irq, void *dummy)
{
	/* Reset the ColdFire timer */
	__raw_writeb(MCFTIMER_TER_CAP | MCFTIMER_TER_REF, TA(MCFTIMER_TER));

	mcftmr_cnt += mcftmr_cycles_per_jiffy;
	return arch_timer_interrupt(irq, dummy);
}

/***************************************************************************/

static struct irqaction mcftmr_timer_irq = {
	.name	 = "timer",
	.flags	 = IRQF_DISABLED | IRQF_TIMER,
	.handler = mcftmr_tick,
};

/***************************************************************************/

static cycle_t mcftmr_read_clk(void)
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
	.shift	= 20,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

/***************************************************************************/

void hw_timer_init(void)
{
	setup_irq(mcf_timervector, &mcftmr_timer_irq);

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

	mcftmr_clk.mult = clocksource_hz2mult(FREQ, mcftmr_clk.shift);
	clocksource_register(&mcftmr_clk);

	mcf_settimericr(1, mcf_timerlevel);

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
#define	PA(a)	(MCF_MBAR + MCFTIMER_BASE2 + (a))

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
	.flags	 = IRQF_DISABLED | IRQF_TIMER,
	.handler = coldfire_profile_tick,
};

void coldfire_profile_init(void)
{
	printk(KERN_INFO "PROFILE: lodging TIMER2 @ %dHz as profile timer\n",
	       PROFILEHZ);

	setup_irq(mcf_profilevector, &coldfire_profile_irq);

	/* Set up TIMER 2 as high speed profile clock */
	__raw_writew(MCFTIMER_TMR_DISABLE, PA(MCFTIMER_TMR));

	__raw_writetrr(((MCF_BUSCLK / 16) / PROFILEHZ), PA(MCFTIMER_TRR));
	__raw_writew(MCFTIMER_TMR_ENORI | MCFTIMER_TMR_CLK16 |
		MCFTIMER_TMR_RESTART | MCFTIMER_TMR_ENABLE, PA(MCFTIMER_TMR));

	mcf_settimericr(2, 7);
}

/***************************************************************************/
#endif	/* CONFIG_HIGHPROFILE */
/***************************************************************************/
