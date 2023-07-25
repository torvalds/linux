// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * maxim-max96789.c  --  I2C register interface access for max96789 serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author:
 */

#include "../core.h"
#include "maxim-max96789.h"

static bool max96789_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case 0x0076:
	case 0x0086:
	case 0x0100:
	case 0x0200 ... 0x02ce:
	case 0x7000:
	case 0x7070:
	case 0x7074:
		return false;
	default:
		return true;
	}
}

static struct regmap_config max96789_regmap_config = {
	.name = "max96789",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x8000,
	.volatile_reg = max96789_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static int MAX96789_MFP0_pins[] = {0};
static int MAX96789_MFP1_pins[] = {1};
static int MAX96789_MFP2_pins[] = {2};
static int MAX96789_MFP3_pins[] = {3};
static int MAX96789_MFP4_pins[] = {4};
static int MAX96789_MFP5_pins[] = {5};
static int MAX96789_MFP6_pins[] = {6};
static int MAX96789_MFP7_pins[] = {7};

static int MAX96789_MFP8_pins[] = {8};
static int MAX96789_MFP9_pins[] = {9};
static int MAX96789_MFP10_pins[] = {10};
static int MAX96789_MFP11_pins[] = {11};
static int MAX96789_MFP12_pins[] = {12};
static int MAX96789_MFP13_pins[] = {13};
static int MAX96789_MFP14_pins[] = {14};
static int MAX96789_MFP15_pins[] = {15};

static int MAX96789_MFP16_pins[] = {16};
static int MAX96789_MFP17_pins[] = {17};
static int MAX96789_MFP18_pins[] = {18};
static int MAX96789_MFP19_pins[] = {19};
static int MAX96789_MFP20_pins[] = {20};

#define GROUP_DESC(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
}

struct serdes_function_data {
	u8 gpio_out_dis:1;
	u8 gpio_tx_en:1;
	u8 gpio_rx_en:1;
	u8 gpio_tx_id;
	u8 gpio_rx_id;
};

static const char *serdes_gpio_groups[] = {
	"MAX96789_MFP0", "MAX96789_MFP1", "MAX96789_MFP2", "MAX96789_MFP3",
	"MAX96789_MFP4", "MAX96789_MFP5", "MAX96789_MFP6", "MAX96789_MFP7",

	"MAX96789_MFP8", "MAX96789_MFP9", "MAX96789_MFP10", "MAX96789_MFP11",
	"MAX96789_MFP12", "MAX96789_MFP13", "MAX96789_MFP14", "MAX96789_MFP15",

	"MAX96789_MFP16", "MAX96789_MFP17", "MAX96789_MFP18", "MAX96789_MFP19",
	"MAX96789_MFP20",
};

#define FUNCTION_DESC_GPIO_INPUT(id) \
{ \
	.name = "DES_GPIO"#id"_INPUT", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT(id) \
{ \
	.name = "DES_GPIO"#id"_OUTPUT", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en = 1, .gpio_tx_id = id } \
	}, \
} \

static struct pinctrl_pin_desc max96789_pins_desc[] = {
	PINCTRL_PIN(MAXIM_MAX96789_MFP0, "MAX96789_MFP0"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP1, "MAX96789_MFP1"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP2, "MAX96789_MFP2"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP3, "MAX96789_MFP3"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP4, "MAX96789_MFP4"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP5, "MAX96789_MFP5"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP6, "MAX96789_MFP6"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP7, "MAX96789_MFP7"),

	PINCTRL_PIN(MAXIM_MAX96789_MFP8, "MAX96789_MFP8"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP9, "MAX96789_MFP9"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP10, "MAX96789_MFP10"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP11, "MAX96789_MFP11"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP12, "MAX96789_MFP12"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP13, "MAX96789_MFP13"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP14, "MAX96789_MFP14"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP15, "MAX96789_MFP15"),

