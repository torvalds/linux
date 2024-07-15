// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (c) 2014 Joachim Eastwood <manabian@gmail.com>
 * Copyright (c) 2012 NeilBrown <neilb@suse.de>
 * Heavily based on earlier code which is:
 * Copyright (c) 2010 Grant Erickson <marathon96@gmail.com>
 *
 * Also based on pwm-samsung.c
 *
 * Description:
 *   This file is the core OMAP support for the generic, Linux
 *   PWM driver / controller, using the OMAP's dual-mode timers
 *   with a timer counter that goes up. When it overflows it gets
 *   reloaded with the load value and the pwm output goes up.
 *   When counter matches with match register, the output goes down.
 *   Reference Manual: https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf
 *
 * Limitations:
 * - When PWM is stopped, timer counter gets stopped immediately. This
 *   doesn't allow the current PWM period to complete and stops abruptly.
 * - When PWM is running and changing both duty cycle and period,
 *   we cannot prevent in software that the output might produce
 *   a period with mixed settings. Especially when period/duty_cyle
 *   is updated while the pwm pin is high, current pwm period/duty_cycle
 *   can get updated as below based on the current timer counter:
 *   	- period for current cycle =  current_period + new period
 *   	- duty_cycle for current period = current period + new duty_cycle.
 * - PWM OMAP DM timer cannot change the polarity when pwm is active. When
 *   user requests a change in polarity when in active state:
 *	- PWM is stopped abruptly(without completing the current cycle)
 *	- Polarity is changed
 *	- A fresh cycle is started.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <clocksource/timer-ti-dm.h>
#include <linux/platform_data/dmtimer-omap.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/time.h>

#define DM_TIMER_LOAD_MIN 0xfffffffe
#define DM_TIMER_MAX      0xffffffff

/**
 * struct pwm_omap_dmtimer_chip - Structure representing a pwm chip
 *				  corresponding to omap dmtimer.
 * @dm_timer:		Pointer to omap dm timer.
 * @pdata:		Pointer to omap dm timer ops.
 * @dm_timer_pdev:	Pointer to omap dm timer platform device
 */
struct pwm_omap_dmtimer_chip {
	/* Mutex to protect pwm apply state */
	struct omap_dm_timer *dm_timer;
	const struct omap_dm_timer_ops *pdata;
	struct platform_device *dm_timer_pdev;
};

static inline struct pwm_omap_dmtimer_chip *
to_pwm_omap_dmtimer_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

/**
 * pwm_omap_dmtimer_get_clock_cycles() - Get clock cycles in a time frame
 * @clk_rate:	pwm timer clock rate
 * @ns:		time frame in nano seconds.
 *
 * Return number of clock cycles in a given period(ins ns).
 */
static u32 pwm_omap_dmtimer_get_clock_cycles(unsigned long clk_rate, int ns)
{
	return DIV_ROUND_CLOSEST_ULL((u64)clk_rate * ns, NSEC_PER_SEC);
}

/**
 * pwm_omap_dmtimer_start() - Start the pwm omap dm timer in pwm mode
 * @omap:	Pointer to pwm omap dm timer chip
 */
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

/**
 * pwm_omap_dmtimer_is_enabled() -  Detect if the pwm is enabled.
 * @omap:	Pointer to pwm omap dm timer chip
 *
 * Return true if pwm is enabled else false.
 */
static bool pwm_omap_dmtimer_is_enabled(struct pwm_omap_dmtimer_chip *omap)
{
	u32 status;

	status = omap->pdata->get_pwm_status(omap->dm_timer);

	return !!(status & OMAP_TIMER_CTRL_ST);
}

/**
 * pwm_omap_dmtimer_polarity() -  Detect the polarity of pwm.
 * @omap:	Pointer to pwm omap dm timer chip
 *
 * Return the polarity of pwm.
 */
static int pwm_omap_dmtimer_polarity(struct pwm_omap_dmtimer_chip *omap)
{
	u32 status;

	status = omap->pdata->get_pwm_status(omap->dm_timer);

	return !!(status & OMAP_TIMER_CTRL_SCPWM);
}

/**
 * pwm_omap_dmtimer_config() - Update the configuration of pwm omap dm timer
 * @chip:	Pointer to PWM controller
 * @pwm:	Pointer to PWM channel
 * @duty_ns:	New duty cycle in nano seconds
 * @period_ns:	New period in nano seconds
 *
 * Return 0 if successfully changed the period/duty_cycle else appropriate
 * error.
 */
