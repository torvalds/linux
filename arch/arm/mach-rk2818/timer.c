/* linux/arch/arm/mach-rk2818/timer.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <linux/cpufreq.h>

#include <asm/mach/time.h>
#include <mach/rk2818_iomap.h>

#define RK2818_TIMER1_BASE      RK2818_TIMER_BASE
#define RK2818_TIMER2_BASE      RK2818_TIMER_BASE + 0x14
#define RK2818_TIMER3_BASE      RK2818_TIMER_BASE + 0x28

#define TIMER_LOAD_COUNT	0x0000
#define TIMER_CUR_VALUE		0x0004
#define TIMER_CONTROL_REG	0x0008
#define TIMER_EOI		0x000C
#define TIMER_INT_STATUS	0x0010

#define TIMER_DISABLE			4
#define TIMER_ENABLE			3
#define TIMER_ENABLE_FREE_RUNNING	1

static struct clk *timer_clk;
static volatile unsigned long timer_mult;

static int rk2818_timer_set_next_event(unsigned long cycles, struct clock_event_device *evt)
{
	writel(TIMER_DISABLE, RK2818_TIMER2_BASE + TIMER_CONTROL_REG);
	writel(cycles * timer_mult, RK2818_TIMER2_BASE + TIMER_LOAD_COUNT);
	writel(TIMER_ENABLE, RK2818_TIMER2_BASE + TIMER_CONTROL_REG);

	return 0;
}

static void rk2818_timer_set_mode(enum clock_event_mode mode, struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		writel(TIMER_DISABLE, RK2818_TIMER2_BASE + TIMER_CONTROL_REG);
		break;
	}
}

static struct clock_event_device clockenent_timer2 = {
	.name           = "timer2",
	.features       = CLOCK_EVT_FEAT_ONESHOT,
	.shift          = 32,
	.rating         = 200,
	.set_next_event = rk2818_timer_set_next_event,
	.set_mode       = rk2818_timer_set_mode,
};

static irqreturn_t rk2818_timer2_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	readl(RK2818_TIMER2_BASE + TIMER_EOI);
	writel(TIMER_DISABLE, RK2818_TIMER2_BASE + TIMER_CONTROL_REG);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction rk2818_timer2_irq = {
	.name		= "timer2",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= rk2818_timer2_interrupt,
	.irq		= IRQ_NR_TIMER2,
	.dev_id		= &clockenent_timer2,
};

static int rk2818_timer_cpufreq_notifier(struct notifier_block *nb, unsigned long val, void *data)
{
	if (val == CPUFREQ_POSTCHANGE) {
		timer_mult = clk_get_rate(timer_clk) / 1000000;
	}

	return 0;
}

static struct notifier_block rk2818_timer_cpufreq_notifier_block = {
	.notifier_call	= rk2818_timer_cpufreq_notifier,
	.priority	= 0x7ffffff,
};

static __init int rk2818_timer_init_cpufreq(void)
{
	cpufreq_register_notifier(&rk2818_timer_cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
	return 0;
}

arch_initcall_sync(rk2818_timer_init_cpufreq);

static __init int rk2818_timer_init_clockevent(void)
{
	struct clock_event_device *ce = &clockenent_timer2;

	timer_clk = clk_get(NULL, "timer");
	timer_mult = clk_get_rate(timer_clk) / 1000000;

	writel(TIMER_DISABLE, RK2818_TIMER2_BASE + TIMER_CONTROL_REG);

	setup_irq(rk2818_timer2_irq.irq, &rk2818_timer2_irq);

	ce->mult = div_sc(1000000, NSEC_PER_SEC, ce->shift);
	ce->max_delta_ns = clockevent_delta2ns(0xFFFFFFFFUL / 256, ce); // max pclk < 256MHz
	ce->min_delta_ns = clockevent_delta2ns(1, ce);

	ce->cpumask = cpumask_of(0);

	clockevents_register_device(ce);

	return 0;
}

static cycle_t rk2818_timer3_read(struct clocksource *cs)
{
	return ~readl(RK2818_TIMER3_BASE + TIMER_CUR_VALUE);
}

static struct clocksource clocksource_timer3 = {
	.name           = "timer3",
	.rating         = 200,
	.read           = rk2818_timer3_read,
	.mask           = CLOCKSOURCE_MASK(32),
	.shift          = 26,
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init rk2818_timer_init_clocksource(void)
{
	static char err[] __initdata = KERN_ERR "%s: can't register clocksource!\n";
	struct clocksource *cs = &clocksource_timer3;

	writel(TIMER_DISABLE, RK2818_TIMER3_BASE + TIMER_CONTROL_REG);
	writel(0xFFFFFFFF, RK2818_TIMER3_BASE + TIMER_LOAD_COUNT);
	writel(TIMER_ENABLE_FREE_RUNNING, RK2818_TIMER3_BASE + TIMER_CONTROL_REG);

	cs->mult = clocksource_hz2mult(24000000, cs->shift);
	if (clocksource_register(cs))
		printk(err, cs->name);
}

static void __init rk2818_timer_init(void)
{
	rk2818_timer_init_clocksource();
	rk2818_timer_init_clockevent();
}

struct sys_timer rk2818_timer = {
	.init = rk2818_timer_init
};

