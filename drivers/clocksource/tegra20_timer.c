/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
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
#include <linux/err.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <linux/delay.h>

#include <asm/mach/time.h>
#include <asm/smp_twd.h>

#define RTC_SECONDS            0x08
#define RTC_SHADOW_SECONDS     0x0c
#define RTC_MILLISECONDS       0x10

#define TIMERUS_CNTR_1US 0x10
#define TIMERUS_USEC_CFG 0x14
#define TIMERUS_CNTR_FREEZE 0x4c

#define TIMER1_BASE 0x0
#define TIMER2_BASE 0x8
#define TIMER3_BASE 0x50
#define TIMER4_BASE 0x58

#define TIMER_PTV 0x0
#define TIMER_PCR 0x4

static void __iomem *timer_reg_base;
static void __iomem *rtc_base;

static struct timespec64 persistent_ts;
static u64 persistent_ms, last_persistent_ms;

static struct delay_timer tegra_delay_timer;

#define timer_writel(value, reg) \
	writel_relaxed(value, timer_reg_base + (reg))
#define timer_readl(reg) \
	readl_relaxed(timer_reg_base + (reg))

static int tegra_timer_set_next_event(unsigned long cycles,
					 struct clock_event_device *evt)
{
	u32 reg;

	reg = 0x80000000 | ((cycles > 1) ? (cycles-1) : 0);
	timer_writel(reg, TIMER3_BASE + TIMER_PTV);

	return 0;
}

static inline void timer_shutdown(struct clock_event_device *evt)
{
	timer_writel(0, TIMER3_BASE + TIMER_PTV);
}

static int tegra_timer_shutdown(struct clock_event_device *evt)
{
	timer_shutdown(evt);
	return 0;
}

static int tegra_timer_set_periodic(struct clock_event_device *evt)
{
	u32 reg = 0xC0000000 | ((1000000 / HZ) - 1);

	timer_shutdown(evt);
	timer_writel(reg, TIMER3_BASE + TIMER_PTV);
	return 0;
}

static struct clock_event_device tegra_clockevent = {
	.name			= "timer0",
	.rating			= 300,
	.features		= CLOCK_EVT_FEAT_ONESHOT |
				  CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_DYNIRQ,
	.set_next_event		= tegra_timer_set_next_event,
	.set_state_shutdown	= tegra_timer_shutdown,
	.set_state_periodic	= tegra_timer_set_periodic,
	.set_state_oneshot	= tegra_timer_shutdown,
	.tick_resume		= tegra_timer_shutdown,
};

static u64 notrace tegra_read_sched_clock(void)
{
	return timer_readl(TIMERUS_CNTR_1US);
}

/*
 * tegra_rtc_read - Reads the Tegra RTC registers
 * Care must be taken that this funciton is not called while the
 * tegra_rtc driver could be executing to avoid race conditions
 * on the RTC shadow register
 */
static u64 tegra_rtc_read_ms(void)
{
	u32 ms = readl(rtc_base + RTC_MILLISECONDS);
	u32 s = readl(rtc_base + RTC_SHADOW_SECONDS);
	return (u64)s * MSEC_PER_SEC + ms;
}

/*
 * tegra_read_persistent_clock64 -  Return time from a persistent clock.
 *
 * Reads the time from a source which isn't disabled during PM, the
 * 32k sync timer.  Convert the cycles elapsed since last read into
 * nsecs and adds to a monotonically increasing timespec64.
 * Care must be taken that this funciton is not called while the
 * tegra_rtc driver could be executing to avoid race conditions
 * on the RTC shadow register
 */
static void tegra_read_persistent_clock64(struct timespec64 *ts)
{
	u64 delta;

	last_persistent_ms = persistent_ms;
	persistent_ms = tegra_rtc_read_ms();
	delta = persistent_ms - last_persistent_ms;

	timespec64_add_ns(&persistent_ts, delta * NSEC_PER_MSEC);
	*ts = persistent_ts;
}

static unsigned long tegra_delay_timer_read_counter_long(void)
{
	return readl(timer_reg_base + TIMERUS_CNTR_1US);
}

static irqreturn_t tegra_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = (struct clock_event_device *)dev_id;
	timer_writel(1<<30, TIMER3_BASE + TIMER_PCR);
	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction tegra_timer_irq = {
	.name		= "timer0",
	.flags		= IRQF_TIMER | IRQF_TRIGGER_HIGH,
	.handler	= tegra_timer_interrupt,
	.dev_id		= &tegra_clockevent,
};

static int __init tegra20_init_timer(struct device_node *np)
{
	struct clk *clk;
	unsigned long rate;
	int ret;

	timer_reg_base = of_iomap(np, 0);
	if (!timer_reg_base) {
		pr_err("Can't map timer registers\n");
		return -ENXIO;
	}

	tegra_timer_irq.irq = irq_of_parse_and_map(np, 2);
	if (tegra_timer_irq.irq <= 0) {
		pr_err("Failed to map timer IRQ\n");
		return -EINVAL;
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		pr_warn("Unable to get timer clock. Assuming 12Mhz input clock.\n");
		rate = 12000000;
	} else {
		clk_prepare_enable(clk);
		rate = clk_get_rate(clk);
	}

	switch (rate) {
	case 12000000:
		timer_writel(0x000b, TIMERUS_USEC_CFG);
		break;
	case 13000000:
		timer_writel(0x000c, TIMERUS_USEC_CFG);
		break;
	case 19200000:
		timer_writel(0x045f, TIMERUS_USEC_CFG);
		break;
	case 26000000:
		timer_writel(0x0019, TIMERUS_USEC_CFG);
		break;
	default:
		WARN(1, "Unknown clock rate");
	}

	sched_clock_register(tegra_read_sched_clock, 32, 1000000);

	ret = clocksource_mmio_init(timer_reg_base + TIMERUS_CNTR_1US,
				    "timer_us", 1000000, 300, 32,
				    clocksource_mmio_readl_up);
	if (ret) {
		pr_err("Failed to register clocksource\n");
		return ret;
	}

	tegra_delay_timer.read_current_timer =
			tegra_delay_timer_read_counter_long;
	tegra_delay_timer.freq = 1000000;
	register_current_timer_delay(&tegra_delay_timer);

	ret = setup_irq(tegra_timer_irq.irq, &tegra_timer_irq);
	if (ret) {
		pr_err("Failed to register timer IRQ: %d\n", ret);
		return ret;
	}

	tegra_clockevent.cpumask = cpu_all_mask;
	tegra_clockevent.irq = tegra_timer_irq.irq;
	clockevents_config_and_register(&tegra_clockevent, 1000000,
					0x1, 0x1fffffff);

	return 0;
}
TIMER_OF_DECLARE(tegra20_timer, "nvidia,tegra20-timer", tegra20_init_timer);

static int __init tegra20_init_rtc(struct device_node *np)
{
	struct clk *clk;

	rtc_base = of_iomap(np, 0);
	if (!rtc_base) {
		pr_err("Can't map RTC registers\n");
		return -ENXIO;
	}

	/*
	 * rtc registers are used by read_persistent_clock, keep the rtc clock
	 * enabled
	 */
	clk = of_clk_get(np, 0);
	if (IS_ERR(clk))
		pr_warn("Unable to get rtc-tegra clock\n");
	else
		clk_prepare_enable(clk);

	return register_persistent_clock(NULL, tegra_read_persistent_clock64);
}
TIMER_OF_DECLARE(tegra20_rtc, "nvidia,tegra20-rtc", tegra20_init_rtc);
