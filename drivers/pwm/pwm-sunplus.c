// SPDX-License-Identifier: GPL-2.0
/*
 * PWM device driver for SUNPLUS SP7021 SoC
 *
 * Links:
 *   Reference Manual:
 *   https://sunplus-tibbo.atlassian.net/wiki/spaces/doc/overview
 *
 *   Reference Manual(PWM module):
 *   https://sunplus.atlassian.net/wiki/spaces/doc/pages/461144198/12.+Pulse+Width+Modulation+PWM
 *
 * Limitations:
 * - Only supports normal polarity.
 * - It output low when PWM channel disabled.
 * - When the parameters change, current running period will not be completed
 *     and run new settings immediately.
 * - In .apply() PWM output need to write register FREQ and DUTY. When first write FREQ
 *     done and not yet write DUTY, it has short timing gap use new FREQ and old DUTY.
 *
 * Author: Hammer Hsieh <hammerh0314@gmail.com>
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>

#define SP7021_PWM_MODE0		0x000
#define SP7021_PWM_MODE0_PWMEN(ch)	BIT(ch)
#define SP7021_PWM_MODE0_BYPASS(ch)	BIT(8 + (ch))
#define SP7021_PWM_MODE1		0x004
#define SP7021_PWM_MODE1_CNT_EN(ch)	BIT(ch)
#define SP7021_PWM_FREQ(ch)		(0x008 + 4 * (ch))
#define SP7021_PWM_FREQ_MAX		GENMASK(15, 0)
#define SP7021_PWM_DUTY(ch)		(0x018 + 4 * (ch))
#define SP7021_PWM_DUTY_DD_SEL(ch)	FIELD_PREP(GENMASK(9, 8), ch)
#define SP7021_PWM_DUTY_MAX		GENMASK(7, 0)
#define SP7021_PWM_DUTY_MASK		SP7021_PWM_DUTY_MAX
#define SP7021_PWM_FREQ_SCALER		256
#define SP7021_PWM_NUM			4

struct sunplus_pwm {
	void __iomem *base;
	struct clk *clk;
};

static inline struct sunplus_pwm *to_sunplus_pwm(struct pwm_chip *chip)
{
	return pwmchip_get_drvdata(chip);
}

static int sunplus_pwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			     const struct pwm_state *state)
{
	struct sunplus_pwm *priv = to_sunplus_pwm(chip);
	u32 dd_freq, duty, mode0, mode1;
	u64 clk_rate;

	if (state->polarity != pwm->state.polarity)
		return -EINVAL;

	if (!state->enabled) {
		/* disable pwm channel output */
		mode0 = readl(priv->base + SP7021_PWM_MODE0);
		mode0 &= ~SP7021_PWM_MODE0_PWMEN(pwm->hwpwm);
		writel(mode0, priv->base + SP7021_PWM_MODE0);
		/* disable pwm channel clk source */
		mode1 = readl(priv->base + SP7021_PWM_MODE1);
		mode1 &= ~SP7021_PWM_MODE1_CNT_EN(pwm->hwpwm);
		writel(mode1, priv->base + SP7021_PWM_MODE1);
		return 0;
	}

	clk_rate = clk_get_rate(priv->clk);

	/*
	 * The following calculations might overflow if clk is bigger
	 * than 256 GHz. In practise it's 202.5MHz, so this limitation
	 * is only theoretic.
	 */
	if (clk_rate > (u64)SP7021_PWM_FREQ_SCALER * NSEC_PER_SEC)
		return -EINVAL;

	/*
	 * With clk_rate limited above we have dd_freq <= state->period,
	 * so this cannot overflow.
	 */
	dd_freq = mul_u64_u64_div_u64(clk_rate, state->period, (u64)SP7021_PWM_FREQ_SCALER
				* NSEC_PER_SEC);

	if (dd_freq == 0)
		return -EINVAL;

	if (dd_freq > SP7021_PWM_FREQ_MAX)
		dd_freq = SP7021_PWM_FREQ_MAX;

	writel(dd_freq, priv->base + SP7021_PWM_FREQ(pwm->hwpwm));

	/* cal and set pwm duty */
	mode0 = readl(priv->base + SP7021_PWM_MODE0);
	mode0 |= SP7021_PWM_MODE0_PWMEN(pwm->hwpwm);
	mode1 = readl(priv->base + SP7021_PWM_MODE1);
	mode1 |= SP7021_PWM_MODE1_CNT_EN(pwm->hwpwm);
	if (state->duty_cycle == state->period) {
		/* PWM channel output = high */
		mode0 |= SP7021_PWM_MODE0_BYPASS(pwm->hwpwm);
		duty = SP7021_PWM_DUTY_DD_SEL(pwm->hwpwm) | SP7021_PWM_DUTY_MAX;
	} else {
		mode0 &= ~SP7021_PWM_MODE0_BYPASS(pwm->hwpwm);
		/*
		 * duty_ns <= period_ns 27 bits, clk_rate 28 bits, won't overflow.
		 */
		duty = mul_u64_u64_div_u64(state->duty_cycle, clk_rate,
					   (u64)dd_freq * NSEC_PER_SEC);
		duty = SP7021_PWM_DUTY_DD_SEL(pwm->hwpwm) | duty;
	}
	writel(duty, priv->base + SP7021_PWM_DUTY(pwm->hwpwm));
	writel(mode1, priv->base + SP7021_PWM_MODE1);
	writel(mode0, priv->base + SP7021_PWM_MODE0);

	return 0;
}

