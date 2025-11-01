// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2025 Bootlin
 *
 * Author: Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>
 */

#include <linux/array_size.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/device/devres.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mfd/max7360.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/stddef.h>

#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinmux.h>

#include "core.h"
#include "pinmux.h"

struct max7360_pinctrl {
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc pinctrl_desc;
};

static const struct pinctrl_pin_desc max7360_pins[] = {
	PINCTRL_PIN(0, "PORT0"),
	PINCTRL_PIN(1, "PORT1"),
	PINCTRL_PIN(2, "PORT2"),
	PINCTRL_PIN(3, "PORT3"),
	PINCTRL_PIN(4, "PORT4"),
	PINCTRL_PIN(5, "PORT5"),
	PINCTRL_PIN(6, "PORT6"),
	PINCTRL_PIN(7, "PORT7"),
};

static const unsigned int port0_pins[] = {0};
static const unsigned int port1_pins[] = {1};
static const unsigned int port2_pins[] = {2};
static const unsigned int port3_pins[] = {3};
static const unsigned int port4_pins[] = {4};
static const unsigned int port5_pins[] = {5};
static const unsigned int port6_pins[] = {6};
static const unsigned int port7_pins[] = {7};
static const unsigned int rotary_pins[] = {6, 7};

static const struct pingroup max7360_groups[] = {
	PINCTRL_PINGROUP("PORT0", port0_pins, ARRAY_SIZE(port0_pins)),
	PINCTRL_PINGROUP("PORT1", port1_pins, ARRAY_SIZE(port1_pins)),
	PINCTRL_PINGROUP("PORT2", port2_pins, ARRAY_SIZE(port2_pins)),
	PINCTRL_PINGROUP("PORT3", port3_pins, ARRAY_SIZE(port3_pins)),
	PINCTRL_PINGROUP("PORT4", port4_pins, ARRAY_SIZE(port4_pins)),
	PINCTRL_PINGROUP("PORT5", port5_pins, ARRAY_SIZE(port5_pins)),
	PINCTRL_PINGROUP("PORT6", port6_pins, ARRAY_SIZE(port6_pins)),
	PINCTRL_PINGROUP("PORT7", port7_pins, ARRAY_SIZE(port7_pins)),
	PINCTRL_PINGROUP("ROTARY", rotary_pins, ARRAY_SIZE(rotary_pins)),
};

static int max7360_pinctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(max7360_groups);
}

static const char *max7360_pinctrl_get_group_name(struct pinctrl_dev *pctldev,
						  unsigned int group)
{
	return max7360_groups[group].name;
}

static int max7360_pinctrl_get_group_pins(struct pinctrl_dev *pctldev,
					  unsigned int group,
					  const unsigned int **pins,
					  unsigned int *num_pins)
{
	*pins = max7360_groups[group].pins;
	*num_pins = max7360_groups[group].npins;
	return 0;
}

static const struct pinctrl_ops max7360_pinctrl_ops = {
	.get_groups_count = max7360_pinctrl_get_groups_count,
	.get_group_name = max7360_pinctrl_get_group_name,
	.get_group_pins = max7360_pinctrl_get_group_pins,
#ifdef CONFIG_OF
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
#endif
};

static const char * const simple_groups[] = {
	"PORT0", "PORT1", "PORT2", "PORT3",
	"PORT4", "PORT5", "PORT6", "PORT7",
};

static const char * const rotary_groups[] = { "ROTARY" };

#define MAX7360_PINCTRL_FN_GPIO		0
#define MAX7360_PINCTRL_FN_PWM		1
#define MAX7360_PINCTRL_FN_ROTARY	2
static const struct pinfunction max7360_functions[] = {
	[MAX7360_PINCTRL_FN_GPIO] = PINCTRL_PINFUNCTION("gpio", simple_groups,
							ARRAY_SIZE(simple_groups)),
	[MAX7360_PINCTRL_FN_PWM] = PINCTRL_PINFUNCTION("pwm", simple_groups,
						       ARRAY_SIZE(simple_groups)),
	[MAX7360_PINCTRL_FN_ROTARY] = PINCTRL_PINFUNCTION("rotary", rotary_groups,
							  ARRAY_SIZE(rotary_groups)),
};

static int max7360_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(max7360_functions);
}

static const char *max7360_get_function_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	return max7360_functions[selector].name;
}

static int max7360_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
				       const char * const **groups,
				       unsigned int * const num_groups)
{
	*groups = max7360_functions[selector].groups;
	*num_groups = max7360_functions[selector].ngroups;

	return 0;
}

static int max7360_set_mux(struct pinctrl_dev *pctldev, unsigned int selector,
			   unsigned int group)
{
	struct regmap *regmap = dev_get_regmap(pctldev->dev->parent, NULL);
	int val;

	/*
	 * GPIO and PWM functions are the same: we only need to handle the
	 * rotary encoder function, on pins 6 and 7.
	 */
	if (max7360_groups[group].pins[0] >= 6) {
		if (selector == MAX7360_PINCTRL_FN_ROTARY)
			val = MAX7360_GPIO_CFG_RTR_EN;
		else
			val = 0;

		return regmap_write_bits(regmap, MAX7360_REG_GPIOCFG, MAX7360_GPIO_CFG_RTR_EN, val);
	}

	return 0;
}

static const struct pinmux_ops max7360_pmxops = {
	.get_functions_count = max7360_get_functions_count,
	.get_function_name = max7360_get_function_name,
	.get_function_groups = max7360_get_function_groups,
	.set_mux = max7360_set_mux,
	.strict = true,
};

static int max7360_pinctrl_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	struct pinctrl_desc *pd;
	struct max7360_pinctrl *chip;
	struct device *dev = &pdev->dev;

	regmap = dev_get_regmap(dev->parent, NULL);
	if (!regmap)
		return dev_err_probe(dev, -ENODEV, "Could not get parent regmap\n");

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	pd = &chip->pinctrl_desc;

	pd->pctlops = &max7360_pinctrl_ops;
	pd->pmxops = &max7360_pmxops;
	pd->name = dev_name(dev);
	pd->pins = max7360_pins;
	pd->npins = MAX7360_MAX_GPIO;
	pd->owner = THIS_MODULE;

	/*
	 * This MFD sub-device does not have any associated device tree node:
	 * properties are stored in the device node of the parent (MFD) device
	 * and this same node is used in phandles of client devices.
	 * Reuse this device tree node here, as otherwise the pinctrl subsystem
	 * would be confused by this topology.
	 */
	device_set_of_node_from_dev(dev, dev->parent);

	chip->pctldev = devm_pinctrl_register(dev, pd, chip);
	if (IS_ERR(chip->pctldev))
		return dev_err_probe(dev, PTR_ERR(chip->pctldev), "can't register controller\n");

	return 0;
}

static struct platform_driver max7360_pinctrl_driver = {
	.driver = {
		.name	= "max7360-pinctrl",
	},
	.probe		= max7360_pinctrl_probe,
};
module_platform_driver(max7360_pinctrl_driver);

MODULE_DESCRIPTION("MAX7360 pinctrl driver");
MODULE_AUTHOR("Mathieu Dubois-Briand <mathieu.dubois-briand@bootlin.com>");
MODULE_LICENSE("GPL");
