// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple SoC pinctrl+GPIO+external IRQ driver
 *
 * Copyright (C) The Asahi Linux Contributors
 * Copyright (C) 2020 Corellium LLC
 *
 * Based on: pinctrl-pistachio.c
 * Copyright (C) 2014 Imagination Technologies Ltd.
 * Copyright (C) 2014 Google, Inc.
 */

#include <dt-bindings/pinctrl/apple.h>
#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "pinctrl-utils.h"
#include "core.h"
#include "pinmux.h"

struct apple_gpio_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctldev;

	void __iomem *base;
	struct regmap *map;

	struct pinctrl_desc pinctrl_desc;
	struct gpio_chip gpio_chip;
	struct irq_chip irq_chip;
	u8 irqgrps[];
};

#define REG_GPIO(x)          (4 * (x))
#define REG_GPIOx_DATA       BIT(0)
#define REG_GPIOx_MODE       GENMASK(3, 1)
#define REG_GPIOx_OUT        1
#define REG_GPIOx_IN_IRQ_HI  2
#define REG_GPIOx_IN_IRQ_LO  3
#define REG_GPIOx_IN_IRQ_UP  4
#define REG_GPIOx_IN_IRQ_DN  5
#define REG_GPIOx_IN_IRQ_ANY 6
#define REG_GPIOx_IN_IRQ_OFF 7
#define REG_GPIOx_PERIPH     GENMASK(6, 5)
#define REG_GPIOx_PULL       GENMASK(8, 7)
#define REG_GPIOx_PULL_OFF   0
#define REG_GPIOx_PULL_DOWN  1
#define REG_GPIOx_PULL_UP_STRONG 2
#define REG_GPIOx_PULL_UP    3
#define REG_GPIOx_INPUT_ENABLE BIT(9)
#define REG_GPIOx_DRIVE_STRENGTH0 GENMASK(11, 10)
#define REG_GPIOx_SCHMITT    BIT(15)
#define REG_GPIOx_GRP        GENMASK(18, 16)
#define REG_GPIOx_LOCK       BIT(21)
#define REG_GPIOx_DRIVE_STRENGTH1 GENMASK(23, 22)
#define REG_IRQ(g, x)        (0x800 + 0x40 * (g) + 4 * ((x) >> 5))

struct regmap_config regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.cache_type = REGCACHE_FLAT,
	.max_register = 512 * sizeof(u32),
	.num_reg_defaults_raw = 512,
	.use_relaxed_mmio = true,
};

/* No locking needed to mask/unmask IRQs as the interrupt mode is per pin-register. */
static void apple_gpio_set_reg(struct apple_gpio_pinctrl *pctl,
                               unsigned int pin, u32 mask, u32 value)
{
	regmap_update_bits(pctl->map, REG_GPIO(pin), mask, value);
}

static u32 apple_gpio_get_reg(struct apple_gpio_pinctrl *pctl,
                              unsigned int pin)
{
	int ret;
	u32 val;

	ret = regmap_read(pctl->map, REG_GPIO(pin), &val);
	if (ret)
		return 0;

	return val;
}

/* Pin controller functions */

static int apple_gpio_dt_node_to_map(struct pinctrl_dev *pctldev,
                                     struct device_node *node,
                                     struct pinctrl_map **map,
                                     unsigned *num_maps)
{
	unsigned reserved_maps;
	struct apple_gpio_pinctrl *pctl;
	u32 pinfunc, pin, func;
	int num_pins, i, ret;
	const char *group_name;
	const char *function_name;

	*map = NULL;
	*num_maps = 0;
	reserved_maps = 0;

	pctl = pinctrl_dev_get_drvdata(pctldev);

	ret = of_property_count_u32_elems(node, "pinmux");
	if (ret <= 0) {
		dev_err(pctl->dev,
			"missing or empty pinmux property in node %pOFn.\n",
			node);
		return ret ? ret : -EINVAL;
	}

	num_pins = ret;

	ret = pinctrl_utils_reserve_map(pctldev, map, &reserved_maps, num_maps, num_pins);
	if (ret)
		return ret;

	for (i = 0; i < num_pins; i++) {
		ret = of_property_read_u32_index(node, "pinmux", i, &pinfunc);
		if (ret)
			goto free_map;

		pin = APPLE_PIN(pinfunc);
		func = APPLE_FUNC(pinfunc);

		if (func >= pinmux_generic_get_function_count(pctldev)) {
			ret = -EINVAL;
			goto free_map;
		}

		group_name = pinctrl_generic_get_group_name(pctldev, pin);
		function_name = pinmux_generic_get_function_name(pctl->pctldev, func);
		ret = pinctrl_utils_add_map_mux(pctl->pctldev, map,
		                                &reserved_maps, num_maps,
		                                group_name, function_name);
		if (ret)
			goto free_map;
	}

free_map:
	if (ret < 0)
		pinctrl_utils_free_map(pctldev, *map, *num_maps);

	return ret;
}

