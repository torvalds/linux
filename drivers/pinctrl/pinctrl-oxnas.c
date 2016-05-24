/*
 * Oxford Semiconductor OXNAS SoC Family pinctrl driver
 *
 * Copyright (C) 2016 Neil Armstrong <narmstrong@baylibre.com>
 *
 * Based on pinctrl-pic32.c
 * Joshua Henderson, <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "pinctrl-utils.h"

#define PINS_PER_BANK		32

#define GPIO_BANK_START(bank)		((bank) * PINS_PER_BANK)

/* Regmap Offsets */
#define PINMUX_PRIMARY_SEL0	0x0c
#define PINMUX_SECONDARY_SEL0	0x14
#define PINMUX_TERTIARY_SEL0	0x8c
#define PINMUX_PRIMARY_SEL1	0x10
#define PINMUX_SECONDARY_SEL1	0x18
#define PINMUX_TERTIARY_SEL1	0x90
#define PINMUX_PULLUP_CTRL0	0xac
#define PINMUX_PULLUP_CTRL1	0xb0

/* GPIO Registers */
#define INPUT_VALUE	0x00
#define OUTPUT_EN	0x04
#define IRQ_PENDING	0x0c
#define OUTPUT_SET	0x14
#define OUTPUT_CLEAR	0x18
#define OUTPUT_EN_SET	0x1c
#define OUTPUT_EN_CLEAR	0x20
#define RE_IRQ_ENABLE	0x28
#define FE_IRQ_ENABLE	0x2c

struct oxnas_function {
	const char *name;
	const char * const *groups;
	unsigned int ngroups;
};

struct oxnas_pin_group {
	const char *name;
	unsigned int pin;
	unsigned int bank;
	struct oxnas_desc_function *functions;
};

struct oxnas_desc_function {
	const char *name;
	unsigned int fct;
};

struct oxnas_gpio_bank {
	void __iomem *reg_base;
	struct gpio_chip gpio_chip;
	struct irq_chip irq_chip;
	unsigned int id;
};

struct oxnas_pinctrl {
	struct regmap *regmap;
	struct device *dev;
	struct pinctrl_dev *pctldev;
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct oxnas_function *functions;
	unsigned int nfunctions;
	const struct oxnas_pin_group *groups;
	unsigned int ngroups;
	struct oxnas_gpio_bank *gpio_banks;
	unsigned int nbanks;
};

static const struct pinctrl_pin_desc oxnas_pins[] = {
	PINCTRL_PIN(0, "gpio0"),
	PINCTRL_PIN(1, "gpio1"),
	PINCTRL_PIN(2, "gpio2"),
	PINCTRL_PIN(3, "gpio3"),
	PINCTRL_PIN(4, "gpio4"),
	PINCTRL_PIN(5, "gpio5"),
	PINCTRL_PIN(6, "gpio6"),
	PINCTRL_PIN(7, "gpio7"),
	PINCTRL_PIN(8, "gpio8"),
	PINCTRL_PIN(9, "gpio9"),
	PINCTRL_PIN(10, "gpio10"),
	PINCTRL_PIN(11, "gpio11"),
	PINCTRL_PIN(12, "gpio12"),
	PINCTRL_PIN(13, "gpio13"),
	PINCTRL_PIN(14, "gpio14"),
	PINCTRL_PIN(15, "gpio15"),
	PINCTRL_PIN(16, "gpio16"),
	PINCTRL_PIN(17, "gpio17"),
	PINCTRL_PIN(18, "gpio18"),
	PINCTRL_PIN(19, "gpio19"),
	PINCTRL_PIN(20, "gpio20"),
	PINCTRL_PIN(21, "gpio21"),
	PINCTRL_PIN(22, "gpio22"),
	PINCTRL_PIN(23, "gpio23"),
	PINCTRL_PIN(24, "gpio24"),
	PINCTRL_PIN(25, "gpio25"),
	PINCTRL_PIN(26, "gpio26"),
	PINCTRL_PIN(27, "gpio27"),
	PINCTRL_PIN(28, "gpio28"),
	PINCTRL_PIN(29, "gpio29"),
	PINCTRL_PIN(30, "gpio30"),
	PINCTRL_PIN(31, "gpio31"),
	PINCTRL_PIN(32, "gpio32"),
	PINCTRL_PIN(33, "gpio33"),
	PINCTRL_PIN(34, "gpio34"),
};

