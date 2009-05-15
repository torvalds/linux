/*
 * arch/arm/plat-orion/time.c
 *
 * Marvell Orion SoC timer handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * Timer 0 is used as free-running clocksource, while timer 1 is
 * used as clock_event_device.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cnt32_to_63.h>
#include <linux/timer.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/mach/time.h>
#include <mach/bridge-regs.h>
#include <mach/hardware.h>

/*
 * Number of timer ticks per jiffy.
 */
static u32 ticks_per_jiffy;


/*
 * Timer block registers.
 */
#define TIMER_CTRL		(TIMER_VIRT_BASE + 0x0000)
#define  TIMER0_EN		0x0001
#define  TIMER0_RELOAD_EN	0x0002
#define  TIMER1_EN		0x0004
#define  TIMER1_RELOAD_EN	0x0008
#define TIMER0_RELOAD		(TIMER_VIRT_BASE + 0x0010)
#define TIMER0_VAL		(TIMER_VIRT_BASE + 0x0014)
#define TIMER1_RELOAD		(TIMER_VIRT_BASE + 0x0018)
#define TIMER1_VAL		(TIMER_VIRT_BASE + 0x001c)


/*
 * Orion's sched_clock implementation. It has a resolution of
 * at least 7.5ns (133MHz TCLK) and a maximum value of 834 days.
 *
 * Because the hardware timer period is quite short (21 secs if
 * 200MHz TCLK) and because cnt32_to_63() needs to be called at
 * least once per half period to work properly, a kernel timer is
 * set up to ensure this requirement is always met.
 */
#define TCLK2NS_SCALE_FACTOR 8

static unsigned long tclk2ns_scale;

unsigned long long sched_clock(void)
{
	unsigned long long v = cnt32_to_63(0xffffffff - readl(TIMER0_VAL));
	return (v * tclk2ns_scale) >> TCLK2NS_SCALE_FACTOR;
}

static struct timer_list cnt32_to_63_keepwarm_timer;

static void cnt32_to_63_keepwarm(unsigned long data)
{
	mod_timer(&cnt32_to_63_keepwarm_timer, round_jiffies(jiffies + data));
	(void) sched_clock();
}

static void __init setup_sched_clock(unsigned long tclk)
{
	unsigned long long v;
	unsigned long data;

	v = NSEC_PER_SEC;
	v <<= TCLK2NS_SCALE_FACTOR;
	v += tclk/2;
	do_div(v, tclk);
	/*
	 * We want an even value to automatically clear the top bit
	 * returned by cnt32_to_63() without an additional run time
	 * instruction. So if the LSB is 1 then round it up.
	 */
	if (v & 1)
		v++;
	tclk2ns_scale = v;

	data = (0xffffffffUL / tclk / 2 - 2) * HZ;
	setup_timer(&cnt32_to_63_keepwarm_timer, cnt32_to_63_keepwarm, data);
	mod_timer(&cnt32_to_63_keepwarm_timer, round_jiffies(jiffies + data));
}

/*
 * Clocksource handling.
 */
static cycle_t orion_clksrc_read(struct clocksource *cs)
{
	return 0xffffffff - readl(TIMER0_VAL);
}

static struct clocksource orion_clksrc = {
	.name		= "orion_clocksource",
	.shift		= 20,
	.rating		= 300,
	.read		= orion_clksrc_read,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};



/*
 * Clockevent handling.
 */
static int
orion_clkevt_next_event(unsigned long delta, struct clock_event_device *dev)
{
	unsigned long flags;
	u32 u;

	if (delta == 0)
		return -ETIME;

	local_irq_save(flags);

	/*
	 * Clear and enable clockevent timer interrupt.
	 */
	writel(BRIDGE_INT_TIMER1_CLR, BRIDGE_CAUSE);

	u = readl(BRIDGE_MASK);
	u |= BRIDGE_INT_TIMER1;
	writel(u, BRIDGE_MASK);

	/*
	 * Setup new clockevent timer value.
	 */
	writel(delta, TIMER1_VAL);

	/*
	 * Enable the timer.
	 */
	u = readl(TIMER_CTRL);
	u = (u & ~TIMER1_RELOAD_EN) | TIMER1_EN;
	writel(u, TIMER_CTRL);

	local_irq_restore(flags);

	return 0;
}

