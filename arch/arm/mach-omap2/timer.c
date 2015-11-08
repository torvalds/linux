/*
 * linux/arch/arm/mach-omap2/timer.c
 *
 * OMAP2 GP timer support.
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Update to use new clocksource/clockevent layers
 * Author: Kevin Hilman, MontaVista Software, Inc. <source@mvista.com>
 * Copyright (C) 2007 MontaVista Software, Inc.
 *
 * Original driver:
 * Copyright (C) 2005 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *         Juha Yrjölä <juha.yrjola@nokia.com>
 * OMAP Dual-mode timer framework support by Timo Teras
 *
 * Some parts based off of TI's 24xx code:
 *
 * Copyright (C) 2004-2009 Texas Instruments, Inc.
 *
 * Roughly modelled after the OMAP1 MPU timer code.
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/platform_data/dmtimer-omap.h>
#include <linux/sched_clock.h>

#include <asm/mach/time.h>
#include <asm/smp_twd.h>

#include "omap_hwmod.h"
#include "omap_device.h"
#include <plat/counter-32k.h>
#include <plat/dmtimer.h>
#include "omap-pm.h"

#include "soc.h"
#include "common.h"
#include "control.h"
#include "powerdomain.h"
#include "omap-secure.h"

#define REALTIME_COUNTER_BASE				0x48243200
#define INCREMENTER_NUMERATOR_OFFSET			0x10
#define INCREMENTER_DENUMERATOR_RELOAD_OFFSET		0x14
#define NUMERATOR_DENUMERATOR_MASK			0xfffff000

/* Clockevent code */

static struct omap_dm_timer clkev;
static struct clock_event_device clockevent_gpt;

#ifdef CONFIG_SOC_HAS_REALTIME_COUNTER
static unsigned long arch_timer_freq;

void set_cntfreq(void)
{
	omap_smc1(OMAP5_DRA7_MON_SET_CNTFRQ_INDEX, arch_timer_freq);
}
#endif

static irqreturn_t omap2_gp_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = &clockevent_gpt;

	__omap_dm_timer_write_status(&clkev, OMAP_TIMER_INT_OVERFLOW);

	evt->event_handler(evt);
	return IRQ_HANDLED;
}

static struct irqaction omap2_gp_timer_irq = {
	.name		= "gp_timer",
	.flags		= IRQF_TIMER | IRQF_IRQPOLL,
	.handler	= omap2_gp_timer_interrupt,
};

static int omap2_gp_timer_set_next_event(unsigned long cycles,
					 struct clock_event_device *evt)
{
	__omap_dm_timer_load_start(&clkev, OMAP_TIMER_CTRL_ST,
				   0xffffffff - cycles, OMAP_TIMER_POSTED);

	return 0;
}

static int omap2_gp_timer_shutdown(struct clock_event_device *evt)
{
	__omap_dm_timer_stop(&clkev, OMAP_TIMER_POSTED, clkev.rate);
	return 0;
}

static int omap2_gp_timer_set_periodic(struct clock_event_device *evt)
{
	u32 period;

	__omap_dm_timer_stop(&clkev, OMAP_TIMER_POSTED, clkev.rate);

	period = clkev.rate / HZ;
	period -= 1;
	/* Looks like we need to first set the load value separately */
	__omap_dm_timer_write(&clkev, OMAP_TIMER_LOAD_REG, 0xffffffff - period,
			      OMAP_TIMER_POSTED);
	__omap_dm_timer_load_start(&clkev,
				   OMAP_TIMER_CTRL_AR | OMAP_TIMER_CTRL_ST,
				   0xffffffff - period, OMAP_TIMER_POSTED);
	return 0;
}

static struct clock_event_device clockevent_gpt = {
	.features		= CLOCK_EVT_FEAT_PERIODIC |
				  CLOCK_EVT_FEAT_ONESHOT,
	.rating			= 300,
	.set_next_event		= omap2_gp_timer_set_next_event,
	.set_state_shutdown	= omap2_gp_timer_shutdown,
	.set_state_periodic	= omap2_gp_timer_set_periodic,
	.set_state_oneshot	= omap2_gp_timer_shutdown,
	.tick_resume		= omap2_gp_timer_shutdown,
};

