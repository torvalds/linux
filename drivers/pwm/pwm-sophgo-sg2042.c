// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2042 PWM Controller Driver
 *
 * Copyright (C) 2024 Sophgo Technology Inc.
 * Copyright (C) 2024 Chen Wang <unicorn_wang@outlook.com>
 *
 * Limitations:
 * - After reset, the output of the PWM channel is always high.
 *   The value of HLPERIOD/PERIOD is 0.
 * - When HLPERIOD or PERIOD is reconfigured, PWM will start to
 *   output waveforms with the new configuration after completing
 *   the running period.
 * - When PERIOD and HLPERIOD is set to 0, the PWM wave output will
 *   be stopped and the output is pulled to high.
 * - SG2044 supports both polarities, SG2042 only normal polarity.
 * See the datasheet [1] for more details.
 * [1]:https://github.com/sophgo/sophgo-doc/tree/main/SG2042/TRM
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/reset.h>

/*
 * Offset RegisterName
 * 0x0000 HLPERIOD0
 * 0x0004 PERIOD0
 * 0x0008 HLPERIOD1
 * 0x000C PERIOD1
 * 0x0010 HLPERIOD2
 * 0x0014 PERIOD2
 * 0x0018 HLPERIOD3
 * 0x001C PERIOD3
 * Four groups and every group is composed of HLPERIOD & PERIOD
 */
#define SG2042_PWM_HLPERIOD(chan) ((chan) * 8 + 0)
#define SG2042_PWM_PERIOD(chan) ((chan) * 8 + 4)

#define SG2044_PWM_POLARITY		0x40
#define SG2044_PWM_PWMSTART		0x44
#define SG2044_PWM_OE			0xd0

#define SG2042_PWM_CHANNELNUM	4

/**
 * struct sg2042_pwm_ddata - private driver data
 * @base:		base address of mapped PWM registers
 * @clk_rate_hz:	rate of base clock in HZ
 */
struct sg2042_pwm_ddata {
	void __iomem *base;
	unsigned long clk_rate_hz;
};

struct sg2042_chip_data {
	const struct pwm_ops ops;
};

/*
 * period_ticks: PERIOD
 * hlperiod_ticks: HLPERIOD
 */
static void pwm_sg2042_config(struct sg2042_pwm_ddata *ddata, unsigned int chan,
			      u32 period_ticks, u32 hlperiod_ticks)
{
	void __iomem *base = ddata->base;

	writel(period_ticks, base + SG2042_PWM_PERIOD(chan));
	writel(hlperiod_ticks, base + SG2042_PWM_HLPERIOD(chan));
}

static void pwm_sg2042_set_dutycycle(struct pwm_chip *chip, struct pwm_device *pwm,
				     const struct pwm_state *state)
{
	struct sg2042_pwm_ddata *ddata = pwmchip_get_drvdata(chip);
	u32 hlperiod_ticks;
	u32 period_ticks;

	/*
	 * Duration of High level (duty_cycle) = HLPERIOD x Period_of_input_clk
	 * Duration of One Cycle (period) = PERIOD x Period_of_input_clk
	 */
	period_ticks = min(mul_u64_u64_div_u64(ddata->clk_rate_hz, state->period, NSEC_PER_SEC), U32_MAX);
	hlperiod_ticks = min(mul_u64_u64_div_u64(ddata->clk_rate_hz, state->duty_cycle, NSEC_PER_SEC), U32_MAX);

	dev_dbg(pwmchip_parent(chip), "chan[%u]: ENABLE=%u, PERIOD=%u, HLPERIOD=%u, POLARITY=%u\n",
		pwm->hwpwm, state->enabled, period_ticks, hlperiod_ticks, state->polarity);

	pwm_sg2042_config(ddata, pwm->hwpwm, period_ticks, hlperiod_ticks);
}

static int pwm_sg2042_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct sg2042_pwm_ddata *ddata = pwmchip_get_drvdata(chip);

	if (state->polarity == PWM_POLARITY_INVERSED)
		return -EINVAL;

	if (!state->enabled) {
		pwm_sg2042_config(ddata, pwm->hwpwm, 0, 0);
		return 0;
	}

	pwm_sg2042_set_dutycycle(chip, pwm, state);

	return 0;
}

