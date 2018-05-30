/*
 * pm.c - Common OMAP2+ power management-related code
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Copyright (C) 2010 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/pm_opp.h>
#include <linux/export.h>
#include <linux/suspend.h>
#include <linux/cpu.h>

#include <asm/system_misc.h>

#include "omap-pm.h"
#include "omap_device.h"
#include "common.h"

#include "soc.h"
#include "prcm-common.h"
#include "voltage.h"
#include "powerdomain.h"
#include "clockdomain.h"
#include "pm.h"
#include "twl-common.h"

#ifdef CONFIG_SUSPEND
/*
 * omap_pm_suspend: points to a function that does the SoC-specific
 * suspend work
 */
static int (*omap_pm_suspend)(void);
#endif

#ifdef CONFIG_PM
/**
 * struct omap2_oscillator - Describe the board main oscillator latencies
 * @startup_time: oscillator startup latency
 * @shutdown_time: oscillator shutdown latency
 */
struct omap2_oscillator {
	u32 startup_time;
	u32 shutdown_time;
};

static struct omap2_oscillator oscillator = {
	.startup_time = ULONG_MAX,
	.shutdown_time = ULONG_MAX,
};

void omap_pm_setup_oscillator(u32 tstart, u32 tshut)
{
	oscillator.startup_time = tstart;
	oscillator.shutdown_time = tshut;
}

void omap_pm_get_oscillator(u32 *tstart, u32 *tshut)
{
	if (!tstart || !tshut)
		return;

	*tstart = oscillator.startup_time;
	*tshut = oscillator.shutdown_time;
}
#endif

static int __init _init_omap_device(char *name)
{
	struct omap_hwmod *oh;
	struct platform_device *pdev;

	oh = omap_hwmod_lookup(name);
	if (WARN(!oh, "%s: could not find omap_hwmod for %s\n",
		 __func__, name))
		return -ENODEV;

	pdev = omap_device_build(oh->name, 0, oh, NULL, 0);
	if (WARN(IS_ERR(pdev), "%s: could not build omap_device for %s\n",
		 __func__, name))
		return -ENODEV;

	return 0;
}

/*
 * Build omap_devices for processors and bus.
 */
static void __init omap2_init_processor_devices(void)
{
	_init_omap_device("mpu");
	if (omap3_has_iva())
		_init_omap_device("iva");

	if (cpu_is_omap44xx()) {
		_init_omap_device("l3_main_1");
		_init_omap_device("dsp");
		_init_omap_device("iva");
	} else {
		_init_omap_device("l3_main");
	}
}

int __init omap_pm_clkdms_setup(struct clockdomain *clkdm, void *unused)
{
	/* XXX The usecount test is racy */
	if ((clkdm->flags & CLKDM_CAN_ENABLE_AUTO) &&
	    !(clkdm->flags & CLKDM_MISSING_IDLE_REPORTING))
		clkdm_allow_idle(clkdm);
	else if (clkdm->flags & CLKDM_CAN_FORCE_SLEEP &&
		 clkdm->usecount == 0)
		clkdm_sleep(clkdm);
	return 0;
}

/*
 * This API is to be called during init to set the various voltage
 * domains to the voltage as per the opp table. Typically we boot up
 * at the nominal voltage. So this function finds out the rate of
 * the clock associated with the voltage domain, finds out the correct
 * opp entry and sets the voltage domain to the voltage specified
 * in the opp entry
 */