static int sunplus_pwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
				 struct pwm_state *state)
{
	struct sunplus_pwm *priv = to_sunplus_pwm(chip);
	u32 mode0, dd_freq, duty;
	u64 clk_rate;

	mode0 = readl(priv->base + SP7021_PWM_MODE0);

	if (mode0 & BIT(pwm->hwpwm)) {
		clk_rate = clk_get_rate(priv->clk);
		dd_freq = readl(priv->base + SP7021_PWM_FREQ(pwm->hwpwm));
		duty = readl(priv->base + SP7021_PWM_DUTY(pwm->hwpwm));
		duty = FIELD_GET(SP7021_PWM_DUTY_MASK, duty);
		/*
		 * dd_freq 16 bits, SP7021_PWM_FREQ_SCALER 8 bits
		 * NSEC_PER_SEC 30 bits, won't overflow.
		 */
		state->period = DIV64_U64_ROUND_UP((u64)dd_freq * (u64)SP7021_PWM_FREQ_SCALER
						* NSEC_PER_SEC, clk_rate);
		/*
		 * dd_freq 16 bits, duty 8 bits, NSEC_PER_SEC 30 bits, won't overflow.
		 */
		state->duty_cycle = DIV64_U64_ROUND_UP((u64)dd_freq * (u64)duty * NSEC_PER_SEC,
						       clk_rate);
		state->enabled = true;
	} else {
		state->enabled = false;
	}

	state->polarity = PWM_POLARITY_NORMAL;

	return 0;
}

static const struct pwm_ops sunplus_pwm_ops = {
	.apply = sunplus_pwm_apply,
	.get_state = sunplus_pwm_get_state,
};

static void sunplus_pwm_clk_release(void *data)
{
	struct clk *clk = data;

	clk_disable_unprepare(clk);
}

static int sunplus_pwm_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pwm_chip *chip;
	struct sunplus_pwm *priv;
	int ret;

	chip = devm_pwmchip_alloc(dev, SP7021_PWM_NUM, sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = to_sunplus_pwm(chip);

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk))
		return dev_err_probe(dev, PTR_ERR(priv->clk),
				     "get pwm clock failed\n");

	ret = clk_prepare_enable(priv->clk);
	if (ret < 0) {
		dev_err(dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	ret = devm_add_action_or_reset(dev, sunplus_pwm_clk_release, priv->clk);
	if (ret < 0) {
		dev_err(dev, "failed to release clock: %d\n", ret);
		return ret;
	}

	chip->ops = &sunplus_pwm_ops;

	ret = devm_pwmchip_add(dev, chip);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Cannot register sunplus PWM\n");

	return 0;
}

static const struct of_device_id sunplus_pwm_of_match[] = {
	{ .compatible = "sunplus,sp7021-pwm", },
	{}
};
MODULE_DEVICE_TABLE(of, sunplus_pwm_of_match);

static struct platform_driver sunplus_pwm_driver = {
	.probe		= sunplus_pwm_probe,
	.driver		= {
		.name	= "sunplus-pwm",
		.of_match_table = sunplus_pwm_of_match,
	},
};
module_platform_driver(sunplus_pwm_driver);

MODULE_DESCRIPTION("Sunplus SoC PWM Driver");
MODULE_AUTHOR("Hammer Hsieh <hammerh0314@gmail.com>");
MODULE_LICENSE("GPL");
