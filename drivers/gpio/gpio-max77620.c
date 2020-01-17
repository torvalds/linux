// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAXIM MAX77620 GPIO driver
 *
 * Copyright (c) 2016, NVIDIA CORPORATION.  All rights reserved.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/mfd/max77620.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define GPIO_REG_ADDR(offset) (MAX77620_REG_GPIO0 + offset)

struct max77620_gpio {
	struct gpio_chip	gpio_chip;
	struct regmap		*rmap;
	struct device		*dev;
};

static const struct regmap_irq max77620_gpio_irqs[] = {
	[0] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE0,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 0,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	[1] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE1,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 1,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	[2] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE2,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 2,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	[3] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE3,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 3,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	[4] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE4,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 4,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	[5] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE5,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 5,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	[6] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE6,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 6,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
	[7] = {
		.reg_offset = 0,
		.mask = MAX77620_IRQ_LVL2_GPIO_EDGE7,
		.type = {
			.type_rising_val = MAX77620_CNFG_GPIO_INT_RISING,
			.type_falling_val = MAX77620_CNFG_GPIO_INT_FALLING,
			.type_reg_mask = MAX77620_CNFG_GPIO_INT_MASK,
			.type_reg_offset = 7,
			.types_supported = IRQ_TYPE_EDGE_BOTH,
		},
	},
};

static const struct regmap_irq_chip max77620_gpio_irq_chip = {
	.name = "max77620-gpio",
	.irqs = max77620_gpio_irqs,
	.num_irqs = ARRAY_SIZE(max77620_gpio_irqs),
	.num_regs = 1,
	.num_type_reg = 8,
	.irq_reg_stride = 1,
	.type_reg_stride = 1,
	.status_base = MAX77620_REG_IRQ_LVL2_GPIO,
	.type_base = MAX77620_REG_GPIO0,
};

static int max77620_gpio_dir_input(struct gpio_chip *gc, unsigned int offset)
{
	struct max77620_gpio *mgpio = gpiochip_get_data(gc);
	int ret;

	ret = regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
				 MAX77620_CNFG_GPIO_DIR_MASK,
				 MAX77620_CNFG_GPIO_DIR_INPUT);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIOx dir update failed: %d\n", ret);

	return ret;
}

static int max77620_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	struct max77620_gpio *mgpio = gpiochip_get_data(gc);
	unsigned int val;
	int ret;

	ret = regmap_read(mgpio->rmap, GPIO_REG_ADDR(offset), &val);
	if (ret < 0) {
		dev_err(mgpio->dev, "CNFG_GPIOx read failed: %d\n", ret);
		return ret;
	}

	if  (val & MAX77620_CNFG_GPIO_DIR_MASK)
		return !!(val & MAX77620_CNFG_GPIO_INPUT_VAL_MASK);
	else
		return !!(val & MAX77620_CNFG_GPIO_OUTPUT_VAL_MASK);
}

static int max77620_gpio_dir_output(struct gpio_chip *gc, unsigned int offset,
				    int value)
{
	struct max77620_gpio *mgpio = gpiochip_get_data(gc);
	u8 val;
	int ret;

	val = (value) ? MAX77620_CNFG_GPIO_OUTPUT_VAL_HIGH :
				MAX77620_CNFG_GPIO_OUTPUT_VAL_LOW;

	ret = regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
				 MAX77620_CNFG_GPIO_OUTPUT_VAL_MASK, val);
	if (ret < 0) {
		dev_err(mgpio->dev, "CNFG_GPIOx val update failed: %d\n", ret);
		return ret;
	}

	ret = regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
				 MAX77620_CNFG_GPIO_DIR_MASK,
				 MAX77620_CNFG_GPIO_DIR_OUTPUT);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIOx dir update failed: %d\n", ret);

	return ret;
}

static int max77620_gpio_set_debounce(struct max77620_gpio *mgpio,
				      unsigned int offset,
				      unsigned int debounce)
{
	u8 val;
	int ret;

	switch (debounce) {
	case 0:
		val = MAX77620_CNFG_GPIO_DBNC_None;
		break;
	case 1 ... 8000:
		val = MAX77620_CNFG_GPIO_DBNC_8ms;
		break;
	case 8001 ... 16000:
		val = MAX77620_CNFG_GPIO_DBNC_16ms;
		break;
	case 16001 ... 32000:
		val = MAX77620_CNFG_GPIO_DBNC_32ms;
		break;
	default:
		dev_err(mgpio->dev, "Illegal value %u\n", debounce);
		return -EINVAL;
	}

	ret = regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
				 MAX77620_CNFG_GPIO_DBNC_MASK, val);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIOx_DBNC update failed: %d\n", ret);

	return ret;
}

