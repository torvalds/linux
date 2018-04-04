/*
 * Copyright (C) Maxime Coquelin 2015
 * Author:  Maxime Coquelin <mcoquelin.stm32@gmail.com>
 * License terms:  GNU General Public License (GPL), version 2
 *
 * Inspired by time-efm32.c from Uwe Kleine-Koenig
 */

#include <linux/kernel.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

#include "timer-of.h"

#define TIM_CR1		0x00
#define TIM_DIER	0x0c
#define TIM_SR		0x10
#define TIM_EGR		0x14
#define TIM_CNT		0x24
#define TIM_PSC		0x28
#define TIM_ARR		0x2c
#define TIM_CCR1	0x34

#define TIM_CR1_CEN	BIT(0)
#define TIM_CR1_UDIS	BIT(1)
#define TIM_CR1_OPM	BIT(3)
#define TIM_CR1_ARPE	BIT(7)

#define TIM_DIER_UIE	BIT(0)
#define TIM_DIER_CC1IE	BIT(1)

#define TIM_SR_UIF	BIT(0)

#define TIM_EGR_UG	BIT(0)

#define TIM_PSC_MAX	USHRT_MAX
#define TIM_PSC_CLKRATE	10000

struct stm32_timer_private {
	int bits;
};

/**
 * stm32_timer_of_bits_set - set accessor helper
 * @to: a timer_of structure pointer
 * @bits: the number of bits (16 or 32)
 *
 * Accessor helper to set the number of bits in the timer-of private
 * structure.
 *
 */
static void stm32_timer_of_bits_set(struct timer_of *to, int bits)
{
	struct stm32_timer_private *pd = to->private_data;

	pd->bits = bits;
}

/**
 * stm32_timer_of_bits_get - get accessor helper
 * @to: a timer_of structure pointer
 *
 * Accessor helper to get the number of bits in the timer-of private
 * structure.
 *
 * Returns an integer corresponding to the number of bits.
 */
static int stm32_timer_of_bits_get(struct timer_of *to)
{
	struct stm32_timer_private *pd = to->private_data;

	return pd->bits;
}

static void __iomem *stm32_timer_cnt __read_mostly;

static u64 notrace stm32_read_sched_clock(void)
{
	return readl_relaxed(stm32_timer_cnt);
}

static struct delay_timer stm32_timer_delay;

static unsigned long stm32_read_delay(void)
{
	return readl_relaxed(stm32_timer_cnt);
}

static void stm32_clock_event_disable(struct timer_of *to)
{
	writel_relaxed(0, timer_of_base(to) + TIM_DIER);
}

/**
 * stm32_timer_start - Start the counter without event
 * @to: a timer_of structure pointer
 *
 * Start the timer in order to have the counter reset and start
 * incrementing but disable interrupt event when there is a counter
 * overflow. By default, the counter direction is used as upcounter.
 */
static void stm32_timer_start(struct timer_of *to)
{
	writel_relaxed(TIM_CR1_UDIS | TIM_CR1_CEN, timer_of_base(to) + TIM_CR1);
}

static int stm32_clock_event_shutdown(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	stm32_clock_event_disable(to);

	return 0;
}

static int stm32_clock_event_set_next_event(unsigned long evt,
					    struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);
	unsigned long now, next;

	next = readl_relaxed(timer_of_base(to) + TIM_CNT) + evt;
	writel_relaxed(next, timer_of_base(to) + TIM_CCR1);
	now = readl_relaxed(timer_of_base(to) + TIM_CNT);

	if ((next - now) > evt)
		return -ETIME;

	writel_relaxed(TIM_DIER_CC1IE, timer_of_base(to) + TIM_DIER);

	return 0;
}

static int stm32_clock_event_set_periodic(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	stm32_timer_start(to);

	return stm32_clock_event_set_next_event(timer_of_period(to), clkevt);
}

static int stm32_clock_event_set_oneshot(struct clock_event_device *clkevt)
{
	struct timer_of *to = to_timer_of(clkevt);

	stm32_timer_start(to);

	return 0;
}

static irqreturn_t stm32_clock_event_handler(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = (struct clock_event_device *)dev_id;
	struct timer_of *to = to_timer_of(clkevt);

	writel_relaxed(0, timer_of_base(to) + TIM_SR);

	if (clockevent_state_periodic(clkevt))
		stm32_clock_event_set_periodic(clkevt);
	else
		stm32_clock_event_shutdown(clkevt);

	clkevt->event_handler(clkevt);

	return IRQ_HANDLED;
}

/**
 * stm32_timer_width - Sort out the timer width (32/16)
 * @to: a pointer to a timer-of structure
 *
 * Write the 32-bit max value and read/return the result. If the timer
 * is 32 bits wide, the result will be UINT_MAX, otherwise it will
 * be truncated by the 16-bit register to USHRT_MAX.
 *
 */
static void __init stm32_timer_set_width(struct timer_of *to)
{
	u32 width;

	writel_relaxed(UINT_MAX, timer_of_base(to) + TIM_ARR);

	width = readl_relaxed(timer_of_base(to) + TIM_ARR);

	stm32_timer_of_bits_set(to, width == UINT_MAX ? 32 : 16);
}

