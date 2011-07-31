/*
 * arch/arch/mach-tegra/timer.c
 *
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
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cnt32_to_63.h>

#include <asm/mach/time.h>
#include <asm/mach/time.h>
#include <asm/localtimer.h>

#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/suspend.h>

#include "board.h"
#include "clock.h"
#include "power.h"

#define RTC_SECONDS		0x08
#define RTC_SHADOW_SECONDS	0x0c
#define RTC_MILLISECONDS	0x10

#define TIMERUS_CNTR_1US 0x10
#define TIMERUS_USEC_CFG 0x14
#define TIMERUS_CNTR_FREEZE 0x4c

#define TIMER1_BASE 0x0
#define TIMER2_BASE 0x8
#define TIMER3_BASE 0x50
#define TIMER4_BASE 0x58

#define TIMER_PTV 0x0
#define TIMER_PCR 0x4

static void __iomem *timer_reg_base = IO_ADDRESS(TEGRA_TMR1_BASE);
static void __iomem *rtc_base = IO_ADDRESS(TEGRA_RTC_BASE);

#define timer_writel(value, reg) \
	__raw_writel(value, (u32)timer_reg_base + (reg))
#define timer_readl(reg) \
	__raw_readl((u32)timer_reg_base + (reg))

static u64 tegra_sched_clock_offset;
static u64 tegra_sched_clock_suspend_val;
static u64 tegra_sched_clock_suspend_rtc;

static int tegra_timer_set_next_event(unsigned long cycles,
					 struct clock_event_device *evt)
{
	u32 reg;

	reg = 0x80000000 | ((cycles > 1) ? (cycles-1) : 0);
	timer_writel(reg, TIMER3_BASE + TIMER_PTV);

	return 0;
}

static void tegra_timer_set_mode(enum clock_event_mode mode,
				    struct clock_event_device *evt)
{
	u32 reg;

	timer_writel(0, TIMER3_BASE + TIMER_PTV);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		reg = 0xC0000000 | ((1000000/HZ)-1);
		timer_writel(reg, TIMER3_BASE + TIMER_PTV);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:
		break;
	}
}

static cycle_t tegra_clocksource_read(struct clocksource *cs)
{
	return timer_readl(TIMERUS_CNTR_1US);
}

static struct clock_event_device tegra_clockevent = {
	.name		= "timer0",
	.rating		= 300,
	.features	= CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC,
	.set_next_event	= tegra_timer_set_next_event,
	.set_mode	= tegra_timer_set_mode,
};

static struct clocksource tegra_clocksource = {
	.name	= "timer_us",
	.rating	= 300,
	.read	= tegra_clocksource_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

unsigned long long sched_clock(void)
{
	return tegra_sched_clock_offset +
		cnt32_to_63(timer_readl(TIMERUS_CNTR_1US)) * NSEC_PER_USEC;
}

static void tegra_sched_clock_suspend(void)
{
	tegra_sched_clock_suspend_val = sched_clock();
	tegra_sched_clock_suspend_rtc = tegra_rtc_read_ms();
}

static void tegra_sched_clock_resume(void)
{
	u64 rtc_offset_ms = tegra_rtc_read_ms() - tegra_sched_clock_suspend_rtc;
	tegra_sched_clock_offset = tegra_sched_clock_suspend_val +
		rtc_offset_ms * NSEC_PER_MSEC -
		(sched_clock() - tegra_sched_clock_offset);
}

/*
 * tegra_rtc_read - Reads the Tegra RTC registers
 * Care must be taken that this funciton is not called while the
 * tegra_rtc driver could be executing to avoid race conditions
 * on the RTC shadow register
 */
u64 tegra_rtc_read_ms(void)
{
	u32 ms = readl(rtc_base + RTC_MILLISECONDS);
	u32 s = readl(rtc_base + RTC_SHADOW_SECONDS);
	return (u64)s * MSEC_PER_SEC + ms;
}

