// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2016 Freescale Semiconductor, Inc.
 * Copyright 2018,2021-2025 NXP
 *
 * NXP System Timer Module:
 *
 *  STM supports commonly required system and application software
 *  timing functions. STM includes a 32-bit count-up timer and four
 *  32-bit compare channels with a separate interrupt source for each
 *  channel. The timer is driven by the STM module clock divided by an
 *  8-bit prescale value (1 to 256). It has ability to stop the timer
 *  in Debug mode
 */
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpuhotplug.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/sched_clock.h>
#include <linux/units.h>

#define STM_CR(__base)		(__base)

#define STM_CR_TEN		BIT(0)
#define STM_CR_FRZ		BIT(1)
#define STM_CR_CPS_OFFSET	8u
#define STM_CR_CPS_MASK		GENMASK(15, STM_CR_CPS_OFFSET)

#define STM_CNT(__base)		((__base) + 0x04)

#define STM_CCR0(__base)	((__base) + 0x10)
#define STM_CCR1(__base)	((__base) + 0x20)
#define STM_CCR2(__base)	((__base) + 0x30)
#define STM_CCR3(__base)	((__base) + 0x40)

#define STM_CCR_CEN		BIT(0)

#define STM_CIR0(__base)	((__base) + 0x14)
#define STM_CIR1(__base)	((__base) + 0x24)
#define STM_CIR2(__base)	((__base) + 0x34)
#define STM_CIR3(__base)	((__base) + 0x44)

#define STM_CIR_CIF		BIT(0)

#define STM_CMP0(__base)	((__base) + 0x18)
#define STM_CMP1(__base)	((__base) + 0x28)
#define STM_CMP2(__base)	((__base) + 0x38)
#define STM_CMP3(__base)	((__base) + 0x48)

#define STM_ENABLE_MASK	(STM_CR_FRZ | STM_CR_TEN)

struct stm_timer {
	void __iomem *base;
	unsigned long rate;
	unsigned long delta;
	unsigned long counter;
	struct clock_event_device ced;
	struct clocksource cs;
	atomic_t refcnt;
};

static DEFINE_PER_CPU(struct stm_timer *, stm_timers);

static struct stm_timer *stm_sched_clock;

/*
 * Global structure for multiple STMs initialization
 */
static int stm_instances;

/*
 * This global lock is used to prevent race conditions with the
 * stm_instances in case the driver is using the ASYNC option
 */
static DEFINE_MUTEX(stm_instances_lock);

DEFINE_GUARD(stm_instances, struct mutex *, mutex_lock(_T), mutex_unlock(_T))

static struct stm_timer *cs_to_stm(struct clocksource *cs)
{
	return container_of(cs, struct stm_timer, cs);
}

static struct stm_timer *ced_to_stm(struct clock_event_device *ced)
{
	return container_of(ced, struct stm_timer, ced);
}

static u64 notrace nxp_stm_read_sched_clock(void)
{
	return readl(STM_CNT(stm_sched_clock->base));
}

static u32 nxp_stm_clocksource_getcnt(struct stm_timer *stm_timer)
{
	return readl(STM_CNT(stm_timer->base));
}

static void nxp_stm_clocksource_setcnt(struct stm_timer *stm_timer, u32 cnt)
{
	writel(cnt, STM_CNT(stm_timer->base));
}

static u64 nxp_stm_clocksource_read(struct clocksource *cs)
{
	struct stm_timer *stm_timer = cs_to_stm(cs);

	return (u64)nxp_stm_clocksource_getcnt(stm_timer);
}

static void nxp_stm_module_enable(struct stm_timer *stm_timer)
{
	u32 reg;

	reg = readl(STM_CR(stm_timer->base));

	reg |= STM_ENABLE_MASK;

	writel(reg, STM_CR(stm_timer->base));
}

static void nxp_stm_module_disable(struct stm_timer *stm_timer)
{
	u32 reg;

	reg = readl(STM_CR(stm_timer->base));

	reg &= ~STM_ENABLE_MASK;

	writel(reg, STM_CR(stm_timer->base));
}

