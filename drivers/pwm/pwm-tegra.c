// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/pwm/pwm-tegra.c
 *
 * Tegra pulse-width-modulation controller driver
 *
 * Copyright (c) 2010-2020, NVIDIA Corporation.
 * Based on arch/arm/plat-mxc/pwm.c by Sascha Hauer <s.hauer@pengutronix.de>
 *
 * Overview of Tegra Pulse Width Modulator Register
 * CSR_0 of Tegra20, Tegra186, and Tegra194:
 * +-------+-------+-----------------------------------------------------------+
 * | Bit   | Field | Description                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 31    | ENB   | Enable Pulse width modulator.                             |
 * |       |       | 0 = DISABLE, 1 = ENABLE.                                  |
 * +-------+-------+-----------------------------------------------------------+
 * | 30:16 | PWM_0 | Pulse width that needs to be programmed.                  |
 * |       |       | 0 = Always low.                                           |
 * |       |       | 1 = 1 / 256 pulse high.                                   |
 * |       |       | 2 = 2 / 256 pulse high.                                   |
 * |       |       | N = N / 256 pulse high.                                   |
 * |       |       | Only 8 bits are usable [23:16].                           |
 * |       |       | Bit[24] can be programmed to 1 to achieve 100% duty       |
 * |       |       | cycle. In this case the other bits [23:16] are set to     |
 * |       |       | don’t care.                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 12:0  | PFM_0 | Frequency divider that needs to be programmed, also known |
 * |       |       | as SCALE. Division by (1 + PFM_0).                        |
 * +-------+-------+-----------------------------------------------------------+
 *
 * CSR_0 of Tegra264:
 * +-------+-------+-----------------------------------------------------------+
 * | Bit   | Field | Description                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 31:16 | PWM_0 | Pulse width that needs to be programmed.                  |
 * |       |       | 0 = Always low.                                           |
 * |       |       | 1 = 1 / (1 + CSR_1.DEPTH) pulse high.                     |
 * |       |       | 2 = 2 / (1 + CSR_1.DEPTH) pulse high.                     |
 * |       |       | N = N / (1 + CSR_1.DEPTH) pulse high.                     |
 * +-------+-------+-----------------------------------------------------------+
 * | 15:0  | PFM_0 | Frequency divider that needs to be programmed, also known |
 * |       |       | as SCALE. Division by (1 + PFM_0).                        |
 * +-------+-------+-----------------------------------------------------------+
 *
 * CSR_1 of Tegra264:
 * +-------+-------+-----------------------------------------------------------+
 * | Bit   | Field | Description                                               |
 * +-------+-------+-----------------------------------------------------------+
 * | 31    | ENB   | Enable Pulse width modulator.                             |
 * |       |       | 0 = DISABLE, 1 = ENABLE.                                  |
 * +-------+-------+-----------------------------------------------------------+
 * | 30:15 | DEPTH | Depth for pulse width modulator. This controls the pulse  |
 * |       |       | time generated. Division by (1 + CSR_1.DEPTH).            |
 * +-------+-------+-----------------------------------------------------------+
 *
 * The PWM clock frequency is divided by DEPTH = (1 + CSR_1.DEPTH) before
 * subdividing it based on the programmable frequency division value to
 * generate the required frequency for PWM output. DEPTH is fixed to 256
 * before Tegra264. The maximum output frequency that can be achieved is
 * (max rate of source clock) / DEPTH.
 * e.g. if source clock rate is 408 MHz, and DEPTH = 256, maximum output
 * frequency can be: 408 MHz / 256 ~= 1.6 MHz.
 * This 1.6 MHz frequency can further be divided using SCALE value in PWM.
 *
 * Limitations:
 * -	When PWM is disabled, the output is driven to inactive.
 * -	It does not allow the current PWM period to complete and
 *	stops abruptly.
 *
 * -	If the register is reconfigured while PWM is running,
 *	it does not complete the currently running period.
 *
 * -	If the user input duty is beyond acceptible limits,
 *	-EINVAL is returned.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm_opp.h>