static void max77620_gpio_set(struct gpio_chip *gc, unsigned int offset,
			      int value)
{
	struct max77620_gpio *mgpio = gpiochip_get_data(gc);
	u8 val;
	int ret;

	val = (value) ? MAX77620_CNFG_GPIO_OUTPUT_VAL_HIGH :
				MAX77620_CNFG_GPIO_OUTPUT_VAL_LOW;

	ret = regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
				 MAX77620_CNFG_GPIO_OUTPUT_VAL_MASK, val);
	if (ret < 0)
		dev_err(mgpio->dev, "CNFG_GPIO_OUT update failed: %d\n", ret);
}

static int max77620_gpio_set_config(struct gpio_chip *gc, unsigned int offset,
				    unsigned long config)
{
	struct max77620_gpio *mgpio = gpiochip_get_data(gc);

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
					  MAX77620_CNFG_GPIO_DRV_MASK,
					  MAX77620_CNFG_GPIO_DRV_OPENDRAIN);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
					  MAX77620_CNFG_GPIO_DRV_MASK,
					  MAX77620_CNFG_GPIO_DRV_PUSHPULL);
	case PIN_CONFIG_INPUT_DEBOUNCE:
		return max77620_gpio_set_debounce(mgpio, offset,
			pinconf_to_config_argument(config));
	default:
		break;
	}

	return -ENOTSUPP;
}

static int max77620_gpio_to_irq(struct gpio_chip *gc, unsigned int offset)
{
	struct max77620_gpio *mgpio = gpiochip_get_data(gc);
	struct max77620_chip *chip = dev_get_drvdata(mgpio->dev->parent);

	return regmap_irq_get_virq(chip->gpio_irq_data, offset);
}

static int max77620_gpio_probe(struct platform_device *pdev)
{
	struct max77620_chip *chip =  dev_get_drvdata(pdev->dev.parent);
	struct max77620_gpio *mgpio;
	int gpio_irq;
	int ret;

	gpio_irq = platform_get_irq(pdev, 0);
	if (gpio_irq <= 0)
		return -ENODEV;

	mgpio = devm_kzalloc(&pdev->dev, sizeof(*mgpio), GFP_KERNEL);
	if (!mgpio)
		return -ENOMEM;

	mgpio->rmap = chip->rmap;
	mgpio->dev = &pdev->dev;

	mgpio->gpio_chip.label = pdev->name;
	mgpio->gpio_chip.parent = &pdev->dev;
	mgpio->gpio_chip.direction_input = max77620_gpio_dir_input;
	mgpio->gpio_chip.get = max77620_gpio_get;
	mgpio->gpio_chip.direction_output = max77620_gpio_dir_output;
	mgpio->gpio_chip.set = max77620_gpio_set;
	mgpio->gpio_chip.set_config = max77620_gpio_set_config;
	mgpio->gpio_chip.to_irq = max77620_gpio_to_irq;
	mgpio->gpio_chip.ngpio = MAX77620_GPIO_NR;
	mgpio->gpio_chip.can_sleep = 1;
	mgpio->gpio_chip.base = -1;
#ifdef CONFIG_OF_GPIO
	mgpio->gpio_chip.of_node = pdev->dev.parent->of_node;
#endif

	platform_set_drvdata(pdev, mgpio);

	ret = devm_gpiochip_add_data(&pdev->dev, &mgpio->gpio_chip, mgpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpio_init: Failed to add max77620_gpio\n");
		return ret;
	}

	ret = devm_regmap_add_irq_chip(&pdev->dev, chip->rmap, gpio_irq,
				       IRQF_ONESHOT, -1,
				       &max77620_gpio_irq_chip,
				       &chip->gpio_irq_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add gpio irq_chip %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct platform_device_id max77620_gpio_devtype[] = {
	{ .name = "max77620-gpio", },
	{ .name = "max20024-gpio", },
	{},
};
MODULE_DEVICE_TABLE(platform, max77620_gpio_devtype);

static struct platform_driver max77620_gpio_driver = {
	.driver.name	= "max77620-gpio",
	.probe		= max77620_gpio_probe,
	.id_table	= max77620_gpio_devtype,
};

module_platform_driver(max77620_gpio_driver);

MODULE_DESCRIPTION("GPIO interface for MAX77620 and MAX20024 PMIC");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_AUTHOR("Chaitanya Bandi <bandik@nvidia.com>");
MODULE_ALIAS("platform:max77620-gpio");
MODULE_LICENSE("GPL v2");
