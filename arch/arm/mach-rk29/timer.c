/* linux/arch/arm/mach-rk29/timer.c
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

#include <asm/sched_clock.h>
#include <asm/mach/time.h>
#include <mach/rk29_iomap.h>

#define TIMER_LOAD_COUNT		0x0000
#define TIMER_CUR_VALUE			0x0004
#define TIMER_CONTROL_REG		0x0008
#define TIMER_EOI			0x000C
#define TIMER_INT_STATUS		0x0010

#define TIMER_DISABLE			6
#define TIMER_ENABLE			3
#define TIMER_ENABLE_FREE_RUNNING	5

static inline void timer_write(u32 n, u32 v, u32 offset)
{
	u32 addr = RK29_TIMER2_BASE + 0x4000 * (n - 2) + offset;
	writel(v, addr);
	dsb();
}

static inline u32 timer_read(u32 n, u32 offset)
{
	u32 addr = RK29_TIMER2_BASE + 0x4000 * (n - 2) + offset;
	return readl(addr);
}

#define RK_TIMER_ENABLE(n)		timer_write(n, TIMER_ENABLE, TIMER_CONTROL_REG)
#define RK_TIMER_ENABLE_FREE_RUNNING(n)	timer_write(n, TIMER_ENABLE_FREE_RUNNING, TIMER_CONTROL_REG)
#define RK_TIMER_DISABLE(n)		timer_write(n, TIMER_DISABLE, TIMER_CONTROL_REG)

#define RK_TIMER_SETCOUNT(n, count)	timer_write(n, count, TIMER_LOAD_COUNT)
#define RK_TIMER_GETCOUNT(n)		timer_read(n, TIMER_LOAD_COUNT)

#define RK_TIMER_READVALUE(n)		timer_read(n, TIMER_CUR_VALUE)
#define RK_TIMER_INT_CLEAR(n)		timer_read(n, TIMER_EOI)

#define RK_TIMER_INT_STATUS(n)		timer_read(n, TIMER_INT_STATUS)

#define TIMER_CLKEVT			2	/* timer2 */
#define IRQ_NR_TIMER_CLKEVT		IRQ_TIMER2
#define TIMER_CLKEVT_NAME		"timer2"

#define TIMER_CLKSRC			3	/* timer3 */
#define IRQ_NR_TIMER_CLKSRC		IRQ_TIMER3
#define TIMER_CLKSRC_NAME		"timer3"

static int rk29_timer_set_next_event(unsigned long cycles, struct clock_event_device *evt)
{
	do {
		RK_TIMER_DISABLE(TIMER_CLKEVT);
		RK_TIMER_SETCOUNT(TIMER_CLKEVT, cycles);
		RK_TIMER_ENABLE(TIMER_CLKEVT);
	} while (RK_TIMER_READVALUE(TIMER_CLKEVT) > cycles);
	return 0;
}

static void rk29_timer_set_mode(enum clock_event_mode mode, struct clock_event_device *evt)
{
	u32 count;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		count = clk_get_rate(clk_get(NULL, TIMER_CLKEVT_NAME)) / HZ - 1;
		do {
			RK_TIMER_DISABLE(TIMER_CLKEVT);
			RK_TIMER_SETCOUNT(TIMER_CLKEVT, count);
			RK_TIMER_ENABLE(TIMER_CLKEVT);
		} while (RK_TIMER_READVALUE(TIMER_CLKEVT) > count);
		break;
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		RK_TIMER_DISABLE(TIMER_CLKEVT);
		break;
	}
}

static struct clock_event_device rk29_timer_clockevent = {
	.name           = TIMER_CLKEVT_NAME,
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.rating         = 200,
	.set_next_event = rk29_timer_set_next_event,
	.set_mode       = rk29_timer_set_mode,
};

static irqreturn_t rk29_timer_clockevent_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	RK_TIMER_INT_CLEAR(TIMER_CLKEVT);
	if (evt->mode == CLOCK_EVT_MODE_ONESHOT)
		RK_TIMER_DISABLE(TIMER_CLKEVT);

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction rk29_timer_clockevent_irq = {
	.name		= TIMER_CLKEVT_NAME,
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= rk29_timer_clockevent_interrupt,
	.irq		= IRQ_NR_TIMER_CLKEVT,
	.dev_id		= &rk29_timer_clockevent,
};

static __init int rk29_timer_init_clockevent(void)
{
	struct clock_event_device *ce = &rk29_timer_clockevent;
	struct clk *clk = clk_get(NULL, TIMER_CLKEVT_NAME);
	struct clk *pclk_periph = clk_get(NULL, "pclk_periph");

	clk_set_parent(clk, pclk_periph);
	clk_enable(clk);

	RK_TIMER_DISABLE(TIMER_CLKEVT);

	setup_irq(rk29_timer_clockevent_irq.irq, &rk29_timer_clockevent_irq);

	clockevents_calc_mult_shift(ce, clk_get_rate(clk), 4);
	ce->max_delta_ns = clockevent_delta2ns(0xFFFFFFFFUL, ce);
	ce->min_delta_ns = clockevent_delta2ns(1, ce) + 1;

	ce->cpumask = cpumask_of(0);

	clockevents_register_device(ce);

	return 0;
}

static cycle_t rk29_timer_read(struct clocksource *cs)
{
	return ~RK_TIMER_READVALUE(TIMER_CLKSRC);
}

#define MASK	(u32)~0

static struct clocksource rk29_timer_clocksource = {
	.name           = TIMER_CLKSRC_NAME,
	.rating         = 200,
	.read           = rk29_timer_read,
	.mask           = CLOCKSOURCE_MASK(32),
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init rk29_timer_init_clocksource(void)
{
	static char err[] __initdata = KERN_ERR "%s: can't register clocksource!\n";
	struct clocksource *cs = &rk29_timer_clocksource;
	struct clk *clk = clk_get(NULL, TIMER_CLKSRC_NAME);
	struct clk *pclk_periph = clk_get(NULL, "pclk_periph");

	clk_set_parent(clk, pclk_periph);
	clk_enable(clk);

	RK_TIMER_DISABLE(TIMER_CLKSRC);
	RK_TIMER_SETCOUNT(TIMER_CLKSRC, 0xFFFFFFFF);
	RK_TIMER_ENABLE_FREE_RUNNING(TIMER_CLKSRC);

	clocksource_calc_mult_shift(cs, clk_get_rate(clk), 60);
	if (clocksource_register(cs))
		printk(err, cs->name);
}

static DEFINE_CLOCK_DATA(cd);

unsigned long long notrace sched_clock(void)
{
	u32 cyc = ~RK_TIMER_READVALUE(TIMER_CLKSRC);
	const struct clocksource *cs = &rk29_timer_clocksource;
	return cyc_to_fixed_sched_clock(&cd, cyc, MASK, cs->mult, cs->shift);
}

static void notrace rk29_update_sched_clock(void)
{
	u32 cyc = ~RK_TIMER_READVALUE(TIMER_CLKSRC);
	update_sched_clock(&cd, cyc, MASK);
}

static void __init rk29_sched_clock_init(void)
{
	init_sched_clock(&cd, rk29_update_sched_clock, 32,
			 clk_get_rate(clk_get(NULL, TIMER_CLKSRC_NAME)));
}

static void __init rk29_timer_init(void)
{
	rk29_timer_init_clocksource();
	rk29_timer_init_clockevent();
	rk29_sched_clock_init();
}

struct sys_timer rk29_timer = {
	.init = rk29_timer_init
};