static int __init omap2_set_init_voltage(char *vdd_name, char *clk_name,
					 const char *oh_name)
{
	struct voltagedomain *voltdm;
	struct clk *clk;
	struct dev_pm_opp *opp;
	unsigned long freq, bootup_volt;
	struct device *dev;

	if (!vdd_name || !clk_name || !oh_name) {
		pr_err("%s: invalid parameters\n", __func__);
		goto exit;
	}

	if (!strncmp(oh_name, "mpu", 3))
		/* 
		 * All current OMAPs share voltage rail and clock
		 * source, so CPU0 is used to represent the MPU-SS.
		 */
		dev = get_cpu_device(0);
	else
		dev = omap_device_get_by_hwmod_name(oh_name);

	if (IS_ERR(dev)) {
		pr_err("%s: Unable to get dev pointer for hwmod %s\n",
			__func__, oh_name);
		goto exit;
	}

	voltdm = voltdm_lookup(vdd_name);
	if (!voltdm) {
		pr_err("%s: unable to get vdd pointer for vdd_%s\n",
			__func__, vdd_name);
		goto exit;
	}

	clk =  clk_get(NULL, clk_name);
	if (IS_ERR(clk)) {
		pr_err("%s: unable to get clk %s\n", __func__, clk_name);
		goto exit;
	}

	freq = clk_get_rate(clk);
	clk_put(clk);

	rcu_read_lock();
	opp = dev_pm_opp_find_freq_ceil(dev, &freq);
	if (IS_ERR(opp)) {
		rcu_read_unlock();
		pr_err("%s: unable to find boot up OPP for vdd_%s\n",
			__func__, vdd_name);
		goto exit;
	}

	bootup_volt = dev_pm_opp_get_voltage(opp);
	rcu_read_unlock();
	if (!bootup_volt) {
		pr_err("%s: unable to find voltage corresponding to the bootup OPP for vdd_%s\n",
		       __func__, vdd_name);
		goto exit;
	}

	voltdm_scale(voltdm, bootup_volt);
	return 0;

exit:
	pr_err("%s: unable to set vdd_%s\n", __func__, vdd_name);
	return -EINVAL;
}

#ifdef CONFIG_SUSPEND
static int omap_pm_enter(suspend_state_t suspend_state)
{
	int ret = 0;

	if (!omap_pm_suspend)
		return -ENOENT; /* XXX doublecheck */

	switch (suspend_state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		ret = omap_pm_suspend();
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int omap_pm_begin(suspend_state_t state)
{
	cpu_idle_poll_ctrl(true);
	if (cpu_is_omap34xx())
		omap_prcm_irq_prepare();
	return 0;
}

static void omap_pm_end(void)
{
	cpu_idle_poll_ctrl(false);
}

static void omap_pm_wake(void)
{
	if (cpu_is_omap34xx())
		omap_prcm_irq_complete();
}

static const struct platform_suspend_ops omap_pm_ops = {
	.begin		= omap_pm_begin,
	.end		= omap_pm_end,
	.enter		= omap_pm_enter,
	.wake		= omap_pm_wake,
	.valid		= suspend_valid_only_mem,
};

/**
 * omap_common_suspend_init - Set common suspend routines for OMAP SoCs
 * @pm_suspend: function pointer to SoC specific suspend function
 */
void omap_common_suspend_init(void *pm_suspend)
{
	omap_pm_suspend = pm_suspend;
	suspend_set_ops(&omap_pm_ops);
}
#endif /* CONFIG_SUSPEND */

static void __init omap3_init_voltages(void)
{
	if (!cpu_is_omap34xx())
		return;

	omap2_set_init_voltage("mpu_iva", "dpll1_ck", "mpu");
	omap2_set_init_voltage("core", "l3_ick", "l3_main");
}

static void __init omap4_init_voltages(void)
{
	if (!cpu_is_omap44xx())
		return;

	omap2_set_init_voltage("mpu", "dpll_mpu_ck", "mpu");
	omap2_set_init_voltage("core", "l3_div_ck", "l3_main_1");
	omap2_set_init_voltage("iva", "dpll_iva_m5x2_ck", "iva");
}

static inline void omap_init_cpufreq(void)
{
	struct platform_device_info devinfo = { };

	if (!of_have_populated_dt())
		devinfo.name = "omap-cpufreq";
	else
		devinfo.name = "cpufreq-dt";
	platform_device_register_full(&devinfo);
}

static int __init omap2_common_pm_init(void)
{
	if (!of_have_populated_dt())
		omap2_init_processor_devices();
	omap_pm_if_init();

	return 0;
}
omap_postcore_initcall(omap2_common_pm_init);

int __init omap2_common_pm_late_init(void)
{
	if (of_have_populated_dt()) {
		omap3_twl_init();
		omap4_twl_init();
	}

	/* Init the voltage layer */
	omap_pmic_late_init();
	omap_voltage_late_init();

	/* Initialize the voltages */
	omap3_init_voltages();
	omap4_init_voltages();

	/* Smartreflex device init */
	omap_devinit_smartreflex();

	/* cpufreq dummy device instantiation */
	omap_init_cpufreq();

	return 0;
}
