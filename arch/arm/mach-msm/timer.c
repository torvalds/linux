/*
 *
 * Copyright (C) 2007 Google, Inc.
 * Copyright (c) 2009-2011, Code Aurora Forum. All rights reserved.
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

#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/io.h>

#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/localtimer.h>

#include <mach/msm_iomap.h>
#include <mach/cpu.h>
#include <mach/board.h>

#define TIMER_MATCH_VAL         0x0000
#define TIMER_COUNT_VAL         0x0004
#define TIMER_ENABLE            0x0008
#define TIMER_ENABLE_CLR_ON_MATCH_EN    BIT(1)
#define TIMER_ENABLE_EN                 BIT(0)
#define TIMER_CLEAR             0x000C
#define DGT_CLK_CTL             0x0034
#define DGT_CLK_CTL_DIV_4	0x3

#define GPT_HZ 32768

#define MSM_DGT_SHIFT 5

static void __iomem *event_base;

static irqreturn_t msm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = *(struct clock_event_device **)dev_id;
	/* Stop the timer tick */
	if (evt->mode == CLOCK_EVT_MODE_ONESHOT) {
		u32 ctrl = readl_relaxed(event_base + TIMER_ENABLE);
		ctrl &= ~TIMER_ENABLE_EN;
		writel_relaxed(ctrl, event_base + TIMER_ENABLE);
	}
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static int msm_timer_set_next_event(unsigned long cycles,
				    struct clock_event_device *evt)
{
	u32 ctrl = readl_relaxed(event_base + TIMER_ENABLE);

	writel_relaxed(0, event_base + TIMER_CLEAR);
	writel_relaxed(cycles, event_base + TIMER_MATCH_VAL);
	writel_relaxed(ctrl | TIMER_ENABLE_EN, event_base + TIMER_ENABLE);
	return 0;
}

static void msm_timer_set_mode(enum clock_event_mode mode,
			      struct clock_event_device *evt)
{
	u32 ctrl;

	ctrl = readl_relaxed(event_base + TIMER_ENABLE);
	ctrl &= ~(TIMER_ENABLE_EN | TIMER_ENABLE_CLR_ON_MATCH_EN);

	switch (mode) {
	case CLOCK_EVT_MODE_RESUME:
	case CLOCK_EVT_MODE_PERIODIC:
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* Timer is enabled in set_next_event */
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		break;
	}
	writel_relaxed(ctrl, event_base + TIMER_ENABLE);
}

static struct clock_event_device msm_clockevent = {
	.name		= "gp_timer",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.rating		= 200,
	.set_next_event	= msm_timer_set_next_event,
	.set_mode	= msm_timer_set_mode,
};

static union {
	struct clock_event_device *evt;
	struct clock_event_device __percpu **percpu_evt;
} msm_evt;

static void __iomem *source_base;

static cycle_t msm_read_timer_count(struct clocksource *cs)
{
	return readl_relaxed(source_base + TIMER_COUNT_VAL);
}

static cycle_t msm_read_timer_count_shift(struct clocksource *cs)
{
	/*
	 * Shift timer count down by a constant due to unreliable lower bits
	 * on some targets.
	 */
	return msm_read_timer_count(cs) >> MSM_DGT_SHIFT;
}