	PINCTRL_PIN(MAXIM_MAX96789_MFP16, "MAX96789_MFP16"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP17, "MAX96789_MFP17"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP18, "MAX96789_MFP18"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP19, "MAX96789_MFP19"),
	PINCTRL_PIN(MAXIM_MAX96789_MFP20, "MAX96789_MFP20"),
};

static struct group_desc max96789_groups_desc[] = {
	GROUP_DESC(MAX96789_MFP0),
	GROUP_DESC(MAX96789_MFP1),
	GROUP_DESC(MAX96789_MFP2),
	GROUP_DESC(MAX96789_MFP3),
	GROUP_DESC(MAX96789_MFP4),
	GROUP_DESC(MAX96789_MFP5),
	GROUP_DESC(MAX96789_MFP6),
	GROUP_DESC(MAX96789_MFP7),

	GROUP_DESC(MAX96789_MFP8),
	GROUP_DESC(MAX96789_MFP9),
	GROUP_DESC(MAX96789_MFP10),
	GROUP_DESC(MAX96789_MFP11),
	GROUP_DESC(MAX96789_MFP12),
	GROUP_DESC(MAX96789_MFP13),
	GROUP_DESC(MAX96789_MFP14),
	GROUP_DESC(MAX96789_MFP15),

	GROUP_DESC(MAX96789_MFP16),
	GROUP_DESC(MAX96789_MFP17),
	GROUP_DESC(MAX96789_MFP18),
	GROUP_DESC(MAX96789_MFP19),
	GROUP_DESC(MAX96789_MFP20),
};

static struct function_desc max96789_functions_desc[] = {
	FUNCTION_DESC_GPIO_INPUT(0),
	FUNCTION_DESC_GPIO_INPUT(1),
	FUNCTION_DESC_GPIO_INPUT(2),
	FUNCTION_DESC_GPIO_INPUT(3),
	FUNCTION_DESC_GPIO_INPUT(4),
	FUNCTION_DESC_GPIO_INPUT(5),
	FUNCTION_DESC_GPIO_INPUT(6),
	FUNCTION_DESC_GPIO_INPUT(7),

	FUNCTION_DESC_GPIO_INPUT(8),
	FUNCTION_DESC_GPIO_INPUT(9),
	FUNCTION_DESC_GPIO_INPUT(10),
	FUNCTION_DESC_GPIO_INPUT(11),
	FUNCTION_DESC_GPIO_INPUT(12),
	FUNCTION_DESC_GPIO_INPUT(13),
	FUNCTION_DESC_GPIO_INPUT(14),
	FUNCTION_DESC_GPIO_INPUT(15),

	FUNCTION_DESC_GPIO_INPUT(16),
	FUNCTION_DESC_GPIO_INPUT(17),
	FUNCTION_DESC_GPIO_INPUT(18),
	FUNCTION_DESC_GPIO_INPUT(19),
	FUNCTION_DESC_GPIO_INPUT(20),

	FUNCTION_DESC_GPIO_OUTPUT(0),
	FUNCTION_DESC_GPIO_OUTPUT(1),
	FUNCTION_DESC_GPIO_OUTPUT(2),
	FUNCTION_DESC_GPIO_OUTPUT(3),
	FUNCTION_DESC_GPIO_OUTPUT(4),
	FUNCTION_DESC_GPIO_OUTPUT(5),
	FUNCTION_DESC_GPIO_OUTPUT(6),
	FUNCTION_DESC_GPIO_OUTPUT(7),

	FUNCTION_DESC_GPIO_OUTPUT(8),
	FUNCTION_DESC_GPIO_OUTPUT(9),
	FUNCTION_DESC_GPIO_OUTPUT(10),
	FUNCTION_DESC_GPIO_OUTPUT(11),
	FUNCTION_DESC_GPIO_OUTPUT(12),
	FUNCTION_DESC_GPIO_OUTPUT(13),
	FUNCTION_DESC_GPIO_OUTPUT(14),
	FUNCTION_DESC_GPIO_OUTPUT(15),

