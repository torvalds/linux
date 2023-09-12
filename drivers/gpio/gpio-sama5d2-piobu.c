// SPDX-License-Identifier: GPL-2.0
/*
 * SAMA5D2 PIOBU GPIO controller
 *
 * Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries
 *
 * Author: Andrei Stefanescu <andrei.stefanescu@microchip.com>
 *
 */
#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define PIOBU_NUM 8
#define PIOBU_REG_SIZE 4

/*
 * backup mode protection register for tamper detection
 * normal mode protection register for tamper detection
 * wakeup signal generation
 */
#define PIOBU_BMPR 0x7C
#define PIOBU_NMPR 0x80
#define PIOBU_WKPR 0x90

#define PIOBU_BASE 0x18 /* PIOBU offset from SECUMOD base register address. */

#define PIOBU_DET_OFFSET 16

/* In the datasheet this bit is called OUTPUT */
#define PIOBU_DIRECTION BIT(8)
#define PIOBU_OUT BIT(8)
#define PIOBU_IN 0

#define PIOBU_SOD BIT(9)
#define PIOBU_PDS BIT(10)

#define PIOBU_HIGH BIT(9)
#define PIOBU_LOW 0

struct sama5d2_piobu {
	struct gpio_chip chip;
	struct regmap *regmap;
};

/*
 * sama5d2_piobu_setup_pin() - prepares a pin for set_direction call
 *
 * Do not consider pin for tamper detection (normal and backup modes)
 * Do not consider pin as tamper wakeup interrupt source
 */
