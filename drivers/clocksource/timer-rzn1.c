// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/N1 timers driver
 *
 * Copyright (C) 2026 Schneider-Electric
 *
 * Author: Herve Codina <herve.codina@bootlin.com>
 */

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/clocksource.h>
#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/sched_clock.h>

/* Per-timer register offsets */
#define RZN1_TIMER_LOAD_COUNT_REG	0x00
#define RZN1_TIMER_CURRENT_COUNT_REG	0x04
#define RZN1_TIMER_CONTROL_REG		0x08
#define   RZN1_TIMER_CONTROL_INT_UNMASK		BIT(3)
#define   RZN1_TIMER_CONTROL_ENABLE		BIT(2)
#define   RZN1_TIMER_CONTROL_AUTORELOAD		BIT(1)
#define   RZN1_TIMER_CONTROL_PRESCALER_DIV25	BIT(0)
#define RZN1_TIMER_CLEAR_INT_REG	0x0c
#define RZN1_TIMER_STATUS_INT0_REG	0x10
#define RZN1_TIMER_STATUS_INT1_REG	0x14

/*
 * 8 timers are available. Among those 8 timers, the first 6 timers are 16-bit
 * timers and the last two ones are 32-bit timers.
 */
#define RZN1_TIMER_BASE_INDEX_16BIT_TIMERS	0
#define RZN1_TIMER_NB_16BIT_TIMERS		6
#define RZN1_TIMER_LAST_INDEX_16BIT_TIMERS	(RZN1_TIMER_BASE_INDEX_16BIT_TIMERS + \
						 RZN1_TIMER_NB_16BIT_TIMERS - 1)

#define RZN1_TIMER_BASE_INDEX_32BIT_TIMERS	6
#define RZN1_TIMER_NB_32BIT_TIMERS		2
#define RZN1_TIMER_LAST_INDEX_32BIT_TIMERS	(RZN1_TIMER_BASE_INDEX_32BIT_TIMERS + \
						 RZN1_TIMER_NB_32BIT_TIMERS - 1)

#define RZN1_TIMER_IS_16BIT_TIMER(_i) ((_i) < RZN1_TIMER_BASE_INDEX_32BIT_TIMERS)

/* Total numbers of timers */
#define RZN1_TIMER_NB_TIMERS	(RZN1_TIMER_NB_16BIT_TIMERS + \
				 RZN1_TIMER_NB_32BIT_TIMERS)

/* Offset of registers related to the timer number _i in the timers regs area */
#define RZN1_TIMER_REGS_OFFSET(_i) (0x20 * (_i))

struct rzn1_timer {
	void __iomem *base;
	unsigned int width;
	unsigned long rate;
	unsigned long hz_period;
	struct clock_event_device ced;
};

static int rzn1_timer_config(struct rzn1_timer *timer, u32 period, bool is_periodic)
{
	u32 ctrl;

	ctrl = readl(timer->base + RZN1_TIMER_CONTROL_REG);

	/* Disable the timer */
	writel(ctrl & ~RZN1_TIMER_CONTROL_ENABLE,
	       timer->base + RZN1_TIMER_CONTROL_REG);

	/* Set the new period */
	writel(period, timer->base + RZN1_TIMER_LOAD_COUNT_REG);

	/* Prepare the timer mode */
	if (is_periodic)
		ctrl |= RZN1_TIMER_CONTROL_AUTORELOAD;
	else
		ctrl &= ~RZN1_TIMER_CONTROL_AUTORELOAD;

	/*
	 * Enable the timer.
	 *
	 * This automatically resets the CURRENT_COUNT register.
	 */
	ctrl |= RZN1_TIMER_CONTROL_ENABLE;
	writel(ctrl, timer->base + RZN1_TIMER_CONTROL_REG);

	return 0;
}

static void rzn1_timer_disable(struct rzn1_timer *timer)
{
	u32 ctrl;

	ctrl = readl(timer->base + RZN1_TIMER_CONTROL_REG);
	ctrl &= ~RZN1_TIMER_CONTROL_ENABLE;
	writel(ctrl, timer->base + RZN1_TIMER_CONTROL_REG);
}

static void rzn1_timer_int_enable(struct rzn1_timer *timer)
{
	u32 ctrl;

	ctrl = readl(timer->base + RZN1_TIMER_CONTROL_REG);
	ctrl |= RZN1_TIMER_CONTROL_INT_UNMASK;
	writel(ctrl, timer->base + RZN1_TIMER_CONTROL_REG);
}