static void nxp_stm_module_put(struct stm_timer *stm_timer)
{
	if (atomic_dec_and_test(&stm_timer->refcnt))
		nxp_stm_module_disable(stm_timer);
}

static void nxp_stm_module_get(struct stm_timer *stm_timer)
{
	if (atomic_inc_return(&stm_timer->refcnt) == 1)
		nxp_stm_module_enable(stm_timer);
}

static int nxp_stm_clocksource_enable(struct clocksource *cs)
{
	struct stm_timer *stm_timer = cs_to_stm(cs);

	nxp_stm_module_get(stm_timer);

	return 0;
}

static void nxp_stm_clocksource_disable(struct clocksource *cs)
{
	struct stm_timer *stm_timer = cs_to_stm(cs);

	nxp_stm_module_put(stm_timer);
}

static void nxp_stm_clocksource_suspend(struct clocksource *cs)
{
	struct stm_timer *stm_timer = cs_to_stm(cs);

	nxp_stm_clocksource_disable(cs);
	stm_timer->counter = nxp_stm_clocksource_getcnt(stm_timer);
}

static void nxp_stm_clocksource_resume(struct clocksource *cs)
{
	struct stm_timer *stm_timer = cs_to_stm(cs);

	nxp_stm_clocksource_setcnt(stm_timer, stm_timer->counter);
	nxp_stm_clocksource_enable(cs);
}

static void __init devm_clocksource_unregister(void *data)
{
	struct stm_timer *stm_timer = data;

	clocksource_unregister(&stm_timer->cs);
}

static int __init nxp_stm_clocksource_init(struct device *dev, struct stm_timer *stm_timer,
					   const char *name, void __iomem *base, struct clk *clk)
{
	int ret;

	stm_timer->base = base;
	stm_timer->rate = clk_get_rate(clk);

	stm_timer->cs.name = name;
	stm_timer->cs.rating = 460;
	stm_timer->cs.read = nxp_stm_clocksource_read;
	stm_timer->cs.enable = nxp_stm_clocksource_enable;
	stm_timer->cs.disable = nxp_stm_clocksource_disable;
	stm_timer->cs.suspend = nxp_stm_clocksource_suspend;
	stm_timer->cs.resume = nxp_stm_clocksource_resume;
	stm_timer->cs.mask = CLOCKSOURCE_MASK(32);
	stm_timer->cs.flags = CLOCK_SOURCE_IS_CONTINUOUS;
	stm_timer->cs.owner = THIS_MODULE;

	ret = clocksource_register_hz(&stm_timer->cs, stm_timer->rate);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, devm_clocksource_unregister, stm_timer);
	if (ret) {
		clocksource_unregister(&stm_timer->cs);
		return ret;
	}

	stm_sched_clock = stm_timer;

	sched_clock_register(nxp_stm_read_sched_clock, 32, stm_timer->rate);

	dev_dbg(dev, "Registered clocksource %s\n", name);

	return 0;
}

static int nxp_stm_clockevent_read_counter(struct stm_timer *stm_timer)
{
	return readl(STM_CNT(stm_timer->base));
}

static void nxp_stm_clockevent_disable(struct stm_timer *stm_timer)
{
	writel(0, STM_CCR0(stm_timer->base));
}

static void nxp_stm_clockevent_enable(struct stm_timer *stm_timer)
{
	writel(STM_CCR_CEN, STM_CCR0(stm_timer->base));
}

static int nxp_stm_clockevent_shutdown(struct clock_event_device *ced)
{
	struct stm_timer *stm_timer = ced_to_stm(ced);

	nxp_stm_clockevent_disable(stm_timer);

	return 0;
}

