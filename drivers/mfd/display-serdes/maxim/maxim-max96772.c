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

struct config_desc {
	u16 reg;
	u8 mask;
	u8 val;
};

struct serdes_group_data {
	const struct config_desc *configs;
	int num_configs;
};

static int MAX96772_GPIO0_pins[] = {0};
static int MAX96772_GPIO1_pins[] = {1};
static int MAX96772_GPIO2_pins[] = {2};
static int MAX96772_GPIO3_pins[] = {3};
static int MAX96772_GPIO4_pins[] = {4};
static int MAX96772_GPIO5_pins[] = {5};
static int MAX96772_GPIO6_pins[] = {6};
static int MAX96772_GPIO7_pins[] = {7};

static int MAX96772_GPIO8_pins[] = {8};
static int MAX96772_GPIO9_pins[] = {9};
static int MAX96772_GPIO10_pins[] = {10};
static int MAX96772_GPIO11_pins[] = {11};
static int MAX96772_GPIO12_pins[] = {12};
static int MAX96772_GPIO13_pins[] = {13};
static int MAX96772_GPIO14_pins[] = {14};
static int MAX96772_GPIO15_pins[] = {15};

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
	"MAX96772_GPIO0", "MAX96772_GPIO1", "MAX96772_GPIO2", "MAX96772_GPIO3",
	"MAX96772_GPIO4", "MAX96772_GPIO5", "MAX96772_GPIO6", "MAX96772_GPIO7",

	"MAX96772_GPIO8", "MAX96772_GPIO9", "MAX96772_GPIO10", "MAX96772_GPIO11",
	"MAX96772_GPIO12", "MAX96772_GPIO13", "MAX96772_GPIO14", "MAX96772_GPIO15",
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

static struct pinctrl_pin_desc max96772_pins_desc[] = {
	PINCTRL_PIN(MAXIM_MAX96772_GPIO0, "MAX96772_GPIO0"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO1, "MAX96772_GPIO1"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO2, "MAX96772_GPIO2"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO3, "MAX96772_GPIO3"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO4, "MAX96772_GPIO4"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO5, "MAX96772_GPIO5"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO6, "MAX96772_GPIO6"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO7, "MAX96772_GPIO7"),

	PINCTRL_PIN(MAXIM_MAX96772_GPIO8, "MAX96772_GPIO8"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO9, "MAX96772_GPIO9"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO10, "MAX96772_GPIO10"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO11, "MAX96772_GPIO11"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO12, "MAX96772_GPIO12"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO13, "MAX96772_GPIO13"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO14, "MAX96772_GPIO14"),
	PINCTRL_PIN(MAXIM_MAX96772_GPIO15, "MAX96772_GPIO15"),
};

static struct group_desc max96772_groups_desc[] = {
	GROUP_DESC(MAX96772_GPIO0),
	GROUP_DESC(MAX96772_GPIO1),
	GROUP_DESC(MAX96772_GPIO2),
	GROUP_DESC(MAX96772_GPIO3),
	GROUP_DESC(MAX96772_GPIO4),
	GROUP_DESC(MAX96772_GPIO5),
	GROUP_DESC(MAX96772_GPIO6),
	GROUP_DESC(MAX96772_GPIO7),

	GROUP_DESC(MAX96772_GPIO8),
	GROUP_DESC(MAX96772_GPIO9),
	GROUP_DESC(MAX96772_GPIO10),
	GROUP_DESC(MAX96772_GPIO11),
	GROUP_DESC(MAX96772_GPIO12),
	GROUP_DESC(MAX96772_GPIO13),
	GROUP_DESC(MAX96772_GPIO14),
	GROUP_DESC(MAX96772_GPIO15),
};

