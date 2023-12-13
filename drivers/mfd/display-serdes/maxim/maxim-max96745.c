// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * maxim-max96745.c  --  I2C register interface access for max96745 serdes chip
 *
 * Copyright (c) 2023-2028 Rockchip Electronics Co. Ltd.
 *
 * Author:
 */

#include "../core.h"
#include "maxim-max96745.h"

static bool max96745_volatile_reg(struct device *dev, unsigned int reg)
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

static struct regmap_config max96745_regmap_config = {
	.name = "max96745",
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x8000,
	.volatile_reg = max96745_volatile_reg,
	.cache_type = REGCACHE_RBTREE,
};

struct serdes_function_data {
	u8 gpio_out_dis:1;
	u8 gpio_io_rx_en:1;
	u8 gpio_tx_en_a:1;
	u8 gpio_tx_en_b:1;
	u8 gpio_rx_en_a:1;
	u8 gpio_rx_en_b:1;
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

static int MAX96745_MFP0_pins[] = {0};
static int MAX96745_MFP1_pins[] = {1};
static int MAX96745_MFP2_pins[] = {2};
static int MAX96745_MFP3_pins[] = {3};
static int MAX96745_MFP4_pins[] = {4};
static int MAX96745_MFP5_pins[] = {5};
static int MAX96745_MFP6_pins[] = {6};
static int MAX96745_MFP7_pins[] = {7};

static int MAX96745_MFP8_pins[] = {8};
static int MAX96745_MFP9_pins[] = {9};
static int MAX96745_MFP10_pins[] = {10};
static int MAX96745_MFP11_pins[] = {11};
static int MAX96745_MFP12_pins[] = {12};
static int MAX96745_MFP13_pins[] = {13};
static int MAX96745_MFP14_pins[] = {14};
static int MAX96745_MFP15_pins[] = {15};

static int MAX96745_MFP16_pins[] = {16};
static int MAX96745_MFP17_pins[] = {17};
static int MAX96745_MFP18_pins[] = {18};
static int MAX96745_MFP19_pins[] = {19};
static int MAX96745_MFP20_pins[] = {20};
static int MAX96745_MFP21_pins[] = {21};
static int MAX96745_MFP22_pins[] = {22};
static int MAX96745_MFP23_pins[] = {23};

static int MAX96745_MFP24_pins[] = {24};
static int MAX96745_MFP25_pins[] = {25};
static int MAX96745_I2C_pins[] = {3, 7};
static int MAX96745_UART_pins[] = {3, 7};

#define GROUP_DESC(nm) \
{ \
	.name = #nm, \
	.pins = nm ## _pins, \
	.num_pins = ARRAY_SIZE(nm ## _pins), \
}

static const char *serdes_gpio_groups[] = {
	"MAX96745_MFP0", "MAX96745_MFP1", "MAX96745_MFP2", "MAX96745_MFP3",
	"MAX96745_MFP4", "MAX96745_MFP5", "MAX96745_MFP6", "MAX96745_MFP7",

	"MAX96745_MFP8", "MAX96745_MFP9", "MAX96745_MFP10", "MAX96745_MFP11",
	"MAX96745_MFP12", "MAX96745_MFP13", "MAX96745_MFP14", "MAX96745_MFP15",

	"MAX96745_MFP16", "MAX96745_MFP17", "MAX96745_MFP18", "MAX96745_MFP19",
	"MAX96745_MFP20", "MAX96745_MFP21", "MAX96745_MFP22", "MAX96745_MFP23",

	"MAX96745_MFP24", "MAX96745_MFP25",
};

static const char *MAX96745_I2C_groups[] = { "MAX96745_I2C" };
static const char *MAX96745_UART_groups[] = { "MAX96745_UART" };

#define FUNCTION_DESC(nm) \
{ \
	.name = #nm, \
	.group_names = nm##_groups, \
	.num_group_names = ARRAY_SIZE(nm##_groups), \
} \

#define FUNCTION_DESC_GPIO_OUTPUT_A(id) \
{ \
	.name = "SER_TXID"#id"_TO_DES_LINKA", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en_a = 1, \
		  .gpio_io_rx_en = 1, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_OUTPUT_B(id) \
{ \
	.name = "SER_TXID"#id"_TO_DES_LINKB", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_out_dis = 1, .gpio_tx_en_b = 1, \
		  .gpio_io_rx_en = 1, .gpio_tx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_INPUT_A(id) \
{ \
	.name = "DES_RXID"#id"_TO_SER_LINKA", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en_a = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO_INPUT_B(id) \
{ \
	.name = "DES_RXID"#id"_TO_SER_LINKB", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ .gpio_rx_en_b = 1, .gpio_rx_id = id } \
	}, \
} \

#define FUNCTION_DESC_GPIO() \
{ \
	.name = "MAX96745_GPIO", \
	.group_names = serdes_gpio_groups, \
	.num_group_names = ARRAY_SIZE(serdes_gpio_groups), \
	.data = (void *)(const struct serdes_function_data []) { \
		{ } \
	}, \
} \

static struct pinctrl_pin_desc max96745_pins_desc[] = {
	PINCTRL_PIN(MAXIM_MAX96745_MFP0, "MAX96745_MFP0"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP1, "MAX96745_MFP1"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP2, "MAX96745_MFP2"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP3, "MAX96745_MFP3"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP4, "MAX96745_MFP4"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP5, "MAX96745_MFP5"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP6, "MAX96745_MFP6"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP7, "MAX96745_MFP7"),

	PINCTRL_PIN(MAXIM_MAX96745_MFP8, "MAX96745_MFP8"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP9, "MAX96745_MFP9"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP10, "MAX96745_MFP10"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP11, "MAX96745_MFP11"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP12, "MAX96745_MFP12"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP13, "MAX96745_MFP13"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP14, "MAX96745_MFP14"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP15, "MAX96745_MFP15"),

	PINCTRL_PIN(MAXIM_MAX96745_MFP16, "MAX96745_MFP16"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP17, "MAX96745_MFP17"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP18, "MAX96745_MFP18"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP19, "MAX96745_MFP19"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP20, "MAX96745_MFP20"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP21, "MAX96745_MFP21"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP22, "MAX96745_MFP22"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP23, "MAX96745_MFP23"),

	PINCTRL_PIN(MAXIM_MAX96745_MFP24, "MAX96745_MFP24"),
	PINCTRL_PIN(MAXIM_MAX96745_MFP25, "MAX96745_MFP25"),
};

static struct group_desc max96745_groups_desc[] = {
	GROUP_DESC(MAX96745_MFP0),
	GROUP_DESC(MAX96745_MFP1),
	GROUP_DESC(MAX96745_MFP2),
	GROUP_DESC(MAX96745_MFP3),
	GROUP_DESC(MAX96745_MFP4),
	GROUP_DESC(MAX96745_MFP5),
	GROUP_DESC(MAX96745_MFP6),
	GROUP_DESC(MAX96745_MFP7),

	GROUP_DESC(MAX96745_MFP8),
	GROUP_DESC(MAX96745_MFP9),
	GROUP_DESC(MAX96745_MFP10),
	GROUP_DESC(MAX96745_MFP11),
	GROUP_DESC(MAX96745_MFP12),
	GROUP_DESC(MAX96745_MFP13),
	GROUP_DESC(MAX96745_MFP14),
	GROUP_DESC(MAX96745_MFP15),

	GROUP_DESC(MAX96745_MFP16),
	GROUP_DESC(MAX96745_MFP17),
	GROUP_DESC(MAX96745_MFP18),
	GROUP_DESC(MAX96745_MFP19),
	GROUP_DESC(MAX96745_MFP20),
	GROUP_DESC(MAX96745_MFP21),
	GROUP_DESC(MAX96745_MFP22),
	GROUP_DESC(MAX96745_MFP23),

	GROUP_DESC(MAX96745_MFP24),
	GROUP_DESC(MAX96745_MFP25),

	GROUP_DESC(MAX96745_I2C),
	GROUP_DESC(MAX96745_UART),
};

static struct function_desc max96745_functions_desc[] = {
	FUNCTION_DESC_GPIO_INPUT_A(0),
	FUNCTION_DESC_GPIO_INPUT_A(1),
	FUNCTION_DESC_GPIO_INPUT_A(2),
	FUNCTION_DESC_GPIO_INPUT_A(3),
	FUNCTION_DESC_GPIO_INPUT_A(4),
	FUNCTION_DESC_GPIO_INPUT_A(5),
	FUNCTION_DESC_GPIO_INPUT_A(6),
	FUNCTION_DESC_GPIO_INPUT_A(7),

	FUNCTION_DESC_GPIO_INPUT_A(8),
	FUNCTION_DESC_GPIO_INPUT_A(9),
	FUNCTION_DESC_GPIO_INPUT_A(10),
	FUNCTION_DESC_GPIO_INPUT_A(11),
	FUNCTION_DESC_GPIO_INPUT_A(12),
	FUNCTION_DESC_GPIO_INPUT_A(13),
	FUNCTION_DESC_GPIO_INPUT_A(14),
	FUNCTION_DESC_GPIO_INPUT_A(15),

	FUNCTION_DESC_GPIO_INPUT_A(16),
	FUNCTION_DESC_GPIO_INPUT_A(17),
	FUNCTION_DESC_GPIO_INPUT_A(18),
	FUNCTION_DESC_GPIO_INPUT_A(19),
	FUNCTION_DESC_GPIO_INPUT_A(20),
	FUNCTION_DESC_GPIO_INPUT_A(21),
	FUNCTION_DESC_GPIO_INPUT_A(22),
	FUNCTION_DESC_GPIO_INPUT_A(23),

	FUNCTION_DESC_GPIO_INPUT_A(24),
	FUNCTION_DESC_GPIO_INPUT_A(25),

	FUNCTION_DESC_GPIO_OUTPUT_A(0),
	FUNCTION_DESC_GPIO_OUTPUT_A(1),
	FUNCTION_DESC_GPIO_OUTPUT_A(2),
	FUNCTION_DESC_GPIO_OUTPUT_A(3),
	FUNCTION_DESC_GPIO_OUTPUT_A(4),
	FUNCTION_DESC_GPIO_OUTPUT_A(5),
	FUNCTION_DESC_GPIO_OUTPUT_A(6),
	FUNCTION_DESC_GPIO_OUTPUT_A(7),

	FUNCTION_DESC_GPIO_OUTPUT_A(8),
	FUNCTION_DESC_GPIO_OUTPUT_A(9),
	FUNCTION_DESC_GPIO_OUTPUT_A(10),
	FUNCTION_DESC_GPIO_OUTPUT_A(11),
	FUNCTION_DESC_GPIO_OUTPUT_A(12),
	FUNCTION_DESC_GPIO_OUTPUT_A(13),
	FUNCTION_DESC_GPIO_OUTPUT_A(14),
	FUNCTION_DESC_GPIO_OUTPUT_A(15),

	FUNCTION_DESC_GPIO_OUTPUT_A(16),
	FUNCTION_DESC_GPIO_OUTPUT_A(17),
	FUNCTION_DESC_GPIO_OUTPUT_A(18),
	FUNCTION_DESC_GPIO_OUTPUT_A(19),
	FUNCTION_DESC_GPIO_OUTPUT_A(20),
	FUNCTION_DESC_GPIO_OUTPUT_A(21),
	FUNCTION_DESC_GPIO_OUTPUT_A(22),
	FUNCTION_DESC_GPIO_OUTPUT_A(23),

	FUNCTION_DESC_GPIO_OUTPUT_A(24),
	FUNCTION_DESC_GPIO_OUTPUT_A(25),

	FUNCTION_DESC_GPIO_INPUT_B(0),
	FUNCTION_DESC_GPIO_INPUT_B(1),
	FUNCTION_DESC_GPIO_INPUT_B(2),
	FUNCTION_DESC_GPIO_INPUT_B(3),
	FUNCTION_DESC_GPIO_INPUT_B(4),
	FUNCTION_DESC_GPIO_INPUT_B(5),
	FUNCTION_DESC_GPIO_INPUT_B(6),
	FUNCTION_DESC_GPIO_INPUT_B(7),

	FUNCTION_DESC_GPIO_INPUT_B(8),
	FUNCTION_DESC_GPIO_INPUT_B(9),
	FUNCTION_DESC_GPIO_INPUT_B(10),
	FUNCTION_DESC_GPIO_INPUT_B(11),
	FUNCTION_DESC_GPIO_INPUT_B(12),
	FUNCTION_DESC_GPIO_INPUT_B(13),
	FUNCTION_DESC_GPIO_INPUT_B(14),
	FUNCTION_DESC_GPIO_INPUT_B(15),

	FUNCTION_DESC_GPIO_INPUT_B(16),
	FUNCTION_DESC_GPIO_INPUT_B(17),
	FUNCTION_DESC_GPIO_INPUT_B(18),
	FUNCTION_DESC_GPIO_INPUT_B(19),
	FUNCTION_DESC_GPIO_INPUT_B(20),
	FUNCTION_DESC_GPIO_INPUT_B(21),
	FUNCTION_DESC_GPIO_INPUT_B(22),
	FUNCTION_DESC_GPIO_INPUT_B(23),

	FUNCTION_DESC_GPIO_INPUT_B(24),
	FUNCTION_DESC_GPIO_INPUT_B(25),

	FUNCTION_DESC_GPIO_OUTPUT_B(0),
	FUNCTION_DESC_GPIO_OUTPUT_B(1),
	FUNCTION_DESC_GPIO_OUTPUT_B(2),
	FUNCTION_DESC_GPIO_OUTPUT_B(3),
	FUNCTION_DESC_GPIO_OUTPUT_B(4),
	FUNCTION_DESC_GPIO_OUTPUT_B(5),
	FUNCTION_DESC_GPIO_OUTPUT_B(6),
	FUNCTION_DESC_GPIO_OUTPUT_B(7),

	FUNCTION_DESC_GPIO_OUTPUT_B(8),
	FUNCTION_DESC_GPIO_OUTPUT_B(9),
	FUNCTION_DESC_GPIO_OUTPUT_B(10),
	FUNCTION_DESC_GPIO_OUTPUT_B(11),
	FUNCTION_DESC_GPIO_OUTPUT_B(12),
	FUNCTION_DESC_GPIO_OUTPUT_B(13),
	FUNCTION_DESC_GPIO_OUTPUT_B(14),
	FUNCTION_DESC_GPIO_OUTPUT_B(15),

	FUNCTION_DESC_GPIO_OUTPUT_B(16),
	FUNCTION_DESC_GPIO_OUTPUT_B(17),
	FUNCTION_DESC_GPIO_OUTPUT_B(18),
	FUNCTION_DESC_GPIO_OUTPUT_B(19),
	FUNCTION_DESC_GPIO_OUTPUT_B(20),
	FUNCTION_DESC_GPIO_OUTPUT_B(21),
	FUNCTION_DESC_GPIO_OUTPUT_B(22),
	FUNCTION_DESC_GPIO_OUTPUT_B(23),

	FUNCTION_DESC_GPIO_OUTPUT_B(24),
	FUNCTION_DESC_GPIO_OUTPUT_B(25),

	FUNCTION_DESC_GPIO(),

	FUNCTION_DESC(MAX96745_I2C),
	FUNCTION_DESC(MAX96745_UART),
};

static struct serdes_chip_pinctrl_info max96745_pinctrl_info = {
	.pins = max96745_pins_desc,
	.num_pins = ARRAY_SIZE(max96745_pins_desc),
	.groups = max96745_groups_desc,
	.num_groups = ARRAY_SIZE(max96745_groups_desc),
	.functions = max96745_functions_desc,
	.num_functions = ARRAY_SIZE(max96745_functions_desc),
};

static bool max96745_vid_tx_active(struct serdes *serdes)
{
	u32 val;
	int i = 0, ret = 0;

	for (i = 0; i < 5; i++) {
		ret = serdes_reg_read(serdes, 0x0107, &val);
		if (!ret)
			break;

		SERDES_DBG_CHIP("serdes %s: false val=%d i=%d ret=%d\n", __func__, val, i, ret);
		msleep(20);
	}

	if (ret) {
		SERDES_DBG_CHIP("serdes %s: false val=%d ret=%d\n", __func__, val, ret);
		return false;
	}

	if (!FIELD_GET(VID_TX_ACTIVE_A | VID_TX_ACTIVE_B, val)) {
		SERDES_DBG_CHIP("serdes %s: false val=%d\n", __func__, val);
		return false;
	}

	return true;
}

static int max96745_bridge_init(struct serdes *serdes)
{
	if (max96745_vid_tx_active(serdes)) {
		extcon_set_state(serdes->extcon, EXTCON_JACK_VIDEO_OUT, true);
		pr_info("serdes %s, extcon is true state=%d\n", __func__, serdes->extcon->state);
	} else {
		pr_info("serdes %s, extcon is false\n", __func__);
	}

	return 0;
}

static bool max96745_bridge_link_locked(struct serdes *serdes)
{
	u32 val;

	if (serdes->lock_gpio) {
		val = gpiod_get_value_cansleep(serdes->lock_gpio);
		SERDES_DBG_CHIP("serdes %s:val=%d\n", __func__, val);
		return val;
	}

	if (serdes_reg_read(serdes, 0x002a, &val)) {
		SERDES_DBG_CHIP("serdes %s: false val=%d\n", __func__, val);
		return false;
	}

	if (!FIELD_GET(LOCKED, val)) {
		SERDES_DBG_CHIP("serdes %s: false val=%d\n", __func__, val);
		return false;
	}

	return true;
}

static int max96745_bridge_attach(struct serdes *serdes)
{
	if (max96745_bridge_link_locked(serdes))
		serdes->serdes_bridge->status = connector_status_connected;
	else
		serdes->serdes_bridge->status = connector_status_disconnected;

	return 0;
}

static enum drm_connector_status
max96745_bridge_detect(struct serdes *serdes)
{
	struct serdes_bridge *serdes_bridge = serdes->serdes_bridge;
	enum drm_connector_status status = connector_status_connected;

	if (!drm_kms_helper_is_poll_worker())
		return serdes_bridge->status;

	if (!max96745_bridge_link_locked(serdes)) {
		status = connector_status_disconnected;
		goto out;
	}

	if (extcon_get_state(serdes->extcon, EXTCON_JACK_VIDEO_OUT)) {
		u32 dprx_trn_status2;

		if (atomic_cmpxchg(&serdes_bridge->triggered, 1, 0)) {
			status = connector_status_disconnected;
			SERDES_DBG_CHIP("1 status=%d state=%d\n", status, serdes->extcon->state);
			goto out;
		}

		if (serdes_reg_read(serdes, 0x641a, &dprx_trn_status2)) {
			status = connector_status_disconnected;
			SERDES_DBG_CHIP("2 status=%d state=%d\n", status, serdes->extcon->state);
			goto out;
		}

		if ((dprx_trn_status2 & DPRX_TRAIN_STATE) != DPRX_TRAIN_STATE) {
			dev_err(serdes->dev, "Training State: 0x%lx\n",
				FIELD_GET(DPRX_TRAIN_STATE, dprx_trn_status2));
			status = connector_status_disconnected;
			SERDES_DBG_CHIP("3 status=%d state=%d\n", status, serdes->extcon->state);
			goto out;
		}
	} else {
		atomic_set(&serdes_bridge->triggered, 0);
		SERDES_DBG_CHIP("4 status=%d state=%d\n", status, serdes->extcon->state);
	}

	if (serdes_bridge->next_bridge && (serdes_bridge->next_bridge->ops & DRM_BRIDGE_OP_DETECT))
		return drm_bridge_detect(serdes_bridge->next_bridge);

out:
	serdes_bridge->status = status;
	SERDES_DBG_CHIP("5 status=%d state=%d\n", status, serdes->extcon->state);
	return status;
}

static int max96745_bridge_enable(struct serdes *serdes)
{
	int ret = 0;

	SERDES_DBG_CHIP("%s: serdes chip %s ret=%d state=%d\n",
				__func__, serdes->chip_data->name, ret, serdes->extcon->state);
	return ret;
}

static int max96745_bridge_disable(struct serdes *serdes)
{
	int ret = 0;

	return ret;
}

static struct serdes_chip_bridge_ops max96745_bridge_ops = {
	.init = max96745_bridge_init,
	.attach = max96745_bridge_attach,
	.detect = max96745_bridge_detect,
	.enable = max96745_bridge_enable,
	.disable = max96745_bridge_disable,
};

static int max96745_pinctrl_set_mux(struct serdes *serdes,
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
			__func__, serdes->chip_data->name,
			func->name, func->data, grp->name, grp->data, grp->num_pins);

	if (func->data) {
		struct serdes_function_data *data = func->data;

		for (i = 0; i < grp->num_pins; i++) {
			serdes_set_bits(serdes,
					GPIO_A_REG(grp->pins[i] - pinctrl->pin_base),
					GPIO_OUT_DIS,
					FIELD_PREP(GPIO_OUT_DIS, data->gpio_out_dis));
			if (data->gpio_tx_en_a || data->gpio_tx_en_b)
				serdes_set_bits(serdes,
						GPIO_B_REG(grp->pins[i] - pinctrl->pin_base),
						GPIO_TX_ID,
						FIELD_PREP(GPIO_TX_ID, data->gpio_tx_id));
			if (data->gpio_rx_en_a || data->gpio_rx_en_b)
				serdes_set_bits(serdes,
						GPIO_C_REG(grp->pins[i] - pinctrl->pin_base),
						GPIO_RX_ID,
						FIELD_PREP(GPIO_RX_ID, data->gpio_rx_id));
			serdes_set_bits(serdes,
					GPIO_D_REG(grp->pins[i] - pinctrl->pin_base),
					GPIO_TX_EN_A | GPIO_TX_EN_B | GPIO_IO_RX_EN |
					GPIO_RX_EN_A | GPIO_RX_EN_B,
					FIELD_PREP(GPIO_TX_EN_A, data->gpio_tx_en_a) |
					FIELD_PREP(GPIO_TX_EN_B, data->gpio_tx_en_b) |
					FIELD_PREP(GPIO_RX_EN_A, data->gpio_rx_en_a) |
					FIELD_PREP(GPIO_RX_EN_B, data->gpio_rx_en_b) |
					FIELD_PREP(GPIO_IO_RX_EN, data->gpio_io_rx_en));
		}
	}

	return 0;
}

static int max96745_pinctrl_config_get(struct serdes *serdes,
				       unsigned int pin, unsigned long *config)
{
	enum pin_config_param param = pinconf_to_config_param(*config);
	unsigned int gpio_a_reg, gpio_b_reg;
	u16 arg = 0;

	serdes_reg_read(serdes, GPIO_A_REG(pin), &gpio_a_reg);
	serdes_reg_read(serdes, GPIO_B_REG(pin), &gpio_b_reg);

	SERDES_DBG_CHIP("%s: serdes chip %s pin=%d param=%d\n", __func__, serdes->chip_data->name, pin, param);

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

static int max96745_pinctrl_config_set(struct serdes *serdes,
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

static struct serdes_chip_pinctrl_ops max96745_pinctrl_ops = {
	.pin_config_get = max96745_pinctrl_config_get,
	.pin_config_set = max96745_pinctrl_config_set,
	.set_mux = max96745_pinctrl_set_mux,
};

static int max96745_gpio_direction_input(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96745_gpio_direction_output(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96745_gpio_get_level(struct serdes *serdes, int gpio)
{
	return 0;
}

static int max96745_gpio_set_level(struct serdes *serdes, int gpio, int value)
{
	return 0;
}

static int max96745_gpio_set_config(struct serdes *serdes, int gpio, unsigned long config)
{
	return 0;
}

static int max96745_gpio_to_irq(struct serdes *serdes, int gpio)
{
	return 0;
}

static struct serdes_chip_gpio_ops max96745_gpio_ops = {
	.direction_input = max96745_gpio_direction_input,
	.direction_output = max96745_gpio_direction_output,
	.get_level = max96745_gpio_get_level,
	.set_level = max96745_gpio_set_level,
	.set_config = max96745_gpio_set_config,
	.to_irq = max96745_gpio_to_irq,
};

static int max96745_select(struct serdes *serdes, int chan)
{
	/*0076 for linkA and 0086 for linkB*/
	if (chan == DUAL_LINK) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		SERDES_DBG_CHIP("%s: enable %s remote i2c of linkA and linkB\n", __func__,
				serdes->chip_data->name);
	} else if (chan == LINKA) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		SERDES_DBG_CHIP("%s: only enable %s remote i2c of linkA\n", __func__,
				serdes->chip_data->name);
	} else if (chan == LINKB) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		SERDES_DBG_CHIP("%s: only enable %s remote i2c of linkB\n", __func__,
				serdes->chip_data->name);
	} else if (chan == SPLITTER_MODE) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		SERDES_DBG_CHIP("%s: enable %s remote i2c of linkA and linkB\n", __func__,
				serdes->chip_data->name);
	}

	return 0;
}

static int max96745_deselect(struct serdes *serdes, int chan)
{

	if (chan == DUAL_LINK) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		SERDES_DBG_CHIP("%s: disable %s remote i2c of linkA and linkB\n", __func__,
				serdes->chip_data->name);
	} else if (chan == LINKA) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		SERDES_DBG_CHIP("%s: only disable %s remote i2c of linkA\n", __func__,
				serdes->chip_data->name);
	} else if (chan == LINKB) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 0));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		SERDES_DBG_CHIP("%s: only disable %s remote i2c of linkB\n", __func__,
				serdes->chip_data->name);
	} else if (chan == SPLITTER_MODE) {
		serdes_set_bits(serdes, 0x0076, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		serdes_set_bits(serdes, 0x0086, DIS_REM_CC,
				   FIELD_PREP(DIS_REM_CC, 1));
		SERDES_DBG_CHIP("%s: disable %s remote i2c of linkA and linkB\n", __func__,
				serdes->chip_data->name);
	}

	return 0;
}


