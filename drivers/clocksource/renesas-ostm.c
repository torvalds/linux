// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas Timer Support - OSTM
 *
 * Copyright (C) 2017 Renesas Electronics America, Inc.
 * Copyright (C) 2017 Chris Brandt
 */

#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/sched_clock.h>
#include <linux/slab.h>

#include "timer-of.h"

/*
 * The OSTM contains independent channels.
 * The first OSTM channel probed will be set up as a free running
 * clocksource. Additionally we will use this clocksource for the system
 * schedule timer sched_clock().
 *
 * The second (or more) channel probed will be set up as an interrupt
 * driven clock event.
 */

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

static void ostm_timer_stop(struct timer_of *to)
{
	if (readb(timer_of_base(to) + OSTM_TE) & TE) {
		writeb(TT, timer_of_base(to) + OSTM_TT);

		/*
		 * Read back the register simply to confirm the write operation
		 * has completed since I/O writes can sometimes get queued by
		 * the bus architecture.
		 */
		while (readb(timer_of_base(to) + OSTM_TE) & TE)
			;
	}
}

static int __init ostm_init_clksrc(struct timer_of *to)
{
	ostm_timer_stop(to);

	writel(0, timer_of_base(to) + OSTM_CMP);
	writeb(CTL_FREERUN, timer_of_base(to) + OSTM_CTL);
	writeb(TS, timer_of_base(to) + OSTM_TS);

	return clocksource_mmio_init(timer_of_base(to) + OSTM_CNT,
				     to->np->full_name, timer_of_rate(to), 300,
				     32, clocksource_mmio_readl_up);
}

static u64 notrace ostm_read_sched_clock(void)
{
	return readl(system_clock);
}

static void __init ostm_init_sched_clock(struct timer_of *to)
{
	system_clock = timer_of_base(to) + OSTM_CNT;
	sched_clock_register(ostm_read_sched_clock, 32, timer_of_rate(to));
}

static int ostm_clock_event_next(unsigned long delta,
				 struct clock_event_device *ced)
{
	struct timer_of *to = to_timer_of(ced);

	ostm_timer_stop(to);

	writel(delta, timer_of_base(to) + OSTM_CMP);
	writeb(CTL_ONESHOT, timer_of_base(to) + OSTM_CTL);
	writeb(TS, timer_of_base(to) + OSTM_TS);

	return 0;
}

static int ostm_shutdown(struct clock_event_device *ced)
{
	struct timer_of *to = to_timer_of(ced);

	ostm_timer_stop(to);

	return 0;
}
static int ostm_set_periodic(struct clock_event_device *ced)
{
	struct timer_of *to = to_timer_of(ced);

	if (clockevent_state_oneshot(ced) || clockevent_state_periodic(ced))
		ostm_timer_stop(to);

	writel(timer_of_period(to) - 1, timer_of_base(to) + OSTM_CMP);
	writeb(CTL_PERIODIC, timer_of_base(to) + OSTM_CTL);
	writeb(TS, timer_of_base(to) + OSTM_TS);

	return 0;
}

static int ostm_set_oneshot(struct clock_event_device *ced)
{
	struct timer_of *to = to_timer_of(ced);

	ostm_timer_stop(to);

	return 0;
}

static irqreturn_t ostm_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *ced = dev_id;

	if (clockevent_state_oneshot(ced))
		ostm_timer_stop(to_timer_of(ced));

	/* notify clockevent layer */
	if (ced->event_handler)
		ced->event_handler(ced);

	return IRQ_HANDLED;
}

static int __init ostm_init_clkevt(struct timer_of *to)
{
	struct clock_event_device *ced = &to->clkevt;

	ced->features = CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_PERIODIC;
	ced->set_state_shutdown = ostm_shutdown;
	ced->set_state_periodic = ostm_set_periodic;
	ced->set_state_oneshot = ostm_set_oneshot;
	ced->set_next_event = ostm_clock_event_next;
	ced->shift = 32;
	ced->rating = 300;
	ced->cpumask = cpumask_of(0);
	clockevents_config_and_register(ced, timer_of_rate(to), 0xf,
					0xffffffff);

	return 0;
}

static int __init ostm_init(struct device_node *np)
{
	struct reset_control *rstc;
	struct timer_of *to;
	int ret;

	to = kzalloc(sizeof(*to), GFP_KERNEL);
	if (!to)
		return -ENOMEM;

	rstc = of_reset_control_get_optional_exclusive(np, NULL);
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		goto err_free;
	}

	reset_control_deassert(rstc);

	to->flags = TIMER_OF_BASE | TIMER_OF_CLOCK;
	if (system_clock) {
		/*
		 * clock sources don't use interrupts, clock events do
		 */
		to->flags |= TIMER_OF_IRQ;
		to->of_irq.flags = IRQF_TIMER | IRQF_IRQPOLL;
		to->of_irq.handler = ostm_timer_interrupt;
	}

	ret = timer_of_init(np, to);
	if (ret)
		goto err_reset;

	/*
	 * First probed device will be used as system clocksource. Any
	 * additional devices will be used as clock events.
	 */
	if (!system_clock) {
		ret = ostm_init_clksrc(to);
		if (ret)
			goto err_cleanup;

		ostm_init_sched_clock(to);
		pr_info("%pOF: used for clocksource\n", np);
	} else {
		ret = ostm_init_clkevt(to);
		if (ret)
			goto err_cleanup;

		pr_info("%pOF: used for clock events\n", np);
	}

	of_node_set_flag(np, OF_POPULATED);
	return 0;

err_cleanup:
	timer_of_cleanup(to);
err_reset:
	reset_control_assert(rstc);
	reset_control_put(rstc);
err_free:
	kfree(to);
	return ret;
}

TIMER_OF_DECLARE(ostm, "renesas,ostm", ostm_init);

#if defined(CONFIG_ARCH_RZG2L) || defined(CONFIG_ARCH_R9A09G057)
static int __init ostm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	return ostm_init(dev->of_node);
}

static const struct of_device_id ostm_of_table[] = {
	{ .compatible = "renesas,ostm", },
	{ /* sentinel */ }
};

static struct platform_driver ostm_device_driver = {
	.driver = {
		.name = "renesas_ostm",
		.of_match_table = of_match_ptr(ostm_of_table),
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(ostm_device_driver, ostm_probe);
#endif