static struct function_desc max96772_functions_desc[] = {
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

static struct serdes_chip_pinctrl_info max96772_pinctrl_info = {
	.pins = max96772_pins_desc,
	.num_pins = ARRAY_SIZE(max96772_pins_desc),
	.groups = max96772_groups_desc,
	.num_groups = ARRAY_SIZE(max96772_groups_desc),
	.functions = max96772_functions_desc,
	.num_functions = ARRAY_SIZE(max96772_functions_desc),
};

static const struct reg_sequence max96772_clk_ref[3][14] = {
	{
		{ 0xe7b2, 0x50 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0xcc },
		{ 0xe7b5, 0x44 },
		{ 0xe7b6, 0x81 },
		{ 0xe7b7, 0x30 },
		{ 0xe7b8, 0x07 },
		{ 0xe7b9, 0x10 },
		{ 0xe7ba, 0x01 },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x00 },
		{ 0xe7be, 0x52 },
		{ 0xe7bf, 0x00 },
	}, {
		{ 0xe7b2, 0x50 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0x00 },
		{ 0xe7b5, 0x40 },
		{ 0xe7b6, 0x6c },
		{ 0xe7b7, 0x20 },
		{ 0xe7b8, 0x07 },
		{ 0xe7b9, 0x00 },
		{ 0xe7ba, 0x01 },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x00 },
		{ 0xe7be, 0x52 },
		{ 0xe7bf, 0x00 },
	}, {
		{ 0xe7b2, 0x30 },
		{ 0xe7b3, 0x00 },
		{ 0xe7b4, 0x00 },
		{ 0xe7b5, 0x40 },
		{ 0xe7b6, 0x6c },
		{ 0xe7b7, 0x20 },
		{ 0xe7b8, 0x14 },
		{ 0xe7b9, 0x00 },
		{ 0xe7ba, 0x2e },
		{ 0xe7bb, 0x00 },
		{ 0xe7bc, 0x00 },
		{ 0xe7bd, 0x01 },
		{ 0xe7be, 0x32 },
		{ 0xe7bf, 0x00 },
	}
};

