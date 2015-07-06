/*
 * Copyright (C) 2014 Oleksij Rempel <linux@rempel-privat.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/clk.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/bitops.h>

#define DRIVER_NAME	"asm9260-timer"

/*
 * this device provide 4 offsets for each register:
 * 0x0 - plain read write mode
 * 0x4 - set mode, OR logic.
 * 0x8 - clr mode, XOR logic.
 * 0xc - togle mode.
 */
#define SET_REG 4
#define CLR_REG 8

#define HW_IR           0x0000 /* RW. Interrupt */
#define BM_IR_CR0	BIT(4)
#define BM_IR_MR3	BIT(3)
#define BM_IR_MR2	BIT(2)
#define BM_IR_MR1	BIT(1)
#define BM_IR_MR0	BIT(0)

#define HW_TCR		0x0010 /* RW. Timer controller */
/* BM_C*_RST
 * Timer Counter and the Prescale Counter are synchronously reset on the
 * next positive edge of PCLK. The counters remain reset until TCR[1] is
 * returned to zero. */
#define BM_C3_RST	BIT(7)
#define BM_C2_RST	BIT(6)
#define BM_C1_RST	BIT(5)
#define BM_C0_RST	BIT(4)
/* BM_C*_EN
 * 1 - Timer Counter and Prescale Counter are enabled for counting
 * 0 - counters are disabled */
#define BM_C3_EN	BIT(3)
#define BM_C2_EN	BIT(2)
#define BM_C1_EN	BIT(1)
#define BM_C0_EN	BIT(0)

#define HW_DIR		0x0020 /* RW. Direction? */
/* 00 - count up
 * 01 - count down
 * 10 - ?? 2^n/2 */
#define BM_DIR_COUNT_UP		0
#define BM_DIR_COUNT_DOWN	1
#define BM_DIR0_SHIFT	0
#define BM_DIR1_SHIFT	4
#define BM_DIR2_SHIFT	8
#define BM_DIR3_SHIFT	12
#define BM_DIR_DEFAULT		(BM_DIR_COUNT_UP << BM_DIR0_SHIFT | \
				 BM_DIR_COUNT_UP << BM_DIR1_SHIFT | \
				 BM_DIR_COUNT_UP << BM_DIR2_SHIFT | \
				 BM_DIR_COUNT_UP << BM_DIR3_SHIFT)

#define HW_TC0		0x0030 /* RO. Timer counter 0 */
/* HW_TC*. Timer counter owerflow (0xffff.ffff to 0x0000.0000) do not generate
 * interrupt. This registers can be used to detect overflow */
#define HW_TC1          0x0040
#define HW_TC2		0x0050
#define HW_TC3		0x0060

#define HW_PR		0x0070 /* RW. prescaler */
#define BM_PR_DISABLE	0
#define HW_PC		0x0080 /* RO. Prescaler counter */
#define HW_MCR		0x0090 /* RW. Match control */
/* enable interrupt on match */
#define BM_MCR_INT_EN(n)	(1 << (n * 3 + 0))
/* enable TC reset on match */
#define BM_MCR_RES_EN(n)	(1 << (n * 3 + 1))
/* enable stop TC on match */
#define BM_MCR_STOP_EN(n)	(1 << (n * 3 + 2))

#define HW_MR0		0x00a0 /* RW. Match reg */
#define HW_MR1		0x00b0
#define HW_MR2		0x00C0
#define HW_MR3		0x00D0

#define HW_CTCR		0x0180 /* Counter control */
#define BM_CTCR0_SHIFT	0
#define BM_CTCR1_SHIFT	2
#define BM_CTCR2_SHIFT	4
#define BM_CTCR3_SHIFT	6
#define BM_CTCR_TM	0	/* Timer mode. Every rising PCLK edge. */
#define BM_CTCR_DEFAULT	(BM_CTCR_TM << BM_CTCR0_SHIFT | \
			 BM_CTCR_TM << BM_CTCR1_SHIFT | \
			 BM_CTCR_TM << BM_CTCR2_SHIFT | \
			 BM_CTCR_TM << BM_CTCR3_SHIFT)

