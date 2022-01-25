// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2017-2019 NXP

#include <linux/interrupt.h>
#include <linux/clockchips.h>

#include "timer-of.h"

#define CMP_OFFSET	0x10000

#define CNTCV_LO	0x8
#define CNTCV_HI	0xc
#define CMPCV_LO	(CMP_OFFSET + 0x20)
#define CMPCV_HI	(CMP_OFFSET + 0x24)
#define CMPCR		(CMP_OFFSET + 0x2c)

#define SYS_CTR_EN		0x1
#define SYS_CTR_IRQ_MASK	0x2

#define SYS_CTR_CLK_DIV		0x3

static void __iomem *sys_ctr_base __ro_after_init;
static u32 cmpcr __ro_after_init;

static void sysctr_timer_enable(bool enable)
{
	writel(enable ? cmpcr | SYS_CTR_EN : cmpcr, sys_ctr_base + CMPCR);
}

static void sysctr_irq_acknowledge(void)
{
	/*
	 * clear the enable bit(EN =0) will clear
	 * the status bit(ISTAT = 0), then the interrupt
	 * signal will be negated(acknowledged).
	 */
	sysctr_timer_enable(false);
}

static inline u64 sysctr_read_counter(void)
{
	u32 cnt_hi, tmp_hi, cnt_lo;

	do {
		cnt_hi = readl_relaxed(sys_ctr_base + CNTCV_HI);
		cnt_lo = readl_relaxed(sys_ctr_base + CNTCV_LO);
		tmp_hi = readl_relaxed(sys_ctr_base + CNTCV_HI);
	} while (tmp_hi != cnt_hi);

	return  ((u64) cnt_hi << 32) | cnt_lo;
}

static int sysctr_set_next_event(unsigned long delta,
				 struct clock_event_device *evt)
{
	u32 cmp_hi, cmp_lo;
	u64 next;

	sysctr_timer_enable(false);

	next = sysctr_read_counter();

	next += delta;

	cmp_hi = (next >> 32) & 0x00fffff;
	cmp_lo = next & 0xffffffff;

	writel_relaxed(cmp_hi, sys_ctr_base + CMPCV_HI);
	writel_relaxed(cmp_lo, sys_ctr_base + CMPCV_LO);

	sysctr_timer_enable(true);

	return 0;
}

static int sysctr_set_state_oneshot(struct clock_event_device *evt)
{
	return 0;
}

static int sysctr_set_state_shutdown(struct clock_event_device *evt)
{
	sysctr_timer_enable(false);

	return 0;
}

static irqreturn_t sysctr_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	sysctr_irq_acknowledge();

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct timer_of to_sysctr = {
	.flags = TIMER_OF_IRQ | TIMER_OF_CLOCK | TIMER_OF_BASE,
	.clkevt = {
		.name			= "i.MX system counter timer",
		.features		= CLOCK_EVT_FEAT_ONESHOT |
						CLOCK_EVT_FEAT_DYNIRQ,
		.set_state_oneshot	= sysctr_set_state_oneshot,
		.set_next_event		= sysctr_set_next_event,
		.set_state_shutdown	= sysctr_set_state_shutdown,
		.rating			= 200,
	},
	.of_irq = {
		.handler		= sysctr_timer_interrupt,
		.flags			= IRQF_TIMER | IRQF_IRQPOLL,
	},
	.of_clk = {
		.name = "per",
	},
};

static void __init sysctr_clockevent_init(void)
{
	to_sysctr.clkevt.cpumask = cpu_possible_mask;

	clockevents_config_and_register(&to_sysctr.clkevt,
					timer_of_rate(&to_sysctr),
					0xff, 0x7fffffff);
}

static int __init sysctr_timer_init(struct device_node *np)
{
	int ret = 0;

	ret = timer_of_init(np, &to_sysctr);
	if (ret)
		return ret;

	/* system counter clock is divided by 3 internally */
	to_sysctr.of_clk.rate /= SYS_CTR_CLK_DIV;

	sys_ctr_base = timer_of_base(&to_sysctr);
	cmpcr = readl(sys_ctr_base + CMPCR);
	cmpcr &= ~SYS_CTR_EN;

	sysctr_clockevent_init();

	return 0;
}
TIMER_OF_DECLARE(sysctr_timer, "nxp,sysctr-timer", sysctr_timer_init);
