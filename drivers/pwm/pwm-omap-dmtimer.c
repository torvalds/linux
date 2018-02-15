/*
 * Copyright (c) 2015 Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (c) 2014 Joachim Eastwood <manabian@gmail.com>
 * Copyright (c) 2012 NeilBrown <neilb@suse.de>
 * Heavily based on earlier code which is:
 * Copyright (c) 2010 Grant Erickson <marathon96@gmail.com>
 *
 * Also based on pwm-samsung.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * Description:
 *   This file is the core OMAP support for the generic, Linux
 *   PWM driver / controller, using the OMAP's dual-mode timers.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_data/dmtimer-omap.h>
#include <linux/platform_data/pwm_omap_dmtimer.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/time.h>

#define DM_TIMER_LOAD_MIN 0xfffffffe
#define DM_TIMER_MAX      0xffffffff

struct pwm_omap_dmtimer_chip {
	struct pwm_chip chip;
	struct mutex mutex;
	pwm_omap_dmtimer *dm_timer;
	const struct omap_dm_timer_ops *pdata;
	struct platform_device *dm_timer_pdev;
};

static inline struct pwm_omap_dmtimer_chip *
to_pwm_omap_dmtimer_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct pwm_omap_dmtimer_chip, chip);
}

static u32 pwm_omap_dmtimer_get_clock_cycles(unsigned long clk_rate, int ns)
{
	return DIV_ROUND_CLOSEST_ULL((u64)clk_rate * ns, NSEC_PER_SEC);
}

static void pwm_omap_dmtimer_start(struct pwm_omap_dmtimer_chip *omap)
{
	/*
	 * According to OMAP 4 TRM section 22.2.4.10 the counter should be
	 * started at 0xFFFFFFFE when overflow and match is used to ensure
	 * that the PWM line is toggled on the first event.
	 *
	 * Note that omap_dm_timer_enable/disable is for register access and
	 * not the timer counter itself.
	 */
	omap->pdata->enable(omap->dm_timer);
	omap->pdata->write_counter(omap->dm_timer, DM_TIMER_LOAD_MIN);
	omap->pdata->disable(omap->dm_timer);

	omap->pdata->start(omap->dm_timer);
}

static int pwm_omap_dmtimer_enable(struct pwm_chip *chip,
				   struct pwm_device *pwm)
{
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);

	mutex_lock(&omap->mutex);
	pwm_omap_dmtimer_start(omap);
	mutex_unlock(&omap->mutex);

	return 0;
}

static void pwm_omap_dmtimer_disable(struct pwm_chip *chip,
				     struct pwm_device *pwm)
{
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);

	mutex_lock(&omap->mutex);
	omap->pdata->stop(omap->dm_timer);
	mutex_unlock(&omap->mutex);
}

