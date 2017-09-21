/*
 * Renesas Timer Support - OSTM
 *
 * Copyright (C) 2017 Renesas Electronics America, Inc.
 * Copyright (C) 2017 Chris Brandt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

/*
 * The OSTM contains independent channels.
 * The first OSTM channel probed will be set up as a free running
 * clocksource. Additionally we will use this clocksource for the system
 * schedule timer sched_clock().
 *
 * The second (or more) channel probed will be set up as an interrupt
 * driven clock event.
 */

struct ostm_device {
	void __iomem *base;
	unsigned long ticks_per_jiffy;
	struct clock_event_device ced;
};

static void __iomem *system_clock;	/* For sched_clock() */

/* OSTM REGISTERS */
#define	OSTM_CMP		0x000	/* RW,32 */
#define	OSTM_CNT		0x004	/* R,32 */
#define	OSTM_TE			0x010	/* R,8 */
#define	OSTM_TS			0x014	/* W,8 */
#define	OSTM_TT			0x018	/* W,8 */
#define	OSTM_CTL		0x020	/* RW,8 */

#define	TE			0x01
#define	TS			0x01
#define	TT			0x01
#define	CTL_PERIODIC		0x00
#define	CTL_ONESHOT		0x02
#define	CTL_FREERUN		0x02

static struct ostm_device *ced_to_ostm(struct clock_event_device *ced)
{
	return container_of(ced, struct ostm_device, ced);
}

static void ostm_timer_stop(struct ostm_device *ostm)
{
	if (readb(ostm->base + OSTM_TE) & TE) {
		writeb(TT, ostm->base + OSTM_TT);

		/*
		 * Read back the register simply to confirm the write operation
		 * has completed since I/O writes can sometimes get queued by
		 * the bus architecture.
		 */
		while (readb(ostm->base + OSTM_TE) & TE)
			;
	}
}

static int __init ostm_init_clksrc(struct ostm_device *ostm, unsigned long rate)
{
	/*
	 * irq not used (clock sources don't use interrupts)
	 */

	ostm_timer_stop(ostm);

	writel(0, ostm->base + OSTM_CMP);
	writeb(CTL_FREERUN, ostm->base + OSTM_CTL);
	writeb(TS, ostm->base + OSTM_TS);

	return clocksource_mmio_init(ostm->base + OSTM_CNT,
			"ostm", rate,
			300, 32, clocksource_mmio_readl_up);
}

static u64 notrace ostm_read_sched_clock(void)
{
	return readl(system_clock);
}

static void __init ostm_init_sched_clock(struct ostm_device *ostm,
			unsigned long rate)
{
	system_clock = ostm->base + OSTM_CNT;
	sched_clock_register(ostm_read_sched_clock, 32, rate);
}

static int ostm_clock_event_next(unsigned long delta,
				     struct clock_event_device *ced)
{
	struct ostm_device *ostm = ced_to_ostm(ced);

	ostm_timer_stop(ostm);

	writel(delta, ostm->base + OSTM_CMP);
	writeb(CTL_ONESHOT, ostm->base + OSTM_CTL);
	writeb(TS, ostm->base + OSTM_TS);

	return 0;
}

static int ostm_shutdown(struct clock_event_device *ced)
{
	struct ostm_device *ostm = ced_to_ostm(ced);

	ostm_timer_stop(ostm);

	return 0;
}
static int ostm_set_periodic(struct clock_event_device *ced)
{
	struct ostm_device *ostm = ced_to_ostm(ced);

	if (clockevent_state_oneshot(ced) || clockevent_state_periodic(ced))
		ostm_timer_stop(ostm);

	writel(ostm->ticks_per_jiffy - 1, ostm->base + OSTM_CMP);
	writeb(CTL_PERIODIC, ostm->base + OSTM_CTL);
	writeb(TS, ostm->base + OSTM_TS);

	return 0;
}

static int ostm_set_oneshot(struct clock_event_device *ced)
{
	struct ostm_device *ostm = ced_to_ostm(ced);

	ostm_timer_stop(ostm);

	return 0;
}

static irqreturn_t ostm_timer_interrupt(int irq, void *dev_id)
{
	struct ostm_device *ostm = dev_id;

	if (clockevent_state_oneshot(&ostm->ced))
		ostm_timer_stop(ostm);

	/* notify clockevent layer */
	if (ostm->ced.event_handler)
		ostm->ced.event_handler(&ostm->ced);

	return IRQ_HANDLED;
}

static int __init ostm_init_clkevt(struct ostm_device *ostm, int irq,
			unsigned long rate)
{
	struct clock_event_device *ced = &ostm->ced;
	int ret = -ENXIO;

	ret = request_irq(irq, ostm_timer_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL,
			  "ostm", ostm);
	if (ret) {
		pr_err("ostm: failed to request irq\n");
		return ret;
	}

	ced->name = "ostm";
	ced->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC;
	ced->set_state_shutdown = ostm_shutdown;
	ced->set_state_periodic = ostm_set_periodic;
	ced->set_state_oneshot = ostm_set_oneshot;
	ced->set_next_event = ostm_clock_event_next;
	ced->shift = 32;
	ced->rating = 300;
	ced->cpumask = cpumask_of(0);
	clockevents_config_and_register(ced, rate, 0xf, 0xffffffff);

	return 0;
}

static int __init ostm_init(struct device_node *np)
{
	struct ostm_device *ostm;
	int ret = -EFAULT;
	struct clk *ostm_clk = NULL;
	int irq;
	unsigned long rate;

	ostm = kzalloc(sizeof(*ostm), GFP_KERNEL);
	if (!ostm)
		return -ENOMEM;

	ostm->base = of_iomap(np, 0);
	if (!ostm->base) {
		pr_err("ostm: failed to remap I/O memory\n");
		goto err;
	}

	irq = irq_of_parse_and_map(np, 0);
	if (irq < 0) {
		pr_err("ostm: Failed to get irq\n");
		goto err;
	}

	ostm_clk = of_clk_get(np, 0);
	if (IS_ERR(ostm_clk)) {
		pr_err("ostm: Failed to get clock\n");
		ostm_clk = NULL;
		goto err;
	}

	ret = clk_prepare_enable(ostm_clk);
	if (ret) {
		pr_err("ostm: Failed to enable clock\n");
		goto err;
	}

	rate = clk_get_rate(ostm_clk);
	ostm->ticks_per_jiffy = (rate + HZ / 2) / HZ;

	/*
	 * First probed device will be used as system clocksource. Any
	 * additional devices will be used as clock events.
	 */
	if (!system_clock) {
		ret = ostm_init_clksrc(ostm, rate);

		if (!ret) {
			ostm_init_sched_clock(ostm, rate);
			pr_info("ostm: used for clocksource\n");
		}

	} else {
		ret = ostm_init_clkevt(ostm, irq, rate);

		if (!ret)
			pr_info("ostm: used for clock events\n");
	}

err:
	if (ret) {
		clk_disable_unprepare(ostm_clk);
		iounmap(ostm->base);
		kfree(ostm);
		return ret;
	}

	return 0;
}

TIMER_OF_DECLARE(ostm, "renesas,ostm", ostm_init);
