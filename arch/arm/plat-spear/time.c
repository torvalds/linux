/*
 * arch/arm/plat-spear/time.c
 *
 * Copyright (C) 2010 ST Microelectronics
 * Shiraz Hashim<shiraz.hashim@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/irq.h>
#include <asm/mach/time.h>
#include <mach/generic.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

/*
 * We would use TIMER0 and TIMER1 as clockevent and clocksource.
 * Timer0 and Timer1 both belong to same gpt block in cpu subbsystem. Further
 * they share same functional clock. Any change in one's functional clock will
 * also affect other timer.
 */

#define CLKEVT	0	/* gpt0, channel0 as clockevent */
#define CLKSRC	1	/* gpt0, channel1 as clocksource */

/* Register offsets, x is channel number */
#define CR(x)		((x) * 0x80 + 0x80)
#define IR(x)		((x) * 0x80 + 0x84)
#define LOAD(x)		((x) * 0x80 + 0x88)
#define COUNT(x)	((x) * 0x80 + 0x8C)

/* Reg bit definitions */
#define CTRL_INT_ENABLE		0x0100
#define CTRL_ENABLE		0x0020
#define CTRL_ONE_SHOT		0x0010

#define CTRL_PRESCALER1		0x0
#define CTRL_PRESCALER2		0x1
#define CTRL_PRESCALER4		0x2
#define CTRL_PRESCALER8		0x3
#define CTRL_PRESCALER16	0x4
#define CTRL_PRESCALER32	0x5
#define CTRL_PRESCALER64	0x6
#define CTRL_PRESCALER128	0x7
#define CTRL_PRESCALER256	0x8

#define INT_STATUS		0x1

/*
 * Minimum clocksource/clockevent timer range in seconds
 */
#define SPEAR_MIN_RANGE 4

static __iomem void *gpt_base;
static struct clk *gpt_clk;

static void clockevent_set_mode(enum clock_event_mode mode,
				struct clock_event_device *clk_event_dev);
static int clockevent_next_event(unsigned long evt,
				 struct clock_event_device *clk_event_dev);

static void spear_clocksource_init(void)
{
	u32 tick_rate;
	u16 val;

	/* program the prescaler (/256)*/
	writew(CTRL_PRESCALER256, gpt_base + CR(CLKSRC));

	/* find out actual clock driving Timer */
	tick_rate = clk_get_rate(gpt_clk);
	tick_rate >>= CTRL_PRESCALER256;

	writew(0xFFFF, gpt_base + LOAD(CLKSRC));

	val = readw(gpt_base + CR(CLKSRC));
	val &= ~CTRL_ONE_SHOT;	/* autoreload mode */
	val |= CTRL_ENABLE ;
	writew(val, gpt_base + CR(CLKSRC));

	/* register the clocksource */
	clocksource_mmio_init(gpt_base + COUNT(CLKSRC), "tmr1", tick_rate,
		200, 16, clocksource_mmio_readw_up);
}

static struct clock_event_device clkevt = {
	.name = "tmr0",
	.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode = clockevent_set_mode,
	.set_next_event = clockevent_next_event,
	.shift = 0,	/* to be computed */
};

static void clockevent_set_mode(enum clock_event_mode mode,
				struct clock_event_device *clk_event_dev)
{
	u32 period;
	u16 val;

	/* stop the timer */
	val = readw(gpt_base + CR(CLKEVT));
	val &= ~CTRL_ENABLE;
	writew(val, gpt_base + CR(CLKEVT));

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		period = clk_get_rate(gpt_clk) / HZ;
		period >>= CTRL_PRESCALER16;
		writew(period, gpt_base + LOAD(CLKEVT));

		val = readw(gpt_base + CR(CLKEVT));
		val &= ~CTRL_ONE_SHOT;
		val |= CTRL_ENABLE | CTRL_INT_ENABLE;
		writew(val, gpt_base + CR(CLKEVT));

		break;
	case CLOCK_EVT_MODE_ONESHOT:
		val = readw(gpt_base + CR(CLKEVT));
		val |= CTRL_ONE_SHOT;
		writew(val, gpt_base + CR(CLKEVT));

		break;
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_RESUME:

		break;
	default:
		pr_err("Invalid mode requested\n");
		break;
	}
}

static int clockevent_next_event(unsigned long cycles,
				 struct clock_event_device *clk_event_dev)
{
	u16 val;

	writew(cycles, gpt_base + LOAD(CLKEVT));

	val = readw(gpt_base + CR(CLKEVT));
	val |= CTRL_ENABLE | CTRL_INT_ENABLE;
	writew(val, gpt_base + CR(CLKEVT));

	return 0;
}

static irqreturn_t spear_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clkevt;

	writew(INT_STATUS, gpt_base + IR(CLKEVT));

	evt->event_handler(evt);

	return IRQ_HANDLED;
}

static struct irqaction spear_timer_irq = {
	.name = "timer",
	.flags = IRQF_DISABLED | IRQF_TIMER,
	.handler = spear_timer_interrupt
};

static void __init spear_clockevent_init(void)
{
	u32 tick_rate;

	/* program the prescaler */
	writew(CTRL_PRESCALER16, gpt_base + CR(CLKEVT));

	tick_rate = clk_get_rate(gpt_clk);
	tick_rate >>= CTRL_PRESCALER16;

	clockevents_calc_mult_shift(&clkevt, tick_rate, SPEAR_MIN_RANGE);

	clkevt.max_delta_ns = clockevent_delta2ns(0xfff0,
			&clkevt);
	clkevt.min_delta_ns = clockevent_delta2ns(3, &clkevt);

	clkevt.cpumask = cpumask_of(0);

	clockevents_register_device(&clkevt);

	setup_irq(SPEAR_GPT0_CHAN0_IRQ, &spear_timer_irq);
}

void __init spear_setup_timer(void)
{
	int ret;

	if (!request_mem_region(SPEAR_GPT0_BASE, SZ_1K, "gpt0")) {
		pr_err("%s:cannot get IO addr\n", __func__);
		return;
	}

	gpt_base = (void __iomem *)ioremap(SPEAR_GPT0_BASE, SZ_1K);
	if (!gpt_base) {
		pr_err("%s:ioremap failed for gpt\n", __func__);
		goto err_mem;
	}

	gpt_clk = clk_get_sys("gpt0", NULL);
	if (!gpt_clk) {
		pr_err("%s:couldn't get clk for gpt\n", __func__);
		goto err_iomap;
	}

	ret = clk_enable(gpt_clk);
	if (ret < 0) {
		pr_err("%s:couldn't enable gpt clock\n", __func__);
		goto err_clk;
	}

	spear_clockevent_init();
	spear_clocksource_init();

	return;

err_clk:
	clk_put(gpt_clk);
err_iomap:
	iounmap(gpt_base);
err_mem:
	release_mem_region(SPEAR_GPT0_BASE, SZ_1K);
}