static void
orion_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *dev)
{
	unsigned long flags;
	u32 u;

	local_irq_save(flags);
	if (mode == CLOCK_EVT_MODE_PERIODIC) {
		/*
		 * Setup timer to fire at 1/HZ intervals.
		 */
		writel(ticks_per_jiffy - 1, TIMER1_RELOAD);
		writel(ticks_per_jiffy - 1, TIMER1_VAL);

		/*
		 * Enable timer interrupt.
		 */
		u = readl(BRIDGE_MASK);
		writel(u | BRIDGE_INT_TIMER1, BRIDGE_MASK);

		/*
		 * Enable timer.
		 */
		u = readl(TIMER_CTRL);
		writel(u | TIMER1_EN | TIMER1_RELOAD_EN, TIMER_CTRL);
	} else {
		/*
		 * Disable timer.
		 */
		u = readl(TIMER_CTRL);
		writel(u & ~TIMER1_EN, TIMER_CTRL);

		/*
		 * Disable timer interrupt.
		 */
		u = readl(BRIDGE_MASK);
		writel(u & ~BRIDGE_INT_TIMER1, BRIDGE_MASK);

		/*
		 * ACK pending timer interrupt.
		 */
		writel(BRIDGE_INT_TIMER1_CLR, BRIDGE_CAUSE);

	}
	local_irq_restore(flags);
}

static struct clock_event_device orion_clkevt = {
	.name		= "orion_tick",
	.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 32,
	.rating		= 300,
	.set_next_event	= orion_clkevt_next_event,
	.set_mode	= orion_clkevt_mode,
};

static irqreturn_t orion_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * ACK timer interrupt and call event handler.
	 */
	writel(BRIDGE_INT_TIMER1_CLR, BRIDGE_CAUSE);
	orion_clkevt.event_handler(&orion_clkevt);

	return IRQ_HANDLED;
}

static struct irqaction orion_timer_irq = {
	.name		= "orion_tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= orion_timer_interrupt
};

void __init orion_time_init(unsigned int irq, unsigned int tclk)
{
	u32 u;

	ticks_per_jiffy = (tclk + HZ/2) / HZ;

	/*
	 * Set scale and timer for sched_clock
	 */
	setup_sched_clock(tclk);

	/*
	 * Setup free-running clocksource timer (interrupts
	 * disabled.)
	 */
	writel(0xffffffff, TIMER0_VAL);
	writel(0xffffffff, TIMER0_RELOAD);
	u = readl(BRIDGE_MASK);
	writel(u & ~BRIDGE_INT_TIMER0, BRIDGE_MASK);
	u = readl(TIMER_CTRL);
	writel(u | TIMER0_EN | TIMER0_RELOAD_EN, TIMER_CTRL);
	orion_clksrc.mult = clocksource_hz2mult(tclk, orion_clksrc.shift);
	clocksource_register(&orion_clksrc);

	/*
	 * Setup clockevent timer (interrupt-driven.)
	 */
	setup_irq(irq, &orion_timer_irq);
	orion_clkevt.mult = div_sc(tclk, NSEC_PER_SEC, orion_clkevt.shift);
	orion_clkevt.max_delta_ns = clockevent_delta2ns(0xfffffffe, &orion_clkevt);
	orion_clkevt.min_delta_ns = clockevent_delta2ns(1, &orion_clkevt);
	orion_clkevt.cpumask = cpumask_of(0);
	clockevents_register_device(&orion_clkevt);
}