#include <linux/pwm.h>
#include <linux/platform_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/reset.h>

#include <soc/tegra/common.h>

#define TEGRA_PWM_ENABLE	(1 << 31)
#define TEGRA_PWM_DUTY_SHIFT	16
#define TEGRA_PWM_SCALE_SHIFT	0

#define TEGRA_PWM_CSR_0	0
#define TEGRA_PWM_CSR_1	4

#define TEGRA_PWM_DEPTH	256

struct tegra_pwm_soc {
	unsigned int num_channels;
	unsigned int enable_reg;

	unsigned int scale_width;
};

struct tegra_pwm_chip {
	struct clk *clk;
	struct reset_control*rst;

	unsigned long clk_rate;
	unsigned long min_period_ns;

	void __iomem *regs;

	const struct tegra_pwm_soc *soc;
};

static inline struct tegra_pwm_chip *to_tegra_pwm_chip(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static inline u32 tegra_pwm_readl(struct pwm_device *pwm, unsigned int offset)
{
	struct tegra_pwm_chip *chip = to_tegra_pwm_chip(pwm->chip);

	return readl(chip->regs + (pwm->hwpwm * 16) + offset);
}

static inline void tegra_pwm_writel(struct pwm_device *pwm, unsigned int offset, u32 value)
{
	struct tegra_pwm_chip *chip = to_tegra_pwm_chip(pwm->chip);

	writel(value, chip->regs + (pwm->hwpwm * 16) + offset);
}

static int tegra_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			    int duty_ns, int period_ns)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	unsigned long long c = duty_ns;
	unsigned long rate, required_clk_rate;
	u32 val = 0;
	int err;

	/*
	 * Convert from duty_ns / period_ns to a fixed number of duty ticks
	 * per TEGRA_PWM_DEPTH cycles and make sure to round to the
	 * nearest integer during division.
	 */
	c *= TEGRA_PWM_DEPTH;
	c = DIV_ROUND_CLOSEST_ULL(c, period_ns);

	val = (u32)c << TEGRA_PWM_DUTY_SHIFT;

	/*
	 *  min period = max clock limit / TEGRA_PWM_DEPTH
	 */
	if (period_ns < pc->min_period_ns)
		return -EINVAL;

	/*
	 * Compute the prescaler value for which TEGRA_PWM_DEPTH
	 * cycles at the PWM clock rate will take period_ns nanoseconds.
	 *
	 * num_channels: If single instance of PWM controller has multiple
	 * channels (e.g. Tegra210 or older) then it is not possible to
	 * configure separate clock rates to each of the channels, in such
	 * case the value stored during probe will be referred.
	 *
	 * If every PWM controller instance has one channel respectively, i.e.
	 * nums_channels == 1 then only the clock rate can be modified
	 * dynamically (e.g. Tegra186 or Tegra194).
	 */
	if (pc->soc->num_channels == 1) {
		/*
		 * Rate is multiplied with TEGRA_PWM_DEPTH so that it matches
		 * with the maximum possible rate that the controller can
		 * provide. Any further lower value can be derived by setting
		 * PFM bits[0:12].
		 *
		 * required_clk_rate is a reference rate for source clock and
		 * it is derived based on user requested period. By setting the
		 * source clock rate as required_clk_rate, PWM controller will
		 * be able to configure the requested period.
		 */
		required_clk_rate = DIV_ROUND_UP_ULL((u64)NSEC_PER_SEC * TEGRA_PWM_DEPTH,
						     period_ns);

		if (required_clk_rate > clk_round_rate(pc->clk, required_clk_rate))
			/*
			 * required_clk_rate is a lower bound for the input
			 * rate; for lower rates there is no value for PWM_SCALE
			 * that yields a period less than or equal to the
			 * requested period. Hence, for lower rates, double the
			 * required_clk_rate to get a clock rate that can meet
			 * the requested period.
			 */
			required_clk_rate *= 2;

		err = dev_pm_opp_set_rate(pwmchip_parent(chip), required_clk_rate);
		if (err < 0)
			return -EINVAL;

		/* Store the new rate for further references */
		pc->clk_rate = clk_get_rate(pc->clk);
	}

	/* Consider precision in scale_width rate calculation */
	rate = mul_u64_u64_div_u64(pc->clk_rate, period_ns,
				   (u64)NSEC_PER_SEC * TEGRA_PWM_DEPTH);

	/*
	 * Since the actual PWM divider is the register's frequency divider
	 * field plus 1, we need to decrement to get the correct value to
	 * write to the register.
	 */
	if (rate > 0)
		rate--;
	else
		return -EINVAL;

	/*
	 * Make sure that the rate will fit in the register's frequency
	 * divider field.
	 */
	if (rate >> pc->soc->scale_width)
		return -EINVAL;

	val |= rate << TEGRA_PWM_SCALE_SHIFT;

	/*
	 * If the PWM channel is disabled, make sure to turn on the clock
	 * before writing the register. Otherwise, keep it enabled.
	 */
	if (!pwm_is_enabled(pwm)) {
		err = pm_runtime_resume_and_get(pwmchip_parent(chip));
		if (err)
			return err;
	} else if (pc->soc->enable_reg == TEGRA_PWM_CSR_0) {
		val |= TEGRA_PWM_ENABLE;
	}

	tegra_pwm_writel(pwm, TEGRA_PWM_CSR_0, val);

	/*
	 * If the PWM is not enabled, turn the clock off again to save power.
	 */
	if (!pwm_is_enabled(pwm))
		pm_runtime_put(pwmchip_parent(chip));

	return 0;
}