static int pwm_sg2042_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				struct pwm_state *state)
{
	struct sg2042_pwm_ddata *ddata = pwmchip_get_drvdata(chip);
	unsigned int chan = pwm->hwpwm;
	u32 hlperiod_ticks;
	u32 period_ticks;

	period_ticks = readl(ddata->base + SG2042_PWM_PERIOD(chan));
	hlperiod_ticks = readl(ddata->base + SG2042_PWM_HLPERIOD(chan));

	if (!period_ticks) {
		state->enabled = false;
		return 0;
	}

	if (hlperiod_ticks > period_ticks)
		hlperiod_ticks = period_ticks;

	state->enabled = true;
	state->period = DIV_ROUND_UP_ULL((u64)period_ticks * NSEC_PER_SEC, ddata->clk_rate_hz);
	state->duty_cycle = DIV_ROUND_UP_ULL((u64)hlperiod_ticks * NSEC_PER_SEC, ddata->clk_rate_hz);
	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static void pwm_sg2044_set_outputen(struct sg2042_pwm_ddata *ddata, struct pwm_device *pwm,
				    bool enabled)
{
	u32 pwmstart;

	pwmstart = readl(ddata->base + SG2044_PWM_PWMSTART);

	if (enabled)
		pwmstart |= BIT(pwm->hwpwm);
	else
		pwmstart &= ~BIT(pwm->hwpwm);

	writel(pwmstart, ddata->base + SG2044_PWM_PWMSTART);
}

static void pwm_sg2044_set_outputdir(struct sg2042_pwm_ddata *ddata, struct pwm_device *pwm,
				     bool enabled)
{
	u32 pwm_oe;

	pwm_oe = readl(ddata->base + SG2044_PWM_OE);

	if (enabled)
		pwm_oe |= BIT(pwm->hwpwm);
	else
		pwm_oe &= ~BIT(pwm->hwpwm);

	writel(pwm_oe, ddata->base + SG2044_PWM_OE);
}

static void pwm_sg2044_set_polarity(struct sg2042_pwm_ddata *ddata, struct pwm_device *pwm,
				    const struct pwm_state *state)
{
	u32 pwm_polarity;

	pwm_polarity = readl(ddata->base + SG2044_PWM_POLARITY);

	if (state->polarity == PWM_POLARITY_NORMAL)
		pwm_polarity &= ~BIT(pwm->hwpwm);
	else
		pwm_polarity |= BIT(pwm->hwpwm);

	writel(pwm_polarity, ddata->base + SG2044_PWM_POLARITY);
}

static int pwm_sg2044_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct sg2042_pwm_ddata *ddata = pwmchip_get_drvdata(chip);

	pwm_sg2044_set_polarity(ddata, pwm, state);

	pwm_sg2042_set_dutycycle(chip, pwm, state);

	/*
	 * re-enable PWMSTART to refresh the register period
	 */
	pwm_sg2044_set_outputen(ddata, pwm, false);

	if (!state->enabled)
		return 0;

	pwm_sg2044_set_outputdir(ddata, pwm, true);
	pwm_sg2044_set_outputen(ddata, pwm, true);

	return 0;
}

static const struct sg2042_chip_data sg2042_chip_data = {
	.ops = {
		.apply = pwm_sg2042_apply,
		.get_state = pwm_sg2042_get_state,
	},
};

static const struct sg2042_chip_data sg2044_chip_data = {
	.ops = {
		.apply = pwm_sg2044_apply,
		.get_state = pwm_sg2042_get_state,
	},
};

static const struct of_device_id sg2042_pwm_ids[] = {
	{
		.compatible = "sophgo,sg2042-pwm",
		.data = &sg2042_chip_data
	},
	{
		.compatible = "sophgo,sg2044-pwm",
		.data = &sg2044_chip_data
	},
	{ }
};
MODULE_DEVICE_TABLE(of, sg2042_pwm_ids);

static int pwm_sg2042_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct sg2042_chip_data *chip_data;
	struct sg2042_pwm_ddata *ddata;
	struct reset_control *rst;
	struct pwm_chip *chip;
	struct clk *clk;
	int ret;

	chip_data = device_get_match_data(dev);
	if (!chip_data)
		return -ENODEV;

	chip = devm_pwmchip_alloc(dev, SG2042_PWM_CHANNELNUM, sizeof(*ddata));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	ddata = pwmchip_get_drvdata(chip);

	ddata->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ddata->base))
		return PTR_ERR(ddata->base);

	clk = devm_clk_get_enabled(dev, "apb");
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "Failed to get base clk\n");

	ret = devm_clk_rate_exclusive_get(dev, clk);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get exclusive rate\n");

	ddata->clk_rate_hz = clk_get_rate(clk);
	/* period = PERIOD * NSEC_PER_SEC / clk_rate_hz */
	if (!ddata->clk_rate_hz || ddata->clk_rate_hz > NSEC_PER_SEC)
		return dev_err_probe(dev, -EINVAL,
				     "Invalid clock rate: %lu\n", ddata->clk_rate_hz);

	rst = devm_reset_control_get_optional_shared_deasserted(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "Failed to get reset\n");

	chip->ops = &chip_data->ops;
	chip->atomic = true;

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to register PWM chip\n");

	return 0;
}

static struct platform_driver pwm_sg2042_driver = {
	.driver	= {
		.name = "sg2042-pwm",
		.of_match_table = sg2042_pwm_ids,
	},
	.probe = pwm_sg2042_probe,
};
module_platform_driver(pwm_sg2042_driver);

MODULE_AUTHOR("Chen Wang");
MODULE_AUTHOR("Longbin Li <looong.bin@gmail.com>");
MODULE_DESCRIPTION("Sophgo SG2042 PWM driver");
MODULE_LICENSE("GPL");
