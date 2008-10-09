/* linux/arch/arm/mach-msm/timer.c
 *
 * Copyright (C) 2007 Google, Inc.
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
#include <mach/msm_iomap.h>

#define MSM_DGT_BASE (MSM_GPT_BASE + 0x10)
#define MSM_DGT_SHIFT (5)

#define TIMER_MATCH_VAL         0x0000
#define TIMER_COUNT_VAL         0x0004
#define TIMER_ENABLE            0x0008
#define TIMER_ENABLE_CLR_ON_MATCH_EN    2
#define TIMER_ENABLE_EN                 1
#define TIMER_CLEAR             0x000C

#define CSR_PROTECTION          0x0020
#define CSR_PROTECTION_EN               1

#define GPT_HZ 32768
#define DGT_HZ 19200000 /* 19.2 MHz or 600 KHz after shift */

struct msm_clock {
	struct clock_event_device   clockevent;
	struct clocksource          clocksource;
	struct irqaction            irq;
	uint32_t                    regbase;
	uint32_t                    freq;
	uint32_t                    shift;
};

static irqreturn_t msm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static cycle_t msm_gpt_read(void)
{
	return readl(MSM_GPT_BASE + TIMER_COUNT_VAL);
}

static cycle_t msm_dgt_read(void)
{
	return readl(MSM_DGT_BASE + TIMER_COUNT_VAL) >> MSM_DGT_SHIFT;
}

static int msm_timer_set_next_event(unsigned long cycles,
				    struct clock_event_device *evt)
{
	struct msm_clock *clock = container_of(evt, struct msm_clock, clockevent);
	uint32_t now = readl(clock->regbase + TIMER_COUNT_VAL);
	uint32_t alarm = now + (cycles << clock->shift);
	int late;

	writel(alarm, clock->regbase + TIMER_MATCH_VAL);
	now = readl(clock->regbase + TIMER_COUNT_VAL);
	late = now - alarm;
	if (late >= (-2 << clock->shift) && late < DGT_HZ*5) {
		printk(KERN_NOTICE "msm_timer_set_next_event(%lu) clock %s, "
		       "alarm already expired, now %x, alarm %x, late %d\n",
		       cycles, clock->clockevent.name, now, alarm, late);
		return -ETIME;
	}
	return 0;
}

static void msm_timer_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	struct msm_clock *clock = container_of(evt, struct msm_clock, clockevent);
	switch (mode) {
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		writel(TIMER_ENABLE_EN, clock->regbase + TIMER_ENABLE);
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		writel(0, clock->regbase + TIMER_ENABLE);
		break;
	}
}

static struct msm_clock msm_clocks[] = {
	{
		.clockevent = {
			.name           = "gp_timer",
			.features       = CLOCK_EVT_FEAT_ONESHOT,
			.shift          = 32,
			.rating         = 200,
			.set_next_event = msm_timer_set_next_event,
			.set_mode       = msm_timer_set_mode,
		},
		.clocksource = {
			.name           = "gp_timer",
			.rating         = 200,
			.read           = msm_gpt_read,
			.mask           = CLOCKSOURCE_MASK(32),
			.shift          = 24,
			.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
		},
		.irq = {
			.name    = "gp_timer",
			.flags   = IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_RISING,
			.handler = msm_timer_interrupt,
			.dev_id  = &msm_clocks[0].clockevent,
			.irq     = INT_GP_TIMER_EXP
		},
		.regbase = MSM_GPT_BASE,
		.freq = GPT_HZ
	},
	{
		.clockevent = {
			.name           = "dg_timer",
			.features       = CLOCK_EVT_FEAT_ONESHOT,
			.shift          = 32 + MSM_DGT_SHIFT,
			.rating         = 300,
			.set_next_event = msm_timer_set_next_event,
			.set_mode       = msm_timer_set_mode,
		},
		.clocksource = {
			.name           = "dg_timer",
			.rating         = 300,
			.read           = msm_dgt_read,
			.mask           = CLOCKSOURCE_MASK((32 - MSM_DGT_SHIFT)),
			.shift          = 24 - MSM_DGT_SHIFT,
			.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
		},
		.irq = {
			.name    = "dg_timer",
			.flags   = IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_RISING,
			.handler = msm_timer_interrupt,
			.dev_id  = &msm_clocks[1].clockevent,
			.irq     = INT_DEBUG_TIMER_EXP
		},
		.regbase = MSM_DGT_BASE,
		.freq = DGT_HZ >> MSM_DGT_SHIFT,
		.shift = MSM_DGT_SHIFT
	}
};

static void __init msm_timer_init(void)
{
	int i;
	int res;

	for (i = 0; i < ARRAY_SIZE(msm_clocks); i++) {
		struct msm_clock *clock = &msm_clocks[i];
		struct clock_event_device *ce = &clock->clockevent;
		struct clocksource *cs = &clock->clocksource;
		writel(0, clock->regbase + TIMER_ENABLE);
		writel(0, clock->regbase + TIMER_CLEAR);
		writel(~0, clock->regbase + TIMER_MATCH_VAL);

		ce->mult = div_sc(clock->freq, NSEC_PER_SEC, ce->shift);
		/* allow at least 10 seconds to notice that the timer wrapped */
		ce->max_delta_ns =
			clockevent_delta2ns(0xf0000000 >> clock->shift, ce);
		/* 4 gets rounded down to 3 */
		ce->min_delta_ns = clockevent_delta2ns(4, ce);
		ce->cpumask = cpumask_of_cpu(0);

		cs->mult = clocksource_hz2mult(clock->freq, cs->shift);
		res = clocksource_register(cs);
		if (res)
			printk(KERN_ERR "msm_timer_init: clocksource_register "
			       "failed for %s\n", cs->name);

		res = setup_irq(clock->irq.irq, &clock->irq);
		if (res)
			printk(KERN_ERR "msm_timer_init: setup_irq "
			       "failed for %s\n", cs->name);

		clockevents_register_device(ce);
	}
}

struct sys_timer msm_timer = {
	.init = msm_timer_init
};
