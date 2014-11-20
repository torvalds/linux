/*
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Derived from pxa PWM driver by eric miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/of.h>
#include <linux/of_device.h>

/* i.MX1 and i.MX21 share the same PWM function block: */

#define MX1_PWMC			0x00   /* PWM Control Register */
#define MX1_PWMS			0x04   /* PWM Sample Register */
#define MX1_PWMP			0x08   /* PWM Period Register */

#define MX1_PWMC_EN			(1 << 4)

/* i.MX27, i.MX31, i.MX35 share the same PWM function block: */

#define MX3_PWMCR			0x00    /* PWM Control Register */
#define MX3_PWMSR			0x04    /* PWM Status Register */
#define MX3_PWMSAR			0x0C    /* PWM Sample Register */
#define MX3_PWMPR			0x10    /* PWM Period Register */
#define MX3_PWMCR_PRESCALER(x)		((((x) - 1) & 0xFFF) << 4)
#define MX3_PWMCR_DOZEEN		(1 << 24)
#define MX3_PWMCR_WAITEN		(1 << 23)
#define MX3_PWMCR_DBGEN			(1 << 22)
#define MX3_PWMCR_CLKSRC_IPG_HIGH	(2 << 16)
#define MX3_PWMCR_CLKSRC_IPG		(1 << 16)
#define MX3_PWMCR_SWR			(1 << 3)
#define MX3_PWMCR_EN			(1 << 0)
#define MX3_PWMSR_FIFOAV_4WORDS		0x4
#define MX3_PWMSR_FIFOAV_MASK		0x7

#define MX3_PWM_SWR_LOOP		5

struct imx_chip {
	struct clk	*clk_per;
	struct clk	*clk_ipg;

	void __iomem	*mmio_base;

	struct pwm_chip	chip;

	int (*config)(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns);
	void (*set_enable)(struct pwm_chip *chip, bool enable);
};

#define to_imx_chip(chip)	container_of(chip, struct imx_chip, chip)

static int imx_pwm_config_v1(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns)
{
	struct imx_chip *imx = to_imx_chip(chip);

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
	u32 max = readl(imx->mmio_base + MX1_PWMP);
	u32 p = max * duty_ns / period_ns;
	writel(max - p, imx->mmio_base + MX1_PWMS);

	return 0;
}

static void imx_pwm_set_enable_v1(struct pwm_chip *chip, bool enable)
{
	struct imx_chip *imx = to_imx_chip(chip);
	u32 val;

	val = readl(imx->mmio_base + MX1_PWMC);

	if (enable)
		val |= MX1_PWMC_EN;
	else
		val &= ~MX1_PWMC_EN;

	writel(val, imx->mmio_base + MX1_PWMC);
}

static int imx_pwm_config_v2(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns)
{
	struct imx_chip *imx = to_imx_chip(chip);
	struct device *dev = chip->dev;
	unsigned long long c;
	unsigned long period_cycles, duty_cycles, prescale;
	unsigned int period_ms;
	bool enable = test_bit(PWMF_ENABLED, &pwm->flags);
	int wait_count = 0, fifoav;
	u32 cr, sr;

	/*
	 * i.MX PWMv2 has a 4-word sample FIFO.
	 * In order to avoid FIFO overflow issue, we do software reset
	 * to clear all sample FIFO if the controller is disabled or
	 * wait for a full PWM cycle to get a relinquished FIFO slot
	 * when the controller is enabled and the FIFO is fully loaded.
	 */
	if (enable) {
		sr = readl(imx->mmio_base + MX3_PWMSR);
		fifoav = sr & MX3_PWMSR_FIFOAV_MASK;
		if (fifoav == MX3_PWMSR_FIFOAV_4WORDS) {
			period_ms = DIV_ROUND_UP(pwm->period, NSEC_PER_MSEC);
			msleep(period_ms);

			sr = readl(imx->mmio_base + MX3_PWMSR);
			if (fifoav == (sr & MX3_PWMSR_FIFOAV_MASK))
				dev_warn(dev, "there is no free FIFO slot\n");
		}
	} else {
		writel(MX3_PWMCR_SWR, imx->mmio_base + MX3_PWMCR);
		do {
			usleep_range(200, 1000);
			cr = readl(imx->mmio_base + MX3_PWMCR);
		} while ((cr & MX3_PWMCR_SWR) &&
			 (wait_count++ < MX3_PWM_SWR_LOOP));

		if (cr & MX3_PWMCR_SWR)
			dev_warn(dev, "software reset timeout\n");
	}

	c = clk_get_rate(imx->clk_per);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	prescale = period_cycles / 0x10000 + 1;

	period_cycles /= prescale;
	c = (unsigned long long)period_cycles * duty_ns;
	do_div(c, period_ns);
	duty_cycles = c;

	/*
	 * according to imx pwm RM, the real period value should be
	 * PERIOD value in PWMPR plus 2.
	 */
	if (period_cycles > 2)
		period_cycles -= 2;
	else
		period_cycles = 0;

	writel(duty_cycles, imx->mmio_base + MX3_PWMSAR);
	writel(period_cycles, imx->mmio_base + MX3_PWMPR);

	cr = MX3_PWMCR_PRESCALER(prescale) |
		MX3_PWMCR_DOZEEN | MX3_PWMCR_WAITEN |
		MX3_PWMCR_DBGEN | MX3_PWMCR_CLKSRC_IPG_HIGH;

	if (enable)
		cr |= MX3_PWMCR_EN;

	writel(cr, imx->mmio_base + MX3_PWMCR);

	return 0;
}

