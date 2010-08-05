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

#define TIMER_LOAD_COUNT	0x0000
#define TIMER_CUR_VALUE		0x0004
#define TIMER_CONTROL_REG	0x0008
#define TIMER_EOI		0x000C
#define TIMER_INT_STATUS	0x0010

#define TIMER_DISABLE			4
#define TIMER_ENABLE			3
#define TIMER_ENABLE_FREE_RUNNING	1

#define CHECK_VBUS_MS			1000	/* ms */

#define RK_TIMER_ENABLE(n)		writel(TIMER_ENABLE, RK2818_TIMER_BASE + 0x14 * (n - 1) + TIMER_CONTROL_REG)
#define RK_TIMER_ENABLE_FREE_RUNNING(n)	writel(TIMER_ENABLE_FREE_RUNNING, RK2818_TIMER_BASE + 0x14 * (n - 1) + TIMER_CONTROL_REG)
#define RK_TIMER_DISABLE(n)		writel(TIMER_DISABLE, RK2818_TIMER_BASE + 0x14 * (n - 1) + TIMER_CONTROL_REG)

#define RK_TIMER_SETCOUNT(n, count)	writel(count, RK2818_TIMER_BASE + 0x14 * (n - 1) + TIMER_LOAD_COUNT)
#define RK_TIMER_GETCOUNT(n)		readl(RK2818_TIMER_BASE + 0x14 * (n - 1) + TIMER_LOAD_COUNT)

#define RK_TIMER_READVALUE(n)		readl(RK2818_TIMER_BASE + 0x14 * (n - 1) + TIMER_CUR_VALUE)
#define RK_TIMER_INT_CLEAR(n)		readl(RK2818_TIMER_BASE + 0x14 * (n - 1) + TIMER_EOI)

#define TIMER_CLKEVT			2	/* timer2 */
#define IRQ_NR_TIMER_CLKEVT		IRQ_NR_TIMER2
#define TIMER_CLKEVT_NAME		"timer2"

#define TIMER_CLKSRC			3	/* timer3 */
#define IRQ_NR_TIMER_CLKSRC		IRQ_NR_TIMER3
#define TIMER_CLKSRC_NAME		"timer3"

static struct clk *timer_clk;
static volatile unsigned long timer_mult; /* timer count = cycle * timer_mult */

static int rk2818_timer_set_next_event(unsigned long cycles, struct clock_event_device *evt)
{
	RK_TIMER_DISABLE(TIMER_CLKEVT);
	RK_TIMER_SETCOUNT(TIMER_CLKEVT, cycles * timer_mult);
	RK_TIMER_ENABLE(TIMER_CLKEVT);

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
		RK_TIMER_DISABLE(TIMER_CLKEVT);
		break;
	}
}

static struct clock_event_device rk2818_timer_clockevent = {
	.name           = TIMER_CLKEVT_NAME,
	.features       = CLOCK_EVT_FEAT_ONESHOT,
	.shift          = 32,
	.rating         = 200,
	.set_next_event = rk2818_timer_set_next_event,
	.set_mode       = rk2818_timer_set_mode,
};

static irqreturn_t rk2818_timer_clockevent_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	RK_TIMER_INT_CLEAR(TIMER_CLKEVT);
	RK_TIMER_DISABLE(TIMER_CLKEVT);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction rk2818_timer_clockevent_irq = {
	.name		= TIMER_CLKEVT_NAME,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= rk2818_timer_clockevent_interrupt,
	.irq		= IRQ_NR_TIMER_CLKEVT,
	.dev_id		= &rk2818_timer_clockevent,
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
	struct clock_event_device *ce = &rk2818_timer_clockevent;

	timer_clk = clk_get(NULL, "timer");
	timer_mult = clk_get_rate(timer_clk) / 1000000;

	RK_TIMER_DISABLE(TIMER_CLKEVT);

	setup_irq(rk2818_timer_clockevent_irq.irq, &rk2818_timer_clockevent_irq);

	ce->mult = div_sc(1000000, NSEC_PER_SEC, ce->shift);
	ce->max_delta_ns = clockevent_delta2ns(0xFFFFFFFFUL / 256, ce); // max pclk < 256MHz
	ce->min_delta_ns = clockevent_delta2ns(1, ce) + 1;

	ce->cpumask = cpumask_of(0);

	clockevents_register_device(ce);

	return 0;
}

static cycle_t rk2818_timer_read(struct clocksource *cs)
{
	return ~RK_TIMER_READVALUE(TIMER_CLKSRC);
}

static struct clocksource rk2818_timer_clocksource = {
	.name           = TIMER_CLKSRC_NAME,
	.rating         = 200,
	.read           = rk2818_timer_read,
	.mask           = CLOCKSOURCE_MASK(32),
	.shift          = 26,
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

#ifdef CONFIG_RK28_USB_WAKE
static irqreturn_t rk2818_timer_clocksource_interrupt(int irq, void *dev_id)
{
	RK_TIMER_INT_CLEAR(TIMER_CLKSRC);
	return IRQ_HANDLED;
}

static struct irqaction rk2818_timer_clocksource_irq = {
	.name		= TIMER_CLKSRC_NAME,
	.handler	= rk2818_timer_clocksource_interrupt,
	.irq		= IRQ_NR_TIMER_CLKSRC,
};
#endif

static void __init rk2818_timer_init_clocksource(void)
{
	static char err[] __initdata = KERN_ERR "%s: can't register clocksource!\n";
	struct clocksource *cs = &rk2818_timer_clocksource;

	RK_TIMER_DISABLE(TIMER_CLKSRC);
	RK_TIMER_SETCOUNT(TIMER_CLKSRC, 0xFFFFFFFF);
	RK_TIMER_ENABLE_FREE_RUNNING(TIMER_CLKSRC);

	cs->mult = clocksource_hz2mult(24000000, cs->shift);
	if (clocksource_register(cs))
		printk(err, cs->name);

#ifdef CONFIG_RK28_USB_WAKE
	setup_irq(rk2818_timer_clocksource_irq.irq, &rk2818_timer_clocksource_irq);
#endif
}

static void __init rk2818_timer_init(void)
{
	rk2818_timer_init_clocksource();
	rk2818_timer_init_clockevent();
}

struct sys_timer rk2818_timer = {
	.init = rk2818_timer_init
};

#ifdef CONFIG_RK28_USB_WAKE
void rockchip_timer_clocksource_suspend_resume(int suspend)
{
	RK_TIMER_DISABLE(TIMER_CLKSRC);
	if (suspend) {
		RK_TIMER_SETCOUNT(TIMER_CLKSRC, 24000 * CHECK_VBUS_MS);
		RK_TIMER_ENABLE(TIMER_CLKSRC);
	} else {
		RK_TIMER_SETCOUNT(TIMER_CLKSRC, 0xFFFFFFFF);
		RK_TIMER_ENABLE_FREE_RUNNING(TIMER_CLKSRC);
	}
}

int rockchip_timer_clocksource_irq_checkandclear(void)
{
	u32 l, h;

	l = readl(RK2818_INTC_BASE + IRQ_REG_MASKSTATUS_L);
	h = readl(RK2818_INTC_BASE + IRQ_REG_MASKSTATUS_H);

	/* clock source irq */
	if (l & (1 << IRQ_NR_TIMER_CLKSRC)) {
		RK_TIMER_INT_CLEAR(TIMER_CLKSRC);
		l &= ~(1 << IRQ_NR_TIMER_CLKSRC);
	}

	/* 20091103,HSL@RK,all irq must be handle !*/
	l |= (h & 0xffff);

	return l;
}
#endif

