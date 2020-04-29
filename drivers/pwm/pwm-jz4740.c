// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform PWM support
 *
 * Limitations:
 * - The .apply callback doesn't complete the currently running period before
 *   reconfiguring the hardware.
 * - Each period starts with the inactive part.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/mfd/ingenic-tcu.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

#define NUM_PWM 8

struct jz4740_pwm_chip {
	struct pwm_chip chip;
	struct regmap *map;
};

static inline struct jz4740_pwm_chip *to_jz4740(struct pwm_chip *chip)
{
	return container_of(chip, struct jz4740_pwm_chip, chip);
}

static bool jz4740_pwm_can_use_chn(struct jz4740_pwm_chip *jz,
				   unsigned int channel)
{
	/* Enable all TCU channels for PWM use by default except channels 0/1 */
	u32 pwm_channels_mask = GENMASK(NUM_PWM - 1, 2);

	device_property_read_u32(jz->chip.dev->parent,
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

	if (!jz4740_pwm_can_use_chn(jz, pwm->hwpwm))
		return -EBUSY;

	snprintf(name, sizeof(name), "timer%u", pwm->hwpwm);

	clk = clk_get(chip->dev, name);
	if (IS_ERR(clk)) {
		if (PTR_ERR(clk) != -EPROBE_DEFER)
			dev_err(chip->dev, "Failed to get clock: %pe", clk);

		return PTR_ERR(clk);
	}

	err = clk_prepare_enable(clk);
	if (err < 0) {
		clk_put(clk);
		return err;
	}

	pwm_set_chip_data(pwm, clk);

	return 0;
}

static void jz4740_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct clk *clk = pwm_get_chip_data(pwm);

	clk_disable_unprepare(clk);
	clk_put(clk);
}

static int jz4740_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct jz4740_pwm_chip *jz = to_jz4740(chip);

	/* Enable PWM output */
	regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
			   TCU_TCSR_PWM_EN, TCU_TCSR_PWM_EN);

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
	regmap_update_bits(jz->map, TCU_REG_TCSRc(pwm->hwpwm),
			   TCU_TCSR_PWM_EN, 0);

	/* Stop counter */
	regmap_write(jz->map, TCU_REG_TECR, BIT(pwm->hwpwm));
}

static int jz4740_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			    const struct pwm_state *state)
{
	struct jz4740_pwm_chip *jz4740 = to_jz4740(pwm->chip);
	unsigned long long tmp = 0xffffull * NSEC_PER_SEC;
	struct clk *clk = pwm_get_chip_data(pwm);
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
		dev_err(chip->dev, "Unable to round rate: %ld", rate);
		return rate;
	}

	/* Calculate period value */
	tmp = (unsigned long long)rate * state->period;
	do_div(tmp, NSEC_PER_SEC);
	period = (unsigned long)tmp;

	/* Calculate duty value */
	tmp = (unsigned long long)period * state->duty_cycle;
	do_div(tmp, state->period);
	duty = period - tmp;

	if (duty >= period)
		duty = period - 1;

	jz4740_pwm_disable(chip, pwm);

	err = clk_set_rate(clk, rate);
	if (err) {
		dev_err(chip->dev, "Unable to set rate: %d", err);
		return err;
	}

	/* Reset counter to 0 */
	regmap_write(jz4740->map, TCU_REG_TCNTc(pwm->hwpwm), 0);

	/* Set duty */
	regmap_write(jz4740->map, TCU_REG_TDHRc(pwm->hwpwm), duty);

	/* Set period */
	regmap_write(jz4740->map, TCU_REG_TDFRc(pwm->hwpwm), period);

	/* Set abrupt shutdown */
	regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
			   TCU_TCSR_PWM_SD, TCU_TCSR_PWM_SD);

	/* Set polarity */
	switch (state->polarity) {
	case PWM_POLARITY_NORMAL:
		regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
				   TCU_TCSR_PWM_INITL_HIGH, 0);
		break;
	case PWM_POLARITY_INVERSED:
		regmap_update_bits(jz4740->map, TCU_REG_TCSRc(pwm->hwpwm),
				   TCU_TCSR_PWM_INITL_HIGH,
				   TCU_TCSR_PWM_INITL_HIGH);
		break;
	}

	if (state->enabled)
		jz4740_pwm_enable(chip, pwm);

	return 0;
}

static const struct pwm_ops jz4740_pwm_ops = {
	.request = jz4740_pwm_request,
	.free = jz4740_pwm_free,
	.apply = jz4740_pwm_apply,
	.owner = THIS_MODULE,
};

static int jz4740_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct jz4740_pwm_chip *jz4740;

	jz4740 = devm_kzalloc(dev, sizeof(*jz4740), GFP_KERNEL);
	if (!jz4740)
		return -ENOMEM;

	jz4740->map = device_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(jz4740->map)) {
		dev_err(dev, "regmap not found: %ld\n", PTR_ERR(jz4740->map));
		return PTR_ERR(jz4740->map);
	}

	jz4740->chip.dev = dev;
	jz4740->chip.ops = &jz4740_pwm_ops;
	jz4740->chip.npwm = NUM_PWM;
	jz4740->chip.base = -1;
	jz4740->chip.of_xlate = of_pwm_xlate_with_flags;
	jz4740->chip.of_pwm_n_cells = 3;

	platform_set_drvdata(pdev, jz4740);

	return pwmchip_add(&jz4740->chip);
}

static int jz4740_pwm_remove(struct platform_device *pdev)
{
	struct jz4740_pwm_chip *jz4740 = platform_get_drvdata(pdev);

	return pwmchip_remove(&jz4740->chip);
}

#ifdef CONFIG_OF
static const struct of_device_id jz4740_pwm_dt_ids[] = {
	{ .compatible = "ingenic,jz4740-pwm", },
	{},
};
MODULE_DEVICE_TABLE(of, jz4740_pwm_dt_ids);
#endif

static struct platform_driver jz4740_pwm_driver = {
	.driver = {
		.name = "jz4740-pwm",
		.of_match_table = of_match_ptr(jz4740_pwm_dt_ids),
	},
	.probe = jz4740_pwm_probe,
	.remove = jz4740_pwm_remove,
};
module_platform_driver(jz4740_pwm_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Ingenic JZ4740 PWM driver");
MODULE_ALIAS("platform:jz4740-pwm");
MODULE_LICENSE("GPL");