static struct property device_disabled = {
	.name = "status",
	.length = sizeof("disabled"),
	.value = "disabled",
};

static const struct of_device_id omap_timer_match[] __initconst = {
	{ .compatible = "ti,omap2420-timer", },
	{ .compatible = "ti,omap3430-timer", },
	{ .compatible = "ti,omap4430-timer", },
	{ .compatible = "ti,omap5430-timer", },
	{ .compatible = "ti,dm814-timer", },
	{ .compatible = "ti,dm816-timer", },
	{ .compatible = "ti,am335x-timer", },
	{ .compatible = "ti,am335x-timer-1ms", },
	{ }
};

/**
 * omap_get_timer_dt - get a timer using device-tree
 * @match	- device-tree match structure for matching a device type
 * @property	- optional timer property to match
 *
 * Helper function to get a timer during early boot using device-tree for use
 * as kernel system timer. Optionally, the property argument can be used to
 * select a timer with a specific property. Once a timer is found then mark
 * the timer node in device-tree as disabled, to prevent the kernel from
 * registering this timer as a platform device and so no one else can use it.
 */
static struct device_node * __init omap_get_timer_dt(const struct of_device_id *match,
						     const char *property)
{
	struct device_node *np;

	for_each_matching_node(np, match) {
		if (!of_device_is_available(np))
			continue;

		if (property && !of_get_property(np, property, NULL))
			continue;

		if (!property && (of_get_property(np, "ti,timer-alwon", NULL) ||
				  of_get_property(np, "ti,timer-dsp", NULL) ||
				  of_get_property(np, "ti,timer-pwm", NULL) ||
				  of_get_property(np, "ti,timer-secure", NULL)))
			continue;

		of_add_property(np, &device_disabled);
		return np;
	}

	return NULL;
}

/**
 * omap_dmtimer_init - initialisation function when device tree is used
 *
 * For secure OMAP3 devices, timers with device type "timer-secure" cannot
 * be used by the kernel as they are reserved. Therefore, to prevent the
 * kernel registering these devices remove them dynamically from the device
 * tree on boot.
 */
static void __init omap_dmtimer_init(void)
{
	struct device_node *np;

	if (!cpu_is_omap34xx())
		return;

	/* If we are a secure device, remove any secure timer nodes */
	if ((omap_type() != OMAP2_DEVICE_TYPE_GP)) {
		np = omap_get_timer_dt(omap_timer_match, "ti,timer-secure");
		of_node_put(np);
	}
}

/**
 * omap_dm_timer_get_errata - get errata flags for a timer
 *
 * Get the timer errata flags that are specific to the OMAP device being used.
 */
static u32 __init omap_dm_timer_get_errata(void)
{
	if (cpu_is_omap24xx())
		return 0;

	return OMAP_TIMER_ERRATA_I103_I767;
}

static int __init omap_dm_timer_init_one(struct omap_dm_timer *timer,
					 const char *fck_source,
					 const char *property,
					 const char **timer_name,
					 int posted)
{
	char name[10]; /* 10 = sizeof("gptXX_Xck0") */
	const char *oh_name = NULL;
	struct device_node *np;
	struct omap_hwmod *oh;
	struct resource irq, mem;
	struct clk *src;
	int r = 0;

	if (of_have_populated_dt()) {
		np = omap_get_timer_dt(omap_timer_match, property);
		if (!np)
			return -ENODEV;

		of_property_read_string_index(np, "ti,hwmods", 0, &oh_name);
		if (!oh_name)
			return -ENODEV;

		timer->irq = irq_of_parse_and_map(np, 0);
		if (!timer->irq)
			return -ENXIO;

		timer->io_base = of_iomap(np, 0);

		of_node_put(np);
	} else {
		if (omap_dm_timer_reserve_systimer(timer->id))
			return -ENODEV;

		sprintf(name, "timer%d", timer->id);
		oh_name = name;
	}

	oh = omap_hwmod_lookup(oh_name);
	if (!oh)
		return -ENODEV;

	*timer_name = oh->name;

	if (!of_have_populated_dt()) {
		r = omap_hwmod_get_resource_byname(oh, IORESOURCE_IRQ, NULL,
						   &irq);
		if (r)
			return -ENXIO;
		timer->irq = irq.start;

		r = omap_hwmod_get_resource_byname(oh, IORESOURCE_MEM, NULL,
						   &mem);
		if (r)
			return -ENXIO;

		/* Static mapping, never released */
		timer->io_base = ioremap(mem.start, mem.end - mem.start);
	}

	if (!timer->io_base)
		return -ENXIO;

	/* After the dmtimer is using hwmod these clocks won't be needed */
	timer->fclk = clk_get(NULL, omap_hwmod_get_main_clk(oh));
	if (IS_ERR(timer->fclk))
		return PTR_ERR(timer->fclk);

	src = clk_get(NULL, fck_source);
	if (IS_ERR(src))
		return PTR_ERR(src);

	WARN(clk_set_parent(timer->fclk, src) < 0,
	     "Cannot set timer parent clock, no PLL clock driver?");

	clk_put(src);

	omap_hwmod_setup_one(oh_name);
	omap_hwmod_enable(oh);
	__omap_dm_timer_init_regs(timer);

	if (posted)
		__omap_dm_timer_enable_posted(timer);

	/* Check that the intended posted configuration matches the actual */
	if (posted != timer->posted)
		return -EINVAL;

	timer->rate = clk_get_rate(timer->fclk);
	timer->reserved = 1;

	return r;
}

