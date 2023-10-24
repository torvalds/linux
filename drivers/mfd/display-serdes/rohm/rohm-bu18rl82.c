// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * maxim-bu18rl82.c  --  I2C register interface access for bu18rl82 serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author: luowei <lw@rock-chips.com>
 */

#include "../core.h"
#include "rohm-bu18rl82.h"

#define PINCTRL_GROUP(a, b, c) { .name = a, .pins = b, .num_pins = c}

static bool bu18rl82_volatile_reg(struct device *dev, unsigned int reg)
{
	return true;
}

static struct regmap_config bu18rl82_regmap_config = {
	.name = "bu18rl82",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x0700,
	.volatile_reg = bu18rl82_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

static int BU18RL82_GPIO0_pins[] = {0};
static int BU18RL82_GPIO1_pins[] = {1};
static int BU18RL82_GPIO2_pins[] = {2};
static int BU18RL82_GPIO3_pins[] = {3};
static int BU18RL82_GPIO4_pins[] = {4};
static int BU18RL82_GPIO5_pins[] = {5};
static int BU18RL82_GPIO6_pins[] = {6};
static int BU18RL82_GPIO7_pins[] = {7};

#define GROUP_DESC(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
}

struct serdes_function_data {
	u8 gpio_rx_en:1;
	u16 gpio_id;
	u16 mdelay;
};

static const char *serdes_gpio_groups[] = {
	"BU18RL82_GPIO0", "BU18RL82_GPIO1", "BU18RL82_GPIO2", "BU18RL82_GPIO3",
	"BU18RL82_GPIO4", "BU18RL82_GPIO5", "BU18RL82_GPIO6", "BU18RL82_GPIO7",
};

/*des -> ser -> soc*/
#define FUNCTION_DESC_GPIO_INPUT(id) \
{ \
	.name = "DES_TO_SER_GPIO"#id, \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en = 1, .gpio_id = id + 2 } \
	}, \
} \

/*soc -> ser -> des*/
#define FUNCTION_DESC_GPIO_OUTPUT(id) \
{ \
	.name = "SER_GPIO"#id"_TO_DES", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en = 0, .gpio_id = id + 2 } \
	}, \
} \

#define FUNCTION_DES_DELAY_MS(ms) \
{ \
	.name = "DELAY_"#ms"MS", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .mdelay = ms, } \
	}, \
} \

/*des -> device*/
#define FUNCTION_DESC_GPIO_OUTPUT_HIGH() \
{ \
	.name = "DES_GPIO_OUTPUT_HIGH", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en = 0, .gpio_id = 1 } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT_LOW() \
{ \
	.name = "DES_GPIO_OUTPUT_LOW", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en = 0, .gpio_id = 0 } \
	}, \
} \

static struct pinctrl_pin_desc bu18rl82_pins_desc[] = {
	PINCTRL_PIN(ROHM_BU18RL82_GPIO0, "BU18RL82_GPIO0"),
	PINCTRL_PIN(ROHM_BU18RL82_GPIO1, "BU18RL82_GPIO1"),
	PINCTRL_PIN(ROHM_BU18RL82_GPIO2, "BU18RL82_GPIO2"),
	PINCTRL_PIN(ROHM_BU18RL82_GPIO3, "BU18RL82_GPIO3"),
	PINCTRL_PIN(ROHM_BU18RL82_GPIO4, "BU18RL82_GPIO4"),
	PINCTRL_PIN(ROHM_BU18RL82_GPIO5, "BU18RL82_GPIO5"),
	PINCTRL_PIN(ROHM_BU18RL82_GPIO6, "BU18RL82_GPIO6"),
	PINCTRL_PIN(ROHM_BU18RL82_GPIO7, "BU18RL82_GPIO7"),
};