static int max96772_aux_dpcd_read(struct serdes *serdes, unsigned int reg, unsigned int *value)
{
	serdes_reg_write(serdes, 0xe778, reg & 0xff);
	serdes_reg_write(serdes, 0xe779, (reg >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe77c, (reg >> 16) & 0xff);
	serdes_reg_write(serdes, 0xe776, 0x10);
	serdes_reg_write(serdes, 0xe777, 0x80);
	/* FIXME */
	msleep(50);
	serdes_reg_read(serdes, 0xe77a, value);

	return 0;
}

static int max96772_panel_init(struct serdes *serdes)
{
	return 0;
}

static int max96772_panel_prepare(struct serdes *serdes)
{
	const struct drm_display_mode *mode = &serdes->serdes_panel->mode;
	u32 hfp, hsa, hbp, hact;
	u32 vact, vsa, vfp, vbp;
	u64 hwords, mvid;
	bool hsync_pol, vsync_pol;

	serdes_reg_write(serdes, 0xe790, serdes->serdes_panel->link_rate);
	serdes_reg_write(serdes, 0xe792, serdes->serdes_panel->lane_count);

	if (serdes->serdes_panel->ssc) {
		serdes_reg_write(serdes, 0xe7b0, 0x01);
		serdes_reg_write(serdes, 0xe7b1, 0x10);
	} else {
		serdes_reg_write(serdes, 0xe7b1, 0x00);
	}

	switch (serdes->serdes_panel->link_rate) {
	case DP_LINK_BW_5_4:
		serdes_multi_reg_write(serdes, max96772_clk_ref[2],
				       ARRAY_SIZE(max96772_clk_ref[2]));
		break;
	case DP_LINK_BW_2_7:
		serdes_multi_reg_write(serdes, max96772_clk_ref[1],
				       ARRAY_SIZE(max96772_clk_ref[1]));
		break;
	case DP_LINK_BW_1_62:
	default:
		serdes_multi_reg_write(serdes, max96772_clk_ref[0],
				       ARRAY_SIZE(max96772_clk_ref[0]));
		break;
	}

	vact = mode->vdisplay;
	vsa = mode->vsync_end - mode->vsync_start;
	vfp = mode->vsync_start - mode->vdisplay;
	vbp = mode->vtotal - mode->vsync_end;
	hact = mode->hdisplay;
	hsa = mode->hsync_end - mode->hsync_start;
	hfp = mode->hsync_start - mode->hdisplay;
	hbp = mode->htotal - mode->hsync_end;

	serdes_reg_write(serdes, 0xe794, hact & 0xff);
	serdes_reg_write(serdes, 0xe795, (hact >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe796, hfp & 0xff);
	serdes_reg_write(serdes, 0xe797, (hfp >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe798, hsa & 0xff);
	serdes_reg_write(serdes, 0xe799, (hsa >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe79a, hbp & 0xff);
	serdes_reg_write(serdes, 0xe79b, (hbp >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe79c, vact & 0xff);
	serdes_reg_write(serdes, 0xe79d, (vact >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe79e, vfp & 0xff);
	serdes_reg_write(serdes, 0xe79f, (vfp >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe7a0, vsa & 0xff);
	serdes_reg_write(serdes, 0xe7a1, (vsa >> 8) & 0xff);
	serdes_reg_write(serdes, 0xe7a2, vbp & 0xff);
	serdes_reg_write(serdes, 0xe7a3, (vbp >> 8) & 0xff);

	hsync_pol = !!(mode->flags & DRM_MODE_FLAG_NHSYNC);
	vsync_pol = !!(mode->flags & DRM_MODE_FLAG_NVSYNC);
	serdes_reg_write(serdes, 0xe7ac, hsync_pol | (vsync_pol << 1));

	/* NVID should always be set to 0x8000 */
	serdes_reg_write(serdes, 0xe7a8, 0);
	serdes_reg_write(serdes, 0xe7a9, 0x80);

	/* HWORDS = ((HRES x bits / pixel) / 16) - LANE_COUNT */
	hwords = DIV_ROUND_CLOSEST_ULL(hact * 24, 16) - serdes->serdes_panel->lane_count;
	serdes_reg_write(serdes, 0xe7a4, hwords);
	serdes_reg_write(serdes, 0xe7a5, hwords >> 8);

	/* MVID = (PCLK x NVID) x 10 / Link Rate */
	mvid = DIV_ROUND_CLOSEST_ULL((u64)mode->clock * 32768,
				     drm_dp_bw_code_to_link_rate(serdes->serdes_panel->link_rate));
	serdes_reg_write(serdes, 0xe7a6, mvid & 0xff);
	serdes_reg_write(serdes, 0xe7a7, (mvid >> 8) & 0xff);

	serdes_reg_write(serdes, 0xe7aa, 0x40);
	serdes_reg_write(serdes, 0xe7ab, 0x00);

	return 0;
}

static int max96772_panel_unprepare(struct serdes *serdes)
{
	return 0;
}

static int max96772_panel_enable(struct serdes *serdes)
{
	u32 status[2];
	u32 val;
	int ret;

	/* Run link training */
	serdes_reg_write(serdes, 0xe776, 0x02);
	serdes_reg_write(serdes, 0xe777, 0x80);

	ret = regmap_read_poll_timeout(serdes->regmap, 0x07f0, val,
				       val & 0x01, MSEC_PER_SEC,
				       500 * MSEC_PER_SEC);
	if (!ret)
		return 0;

	ret = max96772_aux_dpcd_read(serdes, DP_LANE0_1_STATUS, &status[0]);
	if (ret)
		return ret;

	ret = max96772_aux_dpcd_read(serdes, DP_LANE2_3_STATUS, &status[1]);
	if (ret)
		return ret;

	dev_err(serdes->dev, "Link Training failed: LANE0_1_STATUS=0x%02x, LANE2_3_STATUS=0x%02x\n",
		status[0], status[1]);

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

static int max96772_pinctrl_set_mux(struct serdes *serdes,
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

static int max96772_pinctrl_config_get(struct serdes *serdes,
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

static int max96772_pinctrl_config_set(struct serdes *serdes,
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
