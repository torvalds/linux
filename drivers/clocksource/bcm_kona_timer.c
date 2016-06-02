/*
 * Copyright (C) 2012 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/clockchips.h>
#include <linux/types.h>
#include <linux/clk.h>

#include <linux/io.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>


#define KONA_GPTIMER_STCS_OFFSET			0x00000000
#define KONA_GPTIMER_STCLO_OFFSET			0x00000004
#define KONA_GPTIMER_STCHI_OFFSET			0x00000008
#define KONA_GPTIMER_STCM0_OFFSET			0x0000000C

#define KONA_GPTIMER_STCS_TIMER_MATCH_SHIFT		0
#define KONA_GPTIMER_STCS_COMPARE_ENABLE_SHIFT		4

struct kona_bcm_timers {
	int tmr_irq;
	void __iomem *tmr_regs;
};

static struct kona_bcm_timers timers;

static u32 arch_timer_rate;

/*
 * We use the peripheral timers for system tick, the cpu global timer for
 * profile tick
 */
static void kona_timer_disable_and_clear(void __iomem *base)
{
	uint32_t reg;

	/*
	 * clear and disable interrupts
	 * We are using compare/match register 0 for our system interrupts
	 */
	reg = readl(base + KONA_GPTIMER_STCS_OFFSET);

	/* Clear compare (0) interrupt */
	reg |= 1 << KONA_GPTIMER_STCS_TIMER_MATCH_SHIFT;
	/* disable compare */
	reg &= ~(1 << KONA_GPTIMER_STCS_COMPARE_ENABLE_SHIFT);

	writel(reg, base + KONA_GPTIMER_STCS_OFFSET);

}

static void
kona_timer_get_counter(void __iomem *timer_base, uint32_t *msw, uint32_t *lsw)
{
	int loop_limit = 4;

	/*
	 * Read 64-bit free running counter
	 * 1. Read hi-word
	 * 2. Read low-word
	 * 3. Read hi-word again
	 * 4.1
	 *      if new hi-word is not equal to previously read hi-word, then
	 *      start from #1
	 * 4.2
	 *      if new hi-word is equal to previously read hi-word then stop.
	 */

	while (--loop_limit) {
		*msw = readl(timer_base + KONA_GPTIMER_STCHI_OFFSET);
		*lsw = readl(timer_base + KONA_GPTIMER_STCLO_OFFSET);
		if (*msw == readl(timer_base + KONA_GPTIMER_STCHI_OFFSET))
			break;
	}
	if (!loop_limit) {
		pr_err("bcm_kona_timer: getting counter failed.\n");
		pr_err(" Timer will be impacted\n");
	}

	return;
}

static int kona_timer_set_next_event(unsigned long clc,
				  struct clock_event_device *unused)
{
	/*
	 * timer (0) is disabled by the timer interrupt already
	 * so, here we reload the next event value and re-enable
	 * the timer.
	 *
	 * This way, we are potentially losing the time between
	 * timer-interrupt->set_next_event. CPU local timers, when
	 * they come in should get rid of skew.
	 */

	uint32_t lsw, msw;
	uint32_t reg;

	kona_timer_get_counter(timers.tmr_regs, &msw, &lsw);

	/* Load the "next" event tick value */
	writel(lsw + clc, timers.tmr_regs + KONA_GPTIMER_STCM0_OFFSET);

	/* Enable compare */
	reg = readl(timers.tmr_regs + KONA_GPTIMER_STCS_OFFSET);
	reg |= (1 << KONA_GPTIMER_STCS_COMPARE_ENABLE_SHIFT);
	writel(reg, timers.tmr_regs + KONA_GPTIMER_STCS_OFFSET);

	return 0;
}

static int kona_timer_shutdown(struct clock_event_device *evt)
{
	kona_timer_disable_and_clear(timers.tmr_regs);
	return 0;
}

static struct clock_event_device kona_clockevent_timer = {
	.name = "timer 1",
	.features = CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event = kona_timer_set_next_event,
	.set_state_shutdown = kona_timer_shutdown,
	.tick_resume = kona_timer_shutdown,
};

static void __init kona_timer_clockevents_init(void)
{
	kona_clockevent_timer.cpumask = cpumask_of(0);
	clockevents_config_and_register(&kona_clockevent_timer,
		arch_timer_rate, 6, 0xffffffff);
}

static irqreturn_t kona_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &kona_clockevent_timer;

	kona_timer_disable_and_clear(timers.tmr_regs);
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction kona_timer_irq = {
	.name = "Kona Timer Tick",
	.flags = IRQF_TIMER,
	.handler = kona_timer_interrupt,
};

static int __init kona_timer_init(struct device_node *node)
{
	u32 freq;
	struct clk *external_clk;

	external_clk = of_clk_get_by_name(node, NULL);

	if (!IS_ERR(external_clk)) {
		arch_timer_rate = clk_get_rate(external_clk);
		clk_prepare_enable(external_clk);
	} else if (!of_property_read_u32(node, "clock-frequency", &freq)) {
		arch_timer_rate = freq;
	} else {
		pr_err("Kona Timer v1 unable to determine clock-frequency");
		return -EINVAL;
	}

	/* Setup IRQ numbers */
	timers.tmr_irq = irq_of_parse_and_map(node, 0);

	/* Setup IO addresses */
	timers.tmr_regs = of_iomap(node, 0);

	kona_timer_disable_and_clear(timers.tmr_regs);

	kona_timer_clockevents_init();
	setup_irq(timers.tmr_irq, &kona_timer_irq);
	kona_timer_set_next_event((arch_timer_rate / HZ), NULL);

	return 0;
}

CLOCKSOURCE_OF_DECLARE(brcm_kona, "brcm,kona-timer", kona_timer_init);
/*
 * bcm,kona-timer is deprecated by brcm,kona-timer
 * being kept here for driver compatibility
 */
CLOCKSOURCE_OF_DECLARE(bcm_kona, "bcm,kona-timer", kona_timer_init);
