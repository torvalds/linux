/*
 * drivers/gpio/gpio-rk8xx.c
 * Driver for Rockchip RK8xx PMIC GPIO
 *
 * Copyright (C) 2017, Rockchip Technology Co., Ltd.
 * Author: Chen Jianhong <chenjh@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mfd/rk808.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define RK805_GPIO_REG		RK805_OUT_REG
#define RK805_OUT0_VALMASK	BIT(0)
#define RK805_OUT1_VALMASK	BIT(1)

#define RK816_FUN_MASK		BIT(2)
#define RK816_OUT_VALMASK	BIT(3)
#define RK816_DIR_MASK		BIT(4)

struct rk8xx_gpio_reg {
	u8 reg;
	u8 dir_msk;
	u8 val_msk;
	u8 fun_msk;
};

struct rk8xx_gpio_info {
	struct rk808 *rk8xx;
	struct gpio_chip gpio_chip;
	struct rk8xx_gpio_reg *gpio_reg;
	int gpio_nr;
};

static int rk8xx_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	int err;
	struct rk8xx_gpio_info *gi = dev_get_drvdata(chip->parent);

	/* iomux */
	if (gi->gpio_reg[offset].fun_msk) {
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].fun_msk,
					 gi->gpio_reg[offset].fun_msk);
		if (err) {
			dev_err(chip->parent, "set gpio%d func fail: %d\n",
				offset, err);
			return err;
		}
	}

	/* direction */
	if (gi->gpio_reg[offset].dir_msk) {
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].dir_msk,
					 0);
		if (err) {
			dev_err(chip->parent, "set gpio%d input fail: %d\n",
				offset, err);
			return err;
		}
	}

	return 0;
}

static int rk8xx_gpio_direction_output(struct gpio_chip *chip,
				       unsigned offset, int value)
{
	int err;
	struct rk8xx_gpio_info *gi = dev_get_drvdata(chip->parent);

	/* iomux */
	if (gi->gpio_reg[offset].fun_msk) {
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].fun_msk,
					 gi->gpio_reg[offset].fun_msk);
		if (err) {
			dev_err(chip->parent, "set gpio%d func fail: %d\n",
				offset, err);
			return err;
		}
	}

	/* direction */
	if (gi->gpio_reg[offset].dir_msk) {
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].dir_msk,
					 gi->gpio_reg[offset].dir_msk);
		if (err) {
			dev_err(chip->parent,
				"set gpio%d output fail: %d\n", offset, err);
			return err;
		}
	}

	if (value)
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].val_msk,
					 gi->gpio_reg[offset].val_msk);
	else
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].val_msk,
					 0);
	if (err) {
		dev_err(chip->parent, "set gpio%d value fail: %d\n", offset, err);
		return err;
	}

	return 0;
}

static int rk8xx_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
	int err;
	unsigned int val;
	struct rk8xx_gpio_info *gi = dev_get_drvdata(chip->parent);

	err = regmap_read(gi->rk8xx->regmap, gi->gpio_reg[offset].reg, &val);
	if (err) {
		dev_err(chip->parent, "get gpio%d value fail: %d\n", offset, err);
		return err;
	}

	return (val & gi->gpio_reg[offset].val_msk) ? 1 : 0;
}

static void rk8xx_gpio_set_value(struct gpio_chip *chip,
				 unsigned offset, int value)
{
	int err;
	struct rk8xx_gpio_info *gi = dev_get_drvdata(chip->parent);

	if (value)
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].val_msk,
					 gi->gpio_reg[offset].val_msk);
	else
		err = regmap_update_bits(gi->rk8xx->regmap,
					 gi->gpio_reg[offset].reg,
					 gi->gpio_reg[offset].val_msk,
					 0);
	if (err)
		dev_err(chip->parent, "set gpio%d value fail: %d\n", offset, err);
}

/* rk805: two gpio: output only */
static struct rk8xx_gpio_reg rk805_gpio_reg[] = {
	{
		.reg = RK805_GPIO_REG,
		.val_msk = RK805_OUT0_VALMASK,
	},
	{
		.reg = RK805_GPIO_REG,
		.val_msk = RK805_OUT1_VALMASK,
	},
};

static struct rk8xx_gpio_reg rk816_gpio_reg[] = {
	{
		.reg = RK816_GPIO_IO_POL_REG,
		.dir_msk = RK816_DIR_MASK,
		.val_msk = RK816_OUT_VALMASK,
		.fun_msk = RK816_FUN_MASK,
	},
};

static int rk8xx_gpio_probe(struct platform_device *pdev)
{
	struct rk808 *rk8xx = dev_get_drvdata(pdev->dev.parent);
	struct rk8xx_gpio_info *gi;
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(pdev->dev.parent->of_node, "gpio");
	if (np) {
		if (!of_device_is_available(np)) {
			dev_info(&pdev->dev, "device is disabled\n");
			return -EINVAL;
		}
	}

	gi = devm_kzalloc(&pdev->dev, sizeof(*gi), GFP_KERNEL);
	if (!gi)
		return -ENOMEM;

	switch (rk8xx->variant) {
	case RK805_ID:
		gi->gpio_reg = rk805_gpio_reg;
		gi->gpio_nr = ARRAY_SIZE(rk805_gpio_reg);
		break;
	case RK816_ID:
		gi->gpio_reg = rk816_gpio_reg;
		gi->gpio_nr = ARRAY_SIZE(rk816_gpio_reg);
		break;
	default:
		dev_err(&pdev->dev, "unsupported RK8XX ID %lu\n",
			rk8xx->variant);
		return -EINVAL;
	}

	gi->rk8xx = rk8xx;
	gi->gpio_chip.base = -1;
	gi->gpio_chip.can_sleep = true;
	gi->gpio_chip.parent = &pdev->dev;
	gi->gpio_chip.ngpio = gi->gpio_nr;
	gi->gpio_chip.label = pdev->name;
	gi->gpio_chip.get = rk8xx_gpio_get_value;
	gi->gpio_chip.set = rk8xx_gpio_set_value;
	gi->gpio_chip.direction_input = rk8xx_gpio_direction_input;
	gi->gpio_chip.direction_output = rk8xx_gpio_direction_output;
	gi->gpio_chip.owner = THIS_MODULE;
#ifdef CONFIG_OF_GPIO
	gi->gpio_chip.of_node = rk8xx->i2c->dev.of_node;
#endif
	platform_set_drvdata(pdev, gi);

	ret = gpiochip_add(&gi->gpio_chip);
	if (ret) {
		dev_err(&pdev->dev, "register rk8xx gpiochip fail: %d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "register rk%lx gpio successful\n",
		 rk8xx->variant);

	return ret;
}

static int rk8xx_gpio_remove(struct platform_device *pdev)
{
	struct rk8xx_gpio_info *gi = platform_get_drvdata(pdev);

	gpiochip_remove(&gi->gpio_chip);

	return 0;
}

static struct platform_driver rk8xx_gpio_driver = {
	.probe = rk8xx_gpio_probe,
	.remove = rk8xx_gpio_remove,
	.driver = {
		.name = "rk8xx-gpio",
		.owner = THIS_MODULE,
	},
};

static int rk8xx_gpio_init(void)
{
	return platform_driver_register(&rk8xx_gpio_driver);
}
subsys_initcall(rk8xx_gpio_init);

static void rk8xx_gpio_exit(void)
{
	platform_driver_unregister(&rk8xx_gpio_driver);
}
module_exit(rk8xx_gpio_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("RK8xx GPIO driver");
MODULE_AUTHOR("Chen Jianhong <chenjh@rock-chips.com>");