static void __init omap2_gp_clockevent_init(int gptimer_id,
						const char *fck_source,
						const char *property)
{
	int res;

	clkev.id = gptimer_id;
	clkev.errata = omap_dm_timer_get_errata();

	/*
	 * For clock-event timers we never read the timer counter and
	 * so we are not impacted by errata i103 and i767. Therefore,
	 * we can safely ignore this errata for clock-event timers.
	 */
	__omap_dm_timer_override_errata(&clkev, OMAP_TIMER_ERRATA_I103_I767);

	res = omap_dm_timer_init_one(&clkev, fck_source, property,
				     &clockevent_gpt.name, OMAP_TIMER_POSTED);
	BUG_ON(res);

	omap2_gp_timer_irq.dev_id = &clkev;
	setup_irq(clkev.irq, &omap2_gp_timer_irq);

	__omap_dm_timer_int_enable(&clkev, OMAP_TIMER_INT_OVERFLOW);

	clockevent_gpt.cpumask = cpu_possible_mask;
	clockevent_gpt.irq = omap_dm_timer_get_irq(&clkev);
	clockevents_config_and_register(&clockevent_gpt, clkev.rate,
					3, /* Timer internal resynch latency */
					0xffffffff);

	pr_info("OMAP clockevent source: %s at %lu Hz\n", clockevent_gpt.name,
		clkev.rate);
}

/* Clocksource code */
static struct omap_dm_timer clksrc;
static bool use_gptimer_clksrc __initdata;

/*
 * clocksource
 */
static cycle_t clocksource_read_cycles(struct clocksource *cs)
{
	return (cycle_t)__omap_dm_timer_read_counter(&clksrc,
						     OMAP_TIMER_NONPOSTED);
}