static const struct pinctrl_ops apple_gpio_pinctrl_ops = {
	.get_groups_count = pinctrl_generic_get_group_count,
	.get_group_name = pinctrl_generic_get_group_name,
	.get_group_pins = pinctrl_generic_get_group_pins,
	.dt_node_to_map = apple_gpio_dt_node_to_map,
	.dt_free_map = pinctrl_utils_free_map,
};

/* Pin multiplexer functions */

static int apple_gpio_pinmux_set(struct pinctrl_dev *pctldev, unsigned func,
                                 unsigned group)
{
	struct apple_gpio_pinctrl *pctl = pinctrl_dev_get_drvdata(pctldev);

	apple_gpio_set_reg(
		pctl, group, REG_GPIOx_PERIPH | REG_GPIOx_INPUT_ENABLE,
		FIELD_PREP(REG_GPIOx_PERIPH, func) | REG_GPIOx_INPUT_ENABLE);

	return 0;
}

static const struct pinmux_ops apple_gpio_pinmux_ops = {
	.get_functions_count = pinmux_generic_get_function_count,
	.get_function_name = pinmux_generic_get_function_name,
	.get_function_groups = pinmux_generic_get_function_groups,
	.set_mux = apple_gpio_pinmux_set,
	.strict = true,
};

/* GPIO chip functions */

static int apple_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);
	unsigned int reg = apple_gpio_get_reg(pctl, offset);

	if (FIELD_GET(REG_GPIOx_MODE, reg) == REG_GPIOx_OUT)
		return GPIO_LINE_DIRECTION_OUT;
	return GPIO_LINE_DIRECTION_IN;
}

static int apple_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);
	unsigned int reg = apple_gpio_get_reg(pctl, offset);

	/*
	 * If this is an input GPIO, read the actual value (not the
	 * cached regmap value)
	 */
	if (FIELD_GET(REG_GPIOx_MODE, reg) != REG_GPIOx_OUT)
		reg = readl_relaxed(pctl->base + REG_GPIO(offset));

	return !!(reg & REG_GPIOx_DATA);
}

static void apple_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	apple_gpio_set_reg(pctl, offset, REG_GPIOx_DATA, value ? REG_GPIOx_DATA : 0);
}

static int apple_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	apple_gpio_set_reg(pctl, offset,
			   REG_GPIOx_PERIPH | REG_GPIOx_MODE | REG_GPIOx_DATA |
				   REG_GPIOx_INPUT_ENABLE,
			   FIELD_PREP(REG_GPIOx_MODE, REG_GPIOx_IN_IRQ_OFF) |
				   REG_GPIOx_INPUT_ENABLE);
	return 0;
}

static int apple_gpio_direction_output(struct gpio_chip *chip,
                                       unsigned int offset, int value)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	apple_gpio_set_reg(pctl, offset,
			   REG_GPIOx_PERIPH | REG_GPIOx_MODE | REG_GPIOx_DATA,
			   FIELD_PREP(REG_GPIOx_MODE, REG_GPIOx_OUT) |
				   (value ? REG_GPIOx_DATA : 0));
	return 0;
}

/* IRQ chip functions */

static void apple_gpio_irq_ack(struct irq_data *data)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));
	unsigned int irqgrp = FIELD_GET(REG_GPIOx_GRP, apple_gpio_get_reg(pctl, data->hwirq));

	writel(BIT(data->hwirq % 32), pctl->base + REG_IRQ(irqgrp, data->hwirq));
}

static unsigned int apple_gpio_irq_type(unsigned int type)
{
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		return REG_GPIOx_IN_IRQ_UP;
	case IRQ_TYPE_EDGE_FALLING:
		return REG_GPIOx_IN_IRQ_DN;
	case IRQ_TYPE_EDGE_BOTH:
		return REG_GPIOx_IN_IRQ_ANY;
	case IRQ_TYPE_LEVEL_HIGH:
		return REG_GPIOx_IN_IRQ_HI;
	case IRQ_TYPE_LEVEL_LOW:
		return REG_GPIOx_IN_IRQ_LO;
	default:
		return REG_GPIOx_IN_IRQ_OFF;
	}
}

