/*
 * drivers/pwm/pwm-vt8500.c
 *
 * Copyright (C) 2012 Tony Prisk <linux@prisktech.co.nz>
 * Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <asm/div64.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>

/*
 * SoC architecture allocates register space for 4 PWMs but only
 * 2 are currently implemented.
 */
#define VT8500_NR_PWMS	2

struct vt8500_chip {
	struct pwm_chip chip;
	void __iomem *base;
	struct clk *clk;
};

#define to_vt8500_chip(chip)	container_of(chip, struct vt8500_chip, chip)

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)
static inline void pwm_busy_wait(void __iomem *reg, u8 bitmask)
{
	int loops = msecs_to_loops(10);
	while ((readb(reg) & bitmask) && --loops)
		cpu_relax();

	if (unlikely(!loops))
		pr_warn("Waiting for status bits 0x%x to clear timed out\n",
			   bitmask);
}

static int vt8500_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
		int duty_ns, int period_ns)
{
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);
	unsigned long long c;
	unsigned long period_cycles, prescale, pv, dc;

	c = clk_get_rate(vt8500->clk);
	c = c * period_ns;
	do_div(c, 1000000000);
	period_cycles = c;

	if (period_cycles < 1)
		period_cycles = 1;
	prescale = (period_cycles - 1) / 4096;
	pv = period_cycles / (prescale + 1) - 1;
	if (pv > 4095)
		pv = 4095;

	if (prescale > 1023)
		return -EINVAL;

	c = (unsigned long long)pv * duty_ns;
	do_div(c, period_ns);
	dc = c;

	pwm_busy_wait(vt8500->base + 0x40 + pwm->hwpwm, (1 << 1));
	writel(prescale, vt8500->base + 0x4 + (pwm->hwpwm << 4));

	pwm_busy_wait(vt8500->base + 0x40 + pwm->hwpwm, (1 << 2));
	writel(pv, vt8500->base + 0x8 + (pwm->hwpwm << 4));

	pwm_busy_wait(vt8500->base + 0x40 + pwm->hwpwm, (1 << 3));
	writel(dc, vt8500->base + 0xc + (pwm->hwpwm << 4));

	return 0;
}

static int vt8500_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	int err;
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);

	err = clk_enable(vt8500->clk);
	if (err < 0) {
		dev_err(chip->dev, "failed to enable clock\n");
		return err;
	};

	pwm_busy_wait(vt8500->base + 0x40 + pwm->hwpwm, (1 << 0));
	writel(5, vt8500->base + (pwm->hwpwm << 4));
	return 0;
}

static void vt8500_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);

	pwm_busy_wait(vt8500->base + 0x40 + pwm->hwpwm, (1 << 0));
	writel(0, vt8500->base + (pwm->hwpwm << 4));

	clk_disable(vt8500->clk);
}

static struct pwm_ops vt8500_pwm_ops = {
	.enable = vt8500_pwm_enable,
	.disable = vt8500_pwm_disable,
	.config = vt8500_pwm_config,
	.owner = THIS_MODULE,
};

static const struct of_device_id vt8500_pwm_dt_ids[] = {
	{ .compatible = "via,vt8500-pwm", },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, vt8500_pwm_dt_ids);

static int vt8500_pwm_probe(struct platform_device *pdev)
{
	struct vt8500_chip *chip;
	struct resource *r;
	struct device_node *np = pdev->dev.of_node;
	int ret;

	if (!np) {
		dev_err(&pdev->dev, "invalid devicetree node\n");
		return -EINVAL;
	}

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	chip->chip.dev = &pdev->dev;
	chip->chip.ops = &vt8500_pwm_ops;
	chip->chip.base = -1;
	chip->chip.npwm = VT8500_NR_PWMS;

	chip->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(chip->clk)) {
		dev_err(&pdev->dev, "clock source not specified\n");
		return PTR_ERR(chip->clk);
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	chip->base = devm_request_and_ioremap(&pdev->dev, r);
	if (!chip->base)
		return -EADDRNOTAVAIL;

	ret = clk_prepare(chip->clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to prepare clock\n");
		return ret;
	}

	ret = pwmchip_add(&chip->chip);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add PWM chip\n");
		return ret;
	}

	platform_set_drvdata(pdev, chip);
	return ret;
}

static int vt8500_pwm_remove(struct platform_device *pdev)
{
	struct vt8500_chip *chip;

	chip = platform_get_drvdata(pdev);
	if (chip == NULL)
		return -ENODEV;

	clk_unprepare(chip->clk);

	return pwmchip_remove(&chip->chip);
}

static struct platform_driver vt8500_pwm_driver = {
	.probe		= vt8500_pwm_probe,
	.remove		= vt8500_pwm_remove,
	.driver		= {
		.name	= "vt8500-pwm",
		.owner	= THIS_MODULE,
		.of_match_table = vt8500_pwm_dt_ids,
	},
};
module_platform_driver(vt8500_pwm_driver);

MODULE_DESCRIPTION("VT8500 PWM Driver");
MODULE_AUTHOR("Tony Prisk <linux@prisktech.co.nz>");
MODULE_LICENSE("GPL v2");