static int pwm_omap_dmtimer_config(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   int duty_ns, int period_ns)
{
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);
	u32 period_cycles, duty_cycles;
	u32 load_value, match_value;
	struct clk *fclk;
	unsigned long clk_rate;
	bool timer_active;

	dev_dbg(chip->dev, "requested duty cycle: %d ns, period: %d ns\n",
		duty_ns, period_ns);

	mutex_lock(&omap->mutex);
	if (duty_ns == pwm_get_duty_cycle(pwm) &&
	    period_ns == pwm_get_period(pwm)) {
		/* No change - don't cause any transients. */
		mutex_unlock(&omap->mutex);
		return 0;
	}

	fclk = omap->pdata->get_fclk(omap->dm_timer);
	if (!fclk) {
		dev_err(chip->dev, "invalid pmtimer fclk\n");
		goto err_einval;
	}

	clk_rate = clk_get_rate(fclk);
	if (!clk_rate) {
		dev_err(chip->dev, "invalid pmtimer fclk rate\n");
		goto err_einval;
	}

	dev_dbg(chip->dev, "clk rate: %luHz\n", clk_rate);

	/*
	 * Calculate the appropriate load and match values based on the
	 * specified period and duty cycle. The load value determines the
	 * period time and the match value determines the duty time.
	 *
	 * The period lasts for (DM_TIMER_MAX-load_value+1) clock cycles.
	 * Similarly, the active time lasts (match_value-load_value+1) cycles.
	 * The non-active time is the remainder: (DM_TIMER_MAX-match_value)
	 * clock cycles.
	 *
	 * NOTE: It is required that: load_value <= match_value < DM_TIMER_MAX
	 *
	 * References:
	 *   OMAP4430/60/70 TRM sections 22.2.4.10 and 22.2.4.11
	 *   AM335x Sitara TRM sections 20.1.3.5 and 20.1.3.6
	 */
	period_cycles = pwm_omap_dmtimer_get_clock_cycles(clk_rate, period_ns);
	duty_cycles = pwm_omap_dmtimer_get_clock_cycles(clk_rate, duty_ns);

	if (period_cycles < 2) {
		dev_info(chip->dev,
			 "period %d ns too short for clock rate %lu Hz\n",
			 period_ns, clk_rate);
		goto err_einval;
	}

	if (duty_cycles < 1) {
		dev_dbg(chip->dev,
			"duty cycle %d ns is too short for clock rate %lu Hz\n",
			duty_ns, clk_rate);
		dev_dbg(chip->dev, "using minimum of 1 clock cycle\n");
		duty_cycles = 1;
	} else if (duty_cycles >= period_cycles) {
		dev_dbg(chip->dev,
			"duty cycle %d ns is too long for period %d ns at clock rate %lu Hz\n",
			duty_ns, period_ns, clk_rate);
		dev_dbg(chip->dev, "using maximum of 1 clock cycle less than period\n");
		duty_cycles = period_cycles - 1;
	}

	dev_dbg(chip->dev, "effective duty cycle: %lld ns, period: %lld ns\n",
		DIV_ROUND_CLOSEST_ULL((u64)NSEC_PER_SEC * duty_cycles,
				      clk_rate),
		DIV_ROUND_CLOSEST_ULL((u64)NSEC_PER_SEC * period_cycles,
				      clk_rate));

	load_value = (DM_TIMER_MAX - period_cycles) + 1;
	match_value = load_value + duty_cycles - 1;

	/*
	 * We MUST stop the associated dual-mode timer before attempting to
	 * write its registers, but calls to omap_dm_timer_start/stop must
	 * be balanced so check if timer is active before calling timer_stop.
	 */
	timer_active = pm_runtime_active(&omap->dm_timer_pdev->dev);
	if (timer_active)
		omap->pdata->stop(omap->dm_timer);

	omap->pdata->set_load(omap->dm_timer, true, load_value);
	omap->pdata->set_match(omap->dm_timer, true, match_value);

	dev_dbg(chip->dev, "load value: %#08x (%d), match value: %#08x (%d)\n",
		load_value, load_value,	match_value, match_value);

	omap->pdata->set_pwm(omap->dm_timer,
			      pwm_get_polarity(pwm) == PWM_POLARITY_INVERSED,
			      true,
			      PWM_OMAP_DMTIMER_TRIGGER_OVERFLOW_AND_COMPARE);

	/* If config was called while timer was running it must be reenabled. */
	if (timer_active)
		pwm_omap_dmtimer_start(omap);

	mutex_unlock(&omap->mutex);

	return 0;

err_einval:
	mutex_unlock(&omap->mutex);

	return -EINVAL;
}

static int pwm_omap_dmtimer_set_polarity(struct pwm_chip *chip,
					 struct pwm_device *pwm,
					 enum pwm_polarity polarity)
{
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);

	/*
	 * PWM core will not call set_polarity while PWM is enabled so it's
	 * safe to reconfigure the timer here without stopping it first.
	 */
	mutex_lock(&omap->mutex);
	omap->pdata->set_pwm(omap->dm_timer,
			      polarity == PWM_POLARITY_INVERSED,
			      true,
			      PWM_OMAP_DMTIMER_TRIGGER_OVERFLOW_AND_COMPARE);
	mutex_unlock(&omap->mutex);

	return 0;
}

static const struct pwm_ops pwm_omap_dmtimer_ops = {
	.enable	= pwm_omap_dmtimer_enable,
	.disable = pwm_omap_dmtimer_disable,
	.config	= pwm_omap_dmtimer_config,
	.set_polarity = pwm_omap_dmtimer_set_polarity,
	.owner = THIS_MODULE,
};

