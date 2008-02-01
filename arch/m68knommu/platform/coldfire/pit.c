/***************************************************************************/

/*
 *	pit.c -- Freescale ColdFire PIT timer. Currently this type of
 *	         hardware timer only exists in the Freescale ColdFire
 *		 5270/5271, 5282 and 5208 CPUs. No doubt newer ColdFire
 *		 family members will probably use it too.
 *
 *	Copyright (C) 1999-2008, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2004, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/coldfire.h>
#include <asm/mcfpit.h>
#include <asm/mcfsim.h>

/***************************************************************************/

/*
 *	By default use timer1 as the system clock timer.
 */
#define	FREQ	((MCF_CLK / 2) / 64)
#define	TA(a)	(MCF_IPSBAR + MCFPIT_BASE1 + (a))
#define	INTC0	(MCF_IPSBAR + MCFICM_INTC0)

static u32 pit_cycles_per_jiffy;
static u32 pit_cnt;

/***************************************************************************/

static irqreturn_t pit_tick(int irq, void *dummy)
{
	u16 pcsr;

	/* Reset the ColdFire timer */
	pcsr = __raw_readw(TA(MCFPIT_PCSR));
	__raw_writew(pcsr | MCFPIT_PCSR_PIF, TA(MCFPIT_PCSR));

	pit_cnt += pit_cycles_per_jiffy;
	return arch_timer_interrupt(irq, dummy);
}

/***************************************************************************/

static struct irqaction pit_irq = {
	.name	 = "timer",
	.flags	 = IRQF_DISABLED | IRQF_TIMER,
	.handler = pit_tick,
};

/***************************************************************************/

static cycle_t pit_read_clk(void)
{
	unsigned long flags;
	u32 cycles;
	u16 pcntr;

	local_irq_save(flags);
	pcntr = __raw_readw(TA(MCFPIT_PCNTR));
	cycles = pit_cnt;
	local_irq_restore(flags);

	return cycles + pit_cycles_per_jiffy - pcntr;
}

/***************************************************************************/

static struct clocksource pit_clk = {
	.name	= "pit",
	.rating	= 250,
	.read	= pit_read_clk,
	.shift	= 20,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

/***************************************************************************/

void hw_timer_init(void)
{
	u32 imr;

	setup_irq(MCFINT_VECBASE + MCFINT_PIT1, &pit_irq);

	__raw_writeb(ICR_INTRCONF, INTC0 + MCFINTC_ICR0 + MCFINT_PIT1);
	imr = __raw_readl(INTC0 + MCFPIT_IMR);
	imr &= ~MCFPIT_IMR_IBIT;
	__raw_writel(imr, INTC0 + MCFPIT_IMR);

	/* Set up PIT timer 1 as poll clock */
	pit_cycles_per_jiffy = FREQ / HZ;
	__raw_writew(MCFPIT_PCSR_DISABLE, TA(MCFPIT_PCSR));
	__raw_writew(pit_cycles_per_jiffy, TA(MCFPIT_PMR));
	__raw_writew(MCFPIT_PCSR_EN | MCFPIT_PCSR_PIE | MCFPIT_PCSR_OVW |
		MCFPIT_PCSR_RLD | MCFPIT_PCSR_CLK64, TA(MCFPIT_PCSR));

	pit_clk.mult = clocksource_hz2mult(FREQ, pit_clk.shift);
	clocksource_register(&pit_clk);
}

/***************************************************************************/
