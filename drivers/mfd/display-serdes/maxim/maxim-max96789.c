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
	case 0x0002:
	case 0x0010:
	case 0x0013:
	case 0x0053:
	case 0x0057:
	case 0x02be ... 0x02fc:
	case 0x0311:
	case 0x032a:
	case 0x0330 ... 0x0331:
	case 0x0385 ... 0x0387:
	case 0x03a4 ... 0x03ae:
		return false;
	default:
		return true;
	}
}

static struct regmap_config max96789_regmap_config = {
	.name = "max96789",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x2000,
	.volatile_reg = max96789_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

struct serdes_function_data {
	u8 gpio_out_dis:1;
	u8 gpio_tx_en:1;
	u8 gpio_rx_en:1;
	u8 gpio_tx_id;
	u8 gpio_rx_id;
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
static int MAX96789_I2C_pins[] = {19, 20};
static int MAX96789_UART_pins[] = {19, 20};

#define GROUP_DESC(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
}

#define GROUP_DESC_CONFIG(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
	.data = (void *)(const struct serdes_group_data []) { \
		{ \
			.configs = nm ## _configs, \
			.num_configs = ARRAY_SIZE(nm ## _configs), \
		} \
	}, \
}

static const struct config_desc MAX96789_MFP0_configs[] = {
	{ 0x0005, LOCK_EN, 0 },
	{ 0x0048, LOC_MS_EN, 0 },
};

static const struct config_desc MAX96789_MFP1_configs[] = {
	{ 0x0005, ERRB_EN, 0 },
};

static const struct config_desc MAX96789_MFP4_configs[] = {
	{ 0x070, SPI_EN, 0 },
};

static const struct config_desc MAX96789_MFP5_configs[] = {
	{ 0x006, RCLKEN, 0 },
};

static const struct config_desc MAX96789_MFP7_configs[] = {
	{ 0x0002, AUD_TX_EN_X, 0 },
	{ 0x0002, AUD_TX_EN_Y, 0 }
};

static const struct config_desc MAX96789_MFP8_configs[] = {
	{ 0x0002, AUD_TX_EN_X, 0 },
	{ 0x0002, AUD_TX_EN_Y, 0 }
};

static const struct config_desc MAX96789_MFP9_configs[] = {
	{ 0x0002, AUD_TX_EN_X, 0 },
	{ 0x0002, AUD_TX_EN_Y, 0 }
};

static const struct config_desc MAX96789_MFP10_configs[] = {
	{ 0x0001, IIC_2_EN, 0 },
	{ 0x0003, UART_2_EN, 0 },
	{ 0x0140, AUD_RX_EN, 0 },
};

static const struct config_desc MAX96789_MFP11_configs[] = {
	{ 0x0001, IIC_2_EN, 0 },
	{ 0x0003, UART_2_EN, 0 },
	{ 0x0140, AUD_RX_EN, 0 },
};

static const struct config_desc MAX96789_MFP12_configs[] = {
	{ 0x0140, AUD_RX_EN, 0 },
};

static const struct config_desc MAX96789_MFP13_configs[] = {
	{ 0x0005, PU_LF0, 0 },
};

static const struct config_desc MAX96789_MFP14_configs[] = {
	{ 0x0005, PU_LF1, 0 },
};

static const struct config_desc MAX96789_MFP15_configs[] = {
	{ 0x0005, PU_LF2, 0 },
};

static const struct config_desc MAX96789_MFP16_configs[] = {
	{ 0x0005, PU_LF3, 0 },
};

static const struct config_desc MAX96789_MFP17_configs[] = {
	{ 0x0001, IIC_1_EN, 0 },
	{ 0x0003, UART_1_EN, 0 },
};

static const struct config_desc MAX96789_MFP18_configs[] = {
	{ 0x0001, IIC_1_EN, 0 },
	{ 0x0003, UART_1_EN, 0 },
};

static const char *serdes_gpio_groups[] = {
	"MAX96789_MFP0", "MAX96789_MFP1", "MAX96789_MFP2", "MAX96789_MFP3",
	"MAX96789_MFP4", "MAX96789_MFP5", "MAX96789_MFP6", "MAX96789_MFP7",

	"MAX96789_MFP8", "MAX96789_MFP9", "MAX96789_MFP10", "MAX96789_MFP11",
	"MAX96789_MFP12", "MAX96789_MFP13", "MAX96789_MFP14", "MAX96789_MFP15",

	"MAX96789_MFP16", "MAX96789_MFP17", "MAX96789_MFP18", "MAX96789_MFP19",
	"MAX96789_MFP20",
};

static const char *MAX96789_I2C_groups[] = { "MAX96789_I2C" };
static const char *MAX96789_UART_groups[] = { "MAX96789_UART" };

#define FUNCTION_DESC(nm) \
{ \
	.name = #nm, \
	.group_names = nm##_groups, \
	.num_group_names = ARRAY_SIZE(nm##_groups), \
} \

#define FUNCTION_DESC_GPIO_INPUT(id) \
{ \
	.name = "DES_RXID"#id"_TO_SER", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 0, .gpio_rx_en = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT(id) \
{ \
	.name = "SER_TXID"#id"_TO_DES", \
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
	GROUP_DESC_CONFIG(MAX96789_MFP0),
	GROUP_DESC_CONFIG(MAX96789_MFP1),
	GROUP_DESC(MAX96789_MFP2),
	GROUP_DESC(MAX96789_MFP3),
	GROUP_DESC_CONFIG(MAX96789_MFP4),
	GROUP_DESC_CONFIG(MAX96789_MFP5),
	GROUP_DESC(MAX96789_MFP6),
	GROUP_DESC_CONFIG(MAX96789_MFP7),

	GROUP_DESC_CONFIG(MAX96789_MFP8),
	GROUP_DESC_CONFIG(MAX96789_MFP9),
	GROUP_DESC_CONFIG(MAX96789_MFP10),
	GROUP_DESC_CONFIG(MAX96789_MFP11),
	GROUP_DESC_CONFIG(MAX96789_MFP12),
	GROUP_DESC_CONFIG(MAX96789_MFP13),
	GROUP_DESC_CONFIG(MAX96789_MFP14),
	GROUP_DESC_CONFIG(MAX96789_MFP15),

	GROUP_DESC_CONFIG(MAX96789_MFP16),
	GROUP_DESC_CONFIG(MAX96789_MFP17),
	GROUP_DESC_CONFIG(MAX96789_MFP18),
	GROUP_DESC(MAX96789_MFP19),
	GROUP_DESC(MAX96789_MFP20),
	GROUP_DESC(MAX96789_I2C),
	GROUP_DESC(MAX96789_UART),
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

	FUNCTION_DESC(MAX96789_I2C),
	FUNCTION_DESC(MAX96789_UART),
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

static bool max96789_bridge_link_locked(struct serdes *serdes)
{
	u32 val;

	if (serdes->lock_gpio) {
		val = gpiod_get_value_cansleep(serdes->lock_gpio);
		SERDES_DBG_CHIP("%s: lock_gpio val=%d\n", __func__, val);
		return val;
	}

	if (serdes_reg_read(serdes, 0x0013, &val)) {
		SERDES_DBG_CHIP("%s: false val=%d\n", __func__, val);
		return false;
	}

	if (!FIELD_GET(LOCKED, val)) {
		SERDES_DBG_CHIP("%s: false val=%d\n", __func__, val);
		return false;
	}

	SERDES_DBG_CHIP("%s: return true\n", __func__);

	return true;
}

static int max96789_bridge_attach(struct serdes *serdes)
{
	if (max96789_bridge_link_locked(serdes))
		serdes->serdes_bridge->status = connector_status_connected;
	else
		serdes->serdes_bridge->status = connector_status_disconnected;

	return 0;
}

static enum drm_connector_status
max96789_bridge_detect(struct serdes *serdes)
{
	struct serdes_bridge *serdes_bridge = serdes->serdes_bridge;
	enum drm_connector_status status = connector_status_connected;

	if (!drm_kms_helper_is_poll_worker())
		return serdes_bridge->status;

	if (!max96789_bridge_link_locked(serdes)) {
		status = connector_status_disconnected;
		goto out;
	}

	if (extcon_get_state(serdes->extcon, EXTCON_JACK_VIDEO_OUT)) {
		if (atomic_cmpxchg(&serdes_bridge->triggered, 1, 0)) {
			status = connector_status_disconnected;
			goto out;
		}

	} else {
		atomic_set(&serdes_bridge->triggered, 0);
	}

	if (serdes_bridge->next_bridge && (serdes_bridge->next_bridge->ops & DRM_BRIDGE_OP_DETECT))
		return drm_bridge_detect(serdes_bridge->next_bridge);

out:
	serdes_bridge->status = status;
	SERDES_DBG_CHIP("%s: status=%d\n", __func__, status);
	return status;
}

static int max96789_bridge_enable(struct serdes *serdes)
{
	int ret = 0;

	SERDES_DBG_CHIP("%s: serdes chip %s ret=%d\n", __func__, serdes->chip_data->name, ret);
	return ret;
}

static int max96789_bridge_disable(struct serdes *serdes)
{
	int ret = 0;

	return ret;
}

static struct serdes_chip_bridge_ops max96789_bridge_ops = {
	.init = max96789_bridge_init,
	.attach = max96789_bridge_attach,
	.detect = max96789_bridge_detect,
	.enable = max96789_bridge_enable,
	.disable = max96789_bridge_disable,
};

static int max96789_pinctrl_set_mux(struct serdes *serdes,
				    unsigned int function, unsigned int group)
{
	struct serdes_pinctrl *pinctrl = serdes->pinctrl;
	struct function_desc *func;
	struct group_desc *grp;
	int i;

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

		for (i = 0; i < grp->num_pins; i++) {
			serdes_set_bits(serdes, GPIO_A_REG(grp->pins[i] - pinctrl->pin_base),
					GPIO_OUT_DIS | GPIO_RX_EN | GPIO_TX_EN,
					FIELD_PREP(GPIO_OUT_DIS, fdata->gpio_out_dis) |
					FIELD_PREP(GPIO_RX_EN, fdata->gpio_rx_en) |
					FIELD_PREP(GPIO_TX_EN, fdata->gpio_tx_en));

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

static int max96789_pinctrl_config_get(struct serdes *serdes,
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

static int max96789_pinctrl_config_set(struct serdes *serdes,
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

static int max96789_select(struct serdes *serdes, int chan)
{
	u32 link_cfg, val;
	int ret;

	serdes_set_bits(serdes, 0x0001, DIS_REM_CC,
			   FIELD_PREP(DIS_REM_CC, 0));

	serdes_reg_read(serdes, 0x0010, &link_cfg);
	if ((link_cfg & LINK_CFG) == SPLITTER_MODE)
		SERDES_DBG_CHIP("%s: serdes chip %s already split mode cfg=0x%x\n", __func__,
				serdes->chip_data->name, link_cfg);

	if (chan == 0 && (link_cfg & LINK_CFG) != DUAL_LINK) {
		serdes_set_bits(serdes, 0x0004,
				   LINK_EN_B | LINK_EN_A,
				   FIELD_PREP(LINK_EN_A, 1) |
				   FIELD_PREP(LINK_EN_B, 1));
		serdes_set_bits(serdes, 0x0010,
				   RESET_ONESHOT | AUTO_LINK | LINK_CFG,
				   FIELD_PREP(RESET_ONESHOT, 1) |
				   FIELD_PREP(AUTO_LINK, 0) |
				   FIELD_PREP(LINK_CFG, DUAL_LINK));
		SERDES_DBG_CHIP("%s: change to use dual link\n", __func__);
	} else if (chan == 1 && (link_cfg & LINK_CFG) != LINKA) {
		serdes_set_bits(serdes, 0x0004,
				   LINK_EN_B | LINK_EN_A,
				   FIELD_PREP(LINK_EN_A, 1) |
				   FIELD_PREP(LINK_EN_B, 0));
		serdes_set_bits(serdes, 0x0010,
				   RESET_ONESHOT | AUTO_LINK | LINK_CFG,
				   FIELD_PREP(RESET_ONESHOT, 1) |
				   FIELD_PREP(AUTO_LINK, 0) |
				   FIELD_PREP(LINK_CFG, LINKA));
		SERDES_DBG_CHIP("%s: change to use linkA\n", __func__);
	} else if (chan == 2 && (link_cfg & LINK_CFG) != LINKB) {
		serdes_set_bits(serdes, 0x0004,
				   LINK_EN_B | LINK_EN_A,
				   FIELD_PREP(LINK_EN_A, 0) |
				   FIELD_PREP(LINK_EN_B, 1));
		serdes_set_bits(serdes, 0x0010,
				   RESET_ONESHOT | AUTO_LINK | LINK_CFG,
				   FIELD_PREP(RESET_ONESHOT, 1) |
				   FIELD_PREP(AUTO_LINK, 0) |
				   FIELD_PREP(LINK_CFG, LINKB));
		SERDES_DBG_CHIP("%s: change to use linkB\n", __func__);
	} else if (chan == 3 && (link_cfg & LINK_CFG) != SPLITTER_MODE) {
		serdes_set_bits(serdes, 0x0004,
				   LINK_EN_B | LINK_EN_A,
				   FIELD_PREP(LINK_EN_A, 1) |
				   FIELD_PREP(LINK_EN_B, 1));
		serdes_set_bits(serdes, 0x0010,
				   RESET_ONESHOT | AUTO_LINK | LINK_CFG,
				   FIELD_PREP(RESET_ONESHOT, 1) |
				   FIELD_PREP(AUTO_LINK, 0) |
				   FIELD_PREP(LINK_CFG, SPLITTER_MODE));
		SERDES_DBG_CHIP("%s: change to use split mode\n", __func__);
	}

	ret = regmap_read_poll_timeout(serdes->regmap, 0x0013, val,
				       val & LOCKED, 100,
				       50 * USEC_PER_MSEC);
	if (ret < 0) {
		dev_err(serdes->dev, "GMSL2 link lock timeout\n");
		return ret;
	}

	return 0;
}

static int max96789_deselect(struct serdes *serdes, int chan)
{
	//serdes_set_bits(serdes, 0x0001, DIS_REM_CC,
	//		   FIELD_PREP(DIS_REM_CC, 1));

	return 0;
}

static struct serdes_chip_split_ops max96789_split_ops = {
	.select = max96789_select,
	.deselect = max96789_deselect,
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
	.split_ops	= &max96789_split_ops,
	.pm_ops		= &max96789_pm_ops,
	.irq_ops	= &max96789_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_max96789_data);

MODULE_LICENSE("GPL");