static int pwm_omap_dmtimer_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *timer;
	struct platform_device *timer_pdev;
	struct pwm_omap_dmtimer_chip *omap;
	struct dmtimer_platform_data *timer_pdata;
	const struct omap_dm_timer_ops *pdata;
	pwm_omap_dmtimer *dm_timer;
	u32 v;
	int ret = 0;

	timer = of_parse_phandle(np, "ti,timers", 0);
	if (!timer)
		return -ENODEV;

	timer_pdev = of_find_device_by_node(timer);
	if (!timer_pdev) {
		dev_err(&pdev->dev, "Unable to find Timer pdev\n");
		ret = -ENODEV;
		goto put;
	}

	timer_pdata = dev_get_platdata(&timer_pdev->dev);
	if (!timer_pdata) {
		dev_err(&pdev->dev, "dmtimer pdata structure NULL\n");
		ret = -EINVAL;
		goto put;
	}

	pdata = timer_pdata->timer_ops;

	if (!pdata || !pdata->request_by_node ||
	    !pdata->free ||
	    !pdata->enable ||
	    !pdata->disable ||
	    !pdata->get_fclk ||
	    !pdata->start ||
	    !pdata->stop ||
	    !pdata->set_load ||
	    !pdata->set_match ||
	    !pdata->set_pwm ||
	    !pdata->set_prescaler ||
	    !pdata->write_counter) {
		dev_err(&pdev->dev, "Incomplete dmtimer pdata structure\n");
		ret = -EINVAL;
		goto put;
	}

	if (!of_get_property(timer, "ti,timer-pwm", NULL)) {
		dev_err(&pdev->dev, "Missing ti,timer-pwm capability\n");
		ret = -ENODEV;
		goto put;
	}

	dm_timer = pdata->request_by_node(timer);
	if (!dm_timer) {
		ret = -EPROBE_DEFER;
		goto put;
	}

put:
	of_node_put(timer);
	if (ret < 0)
		return ret;

	omap = devm_kzalloc(&pdev->dev, sizeof(*omap), GFP_KERNEL);
	if (!omap) {
		pdata->free(dm_timer);
		return -ENOMEM;
	}

	omap->pdata = pdata;
	omap->dm_timer = dm_timer;
	omap->dm_timer_pdev = timer_pdev;

	/*
	 * Ensure that the timer is stopped before we allow PWM core to call
	 * pwm_enable.
	 */
	if (pm_runtime_active(&omap->dm_timer_pdev->dev))
		omap->pdata->stop(omap->dm_timer);

	if (!of_property_read_u32(pdev->dev.of_node, "ti,prescaler", &v))
		omap->pdata->set_prescaler(omap->dm_timer, v);

	/* setup dmtimer clock source */
	if (!of_property_read_u32(pdev->dev.of_node, "ti,clock-source", &v))
		omap->pdata->set_source(omap->dm_timer, v);

	omap->chip.dev = &pdev->dev;
	omap->chip.ops = &pwm_omap_dmtimer_ops;
	omap->chip.base = -1;
	omap->chip.npwm = 1;
	omap->chip.of_xlate = of_pwm_xlate_with_flags;
	omap->chip.of_pwm_n_cells = 3;

	mutex_init(&omap->mutex);

	ret = pwmchip_add(&omap->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register PWM\n");
		omap->pdata->free(omap->dm_timer);
		return ret;
	}

	platform_set_drvdata(pdev, omap);

	return 0;
}

static int pwm_omap_dmtimer_remove(struct platform_device *pdev)
{
	struct pwm_omap_dmtimer_chip *omap = platform_get_drvdata(pdev);

	if (pm_runtime_active(&omap->dm_timer_pdev->dev))
		omap->pdata->stop(omap->dm_timer);

	omap->pdata->free(omap->dm_timer);

	mutex_destroy(&omap->mutex);

	return pwmchip_remove(&omap->chip);
}

static const struct of_device_id pwm_omap_dmtimer_of_match[] = {
	{.compatible = "ti,omap-dmtimer-pwm"},
	{}
};
MODULE_DEVICE_TABLE(of, pwm_omap_dmtimer_of_match);

static struct platform_driver pwm_omap_dmtimer_driver = {
	.driver = {
		.name = "omap-dmtimer-pwm",
		.of_match_table = of_match_ptr(pwm_omap_dmtimer_of_match),
	},
	.probe = pwm_omap_dmtimer_probe,
	.remove	= pwm_omap_dmtimer_remove,
};
module_platform_driver(pwm_omap_dmtimer_driver);

MODULE_AUTHOR("Grant Erickson <marathon96@gmail.com>");
MODULE_AUTHOR("NeilBrown <neilb@suse.de>");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OMAP PWM Driver using Dual-mode Timers");
