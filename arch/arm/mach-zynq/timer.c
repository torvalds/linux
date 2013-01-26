/*
 * This file contains driver for the Xilinx PS Timer Counter IP.
 *
 *  Copyright (C) 2011 Xilinx
 *
 * based on arch/mips/kernel/time.c timer driver
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/clk-provider.h>

#include "common.h"

/*
 * Timer Register Offset Definitions of Timer 1, Increment base address by 4
 * and use same offsets for Timer 2
 */
#define XTTCPSS_CLK_CNTRL_OFFSET	0x00 /* Clock Control Reg, RW */
#define XTTCPSS_CNT_CNTRL_OFFSET	0x0C /* Counter Control Reg, RW */
#define XTTCPSS_COUNT_VAL_OFFSET	0x18 /* Counter Value Reg, RO */
#define XTTCPSS_INTR_VAL_OFFSET		0x24 /* Interval Count Reg, RW */
#define XTTCPSS_MATCH_1_OFFSET		0x30 /* Match 1 Value Reg, RW */
#define XTTCPSS_MATCH_2_OFFSET		0x3C /* Match 2 Value Reg, RW */
#define XTTCPSS_MATCH_3_OFFSET		0x48 /* Match 3 Value Reg, RW */
#define XTTCPSS_ISR_OFFSET		0x54 /* Interrupt Status Reg, RO */
#define XTTCPSS_IER_OFFSET		0x60 /* Interrupt Enable Reg, RW */

#define XTTCPSS_CNT_CNTRL_DISABLE_MASK	0x1

/* Setup the timers to use pre-scaling, using a fixed value for now that will
 * work across most input frequency, but it may need to be more dynamic
 */
#define PRESCALE_EXPONENT	11	/* 2 ^ PRESCALE_EXPONENT = PRESCALE */
#define PRESCALE		2048	/* The exponent must match this */
#define CLK_CNTRL_PRESCALE	((PRESCALE_EXPONENT - 1) << 1)
#define CLK_CNTRL_PRESCALE_EN	1
#define CNT_CNTRL_RESET		(1<<4)

/**
 * struct xttcpss_timer - This definition defines local timer structure
 *
 * @base_addr:	Base address of timer
 **/
struct xttcpss_timer {
	void __iomem	*base_addr;
};

struct xttcpss_timer_clocksource {
	struct xttcpss_timer	xttc;
	struct clocksource	cs;
};

#define to_xttcpss_timer_clksrc(x) \
		container_of(x, struct xttcpss_timer_clocksource, cs)

struct xttcpss_timer_clockevent {
	struct xttcpss_timer		xttc;
	struct clock_event_device	ce;
	struct clk			*clk;
};

#define to_xttcpss_timer_clkevent(x) \
		container_of(x, struct xttcpss_timer_clockevent, ce)

/**
 * xttcpss_set_interval - Set the timer interval value
 *
 * @timer:	Pointer to the timer instance
 * @cycles:	Timer interval ticks
 **/
static void xttcpss_set_interval(struct xttcpss_timer *timer,
					unsigned long cycles)
{
	u32 ctrl_reg;

	/* Disable the counter, set the counter value  and re-enable counter */
	ctrl_reg = __raw_readl(timer->base_addr + XTTCPSS_CNT_CNTRL_OFFSET);
	ctrl_reg |= XTTCPSS_CNT_CNTRL_DISABLE_MASK;
	__raw_writel(ctrl_reg, timer->base_addr + XTTCPSS_CNT_CNTRL_OFFSET);

	__raw_writel(cycles, timer->base_addr + XTTCPSS_INTR_VAL_OFFSET);

	/* Reset the counter (0x10) so that it starts from 0, one-shot
	   mode makes this needed for timing to be right. */
	ctrl_reg |= CNT_CNTRL_RESET;
	ctrl_reg &= ~XTTCPSS_CNT_CNTRL_DISABLE_MASK;
	__raw_writel(ctrl_reg, timer->base_addr + XTTCPSS_CNT_CNTRL_OFFSET);
}

