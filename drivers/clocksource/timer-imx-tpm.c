// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2016 Freescale Semiconductor, Inc.
// Copyright 2017 NXP

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/sched_clock.h>

#include "timer-of.h"

#define TPM_PARAM			0x4
#define TPM_PARAM_WIDTH_SHIFT		16
#define TPM_PARAM_WIDTH_MASK		(0xff << 16)
#define TPM_SC				0x10
#define TPM_SC_CMOD_INC_PER_CNT		(0x1 << 3)
#define TPM_SC_CMOD_DIV_DEFAULT		0x3
#define TPM_SC_CMOD_DIV_MAX		0x7
#define TPM_SC_TOF_MASK			(0x1 << 7)
#define TPM_CNT				0x14
#define TPM_MOD				0x18
#define TPM_STATUS			0x1c
#define TPM_STATUS_CH0F			BIT(0)
#define TPM_C0SC			0x20
#define TPM_C0SC_CHIE			BIT(6)
#define TPM_C0SC_MODE_SHIFT		2
#define TPM_C0SC_MODE_MASK		0x3c
#define TPM_C0SC_MODE_SW_COMPARE	0x4
#define TPM_C0SC_CHF_MASK		(0x1 << 7)
#define TPM_C0V				0x24

static int counter_width __ro_after_init;
static void __iomem *timer_base __ro_after_init;

static inline void tpm_timer_disable(void)
{
	unsigned int val;

	/* channel disable */
	val = readl(timer_base + TPM_C0SC);
	val &= ~(TPM_C0SC_MODE_MASK | TPM_C0SC_CHIE);
	writel(val, timer_base + TPM_C0SC);
}

static inline void tpm_timer_enable(void)
{
	unsigned int val;

	/* channel enabled in sw compare mode */
	val = readl(timer_base + TPM_C0SC);
	val |= (TPM_C0SC_MODE_SW_COMPARE << TPM_C0SC_MODE_SHIFT) |
	       TPM_C0SC_CHIE;
	writel(val, timer_base + TPM_C0SC);
}

static inline void tpm_irq_acknowledge(void)
{
	writel(TPM_STATUS_CH0F, timer_base + TPM_STATUS);
}

static inline unsigned long tpm_read_counter(void)
{
	return readl(timer_base + TPM_CNT);
}

#if defined(CONFIG_ARM)
static struct delay_timer tpm_delay_timer;

static unsigned long tpm_read_current_timer(void)
{
	return tpm_read_counter();
}

static u64 notrace tpm_read_sched_clock(void)
{
	return tpm_read_counter();
}
#endif

static int tpm_set_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	unsigned long next, prev, now;

	prev = tpm_read_counter();
	next = prev + delta;
	writel(next, timer_base + TPM_C0V);
	now = tpm_read_counter();

	/*
	 * NOTE: We observed in a very small probability, the bus fabric
	 * contention between GPU and A7 may results a few cycles delay
	 * of writing CNT registers which may cause the min_delta event got
	 * missed, so we need add a ETIME check here in case it happened.
	 */
	return (now - prev) >= delta ? -ETIME : 0;
}

static int tpm_set_state_oneshot(struct clock_event_device *evt)
{
	tpm_timer_enable();

	return 0;
}

static int tpm_set_state_shutdown(struct clock_event_device *evt)
{
	tpm_timer_disable();

	return 0;
}

static irqreturn_t tpm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	tpm_irq_acknowledge();

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct timer_of to_tpm = {
	.flags = TIMER_OF_IRQ | TIMER_OF_BASE | TIMER_OF_CLOCK,
	.clkevt = {
		.name			= "i.MX TPM Timer",
		.rating			= 200,
		.features		= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_DYNIRQ,
		.set_state_shutdown	= tpm_set_state_shutdown,
		.set_state_oneshot	= tpm_set_state_oneshot,
		.set_next_event		= tpm_set_next_event,
		.cpumask		= cpu_possible_mask,
	},
	.of_irq = {
		.handler		= tpm_timer_interrupt,
		.flags			= IRQF_TIMER,
	},
	.of_clk = {
		.name = "per",
	},
};

static int __init tpm_clocksource_init(void)
{
#if defined(CONFIG_ARM)
	tpm_delay_timer.read_current_timer = &tpm_read_current_timer;
	tpm_delay_timer.freq = timer_of_rate(&to_tpm) >> 3;
	register_current_timer_delay(&tpm_delay_timer);

	sched_clock_register(tpm_read_sched_clock, counter_width,
			     timer_of_rate(&to_tpm) >> 3);
#endif

	return clocksource_mmio_init(timer_base + TPM_CNT,
				     "imx-tpm",
				     timer_of_rate(&to_tpm) >> 3,
				     to_tpm.clkevt.rating,
				     counter_width,
				     clocksource_mmio_readl_up);
}

static void __init tpm_clockevent_init(void)
{
	clockevents_config_and_register(&to_tpm.clkevt,
					timer_of_rate(&to_tpm) >> 3,
					300,
					GENMASK(counter_width - 1,
					1));
}

static int __init tpm_timer_init(struct device_node *np)
{
	struct clk *ipg;
	int ret;

	ipg = of_clk_get_by_name(np, "ipg");
	if (IS_ERR(ipg)) {
		pr_err("tpm: failed to get ipg clk\n");
		return -ENODEV;
	}
	/* enable clk before accessing registers */
	ret = clk_prepare_enable(ipg);
	if (ret) {
		pr_err("tpm: ipg clock enable failed (%d)\n", ret);
		clk_put(ipg);
		return ret;
	}

	ret = timer_of_init(np, &to_tpm);
	if (ret)
		return ret;

	timer_base = timer_of_base(&to_tpm);

	counter_width = (readl(timer_base + TPM_PARAM)
		& TPM_PARAM_WIDTH_MASK) >> TPM_PARAM_WIDTH_SHIFT;
	/* use rating 200 for 32-bit counter and 150 for 16-bit counter */
	to_tpm.clkevt.rating = counter_width == 0x20 ? 200 : 150;

	/*
	 * Initialize tpm module to a known state
	 * 1) Counter disabled
	 * 2) TPM counter operates in up counting mode
	 * 3) Timer Overflow Interrupt disabled
	 * 4) Channel0 disabled
	 * 5) DMA transfers disabled
	 */
	/* make sure counter is disabled */
	writel(0, timer_base + TPM_SC);
	/* TOF is W1C */
	writel(TPM_SC_TOF_MASK, timer_base + TPM_SC);
	writel(0, timer_base + TPM_CNT);
	/* CHF is W1C */
	writel(TPM_C0SC_CHF_MASK, timer_base + TPM_C0SC);

	/*
	 * increase per cnt,
	 * div 8 for 32-bit counter and div 128 for 16-bit counter
	 */
	writel(TPM_SC_CMOD_INC_PER_CNT |
		(counter_width == 0x20 ?
		TPM_SC_CMOD_DIV_DEFAULT : TPM_SC_CMOD_DIV_MAX),
		timer_base + TPM_SC);

	/* set MOD register to maximum for free running mode */
	writel(GENMASK(counter_width - 1, 0), timer_base + TPM_MOD);

	tpm_clockevent_init();

	return tpm_clocksource_init();
}
TIMER_OF_DECLARE(imx7ulp, "fsl,imx7ulp-tpm", tpm_timer_init);