static void imx_pwm_set_enable_v2(struct pwm_chip *chip, bool enable)
{
	struct imx_chip *imx = to_imx_chip(chip);
	u32 val;

	val = readl(imx->mmio_base + MX3_PWMCR);

	if (enable)
		val |= MX3_PWMCR_EN;
	else
		val &= ~MX3_PWMCR_EN;

	writel(val, imx->mmio_base + MX3_PWMCR);
}

static int imx_pwm_config(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns)
{
	struct imx_chip *imx = to_imx_chip(chip);
	int ret;

	ret = clk_prepare_enable(imx->clk_ipg);
	if (ret)
		return ret;

	ret = imx->config(chip, pwm, duty_ns, period_ns);

	clk_disable_unprepare(imx->clk_ipg);

	return ret;
}

static int imx_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct imx_chip *imx = to_imx_chip(chip);
	int ret;

	ret = clk_prepare_enable(imx->clk_per);
	if (ret)
		return ret;

	imx->set_enable(chip, true);

	return 0;
}

static void imx_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct imx_chip *imx = to_imx_chip(chip);

	imx->set_enable(chip, false);

	clk_disable_unprepare(imx->clk_per);
}

static struct pwm_ops imx_pwm_ops = {
	.enable = imx_pwm_enable,
	.disable = imx_pwm_disable,
	.config = imx_pwm_config,
	.owner = THIS_MODULE,
};

struct imx_pwm_data {
	int (*config)(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns);
	void (*set_enable)(struct pwm_chip *chip, bool enable);
};

static struct imx_pwm_data imx_pwm_data_v1 = {
	.config = imx_pwm_config_v1,
	.set_enable = imx_pwm_set_enable_v1,
};

static struct imx_pwm_data imx_pwm_data_v2 = {
	.config = imx_pwm_config_v2,
	.set_enable = imx_pwm_set_enable_v2,
};

static const struct of_device_id imx_pwm_dt_ids[] = {
	{ .compatible = "fsl,imx1-pwm", .data = &imx_pwm_data_v1, },
	{ .compatible = "fsl,imx27-pwm", .data = &imx_pwm_data_v2, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_pwm_dt_ids);

static int imx_pwm_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id =
			of_match_device(imx_pwm_dt_ids, &pdev->dev);
	const struct imx_pwm_data *data;
	struct imx_chip *imx;
	struct resource *r;
	int ret = 0;

	if (!of_id)
		return -ENODEV;

	imx = devm_kzalloc(&pdev->dev, sizeof(*imx), GFP_KERNEL);
	if (imx == NULL)
		return -ENOMEM;

	imx->clk_per = devm_clk_get(&pdev->dev, "per");
	if (IS_ERR(imx->clk_per)) {
		dev_err(&pdev->dev, "getting per clock failed with %ld\n",
				PTR_ERR(imx->clk_per));
		return PTR_ERR(imx->clk_per);
	}

	imx->clk_ipg = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(imx->clk_ipg)) {
		dev_err(&pdev->dev, "getting ipg clock failed with %ld\n",
				PTR_ERR(imx->clk_ipg));
		return PTR_ERR(imx->clk_ipg);
	}

	imx->chip.ops = &imx_pwm_ops;
	imx->chip.dev = &pdev->dev;
	imx->chip.base = -1;
	imx->chip.npwm = 1;
	imx->chip.can_sleep = true;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	imx->mmio_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(imx->mmio_base))
		return PTR_ERR(imx->mmio_base);

	data = of_id->data;
	imx->config = data->config;
	imx->set_enable = data->set_enable;

	ret = pwmchip_add(&imx->chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, imx);
	return 0;
}

static int imx_pwm_remove(struct platform_device *pdev)
{
	struct imx_chip *imx;

	imx = platform_get_drvdata(pdev);
	if (imx == NULL)
		return -ENODEV;

	return pwmchip_remove(&imx->chip);
}

static struct platform_driver imx_pwm_driver = {
	.driver		= {
		.name	= "imx-pwm",
		.owner = THIS_MODULE,
		.of_match_table = imx_pwm_dt_ids,
	},
	.probe		= imx_pwm_probe,
	.remove		= imx_pwm_remove,
};

module_platform_driver(imx_pwm_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
