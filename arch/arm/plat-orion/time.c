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
#include <linux/timer.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched_clock.h>
#include <plat/time.h>
#include <asm/delay.h>

/*
 * MBus bridge block registers.
 */
#define BRIDGE_CAUSE_OFF	0x0110
#define BRIDGE_MASK_OFF		0x0114
#define  BRIDGE_INT_TIMER0	 0x0002
#define  BRIDGE_INT_TIMER1	 0x0004


/*
 * Timer block registers.
 */
#define TIMER_CTRL_OFF		0x0000
#define  TIMER0_EN		 0x0001
#define  TIMER0_RELOAD_EN	 0x0002
#define  TIMER1_EN		 0x0004
#define  TIMER1_RELOAD_EN	 0x0008
#define TIMER0_RELOAD_OFF	0x0010
#define TIMER0_VAL_OFF		0x0014
#define TIMER1_RELOAD_OFF	0x0018
#define TIMER1_VAL_OFF		0x001c


/*
 * SoC-specific data.
 */
static void __iomem *bridge_base;
static u32 bridge_timer1_clr_mask;
static void __iomem *timer_base;


/*
 * Number of timer ticks per jiffy.
 */
static u32 ticks_per_jiffy;


/*
 * Orion's sched_clock implementation. It has a resolution of
 * at least 7.5ns (133MHz TCLK).
 */

static u64 notrace orion_read_sched_clock(void)
{
	return ~readl(timer_base + TIMER0_VAL_OFF);
}

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
	writel(bridge_timer1_clr_mask, bridge_base + BRIDGE_CAUSE_OFF);

	u = readl(bridge_base + BRIDGE_MASK_OFF);
	u |= BRIDGE_INT_TIMER1;
	writel(u, bridge_base + BRIDGE_MASK_OFF);

	/*
	 * Setup new clockevent timer value.
	 */
	writel(delta, timer_base + TIMER1_VAL_OFF);

	/*
	 * Enable the timer.
	 */
	u = readl(timer_base + TIMER_CTRL_OFF);
	u = (u & ~TIMER1_RELOAD_EN) | TIMER1_EN;
	writel(u, timer_base + TIMER_CTRL_OFF);

	local_irq_restore(flags);

	return 0;
}

static int orion_clkevt_shutdown(struct clock_event_device *evt)
{
	unsigned long flags;
	u32 u;

	local_irq_save(flags);

	/* Disable timer */
	u = readl(timer_base + TIMER_CTRL_OFF);
	writel(u & ~TIMER1_EN, timer_base + TIMER_CTRL_OFF);

	/* Disable timer interrupt */
	u = readl(bridge_base + BRIDGE_MASK_OFF);
	writel(u & ~BRIDGE_INT_TIMER1, bridge_base + BRIDGE_MASK_OFF);

	/* ACK pending timer interrupt */
	writel(bridge_timer1_clr_mask, bridge_base + BRIDGE_CAUSE_OFF);

	local_irq_restore(flags);

	return 0;
}

static int orion_clkevt_set_periodic(struct clock_event_device *evt)
{
	unsigned long flags;
	u32 u;

	local_irq_save(flags);

	/* Setup timer to fire at 1/HZ intervals */
	writel(ticks_per_jiffy - 1, timer_base + TIMER1_RELOAD_OFF);
	writel(ticks_per_jiffy - 1, timer_base + TIMER1_VAL_OFF);

	/* Enable timer interrupt */
	u = readl(bridge_base + BRIDGE_MASK_OFF);
	writel(u | BRIDGE_INT_TIMER1, bridge_base + BRIDGE_MASK_OFF);

	/* Enable timer */
	u = readl(timer_base + TIMER_CTRL_OFF);
	writel(u | TIMER1_EN | TIMER1_RELOAD_EN, timer_base + TIMER_CTRL_OFF);

	local_irq_restore(flags);

	return 0;
}

static struct clock_event_device orion_clkevt = {
	.name			= "orion_tick",
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC,
	.rating			= 300,
	.set_next_event		= orion_clkevt_next_event,
	.set_state_shutdown	= orion_clkevt_shutdown,
	.set_state_periodic	= orion_clkevt_set_periodic,
	.set_state_oneshot	= orion_clkevt_shutdown,
	.tick_resume		= orion_clkevt_shutdown,
};

static irqreturn_t orion_timer_interrupt(int irq, void *dev_id)
{
	/*
	 * ACK timer interrupt and call event handler.
	 */
	writel(bridge_timer1_clr_mask, bridge_base + BRIDGE_CAUSE_OFF);
	orion_clkevt.event_handler(&orion_clkevt);

	return IRQ_HANDLED;
}

static struct irqaction orion_timer_irq = {
	.name		= "orion_tick",
	.flags		= IRQF_TIMER,
	.handler	= orion_timer_interrupt
};

void __init
orion_time_set_base(void __iomem *_timer_base)
{
	timer_base = _timer_base;
}

static unsigned long orion_delay_timer_read(void)
{
	return ~readl(timer_base + TIMER0_VAL_OFF);
}

static struct delay_timer orion_delay_timer = {
	.read_current_timer = orion_delay_timer_read,
};

void __init
orion_time_init(void __iomem *_bridge_base, u32 _bridge_timer1_clr_mask,
		unsigned int irq, unsigned int tclk)
{
	u32 u;

	/*
	 * Set SoC-specific data.
	 */
	bridge_base = _bridge_base;
	bridge_timer1_clr_mask = _bridge_timer1_clr_mask;

	ticks_per_jiffy = (tclk + HZ/2) / HZ;

	orion_delay_timer.freq = tclk;
	register_current_timer_delay(&orion_delay_timer);

	/*
	 * Set scale and timer for sched_clock.
	 */
	sched_clock_register(orion_read_sched_clock, 32, tclk);

	/*
	 * Setup free-running clocksource timer (interrupts
	 * disabled).
	 */
	writel(0xffffffff, timer_base + TIMER0_VAL_OFF);
	writel(0xffffffff, timer_base + TIMER0_RELOAD_OFF);
	u = readl(bridge_base + BRIDGE_MASK_OFF);
	writel(u & ~BRIDGE_INT_TIMER0, bridge_base + BRIDGE_MASK_OFF);
	u = readl(timer_base + TIMER_CTRL_OFF);
	writel(u | TIMER0_EN | TIMER0_RELOAD_EN, timer_base + TIMER_CTRL_OFF);
	clocksource_mmio_init(timer_base + TIMER0_VAL_OFF, "orion_clocksource",
		tclk, 300, 32, clocksource_mmio_readl_down);

	/*
	 * Setup clockevent timer (interrupt-driven).
	 */
	setup_irq(irq, &orion_timer_irq);
	orion_clkevt.cpumask = cpumask_of(0);
	clockevents_config_and_register(&orion_clkevt, tclk, 1, 0xfffffffe);
}