static struct clocksource clocksource_gpt = {
	.rating		= 300,
	.read		= clocksource_read_cycles,
	.mask		= CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static u64 notrace dmtimer_read_sched_clock(void)
{
	if (clksrc.reserved)
		return __omap_dm_timer_read_counter(&clksrc,
						    OMAP_TIMER_NONPOSTED);

	return 0;
}

static const struct of_device_id omap_counter_match[] __initconst = {
	{ .compatible = "ti,omap-counter32k", },
	{ }
};

/* Setup free-running counter for clocksource */
static int __init __maybe_unused omap2_sync32k_clocksource_init(void)
{
	int ret;
	struct device_node *np = NULL;
	struct omap_hwmod *oh;
	void __iomem *vbase;
	const char *oh_name = "counter_32k";

	/*
	 * If device-tree is present, then search the DT blob
	 * to see if the 32kHz counter is supported.
	 */
	if (of_have_populated_dt()) {
		np = omap_get_timer_dt(omap_counter_match, NULL);
		if (!np)
			return -ENODEV;

		of_property_read_string_index(np, "ti,hwmods", 0, &oh_name);
		if (!oh_name)
			return -ENODEV;
	}

	/*
	 * First check hwmod data is available for sync32k counter
	 */
	oh = omap_hwmod_lookup(oh_name);
	if (!oh || oh->slaves_cnt == 0)
		return -ENODEV;

	omap_hwmod_setup_one(oh_name);

	if (np) {
		vbase = of_iomap(np, 0);
		of_node_put(np);
	} else {
		vbase = omap_hwmod_get_mpu_rt_va(oh);
	}

	if (!vbase) {
		pr_warn("%s: failed to get counter_32k resource\n", __func__);
		return -ENXIO;
	}

	ret = omap_hwmod_enable(oh);
	if (ret) {
		pr_warn("%s: failed to enable counter_32k module (%d)\n",
							__func__, ret);
		return ret;
	}

	ret = omap_init_clocksource_32k(vbase);
	if (ret) {
		pr_warn("%s: failed to initialize counter_32k as a clocksource (%d)\n",
							__func__, ret);
		omap_hwmod_idle(oh);
	}

	return ret;
}

static void __init omap2_gptimer_clocksource_init(int gptimer_id,
						  const char *fck_source,
						  const char *property)
{
	int res;

	clksrc.id = gptimer_id;
	clksrc.errata = omap_dm_timer_get_errata();

	res = omap_dm_timer_init_one(&clksrc, fck_source, property,
				     &clocksource_gpt.name,
				     OMAP_TIMER_NONPOSTED);
	BUG_ON(res);

	__omap_dm_timer_load_start(&clksrc,
				   OMAP_TIMER_CTRL_ST | OMAP_TIMER_CTRL_AR, 0,
				   OMAP_TIMER_NONPOSTED);
	sched_clock_register(dmtimer_read_sched_clock, 32, clksrc.rate);

	if (clocksource_register_hz(&clocksource_gpt, clksrc.rate))
		pr_err("Could not register clocksource %s\n",
			clocksource_gpt.name);
	else
		pr_info("OMAP clocksource: %s at %lu Hz\n",
			clocksource_gpt.name, clksrc.rate);
}

#ifdef CONFIG_SOC_HAS_REALTIME_COUNTER
/*
 * The realtime counter also called master counter, is a free-running
 * counter, which is related to real time. It produces the count used
 * by the CPU local timer peripherals in the MPU cluster. The timer counts
 * at a rate of 6.144 MHz. Because the device operates on different clocks
 * in different power modes, the master counter shifts operation between
 * clocks, adjusting the increment per clock in hardware accordingly to
 * maintain a constant count rate.
 */
static void __init realtime_counter_init(void)
{
	void __iomem *base;
	static struct clk *sys_clk;
	unsigned long rate;
	unsigned int reg;
	unsigned long long num, den;

	base = ioremap(REALTIME_COUNTER_BASE, SZ_32);
	if (!base) {
		pr_err("%s: ioremap failed\n", __func__);
		return;
	}
	sys_clk = clk_get(NULL, "sys_clkin");
	if (IS_ERR(sys_clk)) {
		pr_err("%s: failed to get system clock handle\n", __func__);
		iounmap(base);
		return;
	}

	rate = clk_get_rate(sys_clk);

	if (soc_is_dra7xx()) {
		/*
		 * Errata i856 says the 32.768KHz crystal does not start at
		 * power on, so the CPU falls back to an emulated 32KHz clock
		 * based on sysclk / 610 instead. This causes the master counter
		 * frequency to not be 6.144MHz but at sysclk / 610 * 375 / 2
		 * (OR sysclk * 75 / 244)
		 *
		 * This affects at least the DRA7/AM572x 1.0, 1.1 revisions.
		 * Of course any board built without a populated 32.768KHz
		 * crystal would also need this fix even if the CPU is fixed
		 * later.
		 *
		 * Either case can be detected by using the two speedselect bits
		 * If they are not 0, then the 32.768KHz clock driving the
		 * coarse counter that corrects the fine counter every time it
		 * ticks is actually rate/610 rather than 32.768KHz and we
		 * should compensate to avoid the 570ppm (at 20MHz, much worse
		 * at other rates) too fast system time.
		 */
		reg = omap_ctrl_readl(DRA7_CTRL_CORE_BOOTSTRAP);
		if (reg & DRA7_SPEEDSELECT_MASK) {
			num = 75;
			den = 244;
			goto sysclk1_based;
		}
	}

	/* Numerator/denumerator values refer TRM Realtime Counter section */
	switch (rate) {
	case 12000000:
		num = 64;
		den = 125;
		break;
	case 13000000:
		num = 768;
		den = 1625;
		break;
	case 19200000:
		num = 8;
		den = 25;
		break;
	case 20000000:
		num = 192;
		den = 625;
		break;
	case 26000000:
		num = 384;
		den = 1625;
		break;
	case 27000000:
		num = 256;
		den = 1125;
		break;
	case 38400000:
	default:
		/* Program it for 38.4 MHz */
		num = 4;
		den = 25;
		break;
	}

sysclk1_based:
	/* Program numerator and denumerator registers */
	reg = readl_relaxed(base + INCREMENTER_NUMERATOR_OFFSET) &
			NUMERATOR_DENUMERATOR_MASK;
	reg |= num;
	writel_relaxed(reg, base + INCREMENTER_NUMERATOR_OFFSET);

	reg = readl_relaxed(base + INCREMENTER_DENUMERATOR_RELOAD_OFFSET) &
			NUMERATOR_DENUMERATOR_MASK;
	reg |= den;
	writel_relaxed(reg, base + INCREMENTER_DENUMERATOR_RELOAD_OFFSET);

	arch_timer_freq = DIV_ROUND_UP_ULL(rate * num, den);
	set_cntfreq();

	iounmap(base);
}
#else
static inline void __init realtime_counter_init(void)
{}
#endif

#define OMAP_SYS_GP_TIMER_INIT(name, clkev_nr, clkev_src, clkev_prop,	\
			       clksrc_nr, clksrc_src, clksrc_prop)	\