static struct asm9260_timer_priv {
	void __iomem *base;
	unsigned long ticks_per_jiffy;
} priv;

static int asm9260_timer_set_next_event(unsigned long delta,
					 struct clock_event_device *evt)
{
	/* configure match count for TC0 */
	writel_relaxed(delta, priv.base + HW_MR0);
	/* enable TC0 */
	writel_relaxed(BM_C0_EN, priv.base + HW_TCR + SET_REG);
	return 0;
}

static void asm9260_timer_set_mode(enum clock_event_mode mode,
				    struct clock_event_device *evt)
{
	/* stop timer0 */
	writel_relaxed(BM_C0_EN, priv.base + HW_TCR + CLR_REG);

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		/* disable reset and stop on match */
		writel_relaxed(BM_MCR_RES_EN(0) | BM_MCR_STOP_EN(0),
				priv.base + HW_MCR + CLR_REG);
		/* configure match count for TC0 */
		writel_relaxed(priv.ticks_per_jiffy, priv.base + HW_MR0);
		/* enable TC0 */
		writel_relaxed(BM_C0_EN, priv.base + HW_TCR + SET_REG);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		/* enable reset and stop on match */
		writel_relaxed(BM_MCR_RES_EN(0) | BM_MCR_STOP_EN(0),
				priv.base + HW_MCR + SET_REG);
		break;
	default:
		break;
	}
}

static struct clock_event_device event_dev = {
	.name		= DRIVER_NAME,
	.rating		= 200,
	.features       = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_next_event	= asm9260_timer_set_next_event,
	.set_mode	= asm9260_timer_set_mode,
};

static irqreturn_t asm9260_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

	evt->event_handler(evt);

	writel_relaxed(BM_IR_MR0, priv.base + HW_IR);

	return IRQ_HANDLED;
}

/*
 * ---------------------------------------------------------------------------
 * Timer initialization
 * ---------------------------------------------------------------------------
 */
static void __init asm9260_timer_init(struct device_node *np)
{
	int irq;
	struct clk *clk;
	int ret;
	unsigned long rate;

	priv.base = of_io_request_and_map(np, 0, np->name);
	if (IS_ERR(priv.base))
		panic("%s: unable to map resource", np->name);

	clk = of_clk_get(np, 0);

	ret = clk_prepare_enable(clk);
	if (ret)
		panic("Failed to enable clk!\n");

	irq = irq_of_parse_and_map(np, 0);
	ret = request_irq(irq, asm9260_timer_interrupt, IRQF_TIMER,
			DRIVER_NAME, &event_dev);
	if (ret)
		panic("Failed to setup irq!\n");

	/* set all timers for count-up */
	writel_relaxed(BM_DIR_DEFAULT, priv.base + HW_DIR);
	/* disable divider */
	writel_relaxed(BM_PR_DISABLE, priv.base + HW_PR);
	/* make sure all timers use every rising PCLK edge. */
	writel_relaxed(BM_CTCR_DEFAULT, priv.base + HW_CTCR);
	/* enable interrupt for TC0 and clean setting for all other lines */
	writel_relaxed(BM_MCR_INT_EN(0) , priv.base + HW_MCR);

	rate = clk_get_rate(clk);
	clocksource_mmio_init(priv.base + HW_TC1, DRIVER_NAME, rate,
			200, 32, clocksource_mmio_readl_up);

	/* Seems like we can't use counter without match register even if
	 * actions for MR are disabled. So, set MR to max value. */
	writel_relaxed(0xffffffff, priv.base + HW_MR1);
	/* enable TC1 */
	writel_relaxed(BM_C1_EN, priv.base + HW_TCR + SET_REG);

	priv.ticks_per_jiffy = DIV_ROUND_CLOSEST(rate, HZ);
	event_dev.cpumask = cpumask_of(0);
	clockevents_config_and_register(&event_dev, rate, 0x2c00, 0xfffffffe);
}
CLOCKSOURCE_OF_DECLARE(asm9260_timer, "alphascale,asm9260-timer",
		asm9260_timer_init);
