// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * maxim-max96772.c  --  I2C register interface access for max96772 serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author:
 */

#include "../core.h"
#include "maxim-max96772.h"

static const struct regmap_range max96772_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x0800),
	regmap_reg_range(0x1700, 0x1700),
	regmap_reg_range(0x4100, 0x4100),
	regmap_reg_range(0x6230, 0x6230),
	regmap_reg_range(0xe75e, 0xe75e),
	regmap_reg_range(0xe776, 0xe7bf),
};

static const struct regmap_access_table max96772_readable_table = {
	.yes_ranges = max96772_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max96772_readable_ranges),
};

static struct regmap_config max96772_regmap_config = {
	.name = "max96772",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.rd_table = &max96772_readable_table,
};

static int MAX96772_MFP0_pins[] = {0};
static int MAX96772_MFP1_pins[] = {1};
static int MAX96772_MFP2_pins[] = {2};
static int MAX96772_MFP3_pins[] = {3};
static int MAX96772_MFP4_pins[] = {4};
static int MAX96772_MFP5_pins[] = {5};
static int MAX96772_MFP6_pins[] = {6};
static int MAX96772_MFP7_pins[] = {7};

static int MAX96772_MFP8_pins[] = {8};
static int MAX96772_MFP9_pins[] = {9};
static int MAX96772_MFP10_pins[] = {10};
static int MAX96772_MFP11_pins[] = {11};
static int MAX96772_MFP12_pins[] = {12};
static int MAX96772_MFP13_pins[] = {13};
static int MAX96772_MFP14_pins[] = {14};
static int MAX96772_MFP15_pins[] = {15};

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
	"MAX96772_MFP0", "MAX96772_MFP1", "MAX96772_MFP2", "MAX96772_MFP3",
	"MAX96772_MFP4", "MAX96772_MFP5", "MAX96772_MFP6", "MAX96772_MFP7",

	"MAX96772_MFP8", "MAX96772_MFP9", "MAX96772_MFP10", "MAX96772_MFP11",
	"MAX96772_MFP12", "MAX96772_MFP13", "MAX96772_MFP14", "MAX96772_MFP15",
};

#define FUNCTION_DESC_GPIO_INPUT(id) \
{ \
	.name = "MFP"#id"_INPUT", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT(id) \
{ \
	.name = "MFP"#id"_OUTPUT", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en = 1, .gpio_tx_id = id } \
	}, \
} \

static struct pinctrl_pin_desc max96772_pins_desc[] = {
	PINCTRL_PIN(MAXIM_MAX96772_MFP0, "MAX96772_MFP0"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP1, "MAX96772_MFP1"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP2, "MAX96772_MFP2"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP3, "MAX96772_MFP3"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP4, "MAX96772_MFP4"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP5, "MAX96772_MFP5"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP6, "MAX96772_MFP6"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP7, "MAX96772_MFP7"),

	PINCTRL_PIN(MAXIM_MAX96772_MFP8, "MAX96772_MFP8"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP9, "MAX96772_MFP9"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP10, "MAX96772_MFP10"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP11, "MAX96772_MFP11"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP12, "MAX96772_MFP12"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP13, "MAX96772_MFP13"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP14, "MAX96772_MFP14"),
	PINCTRL_PIN(MAXIM_MAX96772_MFP15, "MAX96772_MFP15"),
};

static struct group_desc max96772_groups_desc[] = {
	GROUP_DESC(MAX96772_MFP0),
	GROUP_DESC(MAX96772_MFP1),
	GROUP_DESC(MAX96772_MFP2),
	GROUP_DESC(MAX96772_MFP3),
	GROUP_DESC(MAX96772_MFP4),
	GROUP_DESC(MAX96772_MFP5),
	GROUP_DESC(MAX96772_MFP6),
	GROUP_DESC(MAX96772_MFP7),