void __init omap##name##_gptimer_timer_init(void)			\
{									\
	omap_clk_init();					\
	omap_dmtimer_init();						\
	omap2_gp_clockevent_init((clkev_nr), clkev_src, clkev_prop);	\
	omap2_gptimer_clocksource_init((clksrc_nr), clksrc_src,		\
					clksrc_prop);			\
}

#define OMAP_SYS_32K_TIMER_INIT(name, clkev_nr, clkev_src, clkev_prop,	\
				clksrc_nr, clksrc_src, clksrc_prop)	\
void __init omap##name##_sync32k_timer_init(void)		\
{									\
	omap_clk_init();					\
	omap_dmtimer_init();						\
	omap2_gp_clockevent_init((clkev_nr), clkev_src, clkev_prop);	\
	/* Enable the use of clocksource="gp_timer" kernel parameter */	\
	if (use_gptimer_clksrc)						\
		omap2_gptimer_clocksource_init((clksrc_nr), clksrc_src,	\
						clksrc_prop);		\
	else								\
		omap2_sync32k_clocksource_init();			\
}

#ifdef CONFIG_ARCH_OMAP2
OMAP_SYS_32K_TIMER_INIT(2, 1, "timer_32k_ck", "ti,timer-alwon",
			2, "timer_sys_ck", NULL);
#endif /* CONFIG_ARCH_OMAP2 */

#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_SOC_AM43XX)
OMAP_SYS_32K_TIMER_INIT(3, 1, "timer_32k_ck", "ti,timer-alwon",
			2, "timer_sys_ck", NULL);
OMAP_SYS_32K_TIMER_INIT(3_secure, 12, "secure_32k_fck", "ti,timer-secure",
			2, "timer_sys_ck", NULL);
#endif /* CONFIG_ARCH_OMAP3 */

#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_SOC_AM33XX) || \
	defined(CONFIG_SOC_AM43XX)
OMAP_SYS_GP_TIMER_INIT(3, 2, "timer_sys_ck", NULL,
		       1, "timer_sys_ck", "ti,timer-alwon");
#endif

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
	defined(CONFIG_SOC_DRA7XX)
static OMAP_SYS_32K_TIMER_INIT(4, 1, "timer_32k_ck", "ti,timer-alwon",
			       2, "sys_clkin_ck", NULL);
#endif

