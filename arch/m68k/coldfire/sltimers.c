// SPDX-License-Identifier: GPL-2.0
/***************************************************************************/

/*
 *	sltimers.c -- generic ColdFire slice timer support.
 *
 *	Copyright (C) 2009-2010, Philippe De Muyter <phdm@macqel.be>
 *	based on
 *	timers.c -- generic ColdFire hardware timer support.
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
#include <asm/mcfslt.h>
#include <asm/mcfsim.h>

/***************************************************************************/

#ifdef CONFIG_HIGHPROFILE

/*
 *	By default use Slice Timer 1 as the profiler clock timer.
 */
#define	PA(a)	(MCFSLT_TIMER1 + (a))

/*
 *	Choose a reasonably fast profile timer. Make it an odd value to
 *	try and get good coverage of kernel operations.
 */
#define	PROFILEHZ	1013

irqreturn_t mcfslt_profile_tick(int irq, void *dummy)
{
	/* Reset Slice Timer 1 */
	__raw_writel(MCFSLT_SSR_BE | MCFSLT_SSR_TE, PA(MCFSLT_SSR));
	if (current->pid)
		profile_tick(CPU_PROFILING);
	return IRQ_HANDLED;
}

void mcfslt_profile_init(void)
{
	int ret;

	printk(KERN_INFO "PROFILE: lodging TIMER 1 @ %dHz as profile timer\n",
	       PROFILEHZ);

	ret = request_irq(MCF_IRQ_PROFILER, mcfslt_profile_tick, IRQF_TIMER,
			  "profile timer", NULL);
	if (ret) {
		pr_err("Failed to request irq %d (profile timer): %pe\n",
		       MCF_IRQ_PROFILER, ERR_PTR(ret));
	}

	/* Set up TIMER 2 as high speed profile clock */
	__raw_writel(MCF_BUSCLK / PROFILEHZ - 1, PA(MCFSLT_STCNT));
	__raw_writel(MCFSLT_SCR_RUN | MCFSLT_SCR_IEN | MCFSLT_SCR_TEN,
								PA(MCFSLT_SCR));

}

#endif	/* CONFIG_HIGHPROFILE */

/***************************************************************************/

/*
 *	By default use Slice Timer 0 as the system clock timer.
 */
#define	TA(a)	(MCFSLT_TIMER0 + (a))

static u32 mcfslt_cycles_per_jiffy;
static u32 mcfslt_cnt;

static irqreturn_t mcfslt_tick(int irq, void *dummy)
{
	/* Reset Slice Timer 0 */
	__raw_writel(MCFSLT_SSR_BE | MCFSLT_SSR_TE, TA(MCFSLT_SSR));
	mcfslt_cnt += mcfslt_cycles_per_jiffy;
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

static u64 mcfslt_read_clk(struct clocksource *cs)
{
	unsigned long flags;
	u32 cycles, scnt;

	local_irq_save(flags);
	scnt = __raw_readl(TA(MCFSLT_SCNT));
	cycles = mcfslt_cnt;
	if (__raw_readl(TA(MCFSLT_SSR)) & MCFSLT_SSR_TE) {
		cycles += mcfslt_cycles_per_jiffy;
		scnt = __raw_readl(TA(MCFSLT_SCNT));
	}
	local_irq_restore(flags);

	/* subtract because slice timers count down */
	return cycles + ((mcfslt_cycles_per_jiffy - 1) - scnt);
}

static struct clocksource mcfslt_clk = {
	.name	= "slt",
	.rating	= 250,
	.read	= mcfslt_read_clk,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

void hw_timer_init(void)
{
	int r;

	mcfslt_cycles_per_jiffy = MCF_BUSCLK / HZ;
	/*
	 *	The coldfire slice timer (SLT) runs from STCNT to 0 included,
	 *	then STCNT again and so on.  It counts thus actually
	 *	STCNT + 1 steps for 1 tick, not STCNT.  So if you want
	 *	n cycles, initialize STCNT with n - 1.
	 */
	__raw_writel(mcfslt_cycles_per_jiffy - 1, TA(MCFSLT_STCNT));
	__raw_writel(MCFSLT_SCR_RUN | MCFSLT_SCR_IEN | MCFSLT_SCR_TEN,
								TA(MCFSLT_SCR));
	/* initialize mcfslt_cnt knowing that slice timers count down */
	mcfslt_cnt = mcfslt_cycles_per_jiffy;

	r = request_irq(MCF_IRQ_TIMER, mcfslt_tick, IRQF_TIMER, "timer", NULL);
	if (r) {
		pr_err("Failed to request irq %d (timer): %pe\n", MCF_IRQ_TIMER,
		       ERR_PTR(r));
	}

	clocksource_register_hz(&mcfslt_clk, MCF_BUSCLK);

#ifdef CONFIG_HIGHPROFILE
	mcfslt_profile_init();
#endif
}