static irqreturn_t rzn1_timer_interrupt(int irq, void *dev_id)
{
	struct rzn1_timer *timer = dev_id;

	/* Ack the interrupt */
	readl(timer->base + RZN1_TIMER_CLEAR_INT_REG);

	if (timer->ced.event_handler)
		timer->ced.event_handler(&timer->ced);

	return IRQ_HANDLED;
}

static int rzn1_clkevt_set_next_event(unsigned long cycles,
				      struct clock_event_device *evt)
{
	struct rzn1_timer *timer = container_of(evt, struct rzn1_timer, ced);

	return rzn1_timer_config(timer, cycles, false);
}

static int rzn1_clkevt_shutdown(struct clock_event_device *evt)
{
	struct rzn1_timer *timer = container_of(evt, struct rzn1_timer, ced);

	rzn1_timer_disable(timer);
	return 0;
}

static int rzn1_clkevt_set_periodic(struct clock_event_device *evt)
{
	struct rzn1_timer *timer = container_of(evt, struct rzn1_timer, ced);

	rzn1_timer_config(timer, timer->hz_period, true);
	return 0;
}

/*
 * This global lock is used to prevent race conditions during global timers
 * test and set operation in case the driver is using the async probe option.
 */
static DEFINE_MUTEX(rzn1_global_timers_lock);

static int rzn1_timer_instance;

/* scheduler and clocksource */
static struct rzn1_timer *rzn1_sched_clock;

static u64 notrace rzn1_sched_read(void)
{
	return readl_relaxed(rzn1_sched_clock->base + RZN1_TIMER_CURRENT_COUNT_REG);
}

static u64 rzn1_clksrc_read(struct clocksource *cs)
{
	return readl_relaxed(rzn1_sched_clock->base + RZN1_TIMER_CURRENT_COUNT_REG);
}

static struct clocksource rzn1_clocksource = {
	.name = "rzn1_timer_clocksource",
	.rating = 200,
	.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	.read = rzn1_clksrc_read,
};

/* Timers table where 16-bit timers will be assigned per-cpu */
static struct rzn1_timer *rzn1_tab_timers;

static int rzn1_local_timer_starting_cpu(unsigned int cpu)
{
	struct rzn1_timer *timer;

	/* We assign the first and second 16-bit timer as per-cpu timers. */
	if (cpu >= 2)
		return 0;

	timer = &rzn1_tab_timers[cpu + RZN1_TIMER_BASE_INDEX_16BIT_TIMERS];

	timer->ced.cpumask = cpumask_of(cpu);
	irq_force_affinity(timer->ced.irq, cpumask_of(cpu));

	clockevents_config_and_register(&timer->ced, timer->rate, 1,
					GENMASK(timer->width - 1, 0));
	/*
	 * Time to enable the interrupt even if no interrupt can be fired yet.
	 * Indeed, the timer is still globally disabled.
	 * It will be enabled when rzn1_timer_config() is called
	 */
	enable_irq(timer->ced.irq);
	return 0;
}

static int rzn1_local_timer_dying_cpu(unsigned int cpu)
{
	struct rzn1_timer *timer;

	if (cpu >= 2)
		return 0;

	timer = &rzn1_tab_timers[cpu + RZN1_TIMER_BASE_INDEX_16BIT_TIMERS];

	rzn1_timer_disable(timer);
	disable_irq_nosync(timer->ced.irq);
	return 0;
}

static void rzn1_timer_init(struct rzn1_timer *timer, unsigned int index,
			    void __iomem *timers_base, unsigned long clock_rate)
{
	timer->base = timers_base + RZN1_TIMER_REGS_OFFSET(index);

	/* Disable the timer */
	writel(0, timer->base + RZN1_TIMER_CONTROL_REG);

	/* Clear potential interrupts */
	readl(timer->base + RZN1_TIMER_CLEAR_INT_REG);

	if (RZN1_TIMER_IS_16BIT_TIMER(index)) {
		timer->width = 16;
		/*
		 * Enable pre-scaler for 16-bit timers.
		 *
		 * Timers are fed by a 25MHz clock. With this pre-scaler (1/25)
		 * enabled, the 16-bit timers resolution is 1 microsecond.
		 */
		writel(RZN1_TIMER_CONTROL_PRESCALER_DIV25,
		       timer->base + RZN1_TIMER_CONTROL_REG);
		timer->rate = clock_rate / 25;
	} else {
		timer->width = 32;
		timer->rate = clock_rate;
	}
	timer->hz_period = DIV_ROUND_UP(timer->rate, HZ);
}