/**
 * stm32_timer_set_prescaler - Compute and set the prescaler register
 * @to: a pointer to a timer-of structure
 *
 * Depending on the timer width, compute the prescaler to always
 * target a 10MHz timer rate for 16 bits. 32-bit timers are
 * considered precise and long enough to not use the prescaler.
 */
static void __init stm32_timer_set_prescaler(struct timer_of *to)
{
	int prescaler = 1;

	if (stm32_timer_of_bits_get(to) != 32) {
		prescaler = DIV_ROUND_CLOSEST(timer_of_rate(to),
					      TIM_PSC_CLKRATE);
		/*
		 * The prescaler register is an u16, the variable
		 * can't be greater than TIM_PSC_MAX, let's cap it in
		 * this case.
		 */
		prescaler = prescaler < TIM_PSC_MAX ? prescaler : TIM_PSC_MAX;
	}

	writel_relaxed(prescaler - 1, timer_of_base(to) + TIM_PSC);
	writel_relaxed(TIM_EGR_UG, timer_of_base(to) + TIM_EGR);
	writel_relaxed(0, timer_of_base(to) + TIM_SR);

	/* Adjust rate and period given the prescaler value */
	to->of_clk.rate = DIV_ROUND_CLOSEST(to->of_clk.rate, prescaler);
	to->of_clk.period = DIV_ROUND_UP(to->of_clk.rate, HZ);
}

static int __init stm32_clocksource_init(struct timer_of *to)
{
        u32 bits = stm32_timer_of_bits_get(to);
	const char *name = to->np->full_name;

	/*
	 * This driver allows to register several timers and relies on
	 * the generic time framework to select the right one.
	 * However, nothing allows to do the same for the
	 * sched_clock. We are not interested in a sched_clock for the
	 * 16-bit timers but only for the 32-bit one, so if no 32-bit
	 * timer is registered yet, we select this 32-bit timer as a
	 * sched_clock.
	 */
	if (bits == 32 && !stm32_timer_cnt) {

		/*
		 * Start immediately the counter as we will be using
		 * it right after.
		 */
		stm32_timer_start(to);

		stm32_timer_cnt = timer_of_base(to) + TIM_CNT;
		sched_clock_register(stm32_read_sched_clock, bits, timer_of_rate(to));
		pr_info("%s: STM32 sched_clock registered\n", name);

		stm32_timer_delay.read_current_timer = stm32_read_delay;
		stm32_timer_delay.freq = timer_of_rate(to);
		register_current_timer_delay(&stm32_timer_delay);
		pr_info("%s: STM32 delay timer registered\n", name);
	}

	return clocksource_mmio_init(timer_of_base(to) + TIM_CNT, name,
				     timer_of_rate(to), bits == 32 ? 250 : 100,
				     bits, clocksource_mmio_readl_up);
}

static void __init stm32_clockevent_init(struct timer_of *to)
{
	u32 bits = stm32_timer_of_bits_get(to);

	to->clkevt.name = to->np->full_name;
	to->clkevt.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	to->clkevt.set_state_shutdown = stm32_clock_event_shutdown;
	to->clkevt.set_state_periodic = stm32_clock_event_set_periodic;
	to->clkevt.set_state_oneshot = stm32_clock_event_set_oneshot;
	to->clkevt.tick_resume = stm32_clock_event_shutdown;
	to->clkevt.set_next_event = stm32_clock_event_set_next_event;
	to->clkevt.rating = bits == 32 ? 250 : 100;

	clockevents_config_and_register(&to->clkevt, timer_of_rate(to), 0x1,
					(1 <<  bits) - 1);

	pr_info("%pOF: STM32 clockevent driver initialized (%d bits)\n",
		to->np, bits);
}

static int __init stm32_timer_init(struct device_node *node)
{
	struct reset_control *rstc;
	struct timer_of *to;
	int ret;

	to = kzalloc(sizeof(*to), GFP_KERNEL);
	if (!to)
		return -ENOMEM;

	to->flags = TIMER_OF_IRQ | TIMER_OF_CLOCK | TIMER_OF_BASE;
	to->of_irq.handler = stm32_clock_event_handler;

	ret = timer_of_init(node, to);
	if (ret)
		goto err;

	to->private_data = kzalloc(sizeof(struct stm32_timer_private),
				   GFP_KERNEL);
	if (!to->private_data)
		goto deinit;

	rstc = of_reset_control_get(node, NULL);
	if (!IS_ERR(rstc)) {
		reset_control_assert(rstc);
		reset_control_deassert(rstc);
	}

	stm32_timer_set_width(to);

	stm32_timer_set_prescaler(to);

	ret = stm32_clocksource_init(to);
	if (ret)
		goto deinit;

	stm32_clockevent_init(to);
	return 0;

deinit:
	timer_of_cleanup(to);
err:
	kfree(to);
	return ret;
}

TIMER_OF_DECLARE(stm32, "st,stm32-timer", stm32_timer_init);
