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
 *		eric miao <eric.miao@marvell.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/of_device.h>

#include <asm/div64.h>

#define HAS_SECONDARY_PWM	0x10

static const struct platform_device_id pwm_id_table[] = {
	/*   PWM    has_secondary_pwm? */
	{ "pxa25x-pwm", 0 },
	{ "pxa27x-pwm", HAS_SECONDARY_PWM },
	{ "pxa168-pwm", 0 },
	{ "pxa910-pwm", 0 },
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

	return clk_prepare_enable(pc->clk);
}

static void pxa_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct pxa_pwm_chip *pc = to_pxa_pwm_chip(chip);

	clk_disable_unprepare(pc->clk);
}

static struct pwm_ops pxa_pwm_ops = {
	.config = pxa_pwm_config,
	.enable = pxa_pwm_enable,
	.disable = pxa_pwm_disable,
	.owner = THIS_MODULE,
};

#ifdef CONFIG_OF
/*
 * Device tree users must create one device instance for each pwm channel.
 * Hence we dispense with the HAS_SECONDARY_PWM and "tell" the original driver
 * code that this is a single channel pxa25x-pwm.  Currently all devices are
 * supported identically.
 */
static struct of_device_id pwm_of_match[] = {
	{ .compatible = "marvell,pxa250-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa270-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa168-pwm", .data = &pwm_id_table[0]},
	{ .compatible = "marvell,pxa910-pwm", .data = &pwm_id_table[0]},
	{ }
};
MODULE_DEVICE_TABLE(of, pwm_of_match);
#else
#define pwm_of_match NULL
#endif

static const struct platform_device_id *pxa_pwm_get_id_dt(struct device *dev)
{
	const struct of_device_id *id = of_match_device(pwm_of_match, dev);

	return id ? id->data : NULL;
}

static struct pwm_device *
pxa_pwm_of_xlate(struct pwm_chip *pc, const struct of_phandle_args *args)
{
	struct pwm_device *pwm;

	pwm = pwm_request_from_chip(pc, 0, NULL);
	if (IS_ERR(pwm))
		return pwm;

	pwm_set_period(pwm, args->args[0]);

	return pwm;
}

static int pwm_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct pxa_pwm_chip *pwm;
	struct resource *r;
	int ret = 0;

	if (IS_ENABLED(CONFIG_OF) && id == NULL)
		id = pxa_pwm_get_id_dt(&pdev->dev);

	if (id == NULL)
		return -EINVAL;

	pwm = devm_kzalloc(&pdev->dev, sizeof(*pwm), GFP_KERNEL);
	if (pwm == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	pwm->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(pwm->clk))
		return PTR_ERR(pwm->clk);

	pwm->chip.dev = &pdev->dev;
	pwm->chip.ops = &pxa_pwm_ops;
	pwm->chip.base = -1;
	pwm->chip.npwm = (id->driver_data & HAS_SECONDARY_PWM) ? 2 : 1;

	if (IS_ENABLED(CONFIG_OF)) {
		pwm->chip.of_xlate = pxa_pwm_of_xlate;
		pwm->chip.of_pwm_n_cells = 1;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pwm->mmio_base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(pwm->mmio_base))
		return PTR_ERR(pwm->mmio_base);

	ret = pwmchip_add(&pwm->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "pwmchip_add() failed: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pwm);
	return 0;
}

static int pwm_remove(struct platform_device *pdev)
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
		.of_match_table = pwm_of_match,
	},
	.probe		= pwm_probe,
	.remove		= pwm_remove,
	.id_table	= pwm_id_table,
};

module_platform_driver(pwm_driver);

MODULE_LICENSE("GPL v2");
