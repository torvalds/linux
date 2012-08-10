/*
 * drivers/pwm/pwm-vt8500.c
 *
 *  Copyright (C) 2010 Alexey Charkov <alchark@gmail.com>
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

#include <asm/div64.h>

#define VT8500_NR_PWMS 4

struct vt8500_chip {
	struct pwm_chip chip;
	void __iomem *base;
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

	c = 25000000/2; /* wild guess --- need to implement clocks */
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
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);

	pwm_busy_wait(vt8500->base + 0x40 + pwm->hwpwm, (1 << 0));
	writel(5, vt8500->base + (pwm->hwpwm << 4));
	return 0;
}

static void vt8500_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct vt8500_chip *vt8500 = to_vt8500_chip(chip);

	pwm_busy_wait(vt8500->base + 0x40 + pwm->hwpwm, (1 << 0));
	writel(0, vt8500->base + (pwm->hwpwm << 4));
}

static struct pwm_ops vt8500_pwm_ops = {
	.enable = vt8500_pwm_enable,
	.disable = vt8500_pwm_disable,
	.config = vt8500_pwm_config,
	.owner = THIS_MODULE,
};

static int __devinit pwm_probe(struct platform_device *pdev)
{
	struct vt8500_chip *chip;
	struct resource *r;
	int ret;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		dev_err(&pdev->dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	chip->chip.dev = &pdev->dev;
	chip->chip.ops = &vt8500_pwm_ops;
	chip->chip.base = -1;
	chip->chip.npwm = VT8500_NR_PWMS;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (r == NULL) {
		dev_err(&pdev->dev, "no memory resource defined\n");
		return -ENODEV;
	}

	chip->base = devm_request_and_ioremap(&pdev->dev, r);
	if (chip->base == NULL)
		return -EADDRNOTAVAIL;

	ret = pwmchip_add(&chip->chip);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, chip);
	return ret;
}

static int __devexit pwm_remove(struct platform_device *pdev)
{
	struct vt8500_chip *chip;

	chip = platform_get_drvdata(pdev);
	if (chip == NULL)
		return -ENODEV;

	return pwmchip_remove(&chip->chip);
}

static struct platform_driver pwm_driver = {
	.driver		= {
		.name	= "vt8500-pwm",
		.owner	= THIS_MODULE,
	},
	.probe		= pwm_probe,
	.remove		= __devexit_p(pwm_remove),
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

MODULE_LICENSE("GPL");