static int pwm_omap_dmtimer_config(struct pwm_chip *chip,
				   struct pwm_device *pwm,
				   int duty_ns, int period_ns)
{
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);
	u32 period_cycles, duty_cycles;
	u32 load_value, match_value;
	unsigned long clk_rate;
	struct clk *fclk;

	dev_dbg(pwmchip_parent(chip), "requested duty cycle: %d ns, period: %d ns\n",
		duty_ns, period_ns);

	if (duty_ns == pwm_get_duty_cycle(pwm) &&
	    period_ns == pwm_get_period(pwm))
		return 0;

	fclk = omap->pdata->get_fclk(omap->dm_timer);
	if (!fclk) {
		dev_err(pwmchip_parent(chip), "invalid pmtimer fclk\n");
		return -EINVAL;
	}

	clk_rate = clk_get_rate(fclk);
	if (!clk_rate) {
		dev_err(pwmchip_parent(chip), "invalid pmtimer fclk rate\n");
		return -EINVAL;
	}

	dev_dbg(pwmchip_parent(chip), "clk rate: %luHz\n", clk_rate);

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
		dev_info(pwmchip_parent(chip),
			 "period %d ns too short for clock rate %lu Hz\n",
			 period_ns, clk_rate);
		return -EINVAL;
	}

	if (duty_cycles < 1) {
		dev_dbg(pwmchip_parent(chip),
			"duty cycle %d ns is too short for clock rate %lu Hz\n",
			duty_ns, clk_rate);
		dev_dbg(pwmchip_parent(chip), "using minimum of 1 clock cycle\n");
		duty_cycles = 1;
	} else if (duty_cycles >= period_cycles) {
		dev_dbg(pwmchip_parent(chip),
			"duty cycle %d ns is too long for period %d ns at clock rate %lu Hz\n",
			duty_ns, period_ns, clk_rate);
		dev_dbg(pwmchip_parent(chip), "using maximum of 1 clock cycle less than period\n");
		duty_cycles = period_cycles - 1;
	}

	dev_dbg(pwmchip_parent(chip), "effective duty cycle: %lld ns, period: %lld ns\n",
		DIV_ROUND_CLOSEST_ULL((u64)NSEC_PER_SEC * duty_cycles,
				      clk_rate),
		DIV_ROUND_CLOSEST_ULL((u64)NSEC_PER_SEC * period_cycles,
				      clk_rate));

	load_value = (DM_TIMER_MAX - period_cycles) + 1;
	match_value = load_value + duty_cycles - 1;

	omap->pdata->set_load(omap->dm_timer, load_value);
	omap->pdata->set_match(omap->dm_timer, true, match_value);

	dev_dbg(pwmchip_parent(chip), "load value: %#08x (%d), match value: %#08x (%d)\n",
		load_value, load_value,	match_value, match_value);

	return 0;
}

/**
 * pwm_omap_dmtimer_set_polarity() - Changes the polarity of the pwm dm timer.
 * @chip:	Pointer to PWM controller
 * @pwm:	Pointer to PWM channel
 * @polarity:	New pwm polarity to be set
 */
static void pwm_omap_dmtimer_set_polarity(struct pwm_chip *chip,
					  struct pwm_device *pwm,
					  enum pwm_polarity polarity)
{
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);
	bool enabled;

	/* Disable the PWM before changing the polarity. */
	enabled = pwm_omap_dmtimer_is_enabled(omap);
	if (enabled)
		omap->pdata->stop(omap->dm_timer);

	omap->pdata->set_pwm(omap->dm_timer,
			     polarity == PWM_POLARITY_INVERSED,
			     true, OMAP_TIMER_TRIGGER_OVERFLOW_AND_COMPARE,
			     true);

	if (enabled)
		pwm_omap_dmtimer_start(omap);
}

/**
 * pwm_omap_dmtimer_apply() - Changes the state of the pwm omap dm timer.
 * @chip:	Pointer to PWM controller
 * @pwm:	Pointer to PWM channel
 * @state:	New state to apply
 *
 * Return 0 if successfully changed the state else appropriate error.
 */
static int pwm_omap_dmtimer_apply(struct pwm_chip *chip,
				  struct pwm_device *pwm,
				  const struct pwm_state *state)
{
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);
	int ret;

	if (pwm_omap_dmtimer_is_enabled(omap) && !state->enabled) {
		omap->pdata->stop(omap->dm_timer);
		return 0;
	}

	if (pwm_omap_dmtimer_polarity(omap) != state->polarity)
		pwm_omap_dmtimer_set_polarity(chip, pwm, state->polarity);

	ret = pwm_omap_dmtimer_config(chip, pwm, state->duty_cycle,
				      state->period);
	if (ret)
		return ret;

	if (!pwm_omap_dmtimer_is_enabled(omap) && state->enabled) {
		omap->pdata->set_pwm(omap->dm_timer,
				     state->polarity == PWM_POLARITY_INVERSED,
				     true,
				     OMAP_TIMER_TRIGGER_OVERFLOW_AND_COMPARE,
				     true);
		pwm_omap_dmtimer_start(omap);
	}

	return 0;
}

