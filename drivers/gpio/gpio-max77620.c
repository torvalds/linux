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
	struct mutex		buslock; /* irq_bus_lock */
	unsigned int		irq_type[MAX77620_GPIO_NR];
	bool			irq_enabled[MAX77620_GPIO_NR];
};

static irqreturn_t max77620_gpio_irqhandler(int irq, void *data)
{
	struct max77620_gpio *gpio = data;
	unsigned int value, offset;
	unsigned long pending;
	int err;

	err = regmap_read(gpio->rmap, MAX77620_REG_IRQ_LVL2_GPIO, &value);
	if (err < 0) {
		dev_err(gpio->dev, "REG_IRQ_LVL2_GPIO read failed: %d\n", err);
		return IRQ_NONE;
	}

	pending = value;

	for_each_set_bit(offset, &pending, MAX77620_GPIO_NR) {
		unsigned int virq;

		virq = irq_find_mapping(gpio->gpio_chip.irq.domain, offset);
		handle_nested_irq(virq);
	}

	return IRQ_HANDLED;
}

static void max77620_gpio_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct max77620_gpio *gpio = gpiochip_get_data(chip);

	gpio->irq_enabled[data->hwirq] = false;
	gpiochip_disable_irq(chip, data->hwirq);
}

static void max77620_gpio_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct max77620_gpio *gpio = gpiochip_get_data(chip);

	gpiochip_enable_irq(chip, data->hwirq);
	gpio->irq_enabled[data->hwirq] = true;
}

static int max77620_gpio_set_irq_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct max77620_gpio *gpio = gpiochip_get_data(chip);
	unsigned int irq_type;

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		irq_type = MAX77620_CNFG_GPIO_INT_RISING;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		irq_type = MAX77620_CNFG_GPIO_INT_FALLING;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		irq_type = MAX77620_CNFG_GPIO_INT_RISING |
			   MAX77620_CNFG_GPIO_INT_FALLING;
		break;

	default:
		return -EINVAL;
	}

	gpio->irq_type[data->hwirq] = irq_type;

	return 0;
}

static void max77620_gpio_bus_lock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct max77620_gpio *gpio = gpiochip_get_data(chip);

	mutex_lock(&gpio->buslock);
}

static void max77620_gpio_bus_sync_unlock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct max77620_gpio *gpio = gpiochip_get_data(chip);
	unsigned int value, offset = data->hwirq;
	int err;

	value = gpio->irq_enabled[offset] ? gpio->irq_type[offset] : 0;

	err = regmap_update_bits(gpio->rmap, GPIO_REG_ADDR(offset),
				 MAX77620_CNFG_GPIO_INT_MASK, value);
	if (err < 0)
		dev_err(chip->parent, "failed to update interrupt mask: %d\n",
			err);

	mutex_unlock(&gpio->buslock);
}

static const struct irq_chip max77620_gpio_irqchip = {
	.name		= "max77620-gpio",
	.irq_mask	= max77620_gpio_irq_mask,
	.irq_unmask	= max77620_gpio_irq_unmask,
	.irq_set_type	= max77620_gpio_set_irq_type,
	.irq_bus_lock	= max77620_gpio_bus_lock,
	.irq_bus_sync_unlock = max77620_gpio_bus_sync_unlock,
	.flags		= IRQCHIP_IMMUTABLE | IRQCHIP_MASK_ON_SUSPEND,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
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

static int max77620_gpio_set(struct gpio_chip *gc, unsigned int offset,
			     int value)
{
	struct max77620_gpio *mgpio = gpiochip_get_data(gc);
	u8 val;

	val = (value) ? MAX77620_CNFG_GPIO_OUTPUT_VAL_HIGH :
				MAX77620_CNFG_GPIO_OUTPUT_VAL_LOW;

	return regmap_update_bits(mgpio->rmap, GPIO_REG_ADDR(offset),
				  MAX77620_CNFG_GPIO_OUTPUT_VAL_MASK, val);
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

static int max77620_gpio_irq_init_hw(struct gpio_chip *gc)
{
	struct max77620_gpio *gpio = gpiochip_get_data(gc);
	unsigned int i;
	int err;

	/*
	 * GPIO interrupts may be left ON after bootloader, hence let's
	 * pre-initialize hardware to the expected state by disabling all
	 * the interrupts.
	 */
	for (i = 0; i < MAX77620_GPIO_NR; i++) {
		err = regmap_update_bits(gpio->rmap, GPIO_REG_ADDR(i),
					 MAX77620_CNFG_GPIO_INT_MASK, 0);
		if (err < 0) {
			dev_err(gpio->dev,
				"failed to disable interrupt: %d\n", err);
			return err;
		}
	}

	return 0;
}

static int max77620_gpio_probe(struct platform_device *pdev)
{
	struct max77620_chip *chip =  dev_get_drvdata(pdev->dev.parent);
	struct max77620_gpio *mgpio;
	struct gpio_irq_chip *girq;
	unsigned int gpio_irq;
	int ret;

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	gpio_irq = ret;

	mgpio = devm_kzalloc(&pdev->dev, sizeof(*mgpio), GFP_KERNEL);
	if (!mgpio)
		return -ENOMEM;

	mutex_init(&mgpio->buslock);
	mgpio->rmap = chip->rmap;
	mgpio->dev = &pdev->dev;

	mgpio->gpio_chip.label = pdev->name;
	mgpio->gpio_chip.parent = pdev->dev.parent;
	mgpio->gpio_chip.direction_input = max77620_gpio_dir_input;
	mgpio->gpio_chip.get = max77620_gpio_get;
	mgpio->gpio_chip.direction_output = max77620_gpio_dir_output;
	mgpio->gpio_chip.set = max77620_gpio_set;
	mgpio->gpio_chip.set_config = max77620_gpio_set_config;
	mgpio->gpio_chip.ngpio = MAX77620_GPIO_NR;
	mgpio->gpio_chip.can_sleep = 1;
	mgpio->gpio_chip.base = -1;

	girq = &mgpio->gpio_chip.irq;
	gpio_irq_chip_set_chip(girq, &max77620_gpio_irqchip);
	/* This will let us handle the parent IRQ in the driver */
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_edge_irq;
	girq->init_hw = max77620_gpio_irq_init_hw;
	girq->threaded = true;

	ret = devm_gpiochip_add_data(&pdev->dev, &mgpio->gpio_chip, mgpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "gpio_init: Failed to add max77620_gpio\n");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, gpio_irq, NULL,
					max77620_gpio_irqhandler, IRQF_ONESHOT,
					"max77620-gpio", mgpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ: %d\n", ret);
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
MODULE_LICENSE("GPL v2");
