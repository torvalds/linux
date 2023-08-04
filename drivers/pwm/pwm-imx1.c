// SPDX-License-Identifier: GPL-2.0
/*
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * Derived from pxa PWM driver by eric miao <eric.miao@marvell.com>
 */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>

#define MX1_PWMC			0x00   /* PWM Control Register */
#define MX1_PWMS			0x04   /* PWM Sample Register */
#define MX1_PWMP			0x08   /* PWM Period Register */

#define MX1_PWMC_EN			BIT(4)

struct pwm_imx1_chip {
	struct clk *clk_ipg;
	struct clk *clk_per;
	void __iomem *mmio_base;
	struct pwm_chip chip;
};

#define to_pwm_imx1_chip(chip)	container_of(chip, struct pwm_imx1_chip, chip)

static int pwm_imx1_clk_prepare_enable(struct pwm_chip *chip)
{
	struct pwm_imx1_chip *imx = to_pwm_imx1_chip(chip);
	int ret;

	ret = clk_prepare_enable(imx->clk_ipg);
	if (ret)
		return ret;

	ret = clk_prepare_enable(imx->clk_per);
	if (ret) {
		clk_disable_unprepare(imx->clk_ipg);
		return ret;
	}

	return 0;
}

static void pwm_imx1_clk_disable_unprepare(struct pwm_chip *chip)
{
	struct pwm_imx1_chip *imx = to_pwm_imx1_chip(chip);

	clk_disable_unprepare(imx->clk_per);
	clk_disable_unprepare(imx->clk_ipg);
}

static int pwm_imx1_config(struct pwm_chip *chip,
			   struct pwm_device *pwm, u64 duty_ns, u64 period_ns)
{
	struct pwm_imx1_chip *imx = to_pwm_imx1_chip(chip);
	u32 max, p;

	/*
	 * The PWM subsystem allows for exact frequencies. However,
	 * I cannot connect a scope on my device to the PWM line and
	 * thus cannot provide the program the PWM controller
	 * exactly. Instead, I'm relying on the fact that the
	 * Bootloader (u-boot or WinCE+haret) has programmed the PWM
	 * function group already. So I'll just modify the PWM sample
	 * register to follow the ratio of duty_ns vs. period_ns
	 * accordingly.
	 *
	 * This is good enough for programming the brightness of
	 * the LCD backlight.
	 *
	 * The real implementation would divide PERCLK[0] first by
	 * both the prescaler (/1 .. /128) and then by CLKSEL
	 * (/2 .. /16).
	 */
	max = readl(imx->mmio_base + MX1_PWMP);
	p = mul_u64_u64_div_u64(max, duty_ns, period_ns);

	writel(max - p, imx->mmio_base + MX1_PWMS);

	return 0;
}

static int pwm_imx1_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_imx1_chip *imx = to_pwm_imx1_chip(chip);
	u32 value;
	int ret;

	ret = pwm_imx1_clk_prepare_enable(chip);
	if (ret < 0)
		return ret;

	value = readl(imx->mmio_base + MX1_PWMC);
	value |= MX1_PWMC_EN;
	writel(value, imx->mmio_base + MX1_PWMC);

	return 0;
}

static void pwm_imx1_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pwm_imx1_chip *imx = to_pwm_imx1_chip(chip);
	u32 value;

	value = readl(imx->mmio_base + MX1_PWMC);
	value &= ~MX1_PWMC_EN;
	writel(value, imx->mmio_base + MX1_PWMC);

	pwm_imx1_clk_disable_unprepare(chip);
}

static int pwm_imx1_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			  const struct pwm_state *state)
{
	int err;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (pwm->state.enabled)
			pwm_imx1_disable(chip, pwm);

		return 0;
	}

	err = pwm_imx1_config(chip, pwm, state->duty_cycle, state->period);
	if (err)
		return err;

	if (!pwm->state.enabled)
		return pwm_imx1_enable(chip, pwm);

	return 0;
}

static const struct pwm_ops pwm_imx1_ops = {
	.apply = pwm_imx1_apply,
};

static const struct of_device_id pwm_imx1_dt_ids[] = {
	{ .compatible = "fsl,imx1-pwm", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pwm_imx1_dt_ids);

static int pwm_imx1_probe(struct platform_device *pdev)
{
	struct pwm_imx1_chip *imx;

	imx = devm_kzalloc(&pdev->dev, sizeof(*imx), GFP_KERNEL);
	if (!imx)
		return -ENOMEM;

	imx->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(imx->clk_ipg))
		return dev_err_probe(&pdev->dev, PTR_ERR(imx->clk_ipg),
				     "getting ipg clock failed\n");

	imx->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(imx->clk_per))
		return dev_err_probe(&pdev->dev, PTR_ERR(imx->clk_per),
				     "failed to get peripheral clock\n");

	imx->chip.ops = &pwm_imx1_ops;
	imx->chip.dev = &pdev->dev;
	imx->chip.npwm = 1;

	imx->mmio_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(imx->mmio_base))
		return PTR_ERR(imx->mmio_base);

	return devm_pwmchip_add(&pdev->dev, &imx->chip);
}

static struct platform_driver pwm_imx1_driver = {
	.driver = {
		.name = "pwm-imx1",
		.of_match_table = pwm_imx1_dt_ids,
	},
	.probe = pwm_imx1_probe,
};
module_platform_driver(pwm_imx1_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