static int nxp_stm_clockevent_set_next_event(unsigned long delta, struct clock_event_device *ced)
{
	struct stm_timer *stm_timer = ced_to_stm(ced);
	u32 val;

	nxp_stm_clockevent_disable(stm_timer);

	stm_timer->delta = delta;

	val = nxp_stm_clockevent_read_counter(stm_timer) + delta;

	writel(val, STM_CMP0(stm_timer->base));

	/*
	 * The counter is shared across the channels and can not be
	 * stopped while we are setting the next event. If the delta
	 * is very small it is possible the counter increases above
	 * the computed 'val'. The min_delta value specified when
	 * registering the clockevent will prevent that. The second
	 * case is if the counter wraps while we compute the 'val' and
	 * before writing the comparator register. We read the counter,
	 * check if we are back in time and abort the timer with -ETIME.
	 */
	if (val > nxp_stm_clockevent_read_counter(stm_timer) + delta)
		return -ETIME;

	nxp_stm_clockevent_enable(stm_timer);

	return 0;
}

static int nxp_stm_clockevent_set_periodic(struct clock_event_device *ced)
{
	struct stm_timer *stm_timer = ced_to_stm(ced);

	return nxp_stm_clockevent_set_next_event(stm_timer->rate, ced);
}

static void nxp_stm_clockevent_suspend(struct clock_event_device *ced)
{
	struct stm_timer *stm_timer = ced_to_stm(ced);

	nxp_stm_module_put(stm_timer);
}

static void nxp_stm_clockevent_resume(struct clock_event_device *ced)
{
	struct stm_timer *stm_timer = ced_to_stm(ced);

	nxp_stm_module_get(stm_timer);
}

static int __init nxp_stm_clockevent_per_cpu_init(struct device *dev, struct stm_timer *stm_timer,
						  const char *name, void __iomem *base, int irq,
						  struct clk *clk, int cpu)
{
	stm_timer->base = base;
	stm_timer->rate = clk_get_rate(clk);

	stm_timer->ced.name = name;
	stm_timer->ced.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	stm_timer->ced.set_state_shutdown = nxp_stm_clockevent_shutdown;
	stm_timer->ced.set_state_periodic = nxp_stm_clockevent_set_periodic;
	stm_timer->ced.set_next_event = nxp_stm_clockevent_set_next_event;
	stm_timer->ced.suspend = nxp_stm_clockevent_suspend;
	stm_timer->ced.resume = nxp_stm_clockevent_resume;
	stm_timer->ced.cpumask = cpumask_of(cpu);
	stm_timer->ced.rating = 460;
	stm_timer->ced.irq = irq;
	stm_timer->ced.owner = THIS_MODULE;

	per_cpu(stm_timers, cpu) = stm_timer;

	nxp_stm_module_get(stm_timer);

	dev_dbg(dev, "Initialized per cpu clockevent name=%s, irq=%d, cpu=%d\n", name, irq, cpu);

	return 0;
}

static int nxp_stm_clockevent_starting_cpu(unsigned int cpu)
{
	struct stm_timer *stm_timer = per_cpu(stm_timers, cpu);
	int ret;

	if (WARN_ON(!stm_timer))
		return -EFAULT;

	ret = irq_force_affinity(stm_timer->ced.irq, cpumask_of(cpu));
	if (ret)
		return ret;

	/*
	 * The timings measurement show reading the counter register
	 * and writing to the comparator register takes as a maximum
	 * value 1100 ns at 133MHz rate frequency. The timer must be
	 * set above this value and to be secure we set the minimum
	 * value equal to 2000ns, so 2us.
	 *
	 * minimum ticks = (rate / MICRO) * 2
	 */
	clockevents_config_and_register(&stm_timer->ced, stm_timer->rate,
					(stm_timer->rate / MICRO) * 2, ULONG_MAX);

	return 0;
}