static void apple_gpio_irq_mask(struct irq_data *data)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));

	apple_gpio_set_reg(pctl, data->hwirq, REG_GPIOx_MODE,
	                   FIELD_PREP(REG_GPIOx_MODE, REG_GPIOx_IN_IRQ_OFF));
}

static void apple_gpio_irq_unmask(struct irq_data *data)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));
	unsigned int irqtype = apple_gpio_irq_type(irqd_get_trigger_type(data));

	apple_gpio_set_reg(pctl, data->hwirq, REG_GPIOx_MODE,
	                   FIELD_PREP(REG_GPIOx_MODE, irqtype));
}

static unsigned int apple_gpio_irq_startup(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(chip);

	apple_gpio_set_reg(pctl, data->hwirq, REG_GPIOx_GRP,
	                   FIELD_PREP(REG_GPIOx_GRP, 0));

	apple_gpio_direction_input(chip, data->hwirq);
	apple_gpio_irq_unmask(data);

	return 0;
}

static int apple_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	struct apple_gpio_pinctrl *pctl = gpiochip_get_data(irq_data_get_irq_chip_data(data));
	unsigned int irqtype = apple_gpio_irq_type(type);

	if (irqtype == REG_GPIOx_IN_IRQ_OFF)
		return -EINVAL;

	apple_gpio_set_reg(pctl, data->hwirq, REG_GPIOx_MODE,
	                   FIELD_PREP(REG_GPIOx_MODE, irqtype));

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(data, handle_level_irq);
	else
		irq_set_handler_locked(data, handle_edge_irq);
	return 0;
}

static void apple_gpio_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	u8 *grpp = irq_desc_get_handler_data(desc);
	struct apple_gpio_pinctrl *pctl;
	unsigned int pinh, pinl;
	unsigned long pending;
	struct gpio_chip *gc;

	pctl = container_of(grpp - *grpp, typeof(*pctl), irqgrps[0]);
	gc = &pctl->gpio_chip;

	chained_irq_enter(chip, desc);
	for (pinh = 0; pinh < gc->ngpio; pinh += 32) {
		pending = readl_relaxed(pctl->base + REG_IRQ(*grpp, pinh));
		for_each_set_bit(pinl, &pending, 32)
			generic_handle_domain_irq(gc->irq.domain, pinh + pinl);
	}
	chained_irq_exit(chip, desc);
}

static struct irq_chip apple_gpio_irqchip = {
	.name		= "Apple-GPIO",
	.irq_startup	= apple_gpio_irq_startup,
	.irq_ack	= apple_gpio_irq_ack,
	.irq_mask	= apple_gpio_irq_mask,
	.irq_unmask	= apple_gpio_irq_unmask,
	.irq_set_type	= apple_gpio_irq_set_type,
};

/* Probe & register */

static int apple_gpio_register(struct apple_gpio_pinctrl *pctl)
{
	struct gpio_irq_chip *girq = &pctl->gpio_chip.irq;
	void **irq_data = NULL;
	int ret;

	pctl->irq_chip = apple_gpio_irqchip;

	pctl->gpio_chip.label = dev_name(pctl->dev);
	pctl->gpio_chip.request = gpiochip_generic_request;
	pctl->gpio_chip.free = gpiochip_generic_free;
	pctl->gpio_chip.get_direction = apple_gpio_get_direction;
	pctl->gpio_chip.direction_input = apple_gpio_direction_input;
	pctl->gpio_chip.direction_output = apple_gpio_direction_output;
	pctl->gpio_chip.get = apple_gpio_get;
	pctl->gpio_chip.set = apple_gpio_set;
	pctl->gpio_chip.base = -1;
	pctl->gpio_chip.ngpio = pctl->pinctrl_desc.npins;
	pctl->gpio_chip.parent = pctl->dev;

	if (girq->num_parents) {
		int i;

		girq->chip = &pctl->irq_chip;
		girq->parent_handler = apple_gpio_irq_handler;

		girq->parents = kmalloc_array(girq->num_parents,
					      sizeof(*girq->parents),
					      GFP_KERNEL);
		irq_data = kmalloc_array(girq->num_parents, sizeof(*irq_data),
					 GFP_KERNEL);
		if (!girq->parents || !irq_data) {
			ret = -ENOMEM;
			goto out_free_irq_data;
		}

		for (i = 0; i < girq->num_parents; i++) {
			ret = platform_get_irq(to_platform_device(pctl->dev), i);
			if (ret < 0)
				goto out_free_irq_data;

			girq->parents[i] = ret;
			pctl->irqgrps[i] = i;
			irq_data[i] = &pctl->irqgrps[i];
		}

		girq->parent_handler_data_array = irq_data;
		girq->per_parent_data = true;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_level_irq;
	}

	ret = devm_gpiochip_add_data(pctl->dev, &pctl->gpio_chip, pctl);

out_free_irq_data:
	kfree(girq->parents);
	kfree(irq_data);

	return ret;
}