static int sama5d2_piobu_setup_pin(struct gpio_chip *chip, unsigned int pin)
{
	int ret;
	struct sama5d2_piobu *piobu = container_of(chip, struct sama5d2_piobu,
						   chip);
	unsigned int mask = BIT(PIOBU_DET_OFFSET + pin);

	ret = regmap_update_bits(piobu->regmap, PIOBU_BMPR, mask, 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(piobu->regmap, PIOBU_NMPR, mask, 0);
	if (ret)
		return ret;

	return regmap_update_bits(piobu->regmap, PIOBU_WKPR, mask, 0);
}

/*
 * sama5d2_piobu_write_value() - writes value & mask at the pin's PIOBU register
 */
static int sama5d2_piobu_write_value(struct gpio_chip *chip, unsigned int pin,
				     unsigned int mask, unsigned int value)
{
	int reg;
	struct sama5d2_piobu *piobu = container_of(chip, struct sama5d2_piobu,
						   chip);

	reg = PIOBU_BASE + pin * PIOBU_REG_SIZE;

	return regmap_update_bits(piobu->regmap, reg, mask, value);
}

/*
 * sama5d2_piobu_read_value() - read the value with masking from the pin's PIOBU
 *			      register
 */
static int sama5d2_piobu_read_value(struct gpio_chip *chip, unsigned int pin,
				    unsigned int mask)
{
	struct sama5d2_piobu *piobu = container_of(chip, struct sama5d2_piobu,
						   chip);
	unsigned int val, reg;
	int ret;

	reg = PIOBU_BASE + pin * PIOBU_REG_SIZE;
	ret = regmap_read(piobu->regmap, reg, &val);
	if (ret < 0)
		return ret;

	return val & mask;
}

/*
 * sama5d2_piobu_get_direction() - gpiochip get_direction
 */
static int sama5d2_piobu_get_direction(struct gpio_chip *chip,
				       unsigned int pin)
{
	int ret = sama5d2_piobu_read_value(chip, pin, PIOBU_DIRECTION);

	if (ret < 0)
		return ret;

	return (ret == PIOBU_IN) ? GPIO_LINE_DIRECTION_IN :
				   GPIO_LINE_DIRECTION_OUT;
}

/*
 * sama5d2_piobu_direction_input() - gpiochip direction_input
 */
static int sama5d2_piobu_direction_input(struct gpio_chip *chip,
					 unsigned int pin)
{
	return sama5d2_piobu_write_value(chip, pin, PIOBU_DIRECTION, PIOBU_IN);
}

/*
 * sama5d2_piobu_direction_output() - gpiochip direction_output
 */
static int sama5d2_piobu_direction_output(struct gpio_chip *chip,
					  unsigned int pin, int value)
{
	unsigned int val = PIOBU_OUT;

	if (value)
		val |= PIOBU_HIGH;

	return sama5d2_piobu_write_value(chip, pin, PIOBU_DIRECTION | PIOBU_SOD,
					 val);
}

/*
 * sama5d2_piobu_get() - gpiochip get
 */
static int sama5d2_piobu_get(struct gpio_chip *chip, unsigned int pin)
{
	/* if pin is input, read value from PDS else read from SOD */
	int ret = sama5d2_piobu_get_direction(chip, pin);

	if (ret == GPIO_LINE_DIRECTION_IN)
		ret = sama5d2_piobu_read_value(chip, pin, PIOBU_PDS);
	else if (ret == GPIO_LINE_DIRECTION_OUT)
		ret = sama5d2_piobu_read_value(chip, pin, PIOBU_SOD);

	if (ret < 0)
		return ret;

	return !!ret;
}

/*
 * sama5d2_piobu_set() - gpiochip set
 */
static void sama5d2_piobu_set(struct gpio_chip *chip, unsigned int pin,
			      int value)
{
	if (!value)
		value = PIOBU_LOW;
	else
		value = PIOBU_HIGH;

	sama5d2_piobu_write_value(chip, pin, PIOBU_SOD, value);
}

static int sama5d2_piobu_probe(struct platform_device *pdev)
{
	struct sama5d2_piobu *piobu;
	int ret, i;

	piobu = devm_kzalloc(&pdev->dev, sizeof(*piobu), GFP_KERNEL);
	if (!piobu)
		return -ENOMEM;

	piobu->chip.label = pdev->name;
	piobu->chip.parent = &pdev->dev;
	piobu->chip.owner = THIS_MODULE,
	piobu->chip.get_direction = sama5d2_piobu_get_direction,
	piobu->chip.direction_input = sama5d2_piobu_direction_input,
	piobu->chip.direction_output = sama5d2_piobu_direction_output,
	piobu->chip.get = sama5d2_piobu_get,
	piobu->chip.set = sama5d2_piobu_set,
	piobu->chip.base = -1,
	piobu->chip.ngpio = PIOBU_NUM,
	piobu->chip.can_sleep = 0,

	piobu->regmap = syscon_node_to_regmap(pdev->dev.of_node);
	if (IS_ERR(piobu->regmap)) {
		dev_err(&pdev->dev, "Failed to get syscon regmap %ld\n",
			PTR_ERR(piobu->regmap));
		return PTR_ERR(piobu->regmap);
	}

	ret = devm_gpiochip_add_data(&pdev->dev, &piobu->chip, piobu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to add gpiochip %d\n", ret);
		return ret;
	}

	for (i = 0; i < PIOBU_NUM; ++i) {
		ret = sama5d2_piobu_setup_pin(&piobu->chip, i);
		if (ret) {
			dev_err(&pdev->dev, "Failed to setup pin: %d %d\n",
				i, ret);
			return ret;
		}
	}

	return 0;
}

static const struct of_device_id sama5d2_piobu_ids[] = {
	{ .compatible = "atmel,sama5d2-secumod" },
	{},
};
MODULE_DEVICE_TABLE(of, sama5d2_piobu_ids);

static struct platform_driver sama5d2_piobu_driver = {
	.driver = {
		.name		= "sama5d2-piobu",
		.of_match_table	= sama5d2_piobu_ids,
	},
	.probe = sama5d2_piobu_probe,
};

module_platform_driver(sama5d2_piobu_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SAMA5D2 PIOBU controller driver");
MODULE_AUTHOR("Andrei Stefanescu <andrei.stefanescu@microchip.com>");
