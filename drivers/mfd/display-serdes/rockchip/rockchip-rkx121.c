// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rockchip-rkx121.c  --  I2C register interface access for rkx121 serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "../core.h"

static bool rkx121_volatile_reg(struct device *dev, unsigned int reg)
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

static struct regmap_config rkx121_regmap_config = {
	.name = "rkx121",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x8000,
	.volatile_reg = rkx121_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static struct pinctrl_pin_desc rkx121_pins_desc[] = {
};

static struct group_desc rkx121_groups_desc[] = {
	{ "null", NULL, 1, },
};

static struct function_desc rkx121_functions_desc[] = {
	{ "null", NULL, 1, },
};

static struct serdes_chip_pinctrl_info rkx121_pinctrl_info = {
	.pins = rkx121_pins_desc,
	.num_pins = ARRAY_SIZE(rkx121_pins_desc),
	.groups = rkx121_groups_desc,
	.num_groups = ARRAY_SIZE(rkx121_groups_desc),
	.functions = rkx121_functions_desc,
	.num_functions = ARRAY_SIZE(rkx121_functions_desc),
};

static int rkx121_bridge_init(struct serdes *serdes)
{
	return 0;
}

static int rkx121_bridge_enable(struct serdes *serdes)
{
	return 0;
}

static int rkx121_bridge_disable(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_panel_ops rkx121_panel_ops = {
	.init = rkx121_bridge_init,
	.enable = rkx121_bridge_enable,
	.disable = rkx121_bridge_disable,
};

static int rkx121_pinctrl_config_get(struct serdes *serdes,
				     unsigned int pin,
				     unsigned long *config)
{
	return 0;
}

static int rkx121_pinctrl_config_set(struct serdes *serdes,
				     unsigned int pin,
				     unsigned long *configs,
				     unsigned int num_configs)
{
	return 0;
}

static int rkx121_pinctrl_set_mux(struct serdes *serdes, unsigned int func_selector,
				  unsigned int group_selector)
{
	return 0;
}

static struct serdes_chip_pinctrl_ops rkx121_pinctrl_ops = {
	.pin_config_get = rkx121_pinctrl_config_get,
	.pin_config_set = rkx121_pinctrl_config_set,
	.set_mux = rkx121_pinctrl_set_mux,
};

static int rkx121_gpio_direction_input(struct serdes *serdes, int gpio)
{
	return 0;
}

static int rkx121_gpio_direction_output(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int rkx121_gpio_get_level(struct serdes *serdes, int gpio)
{
	return 0;
}

static int rkx121_gpio_set_level(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int rkx121_gpio_set_config(struct serdes *serdes, int gpio, unsigned long config)
{
	return 0;
}

static int rkx121_gpio_to_irq(struct serdes *serdes, int gpio)
{
	return 0;
}

static struct serdes_chip_gpio_ops rkx121_gpio_ops = {
	.direction_input = rkx121_gpio_direction_input,
	.direction_output = rkx121_gpio_direction_output,
	.get_level = rkx121_gpio_get_level,
	.set_level = rkx121_gpio_set_level,
	.set_config = rkx121_gpio_set_config,
	.to_irq = rkx121_gpio_to_irq,
};

static int rkx121_pm_suspend(struct serdes *serdes)
{
	return 0;
}

static int rkx121_pm_resume(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_pm_ops rkx121_pm_ops = {
	.suspend = rkx121_pm_suspend,
	.resume = rkx121_pm_resume,
};

static int rkx121_irq_lock_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static int rkx121_irq_err_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static struct serdes_chip_irq_ops rkx121_irq_ops = {
	.lock_handle = rkx121_irq_lock_handle,
	.err_handle = rkx121_irq_err_handle,
};

struct serdes_chip_data serdes_rkx121_data = {
	.name		= "rkx121",
	.serdes_type	= TYPE_DES,
	.serdes_id	= ROCKCHIP_ID_RKX121,
	.connector_type	= DRM_MODE_CONNECTOR_LVDS,
	.regmap_config	= &rkx121_regmap_config,
	.pinctrl_info	= &rkx121_pinctrl_info,
	.panel_ops	= &rkx121_panel_ops,
	.pinctrl_ops	= &rkx121_pinctrl_ops,
	.gpio_ops	= &rkx121_gpio_ops,
	.pm_ops		= &rkx121_pm_ops,
	.irq_ops	= &rkx121_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_rkx121_data);

MODULE_LICENSE("GPL");
