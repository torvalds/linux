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
#include <linux/io.h>
#include <linux/pwm.h>
#include <mach/hardware.h>


/* i.MX1 and i.MX21 share the same PWM function block: */

#define MX1_PWMC    0x00   /* PWM Control Register */
#define MX1_PWMS    0x04   /* PWM Sample Register */
#define MX1_PWMP    0x08   /* PWM Period Register */


/* i.MX27, i.MX31, i.MX35 share the same PWM function block: */

#define MX3_PWMCR                 0x00    /* PWM Control Register */
#define MX3_PWMSAR                0x0C    /* PWM Sample Register */
#define MX3_PWMPR                 0x10    /* PWM Period Register */
#define MX3_PWMCR_PRESCALER(x)    (((x - 1) & 0xFFF) << 4)
#define MX3_PWMCR_DOZEEN                (1 << 24)
#define MX3_PWMCR_WAITEN                (1 << 23)
#define MX3_PWMCR_DBGEN			(1 << 22)
#define MX3_PWMCR_CLKSRC_IPG_HIGH (2 << 16)
#define MX3_PWMCR_CLKSRC_IPG      (1 << 16)
#define MX3_PWMCR_EN              (1 << 0)

struct imx_chip {
	struct clk	*clk;

	int		clk_enabled;
	void __iomem	*mmio_base;

	struct pwm_chip	chip;
};

#define to_imx_chip(chip)	container_of(chip, struct imx_chip, chip)

static int imx_pwm_config(struct pwm_chip *chip,
		struct pwm_device *pwm, int duty_ns, int period_ns)
{
	struct imx_chip *imx = to_imx_chip(chip);

	if (!(cpu_is_mx1() || cpu_is_mx21())) {
		unsigned long long c;
		unsigned long period_cycles, duty_cycles, prescale;
		u32 cr;

		c = clk_get_rate(imx->clk);
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
			MX3_PWMCR_DBGEN | MX3_PWMCR_EN;

		if (cpu_is_mx25())
			cr |= MX3_PWMCR_CLKSRC_IPG;
		else
			cr |= MX3_PWMCR_CLKSRC_IPG_HIGH;

		writel(cr, imx->mmio_base + MX3_PWMCR);
	} else if (cpu_is_mx1() || cpu_is_mx21()) {
		/* The PWM subsystem allows for exact frequencies. However,
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
	} else {
		BUG();
	}

	return 0;
}

static int imx_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct imx_chip *imx = to_imx_chip(chip);
	int rc = 0;

	if (!imx->clk_enabled) {
		rc = clk_prepare_enable(imx->clk);
		if (!rc)
			imx->clk_enabled = 1;
	}
	return rc;
}

static void imx_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct imx_chip *imx = to_imx_chip(chip);

	writel(0, imx->mmio_base + MX3_PWMCR);

	if (imx->clk_enabled) {
		clk_disable_unprepare(imx->clk);
		imx->clk_enabled = 0;
	}
}

static struct pwm_ops imx_pwm_ops = {
	.enable = imx_pwm_enable,
	.disable = imx_pwm_disable,
	.config = imx_pwm_config,
	.owner = THIS_MODULE,
};

static int __devinit imx_pwm_probe(struct platform_device *pdev)
{
	struct imx_chip *imx;
	struct resource *r;
	int ret = 0;

	imx = kzalloc(sizeof(*imx), GFP_KERNEL);
	if (imx == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	imx->clk = clk_get(&pdev->dev, "pwm");

	if (IS_ERR(imx->clk)) {
		ret = PTR_ERR(imx->clk);
		goto err_free;
	}

	imx->chip.ops = &imx_pwm_ops;
	imx->chip.dev = &pdev->dev;
	imx->chip.base = -1;
	imx->chip.npwm = 1;

	imx->clk_enabled = 0;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		ret = -ENODEV;
		goto err_free_clk;
	}

	r = request_mem_region(r->start, resource_size(r), pdev->name);
	if (r == NULL) {
		dev_err(&pdev->dev, "failed to request memory resource\n");
		ret = -EBUSY;
		goto err_free_clk;
	}

	imx->mmio_base = ioremap(r->start, resource_size(r));
	if (imx->mmio_base == NULL) {
		dev_err(&pdev->dev, "failed to ioremap() registers\n");
		ret = -ENODEV;
		goto err_free_mem;
	}

	ret = pwmchip_add(&imx->chip);
	if (ret < 0)
		goto err_iounmap;

	platform_set_drvdata(pdev, imx);
	return 0;

err_iounmap:
	iounmap(imx->mmio_base);
err_free_mem:
	release_mem_region(r->start, resource_size(r));
err_free_clk:
	clk_put(imx->clk);
err_free:
	kfree(imx);
	return ret;
}

static int __devexit imx_pwm_remove(struct platform_device *pdev)
{
	struct imx_chip *imx;
	struct resource *r;
	int ret;

	imx = platform_get_drvdata(pdev);
	if (imx == NULL)
		return -ENODEV;

	ret = pwmchip_remove(&imx->chip);
	if (ret)
		return ret;

	iounmap(imx->mmio_base);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(r->start, resource_size(r));

	clk_put(imx->clk);

	kfree(imx);
	return 0;
}

static struct platform_driver imx_pwm_driver = {
	.driver		= {
		.name	= "mxc_pwm",
	},
	.probe		= imx_pwm_probe,
	.remove		= __devexit_p(imx_pwm_remove),
};

static int __init imx_pwm_init(void)
{
	return platform_driver_register(&imx_pwm_driver);
}
arch_initcall(imx_pwm_init);

static void __exit imx_pwm_exit(void)
{
	platform_driver_unregister(&imx_pwm_driver);
}
module_exit(imx_pwm_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de>");