/**
 * xttcpss_clock_event_interrupt - Clock event timer interrupt handler
 *
 * @irq:	IRQ number of the Timer
 * @dev_id:	void pointer to the xttcpss_timer instance
 *
 * returns: Always IRQ_HANDLED - success
 **/
static irqreturn_t xttcpss_clock_event_interrupt(int irq, void *dev_id)
{
	struct xttcpss_timer_clockevent *xttce = dev_id;
	struct xttcpss_timer *timer = &xttce->xttc;

	/* Acknowledge the interrupt and call event handler */
	__raw_writel(__raw_readl(timer->base_addr + XTTCPSS_ISR_OFFSET),
			timer->base_addr + XTTCPSS_ISR_OFFSET);

	xttce->ce.event_handler(&xttce->ce);

	return IRQ_HANDLED;
}

/**
 * __xttc_clocksource_read - Reads the timer counter register
 *
 * returns: Current timer counter register value
 **/
static cycle_t __xttc_clocksource_read(struct clocksource *cs)
{
	struct xttcpss_timer *timer = &to_xttcpss_timer_clksrc(cs)->xttc;

	return (cycle_t)__raw_readl(timer->base_addr +
				XTTCPSS_COUNT_VAL_OFFSET);
}

/**
 * xttcpss_set_next_event - Sets the time interval for next event
 *
 * @cycles:	Timer interval ticks
 * @evt:	Address of clock event instance
 *
 * returns: Always 0 - success
 **/
static int xttcpss_set_next_event(unsigned long cycles,
					struct clock_event_device *evt)
{
	struct xttcpss_timer_clockevent *xttce = to_xttcpss_timer_clkevent(evt);
	struct xttcpss_timer *timer = &xttce->xttc;

	xttcpss_set_interval(timer, cycles);
	return 0;
}

/**
 * xttcpss_set_mode - Sets the mode of timer
 *
 * @mode:	Mode to be set
 * @evt:	Address of clock event instance
 **/
static void xttcpss_set_mode(enum clock_event_mode mode,
					struct clock_event_device *evt)
{
	struct xttcpss_timer_clockevent *xttce = to_xttcpss_timer_clkevent(evt);
	struct xttcpss_timer *timer = &xttce->xttc;
	u32 ctrl_reg;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		xttcpss_set_interval(timer,
				     DIV_ROUND_CLOSEST(clk_get_rate(xttce->clk),
						       PRESCALE * HZ));
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		ctrl_reg = __raw_readl(timer->base_addr +
					XTTCPSS_CNT_CNTRL_OFFSET);
		ctrl_reg |= XTTCPSS_CNT_CNTRL_DISABLE_MASK;
		__raw_writel(ctrl_reg,
				timer->base_addr + XTTCPSS_CNT_CNTRL_OFFSET);
		break;
	case CLOCK_EVT_MODE_RESUME:
		ctrl_reg = __raw_readl(timer->base_addr +
					XTTCPSS_CNT_CNTRL_OFFSET);
		ctrl_reg &= ~XTTCPSS_CNT_CNTRL_DISABLE_MASK;
		__raw_writel(ctrl_reg,
				timer->base_addr + XTTCPSS_CNT_CNTRL_OFFSET);
		break;
	}
}