static int tegra_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	int rc = 0;
	u32 val;

	rc = pm_runtime_resume_and_get(pwmchip_parent(chip));
	if (rc)
		return rc;

	val = tegra_pwm_readl(pwm, pc->soc->enable_reg);
	val |= TEGRA_PWM_ENABLE;
	tegra_pwm_writel(pwm, pc->soc->enable_reg, val);

	return 0;
}

static void tegra_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	u32 val;

	val = tegra_pwm_readl(pwm, pc->soc->enable_reg);
	val &= ~TEGRA_PWM_ENABLE;
	tegra_pwm_writel(pwm, pc->soc->enable_reg, val);

	pm_runtime_put_sync(pwmchip_parent(chip));
}

static int tegra_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	int err;
	bool enabled = pwm->state.enabled;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (enabled)
			tegra_pwm_disable(chip, pwm);

		return 0;
	}

	err = tegra_pwm_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!enabled)
		err = tegra_pwm_enable(chip, pwm);

	return err;
}

static const struct pwm_ops tegra_pwm_ops = {
	.apply = tegra_pwm_apply,
};

static int tegra_pwm_probe(struct platform_device *pdev)
{
	struct pwm_chip *chip;
	struct tegra_pwm_chip *pc;
	const struct tegra_pwm_soc *soc;
	int ret;

	soc = of_device_get_match_data(&pdev->dev);

	chip = devm_pwmchip_alloc(&pdev->dev, soc->num_channels, sizeof(*pc));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	pc = to_tegra_pwm_chip(chip);

	pc->soc = soc;

	pc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pc->regs))
		return PTR_ERR(pc->regs);

	platform_set_drvdata(pdev, chip);

	pc->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pc->clk))
		return PTR_ERR(pc->clk);

	ret = devm_tegra_core_dev_init_opp_table_common(&pdev->dev);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret)
		return ret;

	/* Set maximum frequency of the IP */
	ret = dev_pm_opp_set_rate(&pdev->dev, ULONG_MAX);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to set max frequency: %d\n", ret);
		goto put_pm;
	}

	/*
	 * The requested and configured frequency may differ due to
	 * clock register resolutions. Get the configured frequency
	 * so that PWM period can be calculated more accurately.
	 */
	pc->clk_rate = clk_get_rate(pc->clk);
	if (pc->clk_rate < TEGRA_PWM_DEPTH) {
		dev_err(&pdev->dev, "clock maximum frequency out of range\n");
		ret = -ERANGE;
		goto put_pm;
	}

	/* Set minimum limit of PWM period for the IP */
	pc->min_period_ns =
	    (NSEC_PER_SEC / (pc->clk_rate / TEGRA_PWM_DEPTH)) + 1;

	pc->rst = devm_reset_control_get_exclusive(&pdev->dev, "pwm");
	if (IS_ERR(pc->rst)) {
		ret = PTR_ERR(pc->rst);
		dev_err(&pdev->dev, "Reset control is not found: %d\n", ret);
		goto put_pm;
	}

	reset_control_deassert(pc->rst);

	chip->ops = &tegra_pwm_ops;

	ret = pwmchip_add(chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		reset_control_assert(pc->rst);
		goto put_pm;
	}

	pm_runtime_put(&pdev->dev);

	return 0;