	GROUP_DESC(MAX96772_MFP8),
	GROUP_DESC(MAX96772_MFP9),
	GROUP_DESC(MAX96772_MFP10),
	GROUP_DESC(MAX96772_MFP11),
	GROUP_DESC(MAX96772_MFP12),
	GROUP_DESC(MAX96772_MFP13),
	GROUP_DESC(MAX96772_MFP14),
	GROUP_DESC(MAX96772_MFP15),
};

static struct function_desc max96772_functions_desc[] = {
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

};

static struct serdes_chip_pinctrl_info max96772_pinctrl_info = {
	.pins = max96772_pins_desc,
	.num_pins = ARRAY_SIZE(max96772_pins_desc),
	.groups = max96772_groups_desc,
	.num_groups = ARRAY_SIZE(max96772_groups_desc),
	.functions = max96772_functions_desc,
	.num_functions = ARRAY_SIZE(max96772_functions_desc),
};

static int max96772_panel_init(struct serdes *serdes)
{
	return 0;
}

static int max96772_panel_prepare(struct serdes *serdes)
{
	return 0;
}

static int max96772_panel_unprepare(struct serdes *serdes)
{
	return 0;
}

static int max96772_panel_enable(struct serdes *serdes)
{
	return 0;
}

static int max96772_panel_disable(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_panel_ops max96772_panel_ops = {
	.init = max96772_panel_init,
	.prepare = max96772_panel_prepare,
	.unprepare = max96772_panel_unprepare,
	.enable = max96772_panel_enable,
	.disable = max96772_panel_disable,
};

static int max96772_pinctrl_config_get(struct serdes *serdes,
				       unsigned int pin,
				       unsigned long *config)
{
	return 0;
}

static int max96772_pinctrl_config_set(struct serdes *serdes,
				       unsigned int pin,
				       unsigned long *configs,
				       unsigned int num_configs)
{
	return 0;
}

static int max96772_pinctrl_set_mux(struct serdes *serdes, unsigned int func_selector,
				    unsigned int group_selector)
{
	return 0;
}

static struct serdes_chip_pinctrl_ops max96772_pinctrl_ops = {
	.pin_config_get = max96772_pinctrl_config_get,
	.pin_config_set = max96772_pinctrl_config_set,
	.set_mux = max96772_pinctrl_set_mux,
};

static int max96772_gpio_direction_input(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96772_gpio_direction_output(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96772_gpio_get_level(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96772_gpio_set_level(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96772_gpio_set_config(struct serdes *serdes, int gpio, unsigned long config)
{
	return 0;
}

static int max96772_gpio_to_irq(struct serdes *serdes, int gpio)
{
	return 0;
}

static struct serdes_chip_gpio_ops max96772_gpio_ops = {
	.direction_input = max96772_gpio_direction_input,
	.direction_output = max96772_gpio_direction_output,
	.get_level = max96772_gpio_get_level,
	.set_level = max96772_gpio_set_level,
	.set_config = max96772_gpio_set_config,
	.to_irq = max96772_gpio_to_irq,
};

static int max96772_pm_suspend(struct serdes *serdes)
{
	return 0;
}

static int max96772_pm_resume(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_pm_ops max96772_pm_ops = {
	.suspend = max96772_pm_suspend,
	.resume = max96772_pm_resume,
};

static int max96772_irq_lock_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static int max96772_irq_err_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static struct serdes_chip_irq_ops max96772_irq_ops = {
	.lock_handle = max96772_irq_lock_handle,
	.err_handle = max96772_irq_err_handle,
};

struct serdes_chip_data serdes_max96772_data = {
	.name		= "max96772",
	.serdes_type	= TYPE_DES,
	.serdes_id	= MAXIM_ID_MAX96772,
	.connector_type	= DRM_MODE_CONNECTOR_eDP,
	.regmap_config	= &max96772_regmap_config,
	.pinctrl_info	= &max96772_pinctrl_info,
	.panel_ops	= &max96772_panel_ops,
	.pinctrl_ops	= &max96772_pinctrl_ops,
	.gpio_ops	= &max96772_gpio_ops,
	.pm_ops		= &max96772_pm_ops,
	.irq_ops	= &max96772_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_max96772_data);

MODULE_LICENSE("GPL");