static struct serdes_chip_split_ops max96745_split_ops = {
	.select = max96745_select,
	.deselect = max96745_deselect,
};

static int max96745_pm_suspend(struct serdes *serdes)
{
	return 0;
}

static int max96745_pm_resume(struct serdes *serdes)
{
	return 0;
}

static struct serdes_chip_pm_ops max96745_pm_ops = {
	.suspend = max96745_pm_suspend,
	.resume = max96745_pm_resume,
};

static int max96745_irq_lock_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static int max96745_irq_err_handle(struct serdes *serdes)
{
	return IRQ_HANDLED;
}

static struct serdes_chip_irq_ops max96745_irq_ops = {
	.lock_handle = max96745_irq_lock_handle,
	.err_handle = max96745_irq_err_handle,
};

struct serdes_chip_data serdes_max96745_data = {
	.name		= "max96745",
	.serdes_type	= TYPE_SER,
	.serdes_id	= MAXIM_ID_MAX96745,
	.connector_type	= DRM_MODE_CONNECTOR_eDP,
	.regmap_config	= &max96745_regmap_config,
	.pinctrl_info	= &max96745_pinctrl_info,
	.bridge_ops	= &max96745_bridge_ops,
	.pinctrl_ops	= &max96745_pinctrl_ops,
	.gpio_ops	= &max96745_gpio_ops,
	.split_ops	= &max96745_split_ops,
	.pm_ops		= &max96745_pm_ops,
	.irq_ops	= &max96745_irq_ops,
};
EXPORT_SYMBOL_GPL(serdes_max96745_data);

MODULE_LICENSE("GPL");
