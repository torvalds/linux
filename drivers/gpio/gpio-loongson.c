// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Loongson-2F/3A/3B GPIO Support
 *
 *  Copyright (c) 2008 Richard Liu,  STMicroelectronics	 <richard.liu@st.com>
 *  Copyright (c) 2008-2010 Arnaud Patard <apatard@mandriva.com>
 *  Copyright (c) 2013 Hongbing Hu <huhb@lemote.com>
 *  Copyright (c) 2014 Huacai Chen <chenhc@lemote.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/bitops.h>
#include <asm/types.h>
#include <loongson.h>

#define STLS2F_N_GPIO		4
#define STLS3A_N_GPIO		16

#ifdef CONFIG_CPU_LOONGSON64
#define LOONGSON_N_GPIO	STLS3A_N_GPIO
#else
#define LOONGSON_N_GPIO	STLS2F_N_GPIO
#endif

/*
 * Offset into the register where we read lines, we write them from offset 0.
 * This offset is the only thing that stand between us and using
 * GPIO_GENERIC.
 */
#define LOONGSON_GPIO_IN_OFFSET	16

static DEFINE_SPINLOCK(gpio_lock);

static int loongson_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	u32 val;

	spin_lock(&gpio_lock);
	val = LOONGSON_GPIODATA;
	spin_unlock(&gpio_lock);

	return !!(val & BIT(gpio + LOONGSON_GPIO_IN_OFFSET));
}

static void loongson_gpio_set_value(struct gpio_chip *chip,
		unsigned gpio, int value)
{
	u32 val;

	spin_lock(&gpio_lock);
	val = LOONGSON_GPIODATA;
	if (value)
		val |= BIT(gpio);
	else
		val &= ~BIT(gpio);
	LOONGSON_GPIODATA = val;
	spin_unlock(&gpio_lock);
}

static int loongson_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	u32 temp;

	spin_lock(&gpio_lock);
	temp = LOONGSON_GPIOIE;
	temp |= BIT(gpio);
	LOONGSON_GPIOIE = temp;
	spin_unlock(&gpio_lock);

	return 0;
}

static int loongson_gpio_direction_output(struct gpio_chip *chip,
		unsigned gpio, int level)
{
	u32 temp;

	loongson_gpio_set_value(chip, gpio, level);
	spin_lock(&gpio_lock);
	temp = LOONGSON_GPIOIE;
	temp &= ~BIT(gpio);
	LOONGSON_GPIOIE = temp;
	spin_unlock(&gpio_lock);

	return 0;
}

static int loongson_gpio_probe(struct platform_device *pdev)
{
	struct gpio_chip *gc;
	struct device *dev = &pdev->dev;

	gc = devm_kzalloc(dev, sizeof(*gc), GFP_KERNEL);
	if (!gc)
		return -ENOMEM;

	gc->label = "loongson-gpio-chip";
	gc->base = 0;
	gc->ngpio = LOONGSON_N_GPIO;
	gc->get = loongson_gpio_get_value;
	gc->set = loongson_gpio_set_value;
	gc->direction_input = loongson_gpio_direction_input;
	gc->direction_output = loongson_gpio_direction_output;

	return gpiochip_add_data(gc, NULL);
}

static struct platform_driver loongson_gpio_driver = {
	.driver = {
		.name = "loongson-gpio",
	},
	.probe = loongson_gpio_probe,
};

static int __init loongson_gpio_setup(void)
{
	struct platform_device *pdev;
	int ret;

	ret = platform_driver_register(&loongson_gpio_driver);
	if (ret) {
		pr_err("error registering loongson GPIO driver\n");
		return ret;
	}

	pdev = platform_device_register_simple("loongson-gpio", -1, NULL, 0);
	return PTR_ERR_OR_ZERO(pdev);
}
postcore_initcall(loongson_gpio_setup);
