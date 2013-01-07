/*
 * Copyright 2012 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/stmp_device.h>

#define SET	0x4
#define CLR	0x8
#define TOG	0xc

#define PWM_CTRL		0x0
#define PWM_ACTIVE0		0x10
#define PWM_PERIOD0		0x20
#define  PERIOD_PERIOD(p)	((p) & 0xffff)
#define  PERIOD_PERIOD_MAX	0x10000
#define  PERIOD_ACTIVE_HIGH	(3 << 16)
#define  PERIOD_INACTIVE_LOW	(2 << 18)
#define  PERIOD_CDIV(div)	(((div) & 0x7) << 20)
#define  PERIOD_CDIV_MAX	8

struct mxs_pwm_chip {
	struct pwm_chip chip;
	struct device *dev;
	struct clk *clk;
	void __iomem *base;
};

#define to_mxs_pwm_chip(_chip) container_of(_chip, struct mxs_pwm_chip, chip)

static int mxs_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	struct mxs_pwm_chip *mxs = to_mxs_pwm_chip(chip);
	int ret, div = 0;
	unsigned int period_cycles, duty_cycles;
	unsigned long rate;
	unsigned long long c;

	rate = clk_get_rate(mxs->clk);
	while (1) {
		c = rate / (1 << div);
		c = c * period_ns;
		do_div(c, 1000000000);
		if (c < PERIOD_PERIOD_MAX)
			break;
		div++;
		if (div > PERIOD_CDIV_MAX)
			return -EINVAL;
	}

	period_cycles = c;
	c *= duty_ns;
	do_div(c, period_ns);
	duty_cycles = c;

	/*
	 * If the PWM channel is disabled, make sure to turn on the clock
	 * before writing the register. Otherwise, keep it enabled.
	 */
	if (!test_bit(PWMF_ENABLED, &pwm->flags)) {
		ret = clk_prepare_enable(mxs->clk);
		if (ret)
			return ret;
	}

	writel(duty_cycles << 16,
			mxs->base + PWM_ACTIVE0 + pwm->hwpwm * 0x20);
	writel(PERIOD_PERIOD(period_cycles) | PERIOD_ACTIVE_HIGH |
	       PERIOD_INACTIVE_LOW | PERIOD_CDIV(div),
			mxs->base + PWM_PERIOD0 + pwm->hwpwm * 0x20);

	/*
	 * If the PWM is not enabled, turn the clock off again to save power.
	 */
	if (!test_bit(PWMF_ENABLED, &pwm->flags))
		clk_disable_unprepare(mxs->clk);

	return 0;
}

static int mxs_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mxs_pwm_chip *mxs = to_mxs_pwm_chip(chip);
	int ret;

	ret = clk_prepare_enable(mxs->clk);
	if (ret)
		return ret;

	writel(1 << pwm->hwpwm, mxs->base + PWM_CTRL + SET);

	return 0;
}

static void mxs_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct mxs_pwm_chip *mxs = to_mxs_pwm_chip(chip);

	writel(1 << pwm->hwpwm, mxs->base + PWM_CTRL + CLR);

	clk_disable_unprepare(mxs->clk);
}

static const struct pwm_ops mxs_pwm_ops = {
	.config = mxs_pwm_config,
	.enable = mxs_pwm_enable,
	.disable = mxs_pwm_disable,
	.owner = THIS_MODULE,
};

static int mxs_pwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct mxs_pwm_chip *mxs;
	struct resource *res;
	struct pinctrl *pinctrl;
	int ret;

	mxs = devm_kzalloc(&pdev->dev, sizeof(*mxs), GFP_KERNEL);
	if (!mxs)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mxs->base = devm_request_and_ioremap(&pdev->dev, res);
	if (!mxs->base)
		return -EADDRNOTAVAIL;

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl))
		return PTR_ERR(pinctrl);

	mxs->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(mxs->clk))
		return PTR_ERR(mxs->clk);

	mxs->chip.dev = &pdev->dev;
	mxs->chip.ops = &mxs_pwm_ops;
	mxs->chip.base = -1;
	ret = of_property_read_u32(np, "fsl,pwm-number", &mxs->chip.npwm);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get pwm number: %d\n", ret);
		return ret;
	}

	ret = pwmchip_add(&mxs->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add pwm chip %d\n", ret);
		return ret;
	}

	mxs->dev = &pdev->dev;
	platform_set_drvdata(pdev, mxs);

	stmp_reset_block(mxs->base);

	return 0;
}

static int mxs_pwm_remove(struct platform_device *pdev)
{
	struct mxs_pwm_chip *mxs = platform_get_drvdata(pdev);

	return pwmchip_remove(&mxs->chip);
}

static struct of_device_id mxs_pwm_dt_ids[] = {
	{ .compatible = "fsl,imx23-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxs_pwm_dt_ids);

static struct platform_driver mxs_pwm_driver = {
	.driver = {
		.name = "mxs-pwm",
		.of_match_table = of_match_ptr(mxs_pwm_dt_ids),
	},
	.probe = mxs_pwm_probe,
	.remove = mxs_pwm_remove,
};
module_platform_driver(mxs_pwm_driver);

MODULE_ALIAS("platform:mxs-pwm");
MODULE_AUTHOR("Shawn Guo <shawn.guo@linaro.org>");
MODULE_DESCRIPTION("Freescale MXS PWM Driver");
MODULE_LICENSE("GPL v2");
