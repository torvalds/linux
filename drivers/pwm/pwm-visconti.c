// SPDX-License-Identifier: GPL-2.0-only
/*
 * Toshiba Visconti pulse-width-modulation controller driver
 *
 * Copyright (c) 2020 - 2021 TOSHIBA CORPORATION
 * Copyright (c) 2020 - 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * Authors: Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>
 *
 * Limitations:
 * - The fixed input clock is running at 1 MHz and is divided by either 1,
 *   2, 4 or 8.
 * - When the settings of the PWM are modified, the new values are shadowed
 *   in hardware until the PIPGM_PCSR register is written and the currently
 *   running period is completed. This way the hardware switches atomically
 *   from the old setting to the new.
 * - Disabling the hardware completes the currently running period and keeps
 *   the output at low level at all times.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define PIPGM_PCSR(ch) (0x400 + 4 * (ch))
#define PIPGM_PDUT(ch) (0x420 + 4 * (ch))
#define PIPGM_PWMC(ch) (0x440 + 4 * (ch))

#define PIPGM_PWMC_PWMACT		BIT(5)
#define PIPGM_PWMC_CLK_MASK		GENMASK(1, 0)
#define PIPGM_PWMC_POLARITY_MASK	GENMASK(5, 5)

struct visconti_pwm_chip {
	void __iomem *base;
};

static inline struct visconti_pwm_chip *visconti_pwm_from_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int visconti_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			      const struct pwm_state *state)
{
	struct visconti_pwm_chip *priv = visconti_pwm_from_chip(chip);
	u32 period, duty_cycle, pwmc0;

	if (!state->enabled) {
		writel(0, priv->base + PIPGM_PCSR(pwm->hwpwm));
		return 0;
	}

	/*
	 * The biggest period the hardware can provide is
	 *	(0xffff << 3) * 1000 ns
	 * This value fits easily in an u32, so simplify the maths by
	 * capping the values to 32 bit integers.
	 */
	if (state->period > (0xffff << 3) * 1000)
		period = (0xffff << 3) * 1000;
	else
		period = state->period;

	if (state->duty_cycle > period)
		duty_cycle = period;
	else
		duty_cycle = state->duty_cycle;

	/*
	 * The input clock runs fixed at 1 MHz, so we have only
	 * microsecond resolution and so can divide by
	 * NSEC_PER_SEC / CLKFREQ = 1000 without losing precision.
	 */
	period /= 1000;
	duty_cycle /= 1000;

	if (!period)
		return -ERANGE;

	/*
	 * PWMC controls a divider that divides the input clk by a power of two
	 * between 1 and 8. As a smaller divider yields higher precision, pick
	 * the smallest possible one. As period is at most 0xffff << 3, pwmc0 is
	 * in the intended range [0..3].
	 */
	pwmc0 = fls(period >> 16);
	if (WARN_ON(pwmc0 > 3))
		return -EINVAL;

	period >>= pwmc0;
	duty_cycle >>= pwmc0;

	if (state->polarity == PWM_POLARITY_INVERSED)
		pwmc0 |= PIPGM_PWMC_PWMACT;
	writel(pwmc0, priv->base + PIPGM_PWMC(pwm->hwpwm));
	writel(duty_cycle, priv->base + PIPGM_PDUT(pwm->hwpwm));
	writel(period, priv->base + PIPGM_PCSR(pwm->hwpwm));

	return 0;
}

static int visconti_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				  struct pwm_state *state)
{
	struct visconti_pwm_chip *priv = visconti_pwm_from_chip(chip);
	u32 period, duty, pwmc0, pwmc0_clk;

	period = readl(priv->base + PIPGM_PCSR(pwm->hwpwm));
	duty = readl(priv->base + PIPGM_PDUT(pwm->hwpwm));
	pwmc0 = readl(priv->base + PIPGM_PWMC(pwm->hwpwm));
	pwmc0_clk = pwmc0 & PIPGM_PWMC_CLK_MASK;

	state->period = (period << pwmc0_clk) * NSEC_PER_USEC;
	state->duty_cycle = (duty << pwmc0_clk) * NSEC_PER_USEC;
	if (pwmc0 & PIPGM_PWMC_POLARITY_MASK)
		state->polarity = PWM_POLARITY_INVERSED;
	else
		state->polarity = PWM_POLARITY_NORMAL;

	state->enabled = true;

	return 0;
}

static const struct pwm_ops visconti_pwm_ops = {
	.apply = visconti_pwm_apply,
	.get_state = visconti_pwm_get_state,
};

static int visconti_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip;
	struct visconti_pwm_chip *priv;
	int ret;

	chip = devm_pwmchip_alloc(dev, 4, sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = visconti_pwm_from_chip(chip);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	chip->ops = &visconti_pwm_ops;

	ret = devm_pwmchip_add(&pdev->dev, chip);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "Cannot register visconti PWM\n");

	return 0;
}

static const struct of_device_id visconti_pwm_of_match[] = {
	{ .compatible = "toshiba,visconti-pwm", },
	{ }
};
MODULE_DEVICE_TABLE(of, visconti_pwm_of_match);

static struct platform_driver visconti_pwm_driver = {
	.driver = {
		.name = "pwm-visconti",
		.of_match_table = visconti_pwm_of_match,
	},
	.probe = visconti_pwm_probe,
};
module_platform_driver(visconti_pwm_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Nobuhiro Iwamatsu <nobuhiro1.iwamatsu@toshiba.co.jp>");
MODULE_ALIAS("platform:pwm-visconti");
