// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * maxim-max96752.c  --  I2C register interface access for max96752 serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author:
 */

#include "../core.h"
#include "maxim-max96752.h"

static const struct regmap_range max96752_readable_ranges[] = {
	regmap_reg_range(0x0000, 0x0600),
};

static const struct regmap_access_table max96752_readable_table = {
	.yes_ranges = max96752_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(max96752_readable_ranges),
};

static struct regmap_config max96752_regmap_config = {
	.name = "max96752",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0xffff,
	.rd_table = &max96752_readable_table,
};

static int MAX96752_MFP0_pins[] = {0};
static int MAX96752_MFP1_pins[] = {1};
static int MAX96752_MFP2_pins[] = {2};
static int MAX96752_MFP3_pins[] = {3};
static int MAX96752_MFP4_pins[] = {4};
static int MAX96752_MFP5_pins[] = {5};
static int MAX96752_MFP6_pins[] = {6};
static int MAX96752_MFP7_pins[] = {7};

static int MAX96752_MFP8_pins[] = {8};
static int MAX96752_MFP9_pins[] = {9};
static int MAX96752_MFP10_pins[] = {10};
static int MAX96752_MFP11_pins[] = {11};
static int MAX96752_MFP12_pins[] = {12};
static int MAX96752_MFP13_pins[] = {13};
static int MAX96752_MFP14_pins[] = {14};
static int MAX96752_MFP15_pins[] = {15};

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
	"MAX96752_MFP0", "MAX96752_MFP1", "MAX96752_MFP2", "MAX96752_MFP3",
	"MAX96752_MFP4", "MAX96752_MFP5", "MAX96752_MFP6", "MAX96752_MFP7",

	"MAX96752_MFP8", "MAX96752_MFP9", "MAX96752_MFP10", "MAX96752_MFP11",
	"MAX96752_MFP12", "MAX96752_MFP13", "MAX96752_MFP14", "MAX96752_MFP15",
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

static struct pinctrl_pin_desc max96752_pins_desc[] = {
	PINCTRL_PIN(MAXIM_MAX96752_MFP0, "MAX96752_MFP0"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP1, "MAX96752_MFP1"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP2, "MAX96752_MFP2"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP3, "MAX96752_MFP3"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP4, "MAX96752_MFP4"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP5, "MAX96752_MFP5"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP6, "MAX96752_MFP6"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP7, "MAX96752_MFP7"),

	PINCTRL_PIN(MAXIM_MAX96752_MFP8, "MAX96752_MFP8"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP9, "MAX96752_MFP9"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP10, "MAX96752_MFP10"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP11, "MAX96752_MFP11"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP12, "MAX96752_MFP12"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP13, "MAX96752_MFP13"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP14, "MAX96752_MFP14"),
	PINCTRL_PIN(MAXIM_MAX96752_MFP15, "MAX96752_MFP15"),
};

static struct group_desc max96752_groups_desc[] = {
	GROUP_DESC(MAX96752_MFP0),
	GROUP_DESC(MAX96752_MFP1),
	GROUP_DESC(MAX96752_MFP2),
	GROUP_DESC(MAX96752_MFP3),
	GROUP_DESC(MAX96752_MFP4),
	GROUP_DESC(MAX96752_MFP5),
	GROUP_DESC(MAX96752_MFP6),
	GROUP_DESC(MAX96752_MFP7),

	GROUP_DESC(MAX96752_MFP8),
	GROUP_DESC(MAX96752_MFP9),
	GROUP_DESC(MAX96752_MFP10),
	GROUP_DESC(MAX96752_MFP11),
	GROUP_DESC(MAX96752_MFP12),
	GROUP_DESC(MAX96752_MFP13),
	GROUP_DESC(MAX96752_MFP14),
	GROUP_DESC(MAX96752_MFP15),
};

static struct function_desc max96752_functions_desc[] = {
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

static struct serdes_chip_pinctrl_info max96752_pinctrl_info = {
	.pins = max96752_pins_desc,
	.num_pins = ARRAY_SIZE(max96752_pins_desc),
	.groups = max96752_groups_desc,
	.num_groups = ARRAY_SIZE(max96752_groups_desc),
	.functions = max96752_functions_desc,
	.num_functions = ARRAY_SIZE(max96752_functions_desc),
};

static int max96752_panel_prepare(struct serdes *serdes)
{
	return 0;
}

static int max96752_panel_unprepare(struct serdes *serdes)
{
	//serdes_reg_write(serdes, 0x0215, 0x80);	/* lcd_en */

	return 0;
}

static int max96752_panel_enable(struct serdes *serdes)
{
	return 0;
}

static int max96752_panel_disable(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_panel_ops max96752_panel_ops = {
	.prepare = max96752_panel_prepare,
	.unprepare = max96752_panel_unprepare,
	.enable = max96752_panel_enable,
	.disable = max96752_panel_disable,
};

static int max96752_pinctrl_config_get(struct serdes *serdes,
				       unsigned int pin,
				       unsigned long *config)
{
	return 0;
}

static int max96752_pinctrl_config_set(struct serdes *serdes,
				       unsigned int pin,
				       unsigned long *configs,
				       unsigned int num_configs)
{
	return 0;
}

static int max96752_pinctrl_set_mux(struct serdes *serdes, unsigned int func_selector,
				    unsigned int group_selector)
{
	return 0;
}

static struct serdes_chip_pinctrl_ops max96752_pinctrl_ops = {
	.pin_config_get = max96752_pinctrl_config_get,
	.pin_config_set = max96752_pinctrl_config_set,
	.set_mux = max96752_pinctrl_set_mux,
};

static int max96752_gpio_direction_input(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96752_gpio_direction_output(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96752_gpio_get_level(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96752_gpio_set_level(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96752_gpio_set_config(struct serdes *serdes, int gpio, unsigned long config)
{
	return 0;
}

static int max96752_gpio_to_irq(struct serdes *serdes, int gpio)
{
	return 0;
}

static struct serdes_chip_gpio_ops max96752_gpio_ops = {
	.direction_input = max96752_gpio_direction_input,
	.direction_output = max96752_gpio_direction_output,
	.get_level = max96752_gpio_get_level,
	.set_level = max96752_gpio_set_level,
	.set_config = max96752_gpio_set_config,
	.to_irq = max96752_gpio_to_irq,
};

static int max96752_pm_suspend(struct serdes *serdes)
{
	return 0;
}

static int max96752_pm_resume(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_pm_ops max96752_pm_ops = {
	.suspend = max96752_pm_suspend,
	.resume = max96752_pm_resume,
};

static int max96752_irq_lock_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static int max96752_irq_err_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static struct serdes_chip_irq_ops max96752_irq_ops = {
	.lock_handle = max96752_irq_lock_handle,
	.err_handle = max96752_irq_err_handle,
};

struct serdes_chip_data serdes_max96752_data = {
	.name		= "max96752",
	.serdes_type	= TYPE_DES,
	.serdes_id	= MAXIM_ID_MAX96752,
	.connector_type	= DRM_MODE_CONNECTOR_LVDS,
	.regmap_config	= &max96752_regmap_config,
	.pinctrl_info	= &max96752_pinctrl_info,
	.panel_ops	= &max96752_panel_ops,
	.pinctrl_ops	= &max96752_pinctrl_ops,
	.gpio_ops	= &max96752_gpio_ops,
	.pm_ops		= &max96752_pm_ops,
	.irq_ops	= &max96752_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_max96752_data);

MODULE_LICENSE("GPL");