/*
 * read_persistent_clock -  Return time from a persistent clock.
 *
 * Reads the time from a source which isn't disabled during PM, the
 * 32k sync timer.  Convert the cycles elapsed since last read into
 * nsecs and adds to a monotonically increasing timespec.
 * Care must be taken that this funciton is not called while the
 * tegra_rtc driver could be executing to avoid race conditions
 * on the RTC shadow register
 */
static struct timespec persistent_ts;
static u64 persistent_ms, last_persistent_ms;
void read_persistent_clock(struct timespec *ts)
{
	u64 delta;
	struct timespec *tsp = &persistent_ts;

	last_persistent_ms = persistent_ms;
	persistent_ms = tegra_rtc_read_ms();
	delta = persistent_ms - last_persistent_ms;

	timespec_add_ns(tsp, delta * NSEC_PER_MSEC);
	*ts = *tsp;
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
	.flags		= IRQF_DISABLED | IRQF_TIMER | IRQF_TRIGGER_HIGH,
	.handler	= tegra_timer_interrupt,
	.dev_id		= &tegra_clockevent,
	.irq		= INT_TMR3,
};

static irqreturn_t tegra_lp2wake_interrupt(int irq, void *dev_id)
{
	timer_writel(1<<30, TIMER4_BASE + TIMER_PCR);
	return IRQ_HANDLED;
}

static struct irqaction tegra_lp2wake_irq = {
	.name		= "timer_lp2wake",
	.flags		= IRQF_DISABLED,
	.handler	= tegra_lp2wake_interrupt,
	.dev_id		= NULL,
	.irq		= INT_TMR4,
};

static void __init tegra_init_timer(void)
{
	unsigned long rate = clk_measure_input_freq();
	int ret;

#ifdef CONFIG_HAVE_ARM_TWD
	twd_base = IO_ADDRESS(TEGRA_ARM_PERIF_BASE + 0x600);
#endif

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

	if (clocksource_register_hz(&tegra_clocksource, 1000000)) {
		printk(KERN_ERR "Failed to register clocksource\n");
		BUG();
	}

	ret = setup_irq(tegra_timer_irq.irq, &tegra_timer_irq);
	if (ret) {
		printk(KERN_ERR "Failed to register timer IRQ: %d\n", ret);
		BUG();
	}

	ret = setup_irq(tegra_lp2wake_irq.irq, &tegra_lp2wake_irq);
	if (ret) {
		printk(KERN_ERR "Failed to register LP2 timer IRQ: %d\n", ret);
		BUG();
	}

	clockevents_calc_mult_shift(&tegra_clockevent, 1000000, 5);
	tegra_clockevent.max_delta_ns =
		clockevent_delta2ns(0x1fffffff, &tegra_clockevent);
	tegra_clockevent.min_delta_ns =
		clockevent_delta2ns(0x1, &tegra_clockevent);
	tegra_clockevent.cpumask = cpu_all_mask;
	tegra_clockevent.irq = tegra_timer_irq.irq;
	clockevents_register_device(&tegra_clockevent);

	return;
}

struct sys_timer tegra_timer = {
	.init = tegra_init_timer,
};

void tegra_lp2_set_trigger(unsigned long cycles)
{
	timer_writel(0, TIMER4_BASE + TIMER_PTV);
	if (cycles) {
		u32 reg = 0x80000000ul | min(0x1ffffffful, cycles);
		timer_writel(reg, TIMER4_BASE + TIMER_PTV);
	}
}
EXPORT_SYMBOL(tegra_lp2_set_trigger);

unsigned long tegra_lp2_timer_remain(void)
{
	return timer_readl(TIMER4_BASE + TIMER_PCR) & 0x1ffffffful;
}

static u32 usec_config;
void tegra_timer_suspend(void)
{
	tegra_sched_clock_suspend();
	usec_config = timer_readl(TIMERUS_USEC_CFG);
}

void tegra_timer_resume(void)
{
	timer_writel(usec_config, TIMERUS_USEC_CFG);
	tegra_sched_clock_resume();
}
