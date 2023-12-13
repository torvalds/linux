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

struct config_desc {
	u16 reg;
	u8 mask;
	u8 val;
};

struct serdes_group_data {
	const struct config_desc *configs;
	int num_configs;
};

static int MAX96752_GPIO0_pins[] = {0};
static int MAX96752_GPIO1_pins[] = {1};
static int MAX96752_GPIO2_pins[] = {2};
static int MAX96752_GPIO3_pins[] = {3};
static int MAX96752_GPIO4_pins[] = {4};
static int MAX96752_GPIO5_pins[] = {5};
static int MAX96752_GPIO6_pins[] = {6};
static int MAX96752_GPIO7_pins[] = {7};

static int MAX96752_GPIO8_pins[] = {8};
static int MAX96752_GPIO9_pins[] = {9};
static int MAX96752_GPIO10_pins[] = {10};
static int MAX96752_GPIO11_pins[] = {11};
static int MAX96752_GPIO12_pins[] = {12};
static int MAX96752_GPIO13_pins[] = {13};
static int MAX96752_GPIO14_pins[] = {14};
static int MAX96752_GPIO15_pins[] = {15};

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
	u8 gpio_in_level:1;
	u8 gpio_out_level:1;
	u8 gpio_tx_id;
	u8 gpio_rx_id;
	u16 mdelay;
};

static const char *serdes_gpio_groups[] = {
	"MAX96752_GPIO0", "MAX96752_GPIO1", "MAX96752_GPIO2", "MAX96752_GPIO3",
	"MAX96752_GPIO4", "MAX96752_GPIO5", "MAX96752_GPIO6", "MAX96752_GPIO7",

	"MAX96752_GPIO8", "MAX96752_GPIO9", "MAX96752_GPIO10", "MAX96752_GPIO11",
	"MAX96752_GPIO12", "MAX96752_GPIO13", "MAX96752_GPIO14", "MAX96752_GPIO15",
};