static const struct pwm_ops pwm_omap_dmtimer_ops = {
	.apply = pwm_omap_dmtimer_apply,
};

static int pwm_omap_dmtimer_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dmtimer_platform_data *timer_pdata;
	const struct omap_dm_timer_ops *pdata;
	struct platform_device *timer_pdev;
	struct pwm_chip *chip;
	struct pwm_omap_dmtimer_chip *omap;
	struct omap_dm_timer *dm_timer;
	struct device_node *timer;
	int ret = 0;
	u32 v;

	timer = of_parse_phandle(np, "ti,timers", 0);
	if (!timer)
		return -ENODEV;

	timer_pdev = of_find_device_by_node(timer);
	if (!timer_pdev) {
		dev_err(&pdev->dev, "Unable to find Timer pdev\n");
		ret = -ENODEV;
		goto err_find_timer_pdev;
	}

	timer_pdata = dev_get_platdata(&timer_pdev->dev);
	if (!timer_pdata) {
		dev_dbg(&pdev->dev,
			 "dmtimer pdata structure NULL, deferring probe\n");
		ret = -EPROBE_DEFER;
		goto err_platdata;
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
	    !pdata->get_pwm_status ||
	    !pdata->set_prescaler ||
	    !pdata->write_counter) {
		dev_err(&pdev->dev, "Incomplete dmtimer pdata structure\n");
		ret = -EINVAL;
		goto err_platdata;
	}

	if (!of_get_property(timer, "ti,timer-pwm", NULL)) {
		dev_err(&pdev->dev, "Missing ti,timer-pwm capability\n");
		ret = -ENODEV;
		goto err_timer_property;
	}

	dm_timer = pdata->request_by_node(timer);
	if (!dm_timer) {
		ret = -EPROBE_DEFER;
		goto err_request_timer;
	}

	chip = devm_pwmchip_alloc(&pdev->dev, 1, sizeof(*omap));
	if (IS_ERR(chip)) {
		ret = PTR_ERR(chip);
		goto err_alloc_omap;
	}
	omap = to_pwm_omap_dmtimer_chip(chip);

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

	chip->ops = &pwm_omap_dmtimer_ops;

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register PWM\n");
		goto err_pwmchip_add;
	}

	of_node_put(timer);

	platform_set_drvdata(pdev, chip);

	return 0;

err_pwmchip_add:

	/*
	 * *omap is allocated using devm_kzalloc,
	 * so no free necessary here
	 */
err_alloc_omap:

	pdata->free(dm_timer);
err_request_timer:

err_timer_property:
err_platdata:

	put_device(&timer_pdev->dev);
err_find_timer_pdev:

	of_node_put(timer);

	return ret;
}

static void pwm_omap_dmtimer_remove(struct platform_device *pdev)
{
	struct pwm_chip *chip = platform_get_drvdata(pdev);
	struct pwm_omap_dmtimer_chip *omap = to_pwm_omap_dmtimer_chip(chip);

	pwmchip_remove(chip);

	if (pm_runtime_active(&omap->dm_timer_pdev->dev))
		omap->pdata->stop(omap->dm_timer);

	omap->pdata->free(omap->dm_timer);

	put_device(&omap->dm_timer_pdev->dev);
}

static const struct of_device_id pwm_omap_dmtimer_of_match[] = {
	{.compatible = "ti,omap-dmtimer-pwm"},
	{}
};
MODULE_DEVICE_TABLE(of, pwm_omap_dmtimer_of_match);

static struct platform_driver pwm_omap_dmtimer_driver = {
	.driver = {
		.name = "omap-dmtimer-pwm",
		.of_match_table = pwm_omap_dmtimer_of_match,
	},
	.probe = pwm_omap_dmtimer_probe,
	.remove_new = pwm_omap_dmtimer_remove,
};
module_platform_driver(pwm_omap_dmtimer_driver);

MODULE_AUTHOR("Grant Erickson <marathon96@gmail.com>");
MODULE_AUTHOR("NeilBrown <neilb@suse.de>");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("OMAP PWM Driver using Dual-mode Timers");