static void __init zynq_ttc_setup_clocksource(struct device_node *np,
					     void __iomem *base)
{
	struct xttcpss_timer_clocksource *ttccs;
	struct clk *clk;
	int err;
	u32 reg;

	ttccs = kzalloc(sizeof(*ttccs), GFP_KERNEL);
	if (WARN_ON(!ttccs))
		return;

	err = of_property_read_u32(np, "reg", &reg);
	if (WARN_ON(err))
		return;

	clk = of_clk_get_by_name(np, "cpu_1x");
	if (WARN_ON(IS_ERR(clk)))
		return;

	err = clk_prepare_enable(clk);
	if (WARN_ON(err))
		return;

	ttccs->xttc.base_addr = base + reg * 4;

	ttccs->cs.name = np->name;
	ttccs->cs.rating = 200;
	ttccs->cs.read = __xttc_clocksource_read;
	ttccs->cs.mask = CLOCKSOURCE_MASK(16);
	ttccs->cs.flags = CLOCK_SOURCE_IS_CONTINUOUS;

	__raw_writel(0x0,  ttccs->xttc.base_addr + XTTCPSS_IER_OFFSET);
	__raw_writel(CLK_CNTRL_PRESCALE | CLK_CNTRL_PRESCALE_EN,
		     ttccs->xttc.base_addr + XTTCPSS_CLK_CNTRL_OFFSET);
	__raw_writel(CNT_CNTRL_RESET,
		     ttccs->xttc.base_addr + XTTCPSS_CNT_CNTRL_OFFSET);

	err = clocksource_register_hz(&ttccs->cs, clk_get_rate(clk) / PRESCALE);
	if (WARN_ON(err))
		return;
}

static void __init zynq_ttc_setup_clockevent(struct device_node *np,
					    void __iomem *base)
{
	struct xttcpss_timer_clockevent *ttcce;
	int err, irq;
	u32 reg;

	ttcce = kzalloc(sizeof(*ttcce), GFP_KERNEL);
	if (WARN_ON(!ttcce))
		return;

	err = of_property_read_u32(np, "reg", &reg);
	if (WARN_ON(err))
		return;

	ttcce->xttc.base_addr = base + reg * 4;

	ttcce->clk = of_clk_get_by_name(np, "cpu_1x");
	if (WARN_ON(IS_ERR(ttcce->clk)))
		return;

	err = clk_prepare_enable(ttcce->clk);
	if (WARN_ON(err))
		return;

	irq = irq_of_parse_and_map(np, 0);
	if (WARN_ON(!irq))
		return;

	ttcce->ce.name = np->name;
	ttcce->ce.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	ttcce->ce.set_next_event = xttcpss_set_next_event;
	ttcce->ce.set_mode = xttcpss_set_mode;
	ttcce->ce.rating = 200;
	ttcce->ce.irq = irq;

	__raw_writel(0x23, ttcce->xttc.base_addr + XTTCPSS_CNT_CNTRL_OFFSET);
	__raw_writel(CLK_CNTRL_PRESCALE | CLK_CNTRL_PRESCALE_EN,
		     ttcce->xttc.base_addr + XTTCPSS_CLK_CNTRL_OFFSET);
	__raw_writel(0x1,  ttcce->xttc.base_addr + XTTCPSS_IER_OFFSET);

	err = request_irq(irq, xttcpss_clock_event_interrupt, IRQF_TIMER,
			  np->name, ttcce);
	if (WARN_ON(err))
		return;

	clockevents_config_and_register(&ttcce->ce,
					clk_get_rate(ttcce->clk) / PRESCALE,
					1, 0xfffe);
}

static const __initconst struct of_device_id zynq_ttc_match[] = {
	{ .compatible = "xlnx,ttc-counter-clocksource",
		.data = zynq_ttc_setup_clocksource, },
	{ .compatible = "xlnx,ttc-counter-clockevent",
		.data = zynq_ttc_setup_clockevent, },
	{}
};

/**
 * xttcpss_timer_init - Initialize the timer
 *
 * Initializes the timer hardware and register the clock source and clock event
 * timers with Linux kernal timer framework
 **/
void __init xttcpss_timer_init(void)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, "xlnx,ttc") {
		struct device_node *np_chld;
		void __iomem *base;

		base = of_iomap(np, 0);
		if (WARN_ON(!base))
			return;

		for_each_available_child_of_node(np, np_chld) {
			int (*cb)(struct device_node *np, void __iomem *base);
			const struct of_device_id *match;

			match = of_match_node(zynq_ttc_match, np_chld);
			if (match) {
				cb = match->data;
				cb(np_chld, base);
			}
		}
	}
}
