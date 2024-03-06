// SPDX-License-Identifier: GPL-2.0
/*
 * Clock based PWM controller
 *
 * Copyright (c) 2021 Nikita Travkin <nikita@trvn.ru>
 *
 * This is an "adapter" driver that allows PWM consumers to use
 * system clocks with duty cycle control as PWM outputs.
 *
 * Limitations:
 * - Due to the fact that exact behavior depends on the underlying
 *   clock driver, various limitations are possible.
 * - Underlying clock may not be able to give 0% or 100% duty cycle
 *   (constant off or on), exact behavior will depend on the clock.
 * - When the PWM is disabled, the clock will be disabled as well,
 *   line state will depend on the clock.
 * - The clk API doesn't expose the necessary calls to implement
 *   .get_state().
 */

#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/pwm.h>

struct pwm_clk_chip {
	struct pwm_chip chip;
	struct clk *clk;
	bool clk_enabled;
};

#define to_pwm_clk_chip(_chip) container_of(_chip, struct pwm_clk_chip, chip)

static int pwm_clk_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			 const struct pwm_state *state)
{
	struct pwm_clk_chip *pcchip = to_pwm_clk_chip(chip);
	int ret;
	u32 rate;
	u64 period = state->period;
	u64 duty_cycle = state->duty_cycle;

	if (!state->enabled) {
		if (pwm->state.enabled) {
			clk_disable(pcchip->clk);
			pcchip->clk_enabled = false;
		}
		return 0;
	} else if (!pwm->state.enabled) {
		ret = clk_enable(pcchip->clk);
		if (ret)
			return ret;
		pcchip->clk_enabled = true;
	}

	/*
	 * We have to enable the clk before setting the rate and duty_cycle,
	 * that however results in a window where the clk is on with a
	 * (potentially) different setting. Also setting period and duty_cycle
	 * are two separate calls, so that probably isn't atomic either.
	 */

	rate = DIV64_U64_ROUND_UP(NSEC_PER_SEC, period);
	ret = clk_set_rate(pcchip->clk, rate);
	if (ret)
		return ret;

	if (state->polarity == PWM_POLARITY_INVERSED)
		duty_cycle = period - duty_cycle;

	return clk_set_duty_cycle(pcchip->clk, duty_cycle, period);
}

static const struct pwm_ops pwm_clk_ops = {
	.apply = pwm_clk_apply,
};

static int pwm_clk_probe(struct platform_device *pdev)
{
	struct pwm_clk_chip *pcchip;
	int ret;

	pcchip = devm_kzalloc(&pdev->dev, sizeof(*pcchip), GFP_KERNEL);
	if (!pcchip)
		return -ENOMEM;

	pcchip->clk = devm_clk_get_prepared(&pdev->dev, NULL);
	if (IS_ERR(pcchip->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(pcchip->clk),
				     "Failed to get clock\n");

	pcchip->chip.dev = &pdev->dev;
	pcchip->chip.ops = &pwm_clk_ops;
	pcchip->chip.npwm = 1;

	ret = pwmchip_add(&pcchip->chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Failed to add pwm chip\n");

	platform_set_drvdata(pdev, pcchip);
	return 0;
}

static void pwm_clk_remove(struct platform_device *pdev)
{
	struct pwm_clk_chip *pcchip = platform_get_drvdata(pdev);

	pwmchip_remove(&pcchip->chip);

	if (pcchip->clk_enabled)
		clk_disable(pcchip->clk);
}

static const struct of_device_id pwm_clk_dt_ids[] = {
	{ .compatible = "clk-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_clk_dt_ids);

static struct platform_driver pwm_clk_driver = {
	.driver = {
		.name = "pwm-clk",
		.of_match_table = pwm_clk_dt_ids,
	},
	.probe = pwm_clk_probe,
	.remove_new = pwm_clk_remove,
};
module_platform_driver(pwm_clk_driver);

MODULE_ALIAS("platform:pwm-clk");
MODULE_AUTHOR("Nikita Travkin <nikita@trvn.ru>");
MODULE_DESCRIPTION("Clock based PWM driver");
MODULE_LICENSE("GPL");