static struct group_desc bu18rl82_groups_desc[] = {
	GROUP_DESC(BU18RL82_GPIO0),
	GROUP_DESC(BU18RL82_GPIO1),
	GROUP_DESC(BU18RL82_GPIO2),
	GROUP_DESC(BU18RL82_GPIO3),
	GROUP_DESC(BU18RL82_GPIO4),
	GROUP_DESC(BU18RL82_GPIO5),
	GROUP_DESC(BU18RL82_GPIO6),
	GROUP_DESC(BU18RL82_GPIO7),
};

static struct function_desc bu18rl82_functions_desc[] = {
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

	FUNCTION_DESC_GPIO_OUTPUT_HIGH(),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(),

	FUNCTION_DES_DELAY_MS(10),
	FUNCTION_DES_DELAY_MS(50),
	FUNCTION_DES_DELAY_MS(100),
	FUNCTION_DES_DELAY_MS(200),
	FUNCTION_DES_DELAY_MS(500),
};

static struct serdes_chip_pinctrl_info bu18rl82_pinctrl_info = {
	.pins = bu18rl82_pins_desc,
	.num_pins = ARRAY_SIZE(bu18rl82_pins_desc),
	.groups = bu18rl82_groups_desc,
	.num_groups = ARRAY_SIZE(bu18rl82_groups_desc),
	.functions = bu18rl82_functions_desc,
	.num_functions = ARRAY_SIZE(bu18rl82_functions_desc),
};

static void bu18rl82_bridge_swrst(struct serdes *serdes)
{
	struct device *dev = serdes->dev;
	int ret;

	ret = serdes_reg_write(serdes, BU18RL82_REG_RESET, BIT(0) | BIT(1) | BIT(7));
	if (ret < 0)
		dev_err(dev, "%s: failed to reset bu18rl82 0x11 ret=%d\n", __func__, ret);

	mdelay(20);

	SERDES_DBG_CHIP("%s: serdes %s ret=%d\n", __func__, serdes->chip_data->name, ret);
}

static void bu18rl82_enable_hwint(struct serdes *serdes, int enable)
{
	struct device *dev = serdes->dev;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(bu18rl82_reg_ien); i++) {
		if (enable) {
			ret = serdes_reg_write(serdes, bu18rl82_reg_ien[i].reg,
					       bu18rl82_reg_ien[i].ien);
			if (ret)
				dev_err(dev, "reg 0x%04x write error\n", bu18rl82_reg_ien[i].reg);
		} else {
			ret = serdes_reg_write(serdes, bu18rl82_reg_ien[i].reg, 0);
			if (ret)
				dev_err(dev, "reg 0x%04x write error\n", bu18rl82_reg_ien[i].reg);
		}
	}

	SERDES_DBG_CHIP("%s: serdes %s ret=%d\n", __func__,
			serdes->chip_data->name, enable);
}

static int bu18rl82_bridge_init(struct serdes *serdes)
{
	bu18rl82_bridge_swrst(serdes);

	return 0;
}

static int bu18rl82_bridge_pre_enable(struct serdes *serdes)
{
	int ret = 0;

	/* 1:enable 0:disable */
	bu18rl82_enable_hwint(serdes, 0);

	msleep(100);

	SERDES_DBG_CHIP("%s: serdes %s ret=%d\n", __func__,
			serdes->chip_data->name, ret);
	return ret;
}