	FUNCTION_DESC_GPIO_OUTPUT(16),
	FUNCTION_DESC_GPIO_OUTPUT(17),
	FUNCTION_DESC_GPIO_OUTPUT(18),
	FUNCTION_DESC_GPIO_OUTPUT(19),
	FUNCTION_DESC_GPIO_OUTPUT(20),

};

static struct serdes_chip_pinctrl_info max96789_pinctrl_info = {
	.pins = max96789_pins_desc,
	.num_pins = ARRAY_SIZE(max96789_pins_desc),
	.groups = max96789_groups_desc,
	.num_groups = ARRAY_SIZE(max96789_groups_desc),
	.functions = max96789_functions_desc,
	.num_functions = ARRAY_SIZE(max96789_functions_desc),
};

static int max96789_bridge_init(struct serdes *serdes)
{
	return 0;
}

static int max96789_bridge_enable(struct serdes *serdes)
{
	return 0;
}

static int max96789_bridge_disable(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_bridge_ops max96789_bridge_ops = {
	.init = max96789_bridge_init,
	.enable = max96789_bridge_enable,
	.disable = max96789_bridge_disable,
};

static int max96789_pinctrl_config_get(struct serdes *serdes,
				       unsigned int pin,
				       unsigned long *config)
{
	return 0;
}

static int max96789_pinctrl_config_set(struct serdes *serdes,
				       unsigned int pin,
				       unsigned long *configs,
				       unsigned int num_configs)
{
	return 0;
}

static int max96789_pinctrl_set_mux(struct serdes *serdes, unsigned int func_selector,
				    unsigned int group_selector)
{
	return 0;
}

static struct serdes_chip_pinctrl_ops max96789_pinctrl_ops = {
	.pin_config_get = max96789_pinctrl_config_get,
	.pin_config_set = max96789_pinctrl_config_set,
	.set_mux = max96789_pinctrl_set_mux,
};

static int max96789_gpio_direction_input(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96789_gpio_direction_output(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96789_gpio_get_level(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96789_gpio_set_level(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96789_gpio_set_config(struct serdes *serdes, int gpio, unsigned long config)
{
	return 0;
}

static int max96789_gpio_to_irq(struct serdes *serdes, int gpio)
{
	return 0;
}

static struct serdes_chip_gpio_ops max96789_gpio_ops = {
	.direction_input = max96789_gpio_direction_input,
	.direction_output = max96789_gpio_direction_output,
	.get_level = max96789_gpio_get_level,
	.set_level = max96789_gpio_set_level,
	.set_config = max96789_gpio_set_config,
	.to_irq = max96789_gpio_to_irq,
};

static int max96789_pm_suspend(struct serdes *serdes)
{
	return 0;
}

static int max96789_pm_resume(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_pm_ops max96789_pm_ops = {
	.suspend = max96789_pm_suspend,
	.resume = max96789_pm_resume,
};

static int max96789_irq_lock_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static int max96789_irq_err_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static struct serdes_chip_irq_ops max96789_irq_ops = {
	.lock_handle = max96789_irq_lock_handle,
	.err_handle = max96789_irq_err_handle,
};

struct serdes_chip_data serdes_max96789_data = {
	.name		= "max96789",
	.serdes_type	= TYPE_SER,
	.serdes_id	= MAXIM_ID_MAX96789,
	.connector_type	= DRM_MODE_CONNECTOR_DSI,
	.regmap_config	= &max96789_regmap_config,
	.pinctrl_info	= &max96789_pinctrl_info,
	.bridge_ops	= &max96789_bridge_ops,
	.pinctrl_ops	= &max96789_pinctrl_ops,
	.gpio_ops	= &max96789_gpio_ops,
	.pm_ops		= &max96789_pm_ops,
	.irq_ops	= &max96789_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_max96789_data);

MODULE_LICENSE("GPL");
