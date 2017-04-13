/*
 * drivers/pwm/pwm-tegra.c
 *
 * Tegra pulse-width-modulation controller driver
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 * Based on arch/arm/plat-mxc/pwm.c by Sascha Hauer <s.hauer@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pwm.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/reset.h>

#define PWM_ENABLE	(1 << 31)
#define PWM_DUTY_WIDTH	8
#define PWM_DUTY_SHIFT	16
#define PWM_SCALE_WIDTH	13
#define PWM_SCALE_SHIFT	0

struct tegra_pwm_soc {
	unsigned int num_channels;
};

struct tegra_pwm_chip {
	struct pwm_chip chip;
	struct device *dev;

	struct clk *clk;
	struct reset_control*rst;

	unsigned long clk_rate;

	void __iomem *regs;

	const struct tegra_pwm_soc *soc;
};

static inline struct tegra_pwm_chip *to_tegra_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct tegra_pwm_chip, chip);
}

static inline u32 pwm_readl(struct tegra_pwm_chip *chip, unsigned int num)
{
	return readl(chip->regs + (num << 4));
}

static inline void pwm_writel(struct tegra_pwm_chip *chip, unsigned int num,
			     unsigned long val)
{
	writel(val, chip->regs + (num << 4));
}

static int tegra_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	unsigned long long c = duty_ns, hz;
	unsigned long rate;
	u32 val = 0;
	int err;

	/*
	 * Convert from duty_ns / period_ns to a fixed number of duty ticks
	 * per (1 << PWM_DUTY_WIDTH) cycles and make sure to round to the
	 * nearest integer during division.
	 */
	c *= (1 << PWM_DUTY_WIDTH);
	c = DIV_ROUND_CLOSEST_ULL(c, period_ns);

	val = (u32)c << PWM_DUTY_SHIFT;

	/*
	 * Compute the prescaler value for which (1 << PWM_DUTY_WIDTH)
	 * cycles at the PWM clock rate will take period_ns nanoseconds.
	 */
	rate = pc->clk_rate >> PWM_DUTY_WIDTH;

	/* Consider precision in PWM_SCALE_WIDTH rate calculation */
	hz = DIV_ROUND_CLOSEST_ULL(100ULL * NSEC_PER_SEC, period_ns);
	rate = DIV_ROUND_CLOSEST_ULL(100ULL * rate, hz);

	/*
	 * Since the actual PWM divider is the register's frequency divider
	 * field minus 1, we need to decrement to get the correct value to
	 * write to the register.
	 */
	if (rate > 0)
		rate--;

	/*
	 * Make sure that the rate will fit in the register's frequency
	 * divider field.
	 */
	if (rate >> PWM_SCALE_WIDTH)
		return -EINVAL;

	val |= rate << PWM_SCALE_SHIFT;

	/*
	 * If the PWM channel is disabled, make sure to turn on the clock
	 * before writing the register. Otherwise, keep it enabled.
	 */
	if (!pwm_is_enabled(pwm)) {
		err = clk_prepare_enable(pc->clk);
		if (err < 0)
			return err;
	} else
		val |= PWM_ENABLE;

	pwm_writel(pc, pwm->hwpwm, val);

	/*
	 * If the PWM is not enabled, turn the clock off again to save power.
	 */
	if (!pwm_is_enabled(pwm))
		clk_disable_unprepare(pc->clk);

	return 0;
}

static int tegra_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	int rc = 0;
	u32 val;

	rc = clk_prepare_enable(pc->clk);
	if (rc < 0)
		return rc;

	val = pwm_readl(pc, pwm->hwpwm);
	val |= PWM_ENABLE;
	pwm_writel(pc, pwm->hwpwm, val);

	return 0;
}

static void tegra_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	u32 val;

	val = pwm_readl(pc, pwm->hwpwm);
	val &= ~PWM_ENABLE;
	pwm_writel(pc, pwm->hwpwm, val);

	clk_disable_unprepare(pc->clk);
}

static const struct pwm_ops tegra_pwm_ops = {
	.config = tegra_pwm_config,
	.enable = tegra_pwm_enable,
	.disable = tegra_pwm_disable,
	.owner = THIS_MODULE,
};

static int tegra_pwm_probe(struct platform_device *pdev)
{
	struct tegra_pwm_chip *pwm;
	struct resource *r;
	int ret;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (!pwm)
		return -ENOMEM;

	pwm->soc = of_device_get_match_data(&pdev->dev);
	pwm->dev = &pdev->dev;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->regs = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pwm->regs))
		return PTR_ERR(pwm->regs);

	platform_set_drvdata(pdev, pwm);

	pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pwm->clk))
		return PTR_ERR(pwm->clk);

	/* Read PWM clock rate from source */
	pwm->clk_rate = clk_get_rate(pwm->clk);

	pwm->rst = devm_reset_control_get(&pdev->dev, "pwm");
	if (IS_ERR(pwm->rst)) {
		ret = PTR_ERR(pwm->rst);
		dev_err(&pdev->dev, "Reset control is not found: %d\n", ret);
		return ret;
	}

	reset_control_deassert(pwm->rst);

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &tegra_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = pwm->soc->num_channels;

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		reset_control_assert(pwm->rst);
		return ret;
	}

	return 0;
}

static int tegra_pwm_remove(struct platform_device *pdev)
{
	struct tegra_pwm_chip *pc = platform_get_drvdata(pdev);
	unsigned int i;
	int err;

	if (WARN_ON(!pc))
		return -ENODEV;

	err = clk_prepare_enable(pc->clk);
	if (err < 0)
		return err;

	for (i = 0; i < pc->chip.npwm; i++) {
		struct pwm_device *pwm = &pc->chip.pwms[i];

		if (!pwm_is_enabled(pwm))
			if (clk_prepare_enable(pc->clk) < 0)
				continue;

		pwm_writel(pc, i, 0);

		clk_disable_unprepare(pc->clk);
	}

	reset_control_assert(pc->rst);
	clk_disable_unprepare(pc->clk);

	return pwmchip_remove(&pc->chip);
}

#ifdef CONFIG_PM_SLEEP
static int tegra_pwm_suspend(struct device *dev)
{
	return pinctrl_pm_select_sleep_state(dev);
}

static int tegra_pwm_resume(struct device *dev)
{
	return pinctrl_pm_select_default_state(dev);
}
#endif

static const struct tegra_pwm_soc tegra20_pwm_soc = {
	.num_channels = 4,
};

static const struct tegra_pwm_soc tegra186_pwm_soc = {
	.num_channels = 1,
};

static const struct of_device_id tegra_pwm_of_match[] = {
	{ .compatible = "nvidia,tegra20-pwm", .data = &tegra20_pwm_soc },
	{ .compatible = "nvidia,tegra186-pwm", .data = &tegra186_pwm_soc },
	{ }
};

MODULE_DEVICE_TABLE(of, tegra_pwm_of_match);

static const struct dev_pm_ops tegra_pwm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(tegra_pwm_suspend, tegra_pwm_resume)
};

static struct platform_driver tegra_pwm_driver = {
	.driver = {
		.name = "tegra-pwm",
		.of_match_table = tegra_pwm_of_match,
		.pm = &tegra_pwm_pm_ops,
	},
	.probe = tegra_pwm_probe,
	.remove = tegra_pwm_remove,
};

module_platform_driver(tegra_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("NVIDIA Corporation");
MODULE_ALIAS("platform:tegra-pwm");