static int apple_gpio_pinctrl_probe(struct platform_device *pdev)
{
	struct apple_gpio_pinctrl *pctl;
	struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const char **pin_names;
	unsigned int *pin_nums;
	static const char* pinmux_functions[] = {
		"gpio", "periph1", "periph2", "periph3"
	};
	unsigned int i, nirqs = 0;
	int res;

	if (of_property_read_bool(pdev->dev.of_node, "interrupt-controller")) {
		res = platform_irq_count(pdev);
		if (res > 0)
			nirqs = res;
	}

	pctl = devm_kzalloc(&pdev->dev, struct_size(pctl, irqgrps, nirqs),
			    GFP_KERNEL);
	if (!pctl)
		return -ENOMEM;
	pctl->dev = &pdev->dev;
	pctl->gpio_chip.irq.num_parents = nirqs;
	dev_set_drvdata(&pdev->dev, pctl);

	if (of_property_read_u32(pdev->dev.of_node, "apple,npins", &npins))
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "apple,npins property not found\n");

	pins = devm_kmalloc_array(&pdev->dev, npins, sizeof(pins[0]),
				  GFP_KERNEL);
	pin_names = devm_kmalloc_array(&pdev->dev, npins, sizeof(pin_names[0]),
				       GFP_KERNEL);
	pin_nums = devm_kmalloc_array(&pdev->dev, npins, sizeof(pin_nums[0]),
				      GFP_KERNEL);
	if (!pins || !pin_names || !pin_nums)
		return -ENOMEM;

	pctl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctl->base))
		return PTR_ERR(pctl->base);

	pctl->map = devm_regmap_init_mmio(&pdev->dev, pctl->base, &regmap_config);
	if (IS_ERR(pctl->map))
		return dev_err_probe(&pdev->dev, PTR_ERR(pctl->map),
				     "Failed to create regmap\n");

	for (i = 0; i < npins; i++) {
		pins[i].number = i;
		pins[i].name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "PIN%u", i);
		pins[i].drv_data = pctl;
		pin_names[i] = pins[i].name;
		pin_nums[i] = i;
	}

	pctl->pinctrl_desc.name = dev_name(pctl->dev);
	pctl->pinctrl_desc.pins = pins;
	pctl->pinctrl_desc.npins = npins;
	pctl->pinctrl_desc.pctlops = &apple_gpio_pinctrl_ops;
	pctl->pinctrl_desc.pmxops = &apple_gpio_pinmux_ops;

	pctl->pctldev =	devm_pinctrl_register(&pdev->dev, &pctl->pinctrl_desc, pctl);
	if (IS_ERR(pctl->pctldev))
		return dev_err_probe(&pdev->dev, PTR_ERR(pctl->pctldev),
				     "Failed to register pinctrl device.\n");

	for (i = 0; i < npins; i++) {
		res = pinctrl_generic_add_group(pctl->pctldev, pins[i].name,
						pin_nums + i, 1, pctl);
		if (res < 0)
			return dev_err_probe(pctl->dev, res,
					     "Failed to register group");
	}

	for (i = 0; i < ARRAY_SIZE(pinmux_functions); ++i) {
		res = pinmux_generic_add_function(pctl->pctldev, pinmux_functions[i],
						  pin_names, npins, pctl);
		if (res < 0)
			return dev_err_probe(pctl->dev, res,
					     "Failed to register function.");
	}

	return apple_gpio_register(pctl);
}

static const struct of_device_id apple_gpio_pinctrl_of_match[] = {
	{ .compatible = "apple,pinctrl", },
	{ }
};

static struct platform_driver apple_gpio_pinctrl_driver = {
	.driver = {
		.name = "apple-gpio-pinctrl",
		.of_match_table = apple_gpio_pinctrl_of_match,
		.suppress_bind_attrs = true,
	},
	.probe = apple_gpio_pinctrl_probe,
};
module_platform_driver(apple_gpio_pinctrl_driver);

MODULE_DESCRIPTION("Apple pinctrl/GPIO driver");
MODULE_AUTHOR("Stan Skowronek <stan@corellium.com>");
MODULE_AUTHOR("Joey Gouly <joey.gouly@arm.com>");
MODULE_LICENSE("GPL v2");