put_pm:
	pm_runtime_put_sync_suspend(&pdev->dev);
	pm_runtime_force_suspend(&pdev->dev);
	return ret;
}

static void tegra_pwm_remove(struct platform_device *pdev)
{
	struct pwm_chip *chip = platform_get_drvdata(pdev);
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);

	pwmchip_remove(chip);

	reset_control_assert(pc->rst);

	pm_runtime_force_suspend(&pdev->dev);
}

static int __maybe_unused tegra_pwm_runtime_suspend(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	int err;

	clk_disable_unprepare(pc->clk);

	err = pinctrl_pm_select_sleep_state(dev);
	if (err) {
		clk_prepare_enable(pc->clk);
		return err;
	}

	return 0;
}

static int __maybe_unused tegra_pwm_runtime_resume(struct device *dev)
{
	struct pwm_chip *chip = dev_get_drvdata(dev);
	struct tegra_pwm_chip *pc = to_tegra_pwm_chip(chip);
	int err;

	err = pinctrl_pm_select_default_state(dev);
	if (err)
		return err;

	err = clk_prepare_enable(pc->clk);
	if (err) {
		pinctrl_pm_select_sleep_state(dev);
		return err;
	}

	return 0;
}

static const struct tegra_pwm_soc tegra20_pwm_soc = {
	.num_channels = 4,
	.enable_reg = TEGRA_PWM_CSR_0,
	.scale_width = 13,
};

static const struct tegra_pwm_soc tegra186_pwm_soc = {
	.num_channels = 1,
	.enable_reg = TEGRA_PWM_CSR_0,
	.scale_width = 13,
};

static const struct tegra_pwm_soc tegra264_pwm_soc = {
	.num_channels = 1,
	.enable_reg = TEGRA_PWM_CSR_1,
	.scale_width = 16,
};

static const struct of_device_id tegra_pwm_of_match[] = {
	{ .compatible = "nvidia,tegra20-pwm", .data = &tegra20_pwm_soc },
	{ .compatible = "nvidia,tegra186-pwm", .data = &tegra186_pwm_soc },
	{ .compatible = "nvidia,tegra194-pwm", .data = &tegra186_pwm_soc },
	{ .compatible = "nvidia,tegra264-pwm", .data = &tegra264_pwm_soc },
	{ }
};
MODULE_DEVICE_TABLE(of, tegra_pwm_of_match);

static const struct dev_pm_ops tegra_pwm_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_pwm_runtime_suspend, tegra_pwm_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
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
MODULE_AUTHOR("Sandipan Patra <spatra@nvidia.com>");
MODULE_DESCRIPTION("Tegra PWM controller driver");
MODULE_ALIAS("platform:tegra-pwm");