#ifdef CONFIG_ARCH_OMAP4
#ifdef CONFIG_HAVE_ARM_TWD
void __init omap4_local_timer_init(void)
{
	omap4_sync32k_timer_init();
	clocksource_probe();
}
#else
void __init omap4_local_timer_init(void)
{
	omap4_sync32k_timer_init();
}
#endif /* CONFIG_HAVE_ARM_TWD */
#endif /* CONFIG_ARCH_OMAP4 */

#if defined(CONFIG_SOC_OMAP5) || defined(CONFIG_SOC_DRA7XX)
void __init omap5_realtime_timer_init(void)
{
	omap4_sync32k_timer_init();
	realtime_counter_init();

	clocksource_probe();
}
#endif /* CONFIG_SOC_OMAP5 || CONFIG_SOC_DRA7XX */

/**
 * omap_timer_init - build and register timer device with an
 * associated timer hwmod
 * @oh:	timer hwmod pointer to be used to build timer device
 * @user:	parameter that can be passed from calling hwmod API
 *
 * Called by omap_hwmod_for_each_by_class to register each of the timer
 * devices present in the system. The number of timer devices is known
 * by parsing through the hwmod database for a given class name. At the
 * end of function call memory is allocated for timer device and it is
 * registered to the framework ready to be proved by the driver.
 */
static int __init omap_timer_init(struct omap_hwmod *oh, void *unused)
{
	int id;
	int ret = 0;
	char *name = "omap_timer";
	struct dmtimer_platform_data *pdata;
	struct platform_device *pdev;
	struct omap_timer_capability_dev_attr *timer_dev_attr;

	pr_debug("%s: %s\n", __func__, oh->name);

	/* on secure device, do not register secure timer */
	timer_dev_attr = oh->dev_attr;
	if (omap_type() != OMAP2_DEVICE_TYPE_GP && timer_dev_attr)
		if (timer_dev_attr->timer_capability == OMAP_TIMER_SECURE)
			return ret;

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: No memory for [%s]\n", __func__, oh->name);
		return -ENOMEM;
	}

	/*
	 * Extract the IDs from name field in hwmod database
	 * and use the same for constructing ids' for the
	 * timer devices. In a way, we are avoiding usage of
	 * static variable witin the function to do the same.
	 * CAUTION: We have to be careful and make sure the
	 * name in hwmod database does not change in which case
	 * we might either make corresponding change here or
	 * switch back static variable mechanism.
	 */
	sscanf(oh->name, "timer%2d", &id);

	if (timer_dev_attr)
		pdata->timer_capability = timer_dev_attr->timer_capability;

	pdata->timer_errata = omap_dm_timer_get_errata();
	pdata->get_context_loss_count = omap_pm_get_dev_context_loss_count;

	pdev = omap_device_build(name, id, oh, pdata, sizeof(*pdata));

	if (IS_ERR(pdev)) {
		pr_err("%s: Can't build omap_device for %s: %s.\n",
			__func__, name, oh->name);
		ret = -EINVAL;
	}

	kfree(pdata);

	return ret;
}

/**
 * omap2_dm_timer_init - top level regular device initialization
 *
 * Uses dedicated hwmod api to parse through hwmod database for
 * given class name and then build and register the timer device.
 */
static int __init omap2_dm_timer_init(void)
{
	int ret;

	/* If dtb is there, the devices will be created dynamically */
	if (of_have_populated_dt())
		return -ENODEV;

	ret = omap_hwmod_for_each_by_class("timer", omap_timer_init, NULL);
	if (unlikely(ret)) {
		pr_err("%s: device registration failed.\n", __func__);
		return -EINVAL;
	}

	return 0;
}
omap_arch_initcall(omap2_dm_timer_init);

/**
 * omap2_override_clocksource - clocksource override with user configuration
 *
 * Allows user to override default clocksource, using kernel parameter
 *   clocksource="gp_timer"	(For all OMAP2PLUS architectures)
 *
 * Note that, here we are using same standard kernel parameter "clocksource=",
 * and not introducing any OMAP specific interface.
 */
static int __init omap2_override_clocksource(char *str)
{
	if (!str)
		return 0;
	/*
	 * For OMAP architecture, we only have two options
	 *    - sync_32k (default)
	 *    - gp_timer (sys_clk based)
	 */
	if (!strcmp(str, "gp_timer"))
		use_gptimer_clksrc = true;

	return 0;
}
early_param("clocksource", omap2_override_clocksource);