static int bu18rl82_bridge_post_disable(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_bridge_ops bu18rl82_bridge_ops = {
	.enable = bu18rl82_bridge_pre_enable,
	.disable = bu18rl82_bridge_post_disable,
};

static int bu18rl82_pinctrl_config_get(struct serdes *serdes,
				       unsigned int pin, unsigned long *config)
{
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int bu18rl82_gpio_sw_reg, bu18rl82_gpio_pden_reg, bu18rl82_gpio_oen_reg;
	u16 arg = 0;

	serdes_reg_read(serdes, bu18rl82_gpio_sw[pin].reg, &bu18rl82_gpio_sw_reg);
	serdes_reg_read(serdes, bu18rl82_gpio_pden[pin].reg, &bu18rl82_gpio_pden_reg);
	serdes_reg_read(serdes, bu18rl82_gpio_oen[pin].reg, &bu18rl82_gpio_oen_reg);

	SERDES_DBG_CHIP("%s: serdes chip %s pin=%d param=%d\n",
			__func__, serdes->chip_data->name, pin, param);

	switch (param) {
	case PIN_CONFIG_DRIVE_STRENGTH:
		arg = FIELD_GET(BIT(2) | BIT(1), bu18rl82_gpio_sw_reg);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		arg = FIELD_GET(BIT(4), bu18rl82_gpio_pden_reg);
		break;
	case PIN_CONFIG_OUTPUT:
		arg = FIELD_GET(BIT(3), bu18rl82_gpio_oen_reg);
		break;
	default:
		return -EOPNOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	SERDES_DBG_CHIP("%s: serdes chip %s pin=%d arg=%d\n",
			__func__, serdes->chip_data->name, pin, arg);

	return 0;
}

static int bu18rl82_pinctrl_config_set(struct serdes *serdes,
				       unsigned int pin, unsigned long *configs,
				       unsigned int num_configs)
{
	enum pin_config_param param;
	u32 arg;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_DRIVE_STRENGTH:
			serdes_set_bits(serdes, bu18rl82_gpio_sw[pin].reg,
					bu18rl82_gpio_sw[pin].mask,
					FIELD_PREP(BIT(2) | BIT(1), arg));
			SERDES_DBG_CHIP("%s: serdes chip %s pin=%d i=%d drive-strength arg=0x%x\n",
					 __func__, serdes->chip_data->name, pin, i, arg);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			serdes_set_bits(serdes, bu18rl82_gpio_pden[pin].reg,
					bu18rl82_gpio_pden[i].mask,
					FIELD_PREP(BIT(4), arg));
			SERDES_DBG_CHIP("%s: serdes chip %s pin=%d i=%d pull-down arg=0x%x\n",
					__func__, serdes->chip_data->name, pin, i, arg);
			break;

			break;
		case PIN_CONFIG_OUTPUT:
			serdes_set_bits(serdes, bu18rl82_gpio_oen[pin].reg,
					bu18rl82_gpio_oen[i].mask,
					FIELD_PREP(BIT(3), arg));
			SERDES_DBG_CHIP("%s: serdes chip %s pin=%d i=%d output arg=0x%x\n",
					__func__, serdes->chip_data->name, pin, i, arg);
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

	return 0;
}

static int bu18rl82_pinctrl_set_mux(struct serdes *serdes,
				    unsigned int function, unsigned int group)
{
	struct serdes_pinctrl *pinctrl = serdes->pinctrl;
	struct function_desc *func;
	struct group_desc *grp;
	int i, offset;
	u16 ms;

	func = pinmux_generic_get_function(pinctrl->pctl, function);
	if (!func)
		return -EINVAL;

	grp = pinctrl_generic_get_group(pinctrl->pctl, group);
	if (!grp)
		return -EINVAL;

	SERDES_DBG_CHIP("%s: serdes chip %s func=%s data=%p group=%s data=%p, num_pin=%d\n",
			__func__, serdes->chip_data->name, func->name,
			func->data, grp->name, grp->data, grp->num_pins);

	if (func->data) {
		struct serdes_function_data *fdata = func->data;
		ms = fdata->mdelay;

		for (i = 0; i < grp->num_pins; i++) {
			offset = grp->pins[i] - pinctrl->pin_base;
			if (offset > 7)
				dev_err(serdes->dev, "%s gpio offset=%d too large > 7\n",
					serdes->chip_data->name, offset);
			else
				SERDES_DBG_CHIP("%s: serdes chip %s gpio_id=0x%x, offset=%d\n",
						__func__, serdes->chip_data->name, fdata->gpio_id, offset);

			if (!ms) {
				serdes_set_bits(serdes, bu18rl82_gpio_oen[offset].reg,
						bu18rl82_gpio_oen[offset].mask,
						FIELD_PREP(BIT(3), fdata->gpio_rx_en));

				serdes_set_bits(serdes, bu18rl82_gpio_id_low[offset].reg,
						bu18rl82_gpio_id_low[offset].mask,
						FIELD_PREP(GENMASK(7, 0),
						(fdata->gpio_id & 0xff)));

				serdes_set_bits(serdes, bu18rl82_gpio_id_high[offset].reg,
						bu18rl82_gpio_id_high[offset].mask,
						FIELD_PREP(GENMASK(2, 0),
						((fdata->gpio_id >> 8) & 0x7)));
				serdes_set_bits(serdes, bu18rl82_gpio_pden[offset].reg,
						bu18rl82_gpio_pden[offset].mask,
						FIELD_PREP(BIT(4), 0));
			} else {
				mdelay(ms);
				SERDES_DBG_CHIP("%s: delay %d ms\n",
						__func__, ms);
			}
		}
	}

	return 0;
}

static struct serdes_chip_pinctrl_ops bu18rl82_pinctrl_ops = {
	.pin_config_get = bu18rl82_pinctrl_config_get,
	.pin_config_set = bu18rl82_pinctrl_config_set,
	.set_mux = bu18rl82_pinctrl_set_mux,
};

static int bu18rl82_gpio_direction_input(struct serdes *serdes, int gpio)
{
	return 0;
}

static int bu18rl82_gpio_direction_output(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int bu18rl82_gpio_get_level(struct serdes *serdes, int gpio)
{
	return 0;
}

static int bu18rl82_gpio_set_level(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int bu18rl82_gpio_set_config(struct serdes *serdes, int gpio, unsigned long config)
{
	return 0;
}

static int bu18rl82_gpio_to_irq(struct serdes *serdes, int gpio)
{
	return 0;
}

static struct serdes_chip_gpio_ops bu18rl82_gpio_ops = {
	.direction_input = bu18rl82_gpio_direction_input,
	.direction_output = bu18rl82_gpio_direction_output,
	.get_level = bu18rl82_gpio_get_level,
	.set_level = bu18rl82_gpio_set_level,
	.set_config = bu18rl82_gpio_set_config,
	.to_irq = bu18rl82_gpio_to_irq,
};

static int bu18rl82_pm_suspend(struct serdes *serdes)
{
	return 0;
}

static int bu18rl82_pm_resume(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_pm_ops bu18rl82_pm_ops = {
	.suspend = bu18rl82_pm_suspend,
	.resume = bu18rl82_pm_resume,
};

static int bu18rl82_irq_lock_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static int bu18rl82_irq_err_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static struct serdes_chip_irq_ops bu18rl82_irq_ops = {
	.lock_handle = bu18rl82_irq_lock_handle,
	.err_handle = bu18rl82_irq_err_handle,
};

struct serdes_chip_data serdes_bu18rl82_data = {
	.name		= "bu18rl82",
	.serdes_type	= TYPE_DES,
	.serdes_id	= ROHM_ID_BU18RL82,
	.bridge_type	= TYPE_BRIDGE_BRIDGE,
	.connector_type	= DRM_MODE_CONNECTOR_LVDS,
	.chip_init	= bu18rl82_bridge_init,
	.regmap_config	= &bu18rl82_regmap_config,
	.pinctrl_info	= &bu18rl82_pinctrl_info,
	.bridge_ops	= &bu18rl82_bridge_ops,
	.pinctrl_ops	= &bu18rl82_pinctrl_ops,
	.gpio_ops	= &bu18rl82_gpio_ops,
	.pm_ops		= &bu18rl82_pm_ops,
	.irq_ops	= &bu18rl82_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_bu18rl82_data);

MODULE_LICENSE("GPL");