static void rzn1_timer_clkevt_init_ced(struct rzn1_timer *timer, const char *name,
				       int irq)
{
	timer->ced.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	timer->ced.set_next_event = rzn1_clkevt_set_next_event;
	timer->ced.set_state_shutdown = rzn1_clkevt_shutdown;
	timer->ced.set_state_periodic = rzn1_clkevt_set_periodic;
	timer->ced.cpumask = cpu_possible_mask;

	/*
	 * 32-bit timers are fed with a 25 MHz clock directly derived from the
	 * main PLL (1 GHz). The arch timer, ARMv7 CP15 in RZ/N1 SoCs, is fed
	 * with to a 6.25 MHz clock.
	 *
	 * The clock of the RZ/N1 timers block is a stable one and the RZ/N1
	 * timers have a higher resolution than the ARM CP15 one.
	 *
	 * Use a high rating for the 32-bit RZ/N1 timers.
	 */
	timer->ced.rating = timer->width == 16 ? 200 : 460;
	timer->ced.owner = THIS_MODULE;
	timer->ced.name = name;
	timer->ced.irq = irq;
}

static int rzn1_timer_setup_timers(struct platform_device *pdev,
				   struct rzn1_timer *tab_timers,
				   unsigned int first_timer_index,
				   unsigned int last_timer_index,
				   void __iomem *base, unsigned long clock_rate)
{
	struct device *dev = &pdev->dev;
	struct rzn1_timer *timer;
	unsigned int i;
	char *name;
	int irq;
	int ret;

	for (i = first_timer_index; i <= last_timer_index; i++) {
		timer = &tab_timers[i];

		rzn1_timer_init(timer, i, base, clock_rate);

		irq = platform_get_irq(pdev, i);
		if (irq < 0)
			return irq;

		name = devm_kasprintf(dev, GFP_KERNEL, "%s-%u", dev_name(dev), i);
		if (!name)
			return -ENOMEM;

		rzn1_timer_clkevt_init_ced(timer, name, irq);

		ret = devm_request_irq(dev, timer->ced.irq, rzn1_timer_interrupt,
				       IRQF_TIMER | IRQF_NO_AUTOEN,
				       timer->ced.name, timer);
		if (ret < 0)
			return dev_err_probe(dev, ret, "timer%u: Failed to request IRQ\n", i);

		rzn1_timer_int_enable(timer);
	}
	return 0;
}

static void rzn1_timer_register_clkevt(struct rzn1_timer *tab_timers,
				       unsigned int first_timer_index,
				       unsigned int last_timer_index)
{
	struct rzn1_timer *timer;
	unsigned int i;

	for (i = first_timer_index; i <= last_timer_index; i++) {
		timer = &tab_timers[i];
		clockevents_config_and_register(&timer->ced, timer->rate,
						1, GENMASK(timer->width - 1, 0));
		/*
		 * Time to enable the interrupt even if no interrupt can be
		 * fired yet. Indeed, the timer is still globally disabled.
		 * It will be enabled when rzn1_timer_config() is called
		 */
		enable_irq(timer->ced.irq);
	}
}

