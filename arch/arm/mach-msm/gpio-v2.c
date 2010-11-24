/* Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <mach/msm_iomap.h>
#include "gpiomux.h"

/* Bits of interest in the GPIO_IN_OUT register.
 */
enum {
	GPIO_IN_BIT  = 0,
	GPIO_OUT_BIT = 1
};

/* Bits of interest in the GPIO_CFG register.
 */
enum {
	GPIO_OE_BIT = 9,
};

#define GPIO_CONFIG(gpio)         (MSM_TLMM_BASE + 0x1000 + (0x10 * (gpio)))
#define GPIO_IN_OUT(gpio)         (MSM_TLMM_BASE + 0x1004 + (0x10 * (gpio)))

static DEFINE_SPINLOCK(tlmm_lock);

static int msm_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	return readl(GPIO_IN_OUT(offset)) & BIT(GPIO_IN_BIT);
}

static void msm_gpio_set(struct gpio_chip *chip, unsigned offset, int val)
{
	writel(val ? BIT(GPIO_OUT_BIT) : 0, GPIO_IN_OUT(offset));
}

static int msm_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	writel(readl(GPIO_CONFIG(offset)) & ~BIT(GPIO_OE_BIT),
		GPIO_CONFIG(offset));
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
	return 0;
}

static int msm_gpio_direction_output(struct gpio_chip *chip,
				unsigned offset,
				int val)
{
	unsigned long irq_flags;

	spin_lock_irqsave(&tlmm_lock, irq_flags);
	msm_gpio_set(chip, offset, val);
	writel(readl(GPIO_CONFIG(offset)) | BIT(GPIO_OE_BIT),
		GPIO_CONFIG(offset));
	spin_unlock_irqrestore(&tlmm_lock, irq_flags);
	return 0;
}

static int msm_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	return msm_gpiomux_get(chip->base + offset);
}

static void msm_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	msm_gpiomux_put(chip->base + offset);
}

static struct gpio_chip msm_gpio = {
	.base             = 0,
	.ngpio            = NR_GPIO_IRQS,
	.direction_input  = msm_gpio_direction_input,
	.direction_output = msm_gpio_direction_output,
	.get              = msm_gpio_get,
	.set              = msm_gpio_set,
	.request          = msm_gpio_request,
	.free             = msm_gpio_free,
};

static int __devinit msm_gpio_probe(struct platform_device *dev)
{
	int ret;

	msm_gpio.label = dev->name;
	ret = gpiochip_add(&msm_gpio);

	return ret;
}

static int __devexit msm_gpio_remove(struct platform_device *dev)
{
	int ret = gpiochip_remove(&msm_gpio);

	if (ret < 0)
		return ret;

	set_irq_handler(TLMM_SCSS_SUMMARY_IRQ, NULL);

	return 0;
}

static struct platform_driver msm_gpio_driver = {
	.probe = msm_gpio_probe,
	.remove = __devexit_p(msm_gpio_remove),
	.driver = {
		.name = "msmgpio",
		.owner = THIS_MODULE,
	},
};

static struct platform_device msm_device_gpio = {
	.name = "msmgpio",
	.id   = -1,
};

static int __init msm_gpio_init(void)
{
	int rc;

	rc = platform_driver_register(&msm_gpio_driver);
	if (!rc) {
		rc = platform_device_register(&msm_device_gpio);
		if (rc)
			platform_driver_unregister(&msm_gpio_driver);
	}

	return rc;
}

static void __exit msm_gpio_exit(void)
{
	platform_device_unregister(&msm_device_gpio);
	platform_driver_unregister(&msm_gpio_driver);
}

postcore_initcall(msm_gpio_init);
module_exit(msm_gpio_exit);

MODULE_AUTHOR("Gregory Bean <gbean@codeaurora.org>");
MODULE_DESCRIPTION("Driver for Qualcomm MSM TLMMv2 SoC GPIOs");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:msmgpio");