static irqreturn_t nxp_stm_module_interrupt(int irq, void *dev_id)
{
	struct stm_timer *stm_timer = dev_id;
	struct clock_event_device *ced = &stm_timer->ced;
	u32 val;

	/*
	 * The interrupt is shared across the channels in the
	 * module. But this one is configured to run only one channel,
	 * consequently it is pointless to test the interrupt flags
	 * before and we can directly reset the channel 0 irq flag
	 * register.
	 */
	writel(STM_CIR_CIF, STM_CIR0(stm_timer->base));

	/*
	 * Update STM_CMP value using the counter value
	 */
	val = nxp_stm_clockevent_read_counter(stm_timer) + stm_timer->delta;

	writel(val, STM_CMP0(stm_timer->base));

	/*
	 * stm hardware doesn't support oneshot, it will generate an
	 * interrupt and start the counter again so software needs to
	 * disable the timer to stop the counter loop in ONESHOT mode.
	 */
	if (likely(clockevent_state_oneshot(ced)))
		nxp_stm_clockevent_disable(stm_timer);

	ced->event_handler(ced);

	return IRQ_HANDLED;
}

static int __init nxp_stm_timer_probe(struct platform_device *pdev)
{
	struct stm_timer *stm_timer;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const char *name = of_node_full_name(np);
	struct clk *clk;
	void __iomem *base;
	int irq, ret;

	/*
	 * The device tree can have multiple STM nodes described, so
	 * it makes this driver a good candidate for the async probe.
	 * It is still unclear if the time framework correctly handles
	 * parallel loading of the timers but at least this driver is
	 * ready to support the option.
	 */
	guard(stm_instances)(&stm_instances_lock);

	/*
	 * The S32Gx are SoCs featuring a diverse set of cores. Linux
	 * is expected to run on Cortex-A53 cores, while other
	 * software stacks will operate on Cortex-M cores. The number
	 * of STM instances has been sized to include at most one
	 * instance per core.
	 *
	 * As we need a clocksource and a clockevent per cpu, we
	 * simply initialize a clocksource per cpu along with the
	 * clockevent which makes the resulting code simpler.
	 *
	 * However if the device tree is describing more STM instances
	 * than the number of cores, then we ignore them.
	 */
	if (stm_instances >= num_possible_cpus())
		return 0;

	base = devm_of_iomap(dev, np, 0, NULL);
	if (IS_ERR(base))
		return dev_err_probe(dev, PTR_ERR(base), "Failed to iomap %pOFn\n", np);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return dev_err_probe(dev, irq, "Failed to get IRQ\n");

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Clock not found\n");

	stm_timer = devm_kzalloc(dev, sizeof(*stm_timer), GFP_KERNEL);
	if (!stm_timer)
		return -ENOMEM;

	ret = devm_request_irq(dev, irq, nxp_stm_module_interrupt,
			       IRQF_TIMER | IRQF_NOBALANCING, name, stm_timer);
	if (ret)
		return dev_err_probe(dev, ret, "Unable to allocate interrupt line\n");

	ret = nxp_stm_clocksource_init(dev, stm_timer, name, base, clk);
	if (ret)
		return ret;

	/*
	 * Next probed STM will be a per CPU clockevent, until we
	 * probe as many as we have CPUs available on the system, we
	 * do a partial initialization
	 */
	ret = nxp_stm_clockevent_per_cpu_init(dev, stm_timer, name,
					      base, irq, clk,
					      stm_instances);
	if (ret)
		return ret;

	stm_instances++;

	/*
	 * The number of probed STMs for per CPU clockevent is
	 * equal to the number of available CPUs on the
	 * system. We install the cpu hotplug to finish the
	 * initialization by registering the clockevents
	 */
	if (stm_instances == num_possible_cpus()) {
		ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "STM timer:starting",
					nxp_stm_clockevent_starting_cpu, NULL);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static const struct of_device_id nxp_stm_of_match[] = {
	{ .compatible = "nxp,s32g2-stm" },
	{ }
};
MODULE_DEVICE_TABLE(of, nxp_stm_of_match);

static struct platform_driver nxp_stm_probe = {
	.probe	= nxp_stm_timer_probe,
	.driver	= {
		.name		= "nxp-stm",
		.of_match_table	= nxp_stm_of_match,
	},
};
module_platform_driver(nxp_stm_probe);

MODULE_DESCRIPTION("NXP System Timer Module driver");
MODULE_LICENSE("GPL");
