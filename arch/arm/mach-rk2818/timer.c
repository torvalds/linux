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
#include <linux/delay.h>
#include <linux/io.h>

#include <asm/mach/time.h>
#include <mach/rk2818_iomap.h>

#define RK2818_TIMER1_BASE      RK2818_TIMER_BASE
#define RK2818_TIMER2_BASE      RK2818_TIMER_BASE + 0x14
#define RK2818_TIMER3_BASE      RK2818_TIMER_BASE + 0x28
#define TIMER_LOAD_COUNT		0x0000
#define TIMER_CUR_VALUE			0x0004
#define TIMER_CONTROL_REG		0x0008
#define TIMER_EOI		        0x000C
#define TIMER_INT_STATUS		0x0010

#define TIMER_MATCH_VAL         0x0000
#define TIMER_COUNT_VAL         0x0004
#define TIMER_ENABLE            0x0008
#define TIMER_ENABLE_CLR_ON_MATCH_EN    2
#define TIMER_ENABLE_EN                 3
#define TIMER_CLEAR             0x000C

#define CSR_PROTECTION          0x0020
#define CSR_PROTECTION_EN               1

#define TIMER_HZ	24000000 
#define timer_cycle	(TIMER_HZ+HZ/2)/HZ
uint32_t	tcount;
uint32_t	mycycles = 0;
static int	pit_cnt=0;
struct rk2818_clock {
	struct clock_event_device   clockevent;
	struct clocksource          clocksource;
	struct irqaction            irq;
	uint32_t                    regbase;
	uint32_t                    freq;
	uint32_t                    shift;
};

static irqreturn_t rk2818_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	struct rk2818_clock *clock = container_of(evt, struct rk2818_clock, clockevent);
	readl(clock->regbase + TIMER_EOI);
	pit_cnt +=mycycles;
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static cycle_t rk2818_timer_read(struct clocksource *cs)
{

	unsigned int elapsed;
	unsigned int t;

	t = readl(RK2818_TIMER3_BASE + TIMER_LOAD_COUNT);

	elapsed = __raw_readl(RK2818_TIMER3_BASE + TIMER_CUR_VALUE);
	
	elapsed = t - elapsed;
	elapsed += pit_cnt;
    return elapsed;
}

static int rk2818_timer_set_next_event(unsigned long cycles,
				    struct clock_event_device *evt)
{
	
	struct rk2818_clock *clock = container_of(evt, struct rk2818_clock, clockevent);

    writel(4, clock->regbase + TIMER_CONTROL_REG);
	mycycles = cycles;
    writel(cycles, clock->regbase + TIMER_LOAD_COUNT);
    writel(TIMER_ENABLE_EN, clock->regbase + TIMER_CONTROL_REG);
	return 0;
}

static void rk2818_timer_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	struct rk2818_clock *clock = container_of(evt, struct rk2818_clock, clockevent);
	printk("%s::Enter--mode is %d\n",__FUNCTION__,mode);
	switch (mode) {
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		readl(clock->regbase+TIMER_EOI);
		writel((TIMER_HZ+ HZ/2) / HZ,clock->regbase+TIMER_LOAD_COUNT);
		writel(TIMER_ENABLE_EN, clock->regbase + TIMER_CONTROL_REG);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		writel(4, clock->regbase + TIMER_CONTROL_REG);
		break;
	}
}

static struct rk2818_clock rk2818_system_clocks[] = {
	{
		.clockevent = {
			.name           = "timer",
			.features       = CLOCK_EVT_FEAT_ONESHOT,
			.shift          = 32,
			.rating         = 200,
			.set_next_event = rk2818_timer_set_next_event,
			.set_mode       = rk2818_timer_set_mode,
		},
		.clocksource = {
			.name           = "timer",
			.rating         = 200,
			.read           = rk2818_timer_read,
			.mask           = CLOCKSOURCE_MASK(24),
			.shift          = 26,
			.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
		},
		.irq = {
			.name    = "timer",
			.flags   = IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_RISING,
			.handler = rk2818_timer_interrupt,
			.dev_id  = &rk2818_system_clocks[0].clockevent,
			.irq     = IRQ_NR_TIMER3 
		},
		.regbase = RK2818_TIMER3_BASE,
		.freq = TIMER_HZ
	},
};

static void __init rk2818_timer_init(void)
{
	int i;
	int res;
		
	for (i = 0; i < ARRAY_SIZE(rk2818_system_clocks); i++) {
		struct rk2818_clock *clock = &rk2818_system_clocks[i];
		struct clock_event_device *ce = &clock->clockevent;
		struct clocksource *cs = &clock->clocksource;
		
		printk("%s::Enter %d\n",__FUNCTION__,i+1);		
		writel((TIMER_HZ+ HZ/2) / HZ,clock->regbase+TIMER_LOAD_COUNT);

		writel(0x04, clock->regbase + TIMER_CONTROL_REG);
		
		ce->mult = div_sc(clock->freq, NSEC_PER_SEC, ce->shift);
		/* allow at least 10 seconds to notice that the timer wrapped */
		ce->max_delta_ns =
			clockevent_delta2ns(0xf0000000 >> clock->shift, ce);
		/* 4 gets rounded down to 3 */
		ce->min_delta_ns = clockevent_delta2ns(4, ce);
		ce->cpumask = cpumask_of(0);

		cs->mult = clocksource_hz2mult(clock->freq, cs->shift);
		printk("mult is %x\n",cs->mult);
		res = clocksource_register(cs);
		if (res)
			printk(KERN_ERR "rk2818_timer_init: clocksource_register "
			       "failed for %s\n", cs->name);
		printk("%s::irq is %d\n",__FUNCTION__,clock->irq.irq);
		res = setup_irq(clock->irq.irq, &clock->irq);
		if (res)
			printk(KERN_ERR "rk2818_timer_init: setup_irq "
			       "failed for %s\n", cs->name);

		clockevents_register_device(ce);
	}
}

struct sys_timer rk2818_timer = {
	.init = rk2818_timer_init
};