static struct clocksource msm_clocksource = {
	.name	= "dg_timer",
	.rating	= 300,
	.read	= msm_read_timer_count,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

#ifdef CONFIG_LOCAL_TIMERS
static int __cpuinit msm_local_timer_setup(struct clock_event_device *evt)
{
	/* Use existing clock_event for cpu 0 */
	if (!smp_processor_id())
		return 0;

	writel_relaxed(0, event_base + TIMER_ENABLE);
	writel_relaxed(0, event_base + TIMER_CLEAR);
	writel_relaxed(~0, event_base + TIMER_MATCH_VAL);
	evt->irq = msm_clockevent.irq;
	evt->name = "local_timer";
	evt->features = msm_clockevent.features;
	evt->rating = msm_clockevent.rating;
	evt->set_mode = msm_timer_set_mode;
	evt->set_next_event = msm_timer_set_next_event;
	evt->shift = msm_clockevent.shift;
	evt->mult = div_sc(GPT_HZ, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(0xf0000000, evt);
	evt->min_delta_ns = clockevent_delta2ns(4, evt);

	*__this_cpu_ptr(msm_evt.percpu_evt) = evt;
	clockevents_register_device(evt);
	enable_percpu_irq(evt->irq, 0);
	return 0;
}

static void msm_local_timer_stop(struct clock_event_device *evt)
{
	evt->set_mode(CLOCK_EVT_MODE_UNUSED, evt);
	disable_percpu_irq(evt->irq);
}

static struct local_timer_ops msm_local_timer_ops __cpuinitdata = {
	.setup	= msm_local_timer_setup,
	.stop	= msm_local_timer_stop,
};
#endif /* CONFIG_LOCAL_TIMERS */

static void __init msm_timer_init(void)
{
	struct clock_event_device *ce = &msm_clockevent;
	struct clocksource *cs = &msm_clocksource;
	int res;
	u32 dgt_hz;

	if (cpu_is_msm7x01()) {
		event_base = MSM_CSR_BASE;
		source_base = MSM_CSR_BASE + 0x10;
		dgt_hz = 19200000 >> MSM_DGT_SHIFT; /* 600 KHz */
		cs->read = msm_read_timer_count_shift;
		cs->mask = CLOCKSOURCE_MASK((32 - MSM_DGT_SHIFT));
	} else if (cpu_is_msm7x30()) {
		event_base = MSM_CSR_BASE + 0x04;
		source_base = MSM_CSR_BASE + 0x24;
		dgt_hz = 24576000 / 4;
	} else if (cpu_is_qsd8x50()) {
		event_base = MSM_CSR_BASE;
		source_base = MSM_CSR_BASE + 0x10;
		dgt_hz = 19200000 / 4;
	} else if (cpu_is_msm8x60() || cpu_is_msm8960()) {
		event_base = MSM_TMR_BASE + 0x04;
		/* Use CPU0's timer as the global clock source. */
		source_base = MSM_TMR0_BASE + 0x24;
		dgt_hz = 27000000 / 4;
		writel_relaxed(DGT_CLK_CTL_DIV_4, MSM_TMR_BASE + DGT_CLK_CTL);
	} else
		BUG();

	writel_relaxed(0, event_base + TIMER_ENABLE);
	writel_relaxed(0, event_base + TIMER_CLEAR);
	writel_relaxed(~0, event_base + TIMER_MATCH_VAL);
	ce->cpumask = cpumask_of(0);

	ce->irq = INT_GP_TIMER_EXP;
	clockevents_config_and_register(ce, GPT_HZ, 4, 0xffffffff);
	if (cpu_is_msm8x60() || cpu_is_msm8960()) {
		msm_evt.percpu_evt = alloc_percpu(struct clock_event_device *);
		if (!msm_evt.percpu_evt) {
			pr_err("memory allocation failed for %s\n", ce->name);
			goto err;
		}
		*__this_cpu_ptr(msm_evt.percpu_evt) = ce;
		res = request_percpu_irq(ce->irq, msm_timer_interrupt,
					 ce->name, msm_evt.percpu_evt);
		if (!res) {
			enable_percpu_irq(ce->irq, 0);
#ifdef CONFIG_LOCAL_TIMERS
			local_timer_register(&msm_local_timer_ops);
#endif
		}
	} else {
		msm_evt.evt = ce;
		res = request_irq(ce->irq, msm_timer_interrupt,
				  IRQF_TIMER | IRQF_NOBALANCING |
				  IRQF_TRIGGER_RISING, ce->name, &msm_evt.evt);
	}

	if (res)
		pr_err("request_irq failed for %s\n", ce->name);
err:
	writel_relaxed(TIMER_ENABLE_EN, source_base + TIMER_ENABLE);
	res = clocksource_register_hz(cs, dgt_hz);
	if (res)
		pr_err("clocksource_register failed\n");
}

struct sys_timer msm_timer = {
	.init = msm_timer_init
};
