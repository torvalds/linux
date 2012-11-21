/*
 * drivers/pwm/pwm-pxa.c
 *
 * simple driver for PWM (Pulse Width Modulator) controller
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * 2008-02-13	initial version
 * 		eric miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>

#include <asm/div64.h>

#define HAS_SECONDARY_PWM	0x10
#define PWM_ID_BASE(d)		((d) & 0xf)

static const struct platform_device_id pwm_id_table[] = {
	/*   PWM    has_secondary_pwm? */
	{ "pxa25x-pwm", 0 },
	{ "pxa27x-pwm", 0 | HAS_SECONDARY_PWM },
	{ "pxa168-pwm", 1 },
	{ "pxa910-pwm", 1 },
	{ },
};
MODULE_DEVICE_TABLE(platform, pwm_id_table);

/* PWM registers and bits definitions */
#define PWMCR		(0x00)
#define PWMDCR		(0x04)
#define PWMPCR		(0x08)

#define PWMCR_SD	(1 << 6)
#define PWMDCR_FD	(1 << 10)

struct pxa_pwm_chip {
	struct pwm_chip	chip;
	struct device	*dev;

	struct clk	*clk;
	int		clk_enabled;
	void __iomem	*mmio_base;
};

static inline struct pxa_pwm_chip *to_pxa_pwm_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct pxa_pwm_chip, chip);
}

/*
 * period_ns = 10^9 * (PRESCALE + 1) * (PV + 1) / PWM_CLK_RATE
 * duty_ns   = 10^9 * (PRESCALE + 1) * DC / PWM_CLK_RATE
 */
static int pxa_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			  int duty_ns, int period_ns)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;
	unsigned long offset;
	int rc;

	offset = pwm->hwpwm ? 0x10 : 0;

	c = clk_get_rate(pc->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	if (period_cycles < 1)
		period_cycles = 1;
	prescale = (period_cycles - 1) / 1024;
	pv = period_cycles / (prescale + 1) - 1;

	if (prescale > 63)
		return -EINVAL;

	if (duty_ns == period_ns)
		dc = PWMDCR_FD;
	else
		dc = (pv + 1) * duty_ns / period_ns;

	/* NOTE: the clock to PWM has to be enabled first
	 * before writing to the registers
	 */
	rc = clk_prepare_enable(pc->clk);
	if (rc < 0)
		return rc;

	writel(prescale, pc->mmio_base + offset + PWMCR);
	writel(dc, pc->mmio_base + offset + PWMDCR);
	writel(pv, pc->mmio_base + offset + PWMPCR);

	clk_disable_unprepare(pc->clk);
	return 0;
}

static int pxa_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);
	int rc = 0;

	if (!pc->clk_enabled) {
		rc = clk_prepare_enable(pc->clk);
		if (!rc)
			pc->clk_enabled++;
	}
	return rc;
}

static void pxa_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);

	if (pc->clk_enabled) {
		clk_disable_unprepare(pc->clk);
		pc->clk_enabled--;
	}
}

static struct pwm_ops pxa_pwm_ops = {
	.config = pxa_pwm_config,
	.enable = pxa_pwm_enable,
	.disable = pxa_pwm_disable,
	.owner = THIS_MODULE,
};

static int __devinit pwm_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct pxa_pwm_chip *pwm;
	struct resource *r;
	int ret = 0;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pwm->clk))
		return PTR_ERR(pwm->clk);

	pwm->clk_enabled = 0;

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &pxa_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = (id->driver_data & HAS_SECONDARY_PWM) ? 2 : 1;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	pwm->mmio_base = devm_request_and_ioremap(&pdev->dev, r);
	if (pwm->mmio_base == NULL)
		return -EADDRNOTAVAIL;

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pwm);
	return 0;
}

static int __devexit pwm_remove(struct platform_device *pdev)
{
	struct pxa_pwm_chip *chip;

	chip = platform_get_drvdata(pdev);
	if (chip == NULL)
		return -ENODEV;

	return pwmchip_remove(&chip->chip);
}

static struct platform_driver pwm_driver = {
	.driver		= {
		.name	= "pxa25x-pwm",
		.owner	= THIS_MODULE,
	},
	.probe		= pwm_probe,
	.remove		= __devexit_p(pwm_remove),
	.id_table	= pwm_id_table,
};

static int __init pwm_init(void)
{
	return platform_driver_register(&pwm_driver);
}
arch_initcall(pwm_init);

static void __exit pwm_exit(void)
{
	platform_driver_unregister(&pwm_driver);
}
module_exit(pwm_exit);

MODULE_LICENSE("GPL v2");