#define FUNCTION_DESC_GPIO_INPUT_BYPASS(id) \
{ \
	.name = "SER_TO_DES_RXID"#id, \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT_BYPASS(id) \
{ \
	.name = "DES_TXID"#id"_TO_SER", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en = 1, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT_LOW(id) \
{ \
	.name = "DES_TXID"#id"_OUTPUT_LOW", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 0, .gpio_tx_en = 0, \
		  .gpio_rx_en = 0, .gpio_out_level = 0, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT_HIGH(id) \
{ \
	.name = "DES_TXID"#id"_OUTPUT_HIGH", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 0, .gpio_tx_en = 0, \
		  .gpio_rx_en = 0, .gpio_out_level = 1, .gpio_tx_id = id } \
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

static struct pinctrl_pin_desc max96752_pins_desc[] = {
	PINCTRL_PIN(MAXIM_MAX96752_GPIO0, "MAX96752_GPIO0"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO1, "MAX96752_GPIO1"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO2, "MAX96752_GPIO2"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO3, "MAX96752_GPIO3"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO4, "MAX96752_GPIO4"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO5, "MAX96752_GPIO5"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO6, "MAX96752_GPIO6"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO7, "MAX96752_GPIO7"),

	PINCTRL_PIN(MAXIM_MAX96752_GPIO8, "MAX96752_GPIO8"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO9, "MAX96752_GPIO9"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO10, "MAX96752_GPIO10"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO11, "MAX96752_GPIO11"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO12, "MAX96752_GPIO12"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO13, "MAX96752_GPIO13"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO14, "MAX96752_GPIO14"),
	PINCTRL_PIN(MAXIM_MAX96752_GPIO15, "MAX96752_GPIO15"),
};

static struct group_desc max96752_groups_desc[] = {
	GROUP_DESC(MAX96752_GPIO0),
	GROUP_DESC(MAX96752_GPIO1),
	GROUP_DESC(MAX96752_GPIO2),
	GROUP_DESC(MAX96752_GPIO3),
	GROUP_DESC(MAX96752_GPIO4),
	GROUP_DESC(MAX96752_GPIO5),
	GROUP_DESC(MAX96752_GPIO6),
	GROUP_DESC(MAX96752_GPIO7),

	GROUP_DESC(MAX96752_GPIO8),
	GROUP_DESC(MAX96752_GPIO9),
	GROUP_DESC(MAX96752_GPIO10),
	GROUP_DESC(MAX96752_GPIO11),
	GROUP_DESC(MAX96752_GPIO12),
	GROUP_DESC(MAX96752_GPIO13),
	GROUP_DESC(MAX96752_GPIO14),
	GROUP_DESC(MAX96752_GPIO15),
};

static struct function_desc max96752_functions_desc[] = {
	FUNCTION_DESC_GPIO_INPUT_BYPASS(0),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(1),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(2),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(3),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(4),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(5),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(6),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(7),

	FUNCTION_DESC_GPIO_INPUT_BYPASS(8),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(9),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(10),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(11),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(12),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(13),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(14),
	FUNCTION_DESC_GPIO_INPUT_BYPASS(15),

	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(0),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(1),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(2),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(3),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(4),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(5),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(6),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(7),

	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(8),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(9),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(10),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(11),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(12),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(13),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(14),
	FUNCTION_DESC_GPIO_OUTPUT_BYPASS(15),

	FUNCTION_DESC_GPIO_OUTPUT_LOW(0),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(1),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(2),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(3),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(4),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(5),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(6),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(7),

	FUNCTION_DESC_GPIO_OUTPUT_LOW(8),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(9),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(10),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(11),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(12),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(13),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(14),
	FUNCTION_DESC_GPIO_OUTPUT_LOW(15),

	FUNCTION_DESC_GPIO_OUTPUT_HIGH(0),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(1),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(2),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(3),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(4),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(5),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(6),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(7),

	FUNCTION_DESC_GPIO_OUTPUT_HIGH(8),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(9),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(10),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(11),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(12),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(13),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(14),
	FUNCTION_DESC_GPIO_OUTPUT_HIGH(15),

	FUNCTION_DES_DELAY_MS(10),
	FUNCTION_DES_DELAY_MS(50),
	FUNCTION_DES_DELAY_MS(100),
	FUNCTION_DES_DELAY_MS(200),
	FUNCTION_DES_DELAY_MS(500),
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

static int max96752_bridge_pre_enable(struct serdes *serdes)
{
	int ret = 0;

	SERDES_DBG_CHIP("%s: serdes %s ret=%d\n", __func__,
			serdes->chip_data->name, ret);
	return ret;
}

static int max96752_bridge_post_disable(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_bridge_ops max96752_bridge_ops = {
	.enable = max96752_bridge_pre_enable,
	.disable = max96752_bridge_post_disable,
};


static int max96752_pinctrl_set_mux(struct serdes *serdes,
				    unsigned int function, unsigned int group)
{
	struct serdes_pinctrl *pinctrl = serdes->pinctrl;
	struct function_desc *func;
	struct group_desc *grp;
	int i;
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
			if (!ms) {
				serdes_set_bits(serdes, GPIO_A_REG(grp->pins[i] - pinctrl->pin_base),
						GPIO_OUT_DIS | GPIO_RX_EN | GPIO_TX_EN | GPIO_OUT,
						FIELD_PREP(GPIO_OUT_DIS, fdata->gpio_out_dis) |
						FIELD_PREP(GPIO_RX_EN, fdata->gpio_rx_en) |
						FIELD_PREP(GPIO_TX_EN, fdata->gpio_tx_en) |
						FIELD_PREP(GPIO_OUT, fdata->gpio_out_level));
				if (fdata->gpio_tx_en)
					serdes_set_bits(serdes,
							GPIO_B_REG(grp->pins[i] - pinctrl->pin_base),
							GPIO_TX_ID,
							FIELD_PREP(GPIO_TX_ID, fdata->gpio_tx_id));
				if (fdata->gpio_rx_en)
					serdes_set_bits(serdes,
							GPIO_C_REG(grp->pins[i] - pinctrl->pin_base),
							GPIO_RX_ID,
							FIELD_PREP(GPIO_RX_ID, fdata->gpio_rx_id));
			} else {
				mdelay(ms);
				SERDES_DBG_CHIP("%s: delay %d ms\n",
						__func__, ms);
			}
		}
	}

	if (grp->data) {
		struct serdes_group_data *gdata = grp->data;

		for (i = 0; i < gdata->num_configs; i++) {
			const struct config_desc *config = &gdata->configs[i];

			serdes_set_bits(serdes, config->reg,
					config->mask, config->val);
		}
	}

	return 0;
}

static int max96752_pinctrl_config_get(struct serdes *serdes,
				       unsigned int pin, unsigned long *config)
{
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int gpio_a_reg, gpio_b_reg;
	u16 arg = 0;

	serdes_reg_read(serdes, GPIO_A_REG(pin), &gpio_a_reg);
	serdes_reg_read(serdes, GPIO_B_REG(pin), &gpio_b_reg);

	SERDES_DBG_CHIP("%s: serdes chip %s pin=%d param=%d\n", __func__,
			serdes->chip_data->name, pin, param);

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		if (FIELD_GET(OUT_TYPE, gpio_b_reg))
			return -EINVAL;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		if (!FIELD_GET(OUT_TYPE, gpio_b_reg))
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		if (FIELD_GET(PULL_UPDN_SEL, gpio_b_reg) != 0)
			return -EINVAL;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		if (FIELD_GET(PULL_UPDN_SEL, gpio_b_reg) != 1)
			return -EINVAL;
		switch (FIELD_GET(RES_CFG, gpio_a_reg)) {
		case 0:
			arg = 40000;
			break;
		case 1:
			arg = 10000;
			break;
		}
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		if (FIELD_GET(PULL_UPDN_SEL, gpio_b_reg) != 2)
			return -EINVAL;
		switch (FIELD_GET(RES_CFG, gpio_a_reg)) {
		case 0:
			arg = 40000;
			break;
		case 1:
			arg = 10000;
			break;
		}
		break;
	case PIN_CONFIG_OUTPUT:
		if (FIELD_GET(GPIO_OUT_DIS, gpio_a_reg))
			return -EINVAL;

		arg = FIELD_GET(GPIO_OUT, gpio_a_reg);
		break;
	default:
		return -EOPNOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return 0;
}

static int max96752_pinctrl_config_set(struct serdes *serdes,
				       unsigned int pin, unsigned long *configs,
				       unsigned int num_configs)
{
	enum pin_config_param param;
	u32 arg;
	u8 res_cfg;
	int i;

	for (i = 0; i < num_configs; i++) {
		param = pinconf_to_config_param(configs[i]);
		arg = pinconf_to_config_argument(configs[i]);

		SERDES_DBG_CHIP("%s: serdes chip %s pin=%d param=%d\n", __func__,
				serdes->chip_data->name, pin, param);

		switch (param) {
		case PIN_CONFIG_DRIVE_OPEN_DRAIN:
			serdes_set_bits(serdes, GPIO_B_REG(pin),
					OUT_TYPE, FIELD_PREP(OUT_TYPE, 0));
			break;
		case PIN_CONFIG_DRIVE_PUSH_PULL:
			serdes_set_bits(serdes, GPIO_B_REG(pin),
					OUT_TYPE, FIELD_PREP(OUT_TYPE, 1));
			break;
		case PIN_CONFIG_BIAS_DISABLE:
			serdes_set_bits(serdes, GPIO_C_REG(pin),
					PULL_UPDN_SEL,
					FIELD_PREP(PULL_UPDN_SEL, 0));
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			switch (arg) {
			case 40000:
				res_cfg = 0;
				break;
			case 1000000:
				res_cfg = 1;
				break;
			default:
				return -EINVAL;
			}

			serdes_set_bits(serdes, GPIO_A_REG(pin),
					RES_CFG, FIELD_PREP(RES_CFG, res_cfg));
			serdes_set_bits(serdes, GPIO_C_REG(pin),
					PULL_UPDN_SEL,
					FIELD_PREP(PULL_UPDN_SEL, 1));
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			switch (arg) {
			case 40000:
				res_cfg = 0;
				break;
			case 1000000:
				res_cfg = 1;
				break;
			default:
				return -EINVAL;
			}

			serdes_set_bits(serdes, GPIO_A_REG(pin),
					RES_CFG, FIELD_PREP(RES_CFG, res_cfg));
			serdes_set_bits(serdes, GPIO_C_REG(pin),
					PULL_UPDN_SEL,
					FIELD_PREP(PULL_UPDN_SEL, 2));
			break;
		case PIN_CONFIG_OUTPUT:
			serdes_set_bits(serdes, GPIO_A_REG(pin),
					GPIO_OUT_DIS | GPIO_OUT,
					FIELD_PREP(GPIO_OUT_DIS, 0) |
					FIELD_PREP(GPIO_OUT, arg));
			break;
		default:
			return -EOPNOTSUPP;
		}
	}

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

static int max96752_set_i2c_addr(struct serdes *serdes, int address, int link)
{
	int ret;

	if (link == LINKA) {
		/* TX_SRC_ID[1] = 0 */
		ret = serdes_reg_write(serdes, 0x73, 0x31);
		/* Receive packets with this stream ID = 0 */
		ret = serdes_reg_write(serdes, 0x50, 0x00);
		ret = serdes_reg_write(serdes, 0x00, address << 1);
	} else if (link == LINKB) {
		/* TX_SRC_ID[1] = 1 */
		ret = serdes_reg_write(serdes, 0x73, 0x32);
		/* Receive packets with this stream ID = 1 */
		ret = serdes_reg_write(serdes, 0x50, 0x01);
		ret = serdes_reg_write(serdes, 0x00, address << 1);
	} else {
		dev_info(serdes->dev, "link %d is error\n", link);
		ret = -1;
	}

	SERDES_DBG_CHIP("%s: set serdes chip %s i2c 7bit address to 0x%x\n", __func__,
			serdes->chip_data->name, address);

	return ret;
}

static struct serdes_chip_split_ops max96752_split_ops = {
	.set_i2c_addr = max96752_set_i2c_addr,
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
	.bridge_ops	= &max96752_bridge_ops,
	.pinctrl_ops	= &max96752_pinctrl_ops,
	.split_ops	= &max96752_split_ops,
	.gpio_ops	= &max96752_gpio_ops,
	.pm_ops		= &max96752_pm_ops,
	.irq_ops	= &max96752_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_max96752_data);

MODULE_LICENSE("GPL");