static int rzn1_timer_probe_first(struct platform_device *pdev,
				  struct rzn1_timer *tab_timers,
				  void __iomem *base, unsigned long clock_rate)
{
	struct device *dev = &pdev->dev;
	struct rzn1_timer *timer;
	int ret;

	/*
	 * Each timer instance has 6 x 16-bit timers and 2 x 32-bit timers.
	 *
	 * Probe the first instance. In that case, timers are assigned as
	 * follow:
	 *   - First and second 16-bit timers: clock events per CPU
	 *   - Last (second one) 32-bit timer: clocksource and sched_clock
	 *   - Other timers: clock events for all possible CPUs
	 *
	 * First step, perform all operation that could fail without calling
	 * clockevents_config_and_register(), sched_clock_register() nor
	 * cpuhp_setup_state(). Those operation don't have unregister nor
	 * teardown counterparts and so, once called, we cannot remove the
	 * related resource.
	 */

	/* First step for 16-bit timers and the first 32-bit timers. */
	ret = rzn1_timer_setup_timers(pdev, tab_timers,
				      RZN1_TIMER_BASE_INDEX_16BIT_TIMERS,
				      RZN1_TIMER_BASE_INDEX_32BIT_TIMERS,
				      base, clock_rate);
	if (ret)
		return ret;

	/*
	 * Second step, almost all operations that can fail have been called.
	 * Timers are ready to work. Start with the last operation that can fail,
	 * installing and invoking hotplug callbacks
	 */
	rzn1_tab_timers = tab_timers;
	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
				"clockevents/rzn1/global_timer:starting",
				rzn1_local_timer_starting_cpu,
				rzn1_local_timer_dying_cpu);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Setup CPU hotplug state failed\n");

	/*
	 * At this point, no more operations can fail. Perform remaining
	 * operations. Starting by handling the last (second one) 32-bit timer
	 */
	timer = &tab_timers[RZN1_TIMER_LAST_INDEX_32BIT_TIMERS];

	rzn1_timer_init(timer, RZN1_TIMER_LAST_INDEX_32BIT_TIMERS, base, clock_rate);
	rzn1_timer_config(timer, GENMASK(timer->width - 1, 0), true);

	rzn1_sched_clock = timer;

	sched_clock_register(rzn1_sched_read, rzn1_sched_clock->width, rzn1_sched_clock->rate);

	rzn1_clocksource.mask = CLOCKSOURCE_MASK(rzn1_sched_clock->width);
	clocksource_register_hz(&rzn1_clocksource, rzn1_sched_clock->rate);

	/*
	 * Register clockevents for 16-bit timers (except the first and second
	 * ones) and for the first 32-bit timer. The first and second 16-bit
	 * timers clockevents are registered by CPU hotplug startup function set
	 * previously by the cpuhp_setup_state() call.
	 */
	rzn1_timer_register_clkevt(tab_timers,
				   RZN1_TIMER_BASE_INDEX_16BIT_TIMERS + 2,
				   RZN1_TIMER_BASE_INDEX_32BIT_TIMERS);

	return 0;
}

static int rzn1_timer_probe_other(struct platform_device *pdev,
				  struct rzn1_timer *tab_timers,
				  void __iomem *base, unsigned long clock_rate)
{
	int ret;

	/*
	 * Probe other instance(s), i.e. not the first one. In that case,
	 * all timers are used as clock events and available for all possible
	 * CPUs
	 *
	 * First step, perform all operation that could fail without calling
	 * clockevents_config_and_register(). Unregister counterpart does not
	 * exist and so, once called, we cannot remove the related resource.
	 */
	ret = rzn1_timer_setup_timers(pdev, tab_timers,
				      RZN1_TIMER_BASE_INDEX_16BIT_TIMERS,
				      RZN1_TIMER_LAST_INDEX_32BIT_TIMERS,
				      base, clock_rate);
	if (ret)
		return ret;

	/*
	 * Second step, all operation that can fail have been called. We can
	 * register our timers.
	 */
	rzn1_timer_register_clkevt(tab_timers,
				   RZN1_TIMER_BASE_INDEX_16BIT_TIMERS,
				   RZN1_TIMER_LAST_INDEX_32BIT_TIMERS);
	return 0;
}

static int rzn1_timer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rzn1_timer *tab_timers;
	unsigned long clock_rate;
	void __iomem *base;
	struct clk *clk;
	int ret;

	tab_timers = devm_kcalloc(dev, RZN1_TIMER_NB_TIMERS, sizeof(*tab_timers),
				  GFP_KERNEL);
	if (!tab_timers)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get_enabled(dev, "pclk");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Failed to get pclk\n");

	clock_rate = clk_get_rate(clk);

	scoped_guard(mutex, &rzn1_global_timers_lock) {
		ret = !rzn1_timer_instance ?
			rzn1_timer_probe_first(pdev, tab_timers, base, clock_rate) :
			rzn1_timer_probe_other(pdev, tab_timers, base, clock_rate);
		if (!ret)
			rzn1_timer_instance++;
	}

	return ret;
}

static const struct of_device_id rzn1_timer_of_match[] = {
	{ .compatible = "renesas,rzn1-timer", },
	{ /* sentinel */ }
};

static struct platform_driver rzn1_timer_driver = {
	.driver = {
		.name = "rzn1_timer",
		.of_match_table = rzn1_timer_of_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver_probe(rzn1_timer_driver, rzn1_timer_probe);