static const char * const oxnas_fct0_group[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",
	"gpio4",  "gpio5",  "gpio6",  "gpio7",
	"gpio8",  "gpio9",  "gpio10", "gpio11",
	"gpio12", "gpio13", "gpio14", "gpio15",
	"gpio16", "gpio17", "gpio18", "gpio19",
	"gpio20", "gpio21", "gpio22", "gpio23",
	"gpio24", "gpio25", "gpio26", "gpio27",
	"gpio28", "gpio29", "gpio30", "gpio31",
	"gpio32", "gpio33", "gpio34"
};

static const char * const oxnas_fct3_group[] = {
	"gpio0",  "gpio1",  "gpio2",  "gpio3",
	"gpio4",  "gpio5",  "gpio6",  "gpio7",
	"gpio8",  "gpio9",
	"gpio20",
	"gpio22", "gpio23", "gpio24", "gpio25",
	"gpio26", "gpio27", "gpio28", "gpio29",
	"gpio30", "gpio31", "gpio32", "gpio33",
	"gpio34"
};

#define FUNCTION(_name, _gr)					\
	{							\
		.name = #_name,					\
		.groups = oxnas_##_gr##_group,			\
		.ngroups = ARRAY_SIZE(oxnas_##_gr##_group),	\
	}

static const struct oxnas_function oxnas_functions[] = {
	FUNCTION(gpio, fct0),
	FUNCTION(fct3, fct3),
};

#define OXNAS_PINCTRL_GROUP(_pin, _name, ...)				\
	{								\
		.name = #_name,						\
		.pin = _pin,						\
		.bank = _pin / PINS_PER_BANK,				\
		.functions = (struct oxnas_desc_function[]){		\
			__VA_ARGS__, { } },				\
	}

#define OXNAS_PINCTRL_FUNCTION(_name, _fct)		\
	{						\
		.name = #_name,				\
		.fct = _fct,				\
	}

static const struct oxnas_pin_group oxnas_groups[] = {
	OXNAS_PINCTRL_GROUP(0, gpio0,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(1, gpio1,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(2, gpio2,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(3, gpio3,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(4, gpio4,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(5, gpio5,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(6, gpio6,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(7, gpio7,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(8, gpio8,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(9, gpio9,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(10, gpio10,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(11, gpio11,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(12, gpio12,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(13, gpio13,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(14, gpio14,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(15, gpio15,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(16, gpio16,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(17, gpio17,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(18, gpio18,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(19, gpio19,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(20, gpio20,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(21, gpio21,
			OXNAS_PINCTRL_FUNCTION(gpio, 0)),
	OXNAS_PINCTRL_GROUP(22, gpio22,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(23, gpio23,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(24, gpio24,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(25, gpio25,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(26, gpio26,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(27, gpio27,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(28, gpio28,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(29, gpio29,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(30, gpio30,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(31, gpio31,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(32, gpio32,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(33, gpio33,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
	OXNAS_PINCTRL_GROUP(34, gpio34,
			OXNAS_PINCTRL_FUNCTION(gpio, 0),
			OXNAS_PINCTRL_FUNCTION(fct3, 3)),
};

static inline struct oxnas_gpio_bank *pctl_to_bank(struct oxnas_pinctrl *pctl,
						   unsigned int pin)
{
	return &pctl->gpio_banks[pin / PINS_PER_BANK];
}

static int oxnas_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->ngroups;
}

static const char *oxnas_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int group)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->groups[group].name;
}

static int oxnas_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int group,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*pins = &pctl->groups[group].pin;
	*num_pins = 1;

	return 0;
}

static const struct pinctrl_ops oxnas_pinctrl_ops = {
	.get_groups_count = oxnas_pinctrl_get_groups_count,
	.get_group_name = oxnas_pinctrl_get_group_name,
	.get_group_pins = oxnas_pinctrl_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinctrl_utils_dt_free_map,
};

static int oxnas_pinmux_get_functions_count(struct pinctrl_dev *pctldev)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->nfunctions;
}

static const char *
oxnas_pinmux_get_function_name(struct pinctrl_dev *pctldev, unsigned int func)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	return pctl->functions[func].name;
}

static int oxnas_pinmux_get_function_groups(struct pinctrl_dev *pctldev,
					    unsigned int func,
					    const char * const **groups,
					    unsigned int * const num_groups)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	*groups = pctl->functions[func].groups;
	*num_groups = pctl->functions[func].ngroups;

	return 0;
}

static int oxnas_pinmux_enable(struct pinctrl_dev *pctldev,
			       unsigned int func, unsigned int group)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	const struct oxnas_pin_group *pg = &pctl->groups[group];
	const struct oxnas_function *pf = &pctl->functions[func];
	const char *fname = pf->name;
	struct oxnas_desc_function *functions = pg->functions;
	u32 mask = BIT(pg->pin);

	while (functions->name) {
		if (!strcmp(functions->name, fname)) {
			dev_dbg(pctl->dev,
				"setting function %s bank %d pin %d fct %d mask %x\n",
				fname, pg->bank, pg->pin,
				functions->fct, mask);

			regmap_write_bits(pctl->regmap,
					  (pg->bank ?
						PINMUX_PRIMARY_SEL1 :
						PINMUX_PRIMARY_SEL0),
					  mask,
					  (functions->fct == 1 ?
						mask : 0));
			regmap_write_bits(pctl->regmap,
					  (pg->bank ?
						PINMUX_SECONDARY_SEL1 :
						PINMUX_SECONDARY_SEL0),
					  mask,
					  (functions->fct == 2 ?
						mask : 0));
			regmap_write_bits(pctl->regmap,
					  (pg->bank ?
						PINMUX_TERTIARY_SEL1 :
						PINMUX_TERTIARY_SEL0),
					  mask,
					  (functions->fct == 3 ?
						mask : 0));

			return 0;
		}

		functions++;
	}

	dev_err(pctl->dev, "cannot mux pin %u to function %u\n", group, func);

	return -EINVAL;
}

static int oxnas_gpio_request_enable(struct pinctrl_dev *pctldev,
				     struct pinctrl_gpio_range *range,
				     unsigned int offset)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct oxnas_gpio_bank *bank = gpiochip_get_data(range->gc);
	u32 mask = BIT(offset - bank->gpio_chip.base);

	dev_dbg(pctl->dev, "requesting gpio %d in bank %d (id %d) with mask 0x%x\n",
		offset, bank->gpio_chip.base, bank->id, mask);

	regmap_write_bits(pctl->regmap,
			  (bank->id ?
				PINMUX_PRIMARY_SEL1 :
				PINMUX_PRIMARY_SEL0),
			  mask, 0);
	regmap_write_bits(pctl->regmap,
			  (bank->id ?
				PINMUX_SECONDARY_SEL1 :
				PINMUX_SECONDARY_SEL0),
			  mask, 0);
	regmap_write_bits(pctl->regmap,
			  (bank->id ?
				PINMUX_TERTIARY_SEL1 :
				PINMUX_TERTIARY_SEL0),
			  mask, 0);

	return 0;
}

static int oxnas_gpio_get_direction(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	u32 mask = BIT(offset);

	return !(readl_relaxed(bank->reg_base + OUTPUT_EN) & mask);
}

static int oxnas_gpio_direction_input(struct gpio_chip *chip,
				      unsigned int offset)
{
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	u32 mask = BIT(offset);

	writel_relaxed(mask, bank->reg_base + OUTPUT_EN_CLEAR);

	return 0;
}

static int oxnas_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	u32 mask = BIT(offset);

	return (readl_relaxed(bank->reg_base + INPUT_VALUE) & mask) != 0;
}

static void oxnas_gpio_set(struct gpio_chip *chip, unsigned int offset,
			       int value)
{
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	u32 mask = BIT(offset);

	if (value)
		writel_relaxed(mask, bank->reg_base + OUTPUT_SET);
	else
		writel_relaxed(mask, bank->reg_base + OUTPUT_CLEAR);
}

static int oxnas_gpio_direction_output(struct gpio_chip *chip,
				       unsigned int offset, int value)
{
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	u32 mask = BIT(offset);

	oxnas_gpio_set(chip, offset, value);
	writel_relaxed(mask, bank->reg_base + OUTPUT_EN_SET);

	return 0;
}

static int oxnas_gpio_set_direction(struct pinctrl_dev *pctldev,
				    struct pinctrl_gpio_range *range,
				    unsigned int offset, bool input)
{
	struct gpio_chip *chip = range->gc;

	if (input)
		oxnas_gpio_direction_input(chip, offset);
	else
		oxnas_gpio_direction_output(chip, offset, 0);

	return 0;
}

static const struct pinmux_ops oxnas_pinmux_ops = {
	.get_functions_count = oxnas_pinmux_get_functions_count,
	.get_function_name = oxnas_pinmux_get_function_name,
	.get_function_groups = oxnas_pinmux_get_function_groups,
	.set_mux = oxnas_pinmux_enable,
	.gpio_request_enable = oxnas_gpio_request_enable,
	.gpio_set_direction = oxnas_gpio_set_direction,
};

static int oxnas_pinconf_get(struct pinctrl_dev *pctldev, unsigned int pin,
			     unsigned long *config)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct oxnas_gpio_bank *bank = pctl_to_bank(pctl, pin);
	unsigned int param = pinconf_to_config_param(*config);
	u32 mask = BIT(pin - bank->gpio_chip.base);
	int ret;
	u32 arg;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_UP:
		ret = regmap_read(pctl->regmap,
				  (bank->id ?
					PINMUX_PULLUP_CTRL1 :
					PINMUX_PULLUP_CTRL0),
				  &arg);
		if (ret)
			return ret;

		arg = !!(arg & mask);
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int oxnas_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			     unsigned long *configs, unsigned int num_configs)
{
	struct oxnas_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);
	struct oxnas_gpio_bank *bank = pctl_to_bank(pctl, pin);
	unsigned int param;
	u32 arg;
	unsigned int i;
	u32 offset = pin - bank->gpio_chip.base;
	u32 mask = BIT(offset);

	dev_dbg(pctl->dev, "setting pin %d bank %d mask 0x%x\n",
		pin, bank->gpio_chip.base, mask);

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_PULL_UP:
			dev_dbg(pctl->dev, "   pullup\n");
			regmap_write_bits(pctl->regmap,
					  (bank->id ?
						PINMUX_PULLUP_CTRL1 :
						PINMUX_PULLUP_CTRL0),
					  mask, mask);
			break;
		default:
			dev_err(pctl->dev, "Property %u not supported\n",
				param);
			return -ENOTSUPP;
		}
	}

	return 0;
}

static const struct pinconf_ops oxnas_pinconf_ops = {
	.pin_config_get = oxnas_pinconf_get,
	.pin_config_set = oxnas_pinconf_set,
	.is_generic = true,
};

static struct pinctrl_desc oxnas_pinctrl_desc = {
	.name = "oxnas-pinctrl",
	.pctlops = &oxnas_pinctrl_ops,
	.pmxops = &oxnas_pinmux_ops,
	.confops = &oxnas_pinconf_ops,
	.owner = THIS_MODULE,
};

static void oxnas_gpio_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	u32 mask = BIT(data->hwirq);

	writel(mask, bank->reg_base + IRQ_PENDING);
}

static void oxnas_gpio_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	unsigned int type = irqd_get_trigger_type(data);
	u32 mask = BIT(data->hwirq);

	if (type & IRQ_TYPE_EDGE_RISING)
		writel(readl(bank->reg_base + RE_IRQ_ENABLE) & ~mask,
		       bank->reg_base + RE_IRQ_ENABLE);

	if (type & IRQ_TYPE_EDGE_FALLING)
		writel(readl(bank->reg_base + FE_IRQ_ENABLE) & ~mask,
		       bank->reg_base + FE_IRQ_ENABLE);
}

static void oxnas_gpio_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct oxnas_gpio_bank *bank = gpiochip_get_data(chip);
	unsigned int type = irqd_get_trigger_type(data);
	u32 mask = BIT(data->hwirq);

	if (type & IRQ_TYPE_EDGE_RISING)
		writel(readl(bank->reg_base + RE_IRQ_ENABLE) | mask,
		       bank->reg_base + RE_IRQ_ENABLE);

	if (type & IRQ_TYPE_EDGE_FALLING)
		writel(readl(bank->reg_base + FE_IRQ_ENABLE) | mask,
		       bank->reg_base + FE_IRQ_ENABLE);
}

static unsigned int oxnas_gpio_irq_startup(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);

	oxnas_gpio_direction_input(chip, data->hwirq);
	oxnas_gpio_irq_unmask(data);

	return 0;
}

static int oxnas_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	if ((type & (IRQ_TYPE_EDGE_RISING|IRQ_TYPE_EDGE_FALLING)) == 0)
		return -EINVAL;

	irq_set_handler_locked(data, handle_edge_irq);

	return 0;
}

static void oxnas_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct oxnas_gpio_bank *bank = gpiochip_get_data(gc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long stat;
	unsigned int pin;

	chained_irq_enter(chip, desc);

	stat = readl(bank->reg_base + IRQ_PENDING);

	for_each_set_bit(pin, &stat, BITS_PER_LONG)
		generic_handle_irq(irq_linear_revmap(gc->irqdomain, pin));

	chained_irq_exit(chip, desc);
}

#define GPIO_BANK(_bank)						\
	{								\
		.gpio_chip = {						\
			.label = "GPIO" #_bank,				\
			.request = gpiochip_generic_request,		\
			.free = gpiochip_generic_free,			\
			.get_direction = oxnas_gpio_get_direction,	\
			.direction_input = oxnas_gpio_direction_input,	\
			.direction_output = oxnas_gpio_direction_output, \
			.get = oxnas_gpio_get,				\
			.set = oxnas_gpio_set,				\
			.ngpio = PINS_PER_BANK,				\
			.base = GPIO_BANK_START(_bank),			\
			.owner = THIS_MODULE,				\
			.can_sleep = 0,					\
		},							\
		.irq_chip = {						\
			.name = "GPIO" #_bank,				\
			.irq_startup = oxnas_gpio_irq_startup,	\
			.irq_ack = oxnas_gpio_irq_ack,		\
			.irq_mask = oxnas_gpio_irq_mask,		\
			.irq_unmask = oxnas_gpio_irq_unmask,		\
			.irq_set_type = oxnas_gpio_irq_set_type,	\
		},							\
	}

static struct oxnas_gpio_bank oxnas_gpio_banks[] = {
	GPIO_BANK(0),
	GPIO_BANK(1),
};

static int oxnas_pinctrl_probe(struct platform_device *pdev)
{
	struct oxnas_pinctrl *pctl;

	pctl = devm_kzalloc(&pdev->dev, sizeof(*pctl), GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;
	pctl->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, pctl);

	pctl->regmap = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						       "oxsemi,sys-ctrl");
	if (IS_ERR(pctl->regmap)) {
		dev_err(&pdev->dev, "failed to get sys ctrl regmap\n");
		return -ENODEV;
	}

	pctl->pins = oxnas_pins;
	pctl->npins = ARRAY_SIZE(oxnas_pins);
	pctl->functions = oxnas_functions;
	pctl->nfunctions = ARRAY_SIZE(oxnas_functions);
	pctl->groups = oxnas_groups;
	pctl->ngroups = ARRAY_SIZE(oxnas_groups);
	pctl->gpio_banks = oxnas_gpio_banks;
	pctl->nbanks = ARRAY_SIZE(oxnas_gpio_banks);

	oxnas_pinctrl_desc.pins = pctl->pins;
	oxnas_pinctrl_desc.npins = pctl->npins;

	pctl->pctldev = pinctrl_register(&oxnas_pinctrl_desc,
					 &pdev->dev, pctl);
	if (IS_ERR(pctl->pctldev)) {
		dev_err(&pdev->dev, "Failed to register pinctrl device\n");
		return PTR_ERR(pctl->pctldev);
	}

	return 0;
}

static int oxnas_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct of_phandle_args pinspec;
	struct oxnas_gpio_bank *bank;
	unsigned int id, ngpios;
	int irq, ret;
	struct resource *res;

	if (of_parse_phandle_with_fixed_args(np, "gpio-ranges",
					     3, 0, &pinspec)) {
		dev_err(&pdev->dev, "gpio-ranges property not found\n");
		return -EINVAL;
	}

	id = pinspec.args[1] / PINS_PER_BANK;
	ngpios = pinspec.args[2];

	if (id >= ARRAY_SIZE(oxnas_gpio_banks)) {
		dev_err(&pdev->dev, "invalid gpio-ranges base arg\n");
		return -EINVAL;
	}

	if (ngpios > PINS_PER_BANK) {
		dev_err(&pdev->dev, "invalid gpio-ranges count arg\n");
		return -EINVAL;
	}

	bank = &oxnas_gpio_banks[id];

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	bank->reg_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(bank->reg_base))
		return PTR_ERR(bank->reg_base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "irq get failed\n");
		return irq;
	}

	bank->id = id;
	bank->gpio_chip.parent = &pdev->dev;
	bank->gpio_chip.of_node = np;
	bank->gpio_chip.ngpio = ngpios;
	ret = gpiochip_add_data(&bank->gpio_chip, bank);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add GPIO chip %u: %d\n",
			id, ret);
		return ret;
	}

	ret = gpiochip_irqchip_add(&bank->gpio_chip, &bank->irq_chip,
				0, handle_level_irq, IRQ_TYPE_NONE);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to add IRQ chip %u: %d\n",
			id, ret);
		gpiochip_remove(&bank->gpio_chip);
		return ret;
	}

	gpiochip_set_chained_irqchip(&bank->gpio_chip, &bank->irq_chip,
				     irq, oxnas_gpio_irq_handler);

	return 0;
}

static const struct of_device_id oxnas_pinctrl_of_match[] = {
	{ .compatible = "oxsemi,ox810se-pinctrl", },
	{ },
};

static struct platform_driver oxnas_pinctrl_driver = {
	.driver = {
		.name = "oxnas-pinctrl",
		.of_match_table = oxnas_pinctrl_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = oxnas_pinctrl_probe,
};

static const struct of_device_id oxnas_gpio_of_match[] = {
	{ .compatible = "oxsemi,ox810se-gpio", },
	{ },
};

static struct platform_driver oxnas_gpio_driver = {
	.driver = {
		.name = "oxnas-gpio",
		.of_match_table = oxnas_gpio_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = oxnas_gpio_probe,
};

static int __init oxnas_gpio_register(void)
{
	return platform_driver_register(&oxnas_gpio_driver);
}
arch_initcall(oxnas_gpio_register);

static int __init oxnas_pinctrl_register(void)
{
	return platform_driver_register(&oxnas_pinctrl_driver);
}
arch_initcall(oxnas_pinctrl_register);
