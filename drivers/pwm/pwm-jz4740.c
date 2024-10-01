// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform PWM support
 *
 * Limitations:
 * - The .apply callback doesn't complete the currently running period before
 *   reconfiguring the hardware.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

struct soc_info {
	unsigned int num_pwms;
};

struct jz4740_pwm_chip {
	struct regmap *map;
	struct clk *clk[];
};

static inline struct jz4740_pwm_chip *to_jz4740(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static bool jz4740_pwm_can_use_chn(struct pwm_chip *chip, unsigned int channel)
{
	/* Enable all TCU channels for PWM use by default except channels 0/1 */
	u32 pwm_channels_mask = GENMASK(chip->npwm - 1, 2);

	device_property_read_u32(pwmchip_parent(chip)->parent,
				 "ingenic,pwm-channels-mask",
				 &pwm_channels_mask);

	return !!(pwm_channels_mask & BIT(channel));
}

static int jz4740_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);
	struct clk *clk;
	char name[16];
	int err;

	if (!jz4740_pwm_can_use_chn(chip, pwm->hwpwm))
		return -EBUSY;

	snprintf(name, sizeof(name), "timer%u", pwm->hwpwm);

	clk = clk_get(pwmchip_parent(chip), name);
	if (IS_ERR(clk)) {
		dev_err(pwmchip_parent(chip),
			"error %pe: Failed to get clock\n", clk);
		return PTR_ERR(clk);
	}

	err = clk_prepare_enable(clk);
	if (err < 0) {
		clk_put(clk);
		return err;
	}

	jz->clk[pwm->hwpwm] = clk;

	return 0;
}

static void jz4740_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);
	struct clk *clk = jz->clk[pwm->hwpwm];

	clk_disable_unprepare(clk);
	clk_put(clk);
}

static int jz4740_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/* Enable PWM output */
	regmap_set_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm), TCU_TCSR_PWM_EN);

	/* Start counter */
	regmap_write(jz->map, TCU_REG_TESR, BIT(pwm->hwpwm));

	return 0;
}

static void jz4740_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/*
	 * Set duty > period. This trick allows the TCU channels in TCU2 mode to
	 * properly return to their init level.
	 */
	regmap_write(jz->map, TCU_REG_TDHRc(pwm->hwpwm), 0xffff);
	regmap_write(jz->map, TCU_REG_TDFRc(pwm->hwpwm), 0x0);

	/*
	 * Disable PWM output.
	 * In TCU2 mode (channel 1/2 on JZ4750+), this must be done before the
	 * counter is stopped, while in TCU1 mode the order does not matter.
	 */
	regmap_clear_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm), TCU_TCSR_PWM_EN);

	/* Stop counter */
	regmap_write(jz->map, TCU_REG_TECR, BIT(pwm->hwpwm));
}

static int jz4740_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);
	unsigned long long tmp = 0xffffull * NSEC_PER_SEC;
	struct clk *clk = jz->clk[pwm->hwpwm];
	unsigned long period, duty;
	long rate;
	int err;

	/*
	 * Limit the clock to a maximum rate that still gives us a period value
	 * which fits in 16 bits.
	 */
	do_div(tmp, state->period);

	/*
	 * /!\ IMPORTANT NOTE:
	 * -------------------
	 * This code relies on the fact that clk_round_rate() will always round
	 * down, which is not a valid assumption given by the clk API, but only
	 * happens to be true with the clk drivers used for Ingenic SoCs.
	 *
	 * Right now, there is no alternative as the clk API does not have a
	 * round-down function (and won't have one for a while), but if it ever
	 * comes to light, a round-down function should be used instead.
	 */
	rate = clk_round_rate(clk, tmp);
	if (rate < 0) {
		dev_err(pwmchip_parent(chip), "Unable to round rate: %ld\n", rate);
		return rate;
	}

	/* Calculate period value */
	tmp = (unsigned long long)rate * state->period;
	do_div(tmp, NSEC_PER_SEC);
	period = tmp;

	/* Calculate duty value */
	tmp = (unsigned long long)rate * state->duty_cycle;
	do_div(tmp, NSEC_PER_SEC);
	duty = tmp;

	if (duty >= period)
		duty = period - 1;

	jz4740_pwm_disable(chip, pwm);

	err = clk_set_rate(clk, rate);
	if (err) {
		dev_err(pwmchip_parent(chip), "Unable to set rate: %d\n", err);
		return err;
	}

	/* Reset counter to 0 */
	regmap_write(jz->map, TCU_REG_TCNTc(pwm->hwpwm), 0);

	/* Set duty */
	regmap_write(jz->map, TCU_REG_TDHRc(pwm->hwpwm), duty);

	/* Set period */
	regmap_write(jz->map, TCU_REG_TDFRc(pwm->hwpwm), period);

	/* Set abrupt shutdown */
	regmap_set_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
			TCU_TCSR_PWM_SD);

	/*
	 * Set polarity.
	 *
	 * The PWM starts in inactive state until the internal timer reaches the
	 * duty value, then becomes active until the timer reaches the period
	 * value. In theory, we should then use (period - duty) as the real duty
	 * value, as a high duty value would otherwise result in the PWM pin
	 * being inactive most of the time.
	 *
	 * Here, we don't do that, and instead invert the polarity of the PWM
	 * when it is active. This trick makes the PWM start with its active
	 * state instead of its inactive state.
	 */
	if ((state->polarity == PWM_POLARITY_NORMAL) ^ state->enabled)
		regmap_clear_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
				  TCU_TCSR_PWM_INITL_HIGH);
	else
		regmap_set_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
				TCU_TCSR_PWM_INITL_HIGH);

	if (state->enabled)
		jz4740_pwm_enable(chip, pwm);

	return 0;
}

static const struct pwm_ops jz4740_pwm_ops = {
	.request = jz4740_pwm_request,
	.free = jz4740_pwm_free,
	.apply = jz4740_pwm_apply,
};

static int jz4740_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip;
	struct jz4740_pwm_chip *jz;
	const struct soc_info *info;

	info = device_get_match_data(dev);
	if (!info)
		return -EINVAL;

	chip = devm_pwmchip_alloc(dev, info->num_pwms, struct_size(jz, clk, info->num_pwms));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	jz = to_jz4740(chip);

	jz->map = device_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(jz->map)) {
		dev_err(dev, "regmap not found: %ld\n", PTR_ERR(jz->map));
		return PTR_ERR(jz->map);
	}

	chip->ops = &jz4740_pwm_ops;

	return devm_pwmchip_add(dev, chip);
}

static const struct soc_info jz4740_soc_info = {
	.num_pwms = 8,
};

static const struct soc_info jz4725b_soc_info = {
	.num_pwms = 6,
};

static const struct soc_info x1000_soc_info = {
	.num_pwms = 5,
};

static const struct of_device_id jz4740_pwm_dt_ids[] = {
	{ .compatible = "ingenic,jz4740-pwm", .data = &jz4740_soc_info },
	{ .compatible = "ingenic,jz4725b-pwm", .data = &jz4725b_soc_info },
	{ .compatible = "ingenic,x1000-pwm", .data = &x1000_soc_info },
	{},
};
MODULE_DEVICE_TABLE(of, jz4740_pwm_dt_ids);

static struct platform_driver jz4740_pwm_driver = {
	.driver = {
		.name = "jz4740-pwm",
		.of_match_table = jz4740_pwm_dt_ids,
	},
	.probe = jz4740_pwm_probe,
};
module_platform_driver(jz4740_pwm_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic JZ4740 PWM driver");
MODULE_ALIAS("platform:jz4740-pwm");
MODULE_LICENSE("GPL");
