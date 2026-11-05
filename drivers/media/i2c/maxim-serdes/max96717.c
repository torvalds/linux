// SPDX-License-Identifier: GPL-2.0
/*
 * Maxim MAX96717 GMSL2 Serializer Driver
 *
 * Copyright (C) 2025 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/gpio/driver.h>
#include <linux/iopoll.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/regmap.h>

#include "max_ser.h"

#define MAX96717_REG0				0x0

#define MAX96717_REG2				0x2
#define MAX96717_REG2_VID_TX_EN_P(p)		BIT(4 + (p))

#define MAX96717_REG3				0x3
#define MAX96717_REG3_RCLKSEL			GENMASK(1, 0)
#define MAX96717_REG3_RCLK_ALT			BIT(2)

#define MAX96717_REG6				0x6
#define MAX96717_REG6_RCLKEN			BIT(5)

#define MAX96717_I2C_2(x)			(0x42 + (x) * 0x2)
#define MAX96717_I2C_2_SRC			GENMASK(7, 1)

#define MAX96717_I2C_3(x)			(0x43 + (x) * 0x2)
#define MAX96717_I2C_3_DST			GENMASK(7, 1)

#define MAX96717_TX3(p)				(0x53 + (p) * 0x4)
#define MAX96717_TX3_TX_STR_SEL			GENMASK(1, 0)

#define MAX96717_VIDEO_TX0(p)			(0x100 + (p) * 0x8)
#define MAX96717_VIDEO_TX0_AUTO_BPP		BIT(3)

#define MAX96717_VIDEO_TX1(p)			(0x101 + (p) * 0x8)
#define MAX96717_VIDEO_TX1_BPP			GENMASK(5, 0)

#define MAX96717_VIDEO_TX2(p)			(0x102 + (p) * 0x8)
#define MAX96717_VIDEO_TX2_PCLKDET		BIT(7)
#define MAX96717_VIDEO_TX2_DRIFT_DET_EN		BIT(1)

#define MAX96717_VTX0(p)			(0x1c8 + (p) * 0x43)
#define MAX96717_VTX0_VTG_MODE			GENMASK(1, 0)
#define MAX96717_VTX0_VTG_MODE_FREE_RUNNING	0b11
#define MAX96717_VTX0_DE_INV			BIT(2)
#define MAX96717_VTX0_HS_INV			BIT(3)
#define MAX96717_VTX0_VS_INV			BIT(4)
#define MAX96717_VTX0_GEN_DE			BIT(5)
#define MAX96717_VTX0_GEN_HS			BIT(6)
#define MAX96717_VTX0_GEN_VS			BIT(7)

#define MAX96717_VTX1(p)			(0x1c9 + (p) * 0x43)
#define MAX96717_VTX1_PATGEN_CLK_SRC		GENMASK(3, 1)
#define MAX96717_VTX1_PATGEN_CLK_SRC_25MHZ	0b100
#define MAX96717_VTX1_PATGEN_CLK_SRC_75MHZ	0b101
#define MAX96717_VTX1_PATGEN_CLK_SRC_150MHZ	0b110
#define MAX96717_VTX1_PATGEN_CLK_SRC_375MHZ	0b111

#define MAX96717_VTX2_VS_DLY_2(p)		(0x1ca + (p) * 0x43)
#define MAX96717_VTX5_VS_HIGH_2(p)		(0x1cd + (p) * 0x43)
#define MAX96717_VTX8_VS_LOW_2(p)		(0x1d0 + (p) * 0x43)
#define MAX96717_VTX11_V2H_2(p)			(0x1d3 + (p) * 0x43)
#define MAX96717_VTX14_HS_HIGH_1(p)		(0x1d6 + (p) * 0x43)
#define MAX96717_VTX16_HS_LOW_1(p)		(0x1d8 + (p) * 0x43)
#define MAX96717_VTX18_HS_CNT_1(p)		(0x1da + (p) * 0x43)
#define MAX96717_VTX20_V2D_2(p)			(0x1dc + (p) * 0x43)
#define MAX96717_VTX23_DE_HIGH_1(p)		(0x1df + (p) * 0x43)
#define MAX96717_VTX25_DE_LOW_1(p)		(0x1e1 + (p) * 0x43)
#define MAX96717_VTX27_DE_CNT_1(p)		(0x1e3 + (p) * 0x43)
#define MAX96717_VTX29(p)			(0x1e5 + (p) * 0x43)

#define MAX96717_VTX29_PATGEN_MODE		GENMASK(1, 0)
#define MAX96717_VTX29_PATGEN_MODE_DISABLED	0b00
#define MAX96717_VTX29_PATGEN_MODE_CHECKER	0b01
#define MAX96717_VTX29_PATGEN_MODE_GRADIENT	0b10

#define MAX96717_VTX30_GRAD_INCR(p)		(0x1e6 + (p) * 0x43)
#define MAX96717_VTX31_CHKR_A_L(p)		(0x1e7 + (p) * 0x43)
#define MAX96717_VTX34_CHKR_B_L(p)		(0x1ea + (p) * 0x43)
#define MAX96717_VTX37_CHKR_RPT_A(p)		(0x1ed + (p) * 0x43)
#define MAX96717_VTX38_CHKR_RPT_B(p)		(0x1ee + (p) * 0x43)
#define MAX96717_VTX39_CHKR_ALT(p)		(0x1ef + (p) * 0x43)

#define MAX96717_GPIO_A(x)			(0x2be + (x) * 0x3)
#define MAX96717_GPIO_A_GPIO_OUT_DIS		BIT(0)
#define MAX96717_GPIO_A_GPIO_TX_EN		BIT(1)
#define MAX96717_GPIO_A_GPIO_RX_EN		BIT(2)
#define MAX96717_GPIO_A_GPIO_IN			BIT(3)
#define MAX96717_GPIO_A_GPIO_OUT		BIT(4)
#define MAX96717_GPIO_A_TX_COMP_EN		BIT(5)
#define MAX96717_GPIO_A_RES_CFG			BIT(7)

#define MAX96717_GPIO_B(x)			(0x2bf + (x) * 0x3)
#define MAX96717_GPIO_B_GPIO_TX_ID		GENMASK(4, 0)
#define MAX96717_GPIO_B_OUT_TYPE		BIT(5)
#define MAX96717_GPIO_B_PULL_UPDN_SEL		GENMASK(7, 6)
#define MAX96717_GPIO_B_PULL_UPDN_SEL_NONE	0b00
#define MAX96717_GPIO_B_PULL_UPDN_SEL_PU	0b01
#define MAX96717_GPIO_B_PULL_UPDN_SEL_PD	0b10

#define MAX96717_GPIO_C(x)			(0x2c0 + (x) * 0x3)
#define MAX96717_GPIO_C_GPIO_RX_ID		GENMASK(4, 0)

#define MAX96717_CMU2				0x302
#define MAX96717_CMU2_PFDDIV_RSHORT		GENMASK(6, 4)
#define MAX96717_CMU2_PFDDIV_RSHORT_1_1V	0b001

#define MAX96717_FRONTTOP_0			0x308
#define MAX96717_FRONTTOP_0_CLK_SEL_P(x)	BIT(x)
#define MAX96717_FRONTTOP_0_START_PORT(x)	BIT((x) + 4)

#define MAX96717_FRONTTOP_1(p)			(0x309 + (p) * 0x2)
#define MAX96717_FRONTTOP_2(p)			(0x30a + (p) * 0x2)

#define MAX96717_FRONTTOP_9			0x311
#define MAX96717_FRONTTOP_9_START_PORT(p, x)	BIT((p) + (x) * 4)

#define MAX96717_FRONTTOP_10			0x312
#define MAX96717_FRONTTOP_10_BPP8DBL(p)		BIT(p)

#define MAX96717_FRONTTOP_11			0x313
#define MAX96717_FRONTTOP_11_BPP10DBL(p)	BIT(p)
#define MAX96717_FRONTTOP_11_BPP12DBL(p)	BIT((p) + 4)

#define MAX96717_FRONTTOP_12(p, x)		(0x314 + (p) * 0x2 + (x))
#define MAX96717_MEM_DT_SEL			GENMASK(5, 0)
#define MAX96717_MEM_DT_EN			BIT(6)

#define MAX96717_FRONTTOP_20(p)			(0x31c + (p) * 0x1)
#define MAX96717_FRONTTOP_20_SOFT_BPP_EN	BIT(5)
#define MAX96717_FRONTTOP_20_SOFT_BPP		GENMASK(4, 0)

#define MAX96717_MIPI_RX0			0x330
#define MAX96717_MIPI_RX0_NONCONTCLK_EN		BIT(6)

#define MAX96717_MIPI_RX1			0x331
#define MAX96717_MIPI_RX1_CTRL_NUM_LANES	GENMASK(5, 4)

#define MAX96717_MIPI_RX2			0x332
#define MAX96717_MIPI_RX2_PHY1_LANE_MAP		GENMASK(7, 4)

#define MAX96717_MIPI_RX3			0x333
#define MAX96717_MIPI_RX3_PHY2_LANE_MAP		GENMASK(3, 0)

#define MAX96717_MIPI_RX4			0x334
#define MAX96717_MIPI_RX4_PHY1_POL_MAP		GENMASK(5, 4)

#define MAX96717_MIPI_RX5			0x335
#define MAX96717_MIPI_RX5_PHY2_POL_MAP		GENMASK(1, 0)
#define MAX96717_MIPI_RX5_PHY2_POL_MAP_CLK	BIT(2)

#define MAX96717_EXTA(x)			(0x3dc + (x))

#define MAX96717_EXT11				0x383
#define MAX96717_EXT11_TUN_MODE			BIT(7)

#define MAX96717_EXT21				0x38d
#define MAX96717_EXT22				0x38e
#define MAX96717_EXT23				0x38f
#define MAX96717_EXT24				0x390

#define MAX96717_REF_VTG0			0x3f0
#define MAX96717_REF_VTG0_REFGEN_EN		BIT(0)
#define MAX96717_REF_VTG0_REFGEN_RST		BIT(1)
#define MAX96717_REF_VTG0_REFGEN_PREDEF_FREQ_ALT\
						BIT(3)
#define MAX96717_REF_VTG0_REFGEN_PREDEF_FREQ	GENMASK(5, 4)
#define MAX96717_REF_VTG0_REFGEN_PREDEF_EN	BIT(6)

#define MAX96717_PIO_SLEW_0			0x56f
#define MAX96717_PIO_SLEW_0_PIO00_SLEW		GENMASK(1, 0)
#define MAX96717_PIO_SLEW_0_PIO01_SLEW		GENMASK(3, 2)
#define MAX96717_PIO_SLEW_0_PIO02_SLEW		GENMASK(5, 4)

#define MAX96717_PIO_SLEW_1			0x570
#define MAX96717_PIO_SLEW_1_PIO05_SLEW		GENMASK(3, 2)
#define MAX96717_PIO_SLEW_1_PIO06_SLEW		GENMASK(5, 4)

#define MAX96717_PIO_SLEW_2			0x571
#define MAX96717_PIO_SLEW_2_PIO010_SLEW		GENMASK(5, 4)
#define MAX96717_PIO_SLEW_2_PIO011_SLEW		GENMASK(7, 6)

#define MAX96717_PIO_SLEW_FASTEST		0b00

#define MAX96717_BIAS_PULL_STRENGTH_1000000_OHM	1000000U
#define MAX96717_BIAS_PULL_STRENGTH_40000_OHM	40000U

#define MAX96717_DEFAULT_CLKOUT_RATE		24000000UL

#define MAX96717_NAME				"max96717"
#define MAX96717_PINCTRL_NAME			MAX96717_NAME "-pinctrl"
#define MAX96717_GPIOCHIP_NAME			MAX96717_NAME "-gpiochip"
#define MAX96717_GPIO_NUM			11
#define MAX96717_RCLK_ALT_MFP			2
#define MAX96717_RCLK_MFP			4
#define MAX96717_PIPES_NUM			4
#define MAX96717_PHYS_NUM			2

struct max96717_priv {
	struct max_ser ser;
	struct pinctrl_desc pctldesc;
	struct gpio_chip gc;
	const struct max96717_chip_info *info;

	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct pinctrl_dev *pctldev;

	struct clk_hw clk_hw;
	u8 pll_predef_index;
	bool rclkout_muxed;
};

struct max96717_chip_info {
	bool supports_3_data_lanes;
	bool supports_noncontinuous_clock;
	bool supports_pkt_cnt;
	unsigned int modes;
	unsigned int num_pipes;
	unsigned int num_dts_per_pipe;
	unsigned int pipe_hw_ids[MAX96717_PIPES_NUM];
	unsigned int num_phys;
	unsigned int phy_hw_ids[MAX96717_PHYS_NUM];
};

#define ser_to_priv(_ser) \
	container_of(_ser, struct max96717_priv, ser)

static inline struct max96717_priv *clk_hw_to_priv(struct clk_hw *hw)
{
	return container_of(hw, struct max96717_priv, clk_hw);
}

static const struct regmap_config max96717_i2c_regmap = {
	.reg_bits = 16,
	.val_bits = 8,
	.max_register = 0x1f00,
};

static int max96717_wait_for_device(struct max96717_priv *priv)
{
	unsigned int val;
	int ret, err;

	err = read_poll_timeout(regmap_read, ret,
				!ret && val,
				100 * USEC_PER_MSEC,
				1 * USEC_PER_SEC, false,
				priv->regmap, MAX96717_REG0, &val);
	if (err)
		dev_err(priv->dev, "Timeout waiting for serializer: %d\n", ret);

	return err;
}

#define MAX96717_PIN(n) \
	PINCTRL_PIN(n, "mfp" __stringify(n))

static const struct pinctrl_pin_desc max96717_pins[] = {
	MAX96717_PIN(0),
	MAX96717_PIN(1),
	MAX96717_PIN(2),
	MAX96717_PIN(3),
	MAX96717_PIN(4),
	MAX96717_PIN(5),
	MAX96717_PIN(6),
	MAX96717_PIN(7),
	MAX96717_PIN(8),
	MAX96717_PIN(9),
	MAX96717_PIN(10),
};

#define MAX96717_GROUP_PINS(name, ...) \
	static const unsigned int name ## _pins[] = { __VA_ARGS__ }

MAX96717_GROUP_PINS(mfp0, 0);
MAX96717_GROUP_PINS(mfp1, 1);
MAX96717_GROUP_PINS(mfp2, 2);
MAX96717_GROUP_PINS(mfp3, 3);
MAX96717_GROUP_PINS(mfp4, 4);
MAX96717_GROUP_PINS(mfp5, 5);
MAX96717_GROUP_PINS(mfp6, 6);
MAX96717_GROUP_PINS(mfp7, 7);
MAX96717_GROUP_PINS(mfp8, 8);
MAX96717_GROUP_PINS(mfp9, 9);
MAX96717_GROUP_PINS(mfp10, 10);

#define MAX96717_GROUP(name) \
	PINCTRL_PINGROUP(__stringify(name), name ## _pins, ARRAY_SIZE(name ## _pins))

static const struct pingroup max96717_ctrl_groups[] = {
	MAX96717_GROUP(mfp0),
	MAX96717_GROUP(mfp1),
	MAX96717_GROUP(mfp2),
	MAX96717_GROUP(mfp3),
	MAX96717_GROUP(mfp4),
	MAX96717_GROUP(mfp5),
	MAX96717_GROUP(mfp6),
	MAX96717_GROUP(mfp7),
	MAX96717_GROUP(mfp8),
	MAX96717_GROUP(mfp9),
	MAX96717_GROUP(mfp10),
};

#define MAX96717_FUNC_GROUPS(name, ...) \
	static const char * const name ## _groups[] = { __VA_ARGS__ }

MAX96717_FUNC_GROUPS(gpio, "mfp0", "mfp1", "mfp2", "mfp3", "mfp4", "mfp5",
		     "mfp6", "mfp7", "mfp8", "mfp9", "mfp10");
MAX96717_FUNC_GROUPS(rclkout, "mfp2", "mfp4");

enum max96717_func {
	max96717_func_gpio,
	max96717_func_rclkout,
};

#define MAX96717_FUNC(name)						\
	[max96717_func_ ## name] =					\
		PINCTRL_PINFUNCTION(__stringify(name), name ## _groups,	\
				    ARRAY_SIZE(name ## _groups))

static const struct pinfunction max96717_functions[] = {
	MAX96717_FUNC(gpio),
	MAX96717_FUNC(rclkout),
};

#define MAX96717_PINCTRL_X(x)			(PIN_CONFIG_END + (x))
#define MAX96717_PINCTRL_JITTER_COMPENSATION_EN	MAX96717_PINCTRL_X(1)
#define MAX96717_PINCTRL_TX_ID			MAX96717_PINCTRL_X(2)
#define MAX96717_PINCTRL_RX_ID			MAX96717_PINCTRL_X(3)
#define MAX96717_PINCTRL_PULL_STRENGTH_HIGH	MAX96717_PINCTRL_X(4)
#define MAX96717_PINCTRL_INPUT_VALUE		MAX96717_PINCTRL_X(5)
#define MAX96717_PINCTRL_TX_EN			MAX96717_PINCTRL_X(6)
#define MAX96717_PINCTRL_RX_EN			MAX96717_PINCTRL_X(7)

static const struct pinconf_generic_params max96717_cfg_params[] = {
	{ "maxim,jitter-compensation", MAX96717_PINCTRL_JITTER_COMPENSATION_EN, 1 },
	{ "maxim,tx-id", MAX96717_PINCTRL_TX_ID, 0 },
	{ "maxim,rx-id", MAX96717_PINCTRL_RX_ID, 0 },
};

static int max96717_ctrl_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(max96717_ctrl_groups);
}

static const char *max96717_ctrl_get_group_name(struct pinctrl_dev *pctldev,
						unsigned int selector)
{
	return max96717_ctrl_groups[selector].name;
}

static int max96717_ctrl_get_group_pins(struct pinctrl_dev *pctldev,
					unsigned int selector,
					const unsigned int **pins,
					unsigned int *num_pins)
{
	*pins = (unsigned int *)max96717_ctrl_groups[selector].pins;
	*num_pins = max96717_ctrl_groups[selector].npins;

	return 0;
}

static int max96717_get_pin_config_reg(unsigned int offset, u32 param,
				       unsigned int *reg, unsigned int *mask,
				       unsigned int *val)
{
	*reg = MAX96717_GPIO_A(offset);

	switch (param) {
	case PIN_CONFIG_OUTPUT_ENABLE:
		*mask = MAX96717_GPIO_A_GPIO_OUT_DIS;
		*val = 0b0;
		return 0;
	case PIN_CONFIG_INPUT_ENABLE:
		*mask = MAX96717_GPIO_A_GPIO_OUT_DIS;
		*val = 0b1;
		return 0;
	case MAX96717_PINCTRL_TX_EN:
		*mask = MAX96717_GPIO_A_GPIO_TX_EN;
		*val = 0b1;
		return 0;
	case MAX96717_PINCTRL_RX_EN:
		*mask = MAX96717_GPIO_A_GPIO_RX_EN;
		*val = 0b1;
		return 0;
	case MAX96717_PINCTRL_INPUT_VALUE:
		*mask = MAX96717_GPIO_A_GPIO_IN;
		*val = 0b1;
		return 0;
	case PIN_CONFIG_LEVEL:
		*mask = MAX96717_GPIO_A_GPIO_OUT;
		*val = 0b1;
		return 0;
	case MAX96717_PINCTRL_JITTER_COMPENSATION_EN:
		*mask = MAX96717_GPIO_A_TX_COMP_EN;
		*val = 0b1;
		return 0;
	case MAX96717_PINCTRL_PULL_STRENGTH_HIGH:
		*mask = MAX96717_GPIO_A_RES_CFG;
		*val = 0b1;
		return 0;
	}

	*reg = MAX96717_GPIO_B(offset);

	switch (param) {
	case MAX96717_PINCTRL_TX_ID:
		*mask = MAX96717_GPIO_B_GPIO_TX_ID;
		return 0;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		*mask = MAX96717_GPIO_B_OUT_TYPE;
		*val = 0b0;
		return 0;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		*mask = MAX96717_GPIO_B_OUT_TYPE;
		*val = 0b1;
		return 0;
	case PIN_CONFIG_BIAS_DISABLE:
		*mask = MAX96717_GPIO_B_PULL_UPDN_SEL;
		*val = MAX96717_GPIO_B_PULL_UPDN_SEL_NONE;
		return 0;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		*mask = MAX96717_GPIO_B_PULL_UPDN_SEL;
		*val = MAX96717_GPIO_B_PULL_UPDN_SEL_PD;
		return 0;
	case PIN_CONFIG_BIAS_PULL_UP:
		*mask = MAX96717_GPIO_B_PULL_UPDN_SEL;
		*val = MAX96717_GPIO_B_PULL_UPDN_SEL_PU;
		return 0;
	}

	switch (param) {
	case PIN_CONFIG_SLEW_RATE:
		if (offset < 3) {
			*reg = MAX96717_PIO_SLEW_0;
			if (offset == 0)
				*mask = MAX96717_PIO_SLEW_0_PIO00_SLEW;
			else if (offset == 1)
				*mask = MAX96717_PIO_SLEW_0_PIO01_SLEW;
			else
				*mask = MAX96717_PIO_SLEW_0_PIO02_SLEW;
		} else if (offset < 5) {
			*reg = MAX96717_PIO_SLEW_1;
			if (offset == 3)
				*mask = MAX96717_PIO_SLEW_1_PIO05_SLEW;
			else
				*mask = MAX96717_PIO_SLEW_1_PIO06_SLEW;
		} else if (offset < 7) {
			return -EINVAL;
		} else if (offset < 9) {
			*reg  = MAX96717_PIO_SLEW_2;
			if (offset == 7)
				*mask = MAX96717_PIO_SLEW_2_PIO010_SLEW;
			else
				*mask = MAX96717_PIO_SLEW_2_PIO011_SLEW;
		} else {
			return -EINVAL;
		}
		return 0;
	case MAX96717_PINCTRL_RX_ID:
		*reg = MAX96717_GPIO_C(offset);
		*mask = MAX96717_GPIO_C_GPIO_RX_ID;
		return 0;
	default:
		return -ENOTSUPP;
	}
}

static int max96717_conf_pin_config_get(struct pinctrl_dev *pctldev,
					unsigned int offset,
					unsigned long *config)
{
	struct max96717_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	u32 param = pinconf_to_config_param(*config);
	unsigned int reg, mask, val, en_val;
	int ret;

	ret = max96717_get_pin_config_reg(offset, param, &reg, &mask, &en_val);
	if (ret)
		return ret;

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_PUSH_PULL:
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
	case MAX96717_PINCTRL_JITTER_COMPENSATION_EN:
	case MAX96717_PINCTRL_TX_EN:
	case MAX96717_PINCTRL_RX_EN:
		ret = regmap_read(priv->regmap, reg, &val);
		if (ret)
			return ret;

		val = field_get(mask, val) == en_val;
		if (!val)
			return -EINVAL;

		break;
	case PIN_CONFIG_OUTPUT_ENABLE:
	case PIN_CONFIG_INPUT_ENABLE:
	case MAX96717_PINCTRL_PULL_STRENGTH_HIGH:
	case MAX96717_PINCTRL_INPUT_VALUE:
	case PIN_CONFIG_LEVEL:
		ret = regmap_read(priv->regmap, reg, &val);
		if (ret)
			return ret;

		val = field_get(mask, val) == en_val;
		break;
	case MAX96717_PINCTRL_TX_ID:
	case MAX96717_PINCTRL_RX_ID:
	case PIN_CONFIG_SLEW_RATE:
		ret = regmap_read(priv->regmap, reg, &val);
		if (ret)
			return ret;

		val = field_get(mask, val);
		break;
	default:
		return -ENOTSUPP;
	}

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		*config = pinconf_to_config_packed(MAX96717_PINCTRL_PULL_STRENGTH_HIGH, 0);

		ret = max96717_conf_pin_config_get(pctldev, offset, config);
		if (ret)
			return ret;

		val = pinconf_to_config_argument(*config);
		if (val)
			val = MAX96717_BIAS_PULL_STRENGTH_1000000_OHM;
		else
			val = MAX96717_BIAS_PULL_STRENGTH_40000_OHM;

		break;
	case MAX96717_PINCTRL_TX_ID:
		*config = pinconf_to_config_packed(MAX96717_PINCTRL_TX_EN, 0);

		ret = max96717_conf_pin_config_get(pctldev, offset, config);
		if (ret)
			return ret;

		break;
	case MAX96717_PINCTRL_RX_ID:
		*config = pinconf_to_config_packed(MAX96717_PINCTRL_RX_EN, 0);

		ret = max96717_conf_pin_config_get(pctldev, offset, config);
		if (ret)
			return ret;

		break;
	default:
		break;
	}

	*config = pinconf_to_config_packed(param, val);

	return 0;
}

static int max96717_conf_pin_config_set_one(struct max96717_priv *priv,
					    unsigned int offset,
					    unsigned long config)
{
	u32 param = pinconf_to_config_param(config);
	u32 arg = pinconf_to_config_argument(config);
	unsigned int reg, mask, val, en_val;
	int ret;

	ret = max96717_get_pin_config_reg(offset, param, &reg, &mask, &en_val);
	if (ret)
		return ret;

	switch (param) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
	case PIN_CONFIG_DRIVE_PUSH_PULL:
	case PIN_CONFIG_BIAS_DISABLE:
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		val = field_prep(mask, en_val);

		ret = regmap_update_bits(priv->regmap, reg, mask, val);
		break;
	case MAX96717_PINCTRL_JITTER_COMPENSATION_EN:
	case MAX96717_PINCTRL_PULL_STRENGTH_HIGH:
	case MAX96717_PINCTRL_TX_EN:
	case MAX96717_PINCTRL_RX_EN:
	case PIN_CONFIG_OUTPUT_ENABLE:
	case PIN_CONFIG_INPUT_ENABLE:
	case PIN_CONFIG_LEVEL:
		val = field_prep(mask, arg ? en_val : ~en_val);

		ret = regmap_update_bits(priv->regmap, reg, mask, val);
		break;
	case MAX96717_PINCTRL_TX_ID:
	case MAX96717_PINCTRL_RX_ID:
	case PIN_CONFIG_SLEW_RATE:
		val = field_prep(mask, arg);

		ret = regmap_update_bits(priv->regmap, reg, mask, val);
		break;
	default:
		return -ENOTSUPP;
	}

	if (ret)
		return ret;

	switch (param) {
	case PIN_CONFIG_BIAS_PULL_DOWN:
	case PIN_CONFIG_BIAS_PULL_UP:
		arg = arg >= MAX96717_BIAS_PULL_STRENGTH_1000000_OHM;
		config = pinconf_to_config_packed(MAX96717_PINCTRL_PULL_STRENGTH_HIGH, arg);
		return max96717_conf_pin_config_set_one(priv, offset, config);
	case PIN_CONFIG_LEVEL:
		config = pinconf_to_config_packed(PIN_CONFIG_OUTPUT_ENABLE, 1);
		return max96717_conf_pin_config_set_one(priv, offset, config);
	case PIN_CONFIG_OUTPUT_ENABLE:
		config = pinconf_to_config_packed(MAX96717_PINCTRL_RX_EN, 0);
		return max96717_conf_pin_config_set_one(priv, offset, config);
	case MAX96717_PINCTRL_TX_ID:
		config = pinconf_to_config_packed(MAX96717_PINCTRL_TX_EN, 1);
		return max96717_conf_pin_config_set_one(priv, offset, config);
	case MAX96717_PINCTRL_RX_ID:
		config = pinconf_to_config_packed(MAX96717_PINCTRL_RX_EN, 1);
		return max96717_conf_pin_config_set_one(priv, offset, config);
	default:
		break;
	}

	return 0;
}

static int max96717_conf_pin_config_set(struct pinctrl_dev *pctldev,
					unsigned int offset,
					unsigned long *configs,
					unsigned int num_configs)
{
	struct max96717_priv *priv = pinctrl_dev_get_drvdata(pctldev);
	int ret;

	while (num_configs--) {
		unsigned long config = *configs;

		ret = max96717_conf_pin_config_set_one(priv, offset, config);
		if (ret)
			return ret;

		configs++;
	}

	return 0;
}

static int max96717_mux_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(max96717_functions);
}

static const char *max96717_mux_get_function_name(struct pinctrl_dev *pctldev,
						  unsigned int selector)
{
	return max96717_functions[selector].name;
}

static int max96717_mux_get_groups(struct pinctrl_dev *pctldev,
				   unsigned int selector,
				   const char * const **groups,
				   unsigned int * const num_groups)
{
	*groups = max96717_functions[selector].groups;
	*num_groups = max96717_functions[selector].ngroups;

	return 0;
}

static int max96717_mux_set_rclkout(struct max96717_priv *priv, unsigned int group)
{
	unsigned long config;
	int ret;

	config = pinconf_to_config_packed(PIN_CONFIG_SLEW_RATE,
					  MAX96717_PIO_SLEW_FASTEST);
	ret = max96717_conf_pin_config_set_one(priv, group, config);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_REG3,
				 MAX96717_REG3_RCLK_ALT,
				 group == MAX96717_RCLK_ALT_MFP);
	if (ret)
		return ret;

	priv->rclkout_muxed = true;

	return 0;
}

static int max96717_mux_set(struct pinctrl_dev *pctldev, unsigned int selector,
			    unsigned int group)
{
	struct max96717_priv *priv = pinctrl_dev_get_drvdata(pctldev);

	switch (selector) {
	case max96717_func_rclkout:
		return max96717_mux_set_rclkout(priv, group);
	}

	return 0;
}

static int max96717_gpio_get_direction(struct gpio_chip *gc, unsigned int offset)
{
	unsigned long config = pinconf_to_config_packed(PIN_CONFIG_OUTPUT_ENABLE, 0);
	struct max96717_priv *priv = gpiochip_get_data(gc);
	int ret;

	ret = max96717_conf_pin_config_get(priv->pctldev, offset, &config);
	if (ret)
		return ret;

	return pinconf_to_config_argument(config) ? GPIO_LINE_DIRECTION_OUT
						  : GPIO_LINE_DIRECTION_IN;
}

static int max96717_gpio_direction_input(struct gpio_chip *gc, unsigned int offset)
{
	unsigned long config = pinconf_to_config_packed(PIN_CONFIG_INPUT_ENABLE, 1);
	struct max96717_priv *priv = gpiochip_get_data(gc);

	return max96717_conf_pin_config_set_one(priv, offset, config);
}

static int max96717_gpio_direction_output(struct gpio_chip *gc, unsigned int offset,
					  int value)
{
	unsigned long config = pinconf_to_config_packed(PIN_CONFIG_LEVEL, value);
	struct max96717_priv *priv = gpiochip_get_data(gc);

	return max96717_conf_pin_config_set_one(priv, offset, config);
}

static int max96717_gpio_get(struct gpio_chip *gc, unsigned int offset)
{
	unsigned long config = pinconf_to_config_packed(MAX96717_PINCTRL_INPUT_VALUE, 0);
	struct max96717_priv *priv = gpiochip_get_data(gc);
	int ret;

	ret = max96717_conf_pin_config_get(priv->pctldev, offset, &config);
	if (ret)
		return ret;

	return pinconf_to_config_argument(config);
}

static int max96717_gpio_set(struct gpio_chip *gc, unsigned int offset, int value)
{
	unsigned long config = pinconf_to_config_packed(PIN_CONFIG_LEVEL, value);
	struct max96717_priv *priv = gpiochip_get_data(gc);

	return max96717_conf_pin_config_set_one(priv, offset, config);
}

static unsigned int max96717_pipe_id(struct max96717_priv *priv,
				     struct max_ser_pipe *pipe)
{
	return priv->info->pipe_hw_ids[pipe->index];
}

static unsigned int max96717_phy_id(struct max96717_priv *priv,
				    struct max_ser_phy *phy)
{
	return priv->info->phy_hw_ids[phy->index];
}

static int max96717_set_pipe_enable(struct max_ser *ser,
				    struct max_ser_pipe *pipe, bool enable)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);
	unsigned int mask = MAX96717_REG2_VID_TX_EN_P(index);

	return regmap_assign_bits(priv->regmap, MAX96717_REG2, mask, enable);
}

static int __maybe_unused max96717_reg_read(struct max_ser *ser, unsigned int reg,
					    unsigned int *val)
{
	struct max96717_priv *priv = ser_to_priv(ser);

	return regmap_read(priv->regmap, reg, val);
}

static int __maybe_unused max96717_reg_write(struct max_ser *ser, unsigned int reg,
					     unsigned int val)
{
	struct max96717_priv *priv = ser_to_priv(ser);

	return regmap_write(priv->regmap, reg, val);
}

static int max96717_set_pipe_dt_en(struct max_ser *ser, struct max_ser_pipe *pipe,
				   unsigned int i, bool enable)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);
	unsigned int reg;

	if (i < 2)
		reg = MAX96717_FRONTTOP_12(index, i);
	else
		/*
		 * DT 7 and 8 are only supported on MAX96717, no need for pipe
		 * index to be taken into account.
		 */
		reg = MAX96717_EXTA(i - 2);

	return regmap_assign_bits(priv->regmap, reg, MAX96717_MEM_DT_EN, enable);
}

static int max96717_set_pipe_dt(struct max_ser *ser, struct max_ser_pipe *pipe,
				unsigned int i, unsigned int dt)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);
	unsigned int reg;

	if (i < 2)
		reg = MAX96717_FRONTTOP_12(index,  i);
	else
		reg = MAX96717_EXTA(i - 2);

	return regmap_update_bits(priv->regmap, reg, MAX96717_MEM_DT_SEL,
				  FIELD_PREP(MAX96717_MEM_DT_SEL, dt));
}

static int max96717_set_pipe_vcs(struct max_ser *ser,
				 struct max_ser_pipe *pipe,
				 unsigned int vcs)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);
	int ret;

	ret = regmap_write(priv->regmap, MAX96717_FRONTTOP_1(index),
			   (vcs >> 0) & 0xff);
	if (ret)
		return ret;

	return regmap_write(priv->regmap, MAX96717_FRONTTOP_2(index),
			      (vcs >> 8) & 0xff);
}

static int max96717_log_status(struct max_ser *ser)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int val;
	int ret;

	if (!(priv->info->modes & BIT(MAX_SERDES_GMSL_TUNNEL_MODE)))
		return 0;

	ret = regmap_read(priv->regmap, MAX96717_EXT23, &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "tun_pkt_cnt: %u\n", val);

	return 0;
}

static int max96717_log_pipe_status(struct max_ser *ser,
				    struct max_ser_pipe *pipe)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);
	unsigned int val;
	int ret;

	ret = regmap_read(priv->regmap, MAX96717_VIDEO_TX2(index), &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "\tpclkdet: %u\n",
		 !!(val & MAX96717_VIDEO_TX2_PCLKDET));

	return 0;
}

static int max96717_log_phy_status(struct max_ser *ser,
				   struct max_ser_phy *phy)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int val;
	int ret;

	if (!priv->info->supports_pkt_cnt)
		return 0;

	ret = regmap_read(priv->regmap, MAX96717_EXT21, &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "\tphy_pkt_cnt: %u\n", val);

	ret = regmap_read(priv->regmap, MAX96717_EXT22, &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "\tcsi_pkt_cnt: %u\n", val);

	ret = regmap_read(priv->regmap, MAX96717_EXT24, &val);
	if (ret)
		return ret;

	dev_info(priv->dev, "\tphy_clk_cnt: %u\n", val);

	return 0;
}

static int max96717_init_phy(struct max_ser *ser,
			     struct max_ser_phy *phy)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int num_data_lanes = phy->mipi.num_data_lanes;
	unsigned int used_data_lanes = 0;
	unsigned int val;
	unsigned int i;
	int ret;

	if (num_data_lanes == 3 && !priv->info->supports_3_data_lanes) {
		dev_err(priv->dev, "Unsupported 3 data lane mode\n");
		return -EINVAL;
	}

	if (phy->mipi.flags & V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK &&
	    !priv->info->supports_noncontinuous_clock) {
		dev_err(priv->dev, "Unsupported non-continuous mode\n");
		return -EINVAL;
	}

	/* Configure a lane count. */
	ret = regmap_update_bits(priv->regmap, MAX96717_MIPI_RX1,
				 MAX96717_MIPI_RX1_CTRL_NUM_LANES,
				 FIELD_PREP(MAX96717_MIPI_RX1_CTRL_NUM_LANES,
					    num_data_lanes - 1));
	if (ret)
		return ret;

	/* Configure lane mapping. */
	val = 0;
	for (i = 0; i < 4; i++) {
		unsigned int map;

		if (i < num_data_lanes)
			map = phy->mipi.data_lanes[i] - 1;
		else
			map = ffz(used_data_lanes);

		val |= map << (i * 2);
		used_data_lanes |= BIT(map);
	}

	ret = regmap_update_bits(priv->regmap, MAX96717_MIPI_RX3,
				 MAX96717_MIPI_RX3_PHY2_LANE_MAP,
				 FIELD_PREP(MAX96717_MIPI_RX3_PHY2_LANE_MAP, val));
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, MAX96717_MIPI_RX2,
				 MAX96717_MIPI_RX2_PHY1_LANE_MAP,
				 FIELD_PREP(MAX96717_MIPI_RX2_PHY1_LANE_MAP, val >> 4));
	if (ret)
		return ret;

	/* Configure lane polarity. */
	for (i = 0, val = 0; i < num_data_lanes; i++)
		if (phy->mipi.lane_polarities[i + 1])
			val |= BIT(i);

	ret = regmap_update_bits(priv->regmap, MAX96717_MIPI_RX5,
				 MAX96717_MIPI_RX5_PHY2_POL_MAP,
				 FIELD_PREP(MAX96717_MIPI_RX5_PHY2_POL_MAP, val));
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, MAX96717_MIPI_RX4,
				 MAX96717_MIPI_RX4_PHY1_POL_MAP,
				 FIELD_PREP(MAX96717_MIPI_RX4_PHY1_POL_MAP, val >> 2));
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_MIPI_RX5,
				 MAX96717_MIPI_RX5_PHY2_POL_MAP_CLK,
				 phy->mipi.lane_polarities[0]);
	if (ret)
		return ret;

	if (priv->info->supports_noncontinuous_clock) {
		ret = regmap_assign_bits(priv->regmap, MAX96717_MIPI_RX0,
					 MAX96717_MIPI_RX0_NONCONTCLK_EN,
					 phy->mipi.flags &
					 V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK);
		if (ret)
			return ret;
	}

	return 0;
}

static int max96717_set_phy_active(struct max_ser *ser, struct max_ser_phy *phy,
				   bool enable)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_phy_id(priv, phy);

	return regmap_assign_bits(priv->regmap, MAX96717_FRONTTOP_0,
				  MAX96717_FRONTTOP_0_START_PORT(index), enable);
}

static int max96717_set_pipe_stream_id(struct max_ser *ser,
				       struct max_ser_pipe *pipe,
				       unsigned int stream_id)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);

	return regmap_update_bits(priv->regmap, MAX96717_TX3(index),
				  MAX96717_TX3_TX_STR_SEL,
				  FIELD_PREP(MAX96717_TX3_TX_STR_SEL, stream_id));
}

static int max96717_set_pipe_phy(struct max_ser *ser, struct max_ser_pipe *pipe,
				 struct max_ser_phy *phy)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);
	unsigned int phy_id = max96717_phy_id(priv, phy);
	int ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_FRONTTOP_0,
				 MAX96717_FRONTTOP_0_CLK_SEL_P(index),
				 phy_id == 1);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_FRONTTOP_9,
				 MAX96717_FRONTTOP_9_START_PORT(index, 0),
				 phy_id == 0);
	if (ret)
		return ret;

	return regmap_assign_bits(priv->regmap, MAX96717_FRONTTOP_9,
				  MAX96717_FRONTTOP_9_START_PORT(index, 1),
				  phy_id == 1);
}

static int max96717_set_pipe_mode(struct max_ser *ser,
				  struct max_ser_pipe *pipe,
				  struct max_ser_pipe_mode *mode)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	unsigned int index = max96717_pipe_id(priv, pipe);
	int ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_VIDEO_TX0(index),
				 MAX96717_VIDEO_TX0_AUTO_BPP, !mode->bpp);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, MAX96717_VIDEO_TX1(index),
				 MAX96717_VIDEO_TX1_BPP,
				 FIELD_PREP(MAX96717_VIDEO_TX1_BPP, mode->bpp));
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_VIDEO_TX2(index),
				 MAX96717_VIDEO_TX2_DRIFT_DET_EN, !mode->bpp);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_FRONTTOP_10,
				 MAX96717_FRONTTOP_10_BPP8DBL(index),
				 mode->dbl8);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_FRONTTOP_11,
				 MAX96717_FRONTTOP_11_BPP10DBL(index),
				 mode->dbl10);
	if (ret)
		return ret;

	ret = regmap_assign_bits(priv->regmap, MAX96717_FRONTTOP_11,
				 MAX96717_FRONTTOP_11_BPP12DBL(index),
				 mode->dbl12);
	if (ret)
		return ret;

	return regmap_update_bits(priv->regmap, MAX96717_FRONTTOP_20(index),
				  MAX96717_FRONTTOP_20_SOFT_BPP |
				  MAX96717_FRONTTOP_20_SOFT_BPP_EN,
				  FIELD_PREP(MAX96717_FRONTTOP_20_SOFT_BPP,
					     mode->soft_bpp) |
				  FIELD_PREP(MAX96717_FRONTTOP_20_SOFT_BPP_EN,
					     !!mode->soft_bpp));
}

static int max96717_set_i2c_xlate(struct max_ser *ser, unsigned int i,
				  struct max_serdes_i2c_xlate *xlate)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	int ret;

	ret = regmap_update_bits(priv->regmap, MAX96717_I2C_2(i),
				 MAX96717_I2C_2_SRC,
				 FIELD_PREP(MAX96717_I2C_2_SRC, xlate->src));
	if (ret)
		return ret;

	return regmap_update_bits(priv->regmap, MAX96717_I2C_3(i),
				  MAX96717_I2C_3_DST,
				  FIELD_PREP(MAX96717_I2C_3_DST, xlate->dst));
}

static int max96717_set_tunnel_enable(struct max_ser *ser, bool enable)
{
	struct max96717_priv *priv = ser_to_priv(ser);

	return regmap_assign_bits(priv->regmap, MAX96717_EXT11,
				  MAX96717_EXT11_TUN_MODE, enable);
}

static int max96717_set_tpg_timings(struct max96717_priv *priv,
				    const struct max_serdes_tpg_timings *tm,
				    unsigned int index)
{
	const struct reg_sequence regs[] = {
		REG_SEQUENCE_3(MAX96717_VTX2_VS_DLY_2(index), tm->vs_dly),
		REG_SEQUENCE_3(MAX96717_VTX5_VS_HIGH_2(index), tm->vs_high),
		REG_SEQUENCE_3(MAX96717_VTX8_VS_LOW_2(index), tm->vs_low),
		REG_SEQUENCE_3(MAX96717_VTX11_V2H_2(index), tm->v2h),
		REG_SEQUENCE_2(MAX96717_VTX14_HS_HIGH_1(index), tm->hs_high),
		REG_SEQUENCE_2(MAX96717_VTX16_HS_LOW_1(index), tm->hs_low),
		REG_SEQUENCE_2(MAX96717_VTX18_HS_CNT_1(index), tm->hs_cnt),
		REG_SEQUENCE_3(MAX96717_VTX20_V2D_2(index), tm->v2d),
		REG_SEQUENCE_2(MAX96717_VTX23_DE_HIGH_1(index), tm->de_high),
		REG_SEQUENCE_2(MAX96717_VTX25_DE_LOW_1(index), tm->de_low),
		REG_SEQUENCE_2(MAX96717_VTX27_DE_CNT_1(index), tm->de_cnt),
	};
	int ret;

	ret = regmap_multi_reg_write(priv->regmap, regs, ARRAY_SIZE(regs));
	if (ret)
		return ret;

	return regmap_write(priv->regmap, MAX96717_VTX0(index),
			    FIELD_PREP(MAX96717_VTX0_VTG_MODE,
				       MAX96717_VTX0_VTG_MODE_FREE_RUNNING) |
			    FIELD_PREP(MAX96717_VTX0_DE_INV, tm->de_inv) |
			    FIELD_PREP(MAX96717_VTX0_HS_INV, tm->hs_inv) |
			    FIELD_PREP(MAX96717_VTX0_VS_INV, tm->vs_inv) |
			    FIELD_PREP(MAX96717_VTX0_GEN_DE, tm->gen_de) |
			    FIELD_PREP(MAX96717_VTX0_GEN_HS, tm->gen_hs) |
			    FIELD_PREP(MAX96717_VTX0_GEN_VS, tm->gen_vs));
}

static int max96717_set_tpg_clk(struct max96717_priv *priv, u32 clock,
				unsigned int index)
{
	u8 pclk_src;

	switch (clock) {
	case 25000000:
		pclk_src = MAX96717_VTX1_PATGEN_CLK_SRC_25MHZ;
		break;
	case 75000000:
		pclk_src = MAX96717_VTX1_PATGEN_CLK_SRC_75MHZ;
		break;
	case 150000000:
		pclk_src = MAX96717_VTX1_PATGEN_CLK_SRC_150MHZ;
		break;
	case 375000000:
		pclk_src = MAX96717_VTX1_PATGEN_CLK_SRC_375MHZ;
		break;
	case 0:
		return 0;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(priv->regmap, MAX96717_VTX1(index),
				  MAX96717_VTX1_PATGEN_CLK_SRC,
				  FIELD_PREP(MAX96717_VTX1_PATGEN_CLK_SRC,
					     pclk_src));
}

static int max96717_set_tpg_mode(struct max96717_priv *priv, bool enable,
				 unsigned int index)
{
	unsigned int patgen_mode;

	switch (priv->ser.tpg_pattern) {
	case MAX_SERDES_TPG_PATTERN_GRADIENT:
		patgen_mode = MAX96717_VTX29_PATGEN_MODE_GRADIENT;
		break;
	case MAX_SERDES_TPG_PATTERN_CHECKERBOARD:
		patgen_mode = MAX96717_VTX29_PATGEN_MODE_CHECKER;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(priv->regmap, MAX96717_VTX29(index),
				  MAX96717_VTX29_PATGEN_MODE,
				  FIELD_PREP(MAX96717_VTX29_PATGEN_MODE,
					     enable ? patgen_mode
						    : MAX96717_VTX29_PATGEN_MODE_DISABLED));
}

static int max96717_set_tpg(struct max_ser *ser,
			    const struct max_serdes_tpg_entry *entry)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	/*
	 * MAX9295A supports multiple pipes, each with a pattern generator,
	 * use only the first pipe for simplicity.
	 */
	unsigned int index = max96717_pipe_id(priv, &ser->pipes[0]);
	struct max_serdes_tpg_timings timings = { 0 };
	int ret;

	ret = max_serdes_get_tpg_timings(entry, &timings);
	if (ret)
		return ret;

	ret = max96717_set_tpg_timings(priv, &timings, index);
	if (ret)
		return ret;

	ret = max96717_set_tpg_clk(priv, timings.clock, index);
	if (ret)
		return ret;

	return max96717_set_tpg_mode(priv, entry, index);
}

static const struct max_serdes_phys_config max96717_phys_configs[] = {
	{ { 4 } },
};

static int max96717_init_tpg(struct max_ser *ser)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	/*
	 * MAX9295A supports multiple pipes, each with a pattern generator,
	 * use only the first pipe for simplicity.
	 */
	unsigned int index = max96717_pipe_id(priv, &ser->pipes[0]);

	const struct reg_sequence regs[] = {
		{ MAX96717_VTX30_GRAD_INCR(index), MAX_SERDES_GRAD_INCR },
		REG_SEQUENCE_3_LE(MAX96717_VTX31_CHKR_A_L(index),
				  MAX_SERDES_CHECKER_COLOR_A),
		REG_SEQUENCE_3_LE(MAX96717_VTX34_CHKR_B_L(index),
				  MAX_SERDES_CHECKER_COLOR_B),
		{ MAX96717_VTX37_CHKR_RPT_A(index), MAX_SERDES_CHECKER_SIZE },
		{ MAX96717_VTX38_CHKR_RPT_B(index), MAX_SERDES_CHECKER_SIZE },
		{ MAX96717_VTX39_CHKR_ALT(index), MAX_SERDES_CHECKER_SIZE },
	};

	return regmap_multi_reg_write(priv->regmap, regs, ARRAY_SIZE(regs));
}

static int max96717_init(struct max_ser *ser)
{
	struct max96717_priv *priv = ser_to_priv(ser);
	int ret;

	/*
	 * Set CMU2 PFDDIV to 1.1V for correct functionality of the device,
	 * as mentioned in the datasheet, under section MANDATORY REGISTER PROGRAMMING.
	 */
	ret = regmap_update_bits(priv->regmap, MAX96717_CMU2,
				 MAX96717_CMU2_PFDDIV_RSHORT,
				 FIELD_PREP(MAX96717_CMU2_PFDDIV_RSHORT,
					    MAX96717_CMU2_PFDDIV_RSHORT_1_1V));
	if (ret)
		return ret;

	if (ser->ops->set_tunnel_enable) {
		ret = ser->ops->set_tunnel_enable(ser, false);
		if (ret)
			return ret;
	}

	return max96717_init_tpg(ser);
}

static const struct pinctrl_ops max96717_ctrl_ops = {
	.get_groups_count = max96717_ctrl_get_groups_count,
	.get_group_name = max96717_ctrl_get_group_name,
	.get_group_pins = max96717_ctrl_get_group_pins,
#if IS_BUILTIN(CONFIG_OF)
	.dt_node_to_map = pinconf_generic_dt_node_to_map_pin,
	.dt_free_map = pinconf_generic_dt_free_map,
#endif
};

static const struct pinconf_ops max96717_conf_ops = {
	.pin_config_get = max96717_conf_pin_config_get,
	.pin_config_set = max96717_conf_pin_config_set,
	.is_generic = true,
};

static const struct pinmux_ops max96717_mux_ops = {
	.get_functions_count = max96717_mux_get_functions_count,
	.get_function_name = max96717_mux_get_function_name,
	.get_function_groups = max96717_mux_get_groups,
	.set_mux = max96717_mux_set,
};

static const struct max_serdes_tpg_entry max96717_tpg_entries[] = {
	MAX_TPG_ENTRY_640X480P60_RGB888,
	MAX_TPG_ENTRY_1920X1080P30_RGB888,
	MAX_TPG_ENTRY_1920X1080P60_RGB888,
};

static const struct max_ser_ops max96717_ops = {
	.num_i2c_xlates = 2,
	.phys_configs = {
		.num_configs = ARRAY_SIZE(max96717_phys_configs),
		.configs = max96717_phys_configs,
	},
	.tpg_entries = {
		.num_entries = ARRAY_SIZE(max96717_tpg_entries),
		.entries = max96717_tpg_entries,
	},
	.tpg_mode = MAX_SERDES_GMSL_PIXEL_MODE,
	.tpg_patterns = BIT(MAX_SERDES_TPG_PATTERN_CHECKERBOARD) |
			BIT(MAX_SERDES_TPG_PATTERN_GRADIENT),
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.reg_read = max96717_reg_read,
	.reg_write = max96717_reg_write,
#endif
	.log_status = max96717_log_status,
	.log_pipe_status = max96717_log_pipe_status,
	.log_phy_status = max96717_log_phy_status,
	.init = max96717_init,
	.set_i2c_xlate = max96717_set_i2c_xlate,
	.set_tpg = max96717_set_tpg,
	.init_phy = max96717_init_phy,
	.set_phy_active = max96717_set_phy_active,
	.set_pipe_enable = max96717_set_pipe_enable,
	.set_pipe_dt = max96717_set_pipe_dt,
	.set_pipe_dt_en = max96717_set_pipe_dt_en,
	.set_pipe_vcs = max96717_set_pipe_vcs,
	.set_pipe_mode = max96717_set_pipe_mode,
	.set_pipe_stream_id = max96717_set_pipe_stream_id,
	.set_pipe_phy = max96717_set_pipe_phy,
};

struct max96717_pll_predef_freq {
	unsigned long freq;
	bool is_rclk;
	bool is_alt;
	u8 val;
	u8 rclksel;
};

static const struct max96717_pll_predef_freq max96717_predef_freqs[] = {
	{  6250000, true,  false, 0, 2 },
	{ 12500000, true,  false, 0, 1 },
	{ 13500000, false, true,  0, 3 },
	{ 19200000, false, false, 0, 3 },
	{ 24000000, false, true,  1, 3 },
	{ 25000000, true,  false, 0, 0 },
	{ 27000000, false, false, 1, 3 },
	{ 37125000, false, false, 2, 3 },
	{ 74250000, false, false, 3, 3 },
};

static unsigned long
max96717_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct max96717_priv *priv = clk_hw_to_priv(hw);

	return max96717_predef_freqs[priv->pll_predef_index].freq;
}

static unsigned int max96717_clk_find_best_index(struct max96717_priv *priv,
						 unsigned long rate)
{
	unsigned int i, idx = 0;
	unsigned long diff_new, diff_old = U32_MAX;

	for (i = 0; i < ARRAY_SIZE(max96717_predef_freqs); i++) {
		diff_new = abs_diff(rate, max96717_predef_freqs[i].freq);
		if (diff_new < diff_old) {
			diff_old = diff_new;
			idx = i;
		}
	}

	return idx;
}

static int max96717_clk_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct max96717_priv *priv = clk_hw_to_priv(hw);
	struct device *dev = &priv->client->dev;
	unsigned int idx;

	idx = max96717_clk_find_best_index(priv, req->rate);

	if (req->rate != max96717_predef_freqs[idx].freq) {
		dev_dbg(dev, "Request CLK freq:%lu, found CLK freq:%lu\n",
			req->rate, max96717_predef_freqs[idx].freq);
	}

	req->rate = max96717_predef_freqs[idx].freq;

	return 0;
}

static int max96717_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	const struct max96717_pll_predef_freq *predef_freq;
	struct max96717_priv *priv = clk_hw_to_priv(hw);
	unsigned int val, idx;
	int ret = 0;

	idx = max96717_clk_find_best_index(priv, rate);
	predef_freq = &max96717_predef_freqs[idx];

	ret = regmap_update_bits(priv->regmap, MAX96717_REG3,
				 MAX96717_REG3_RCLKSEL,
				 FIELD_PREP(MAX96717_REG3_RCLKSEL,
					    predef_freq->rclksel));
	if (ret)
		return ret;

	val = FIELD_PREP(MAX96717_REF_VTG0_REFGEN_PREDEF_FREQ,
			 predef_freq->val);

	if (predef_freq->is_alt)
		val |= MAX96717_REF_VTG0_REFGEN_PREDEF_FREQ_ALT;
	if (!predef_freq->is_rclk)
		val |= MAX96717_REF_VTG0_REFGEN_EN;

	val |= MAX96717_REF_VTG0_REFGEN_RST;
	val |= MAX96717_REF_VTG0_REFGEN_PREDEF_EN;

	ret = regmap_write(priv->regmap, MAX96717_REF_VTG0, val);
	if (ret)
		return ret;

	ret = regmap_clear_bits(priv->regmap, MAX96717_REF_VTG0,
				MAX96717_REF_VTG0_REFGEN_RST);
	if (ret)
		return ret;

	priv->pll_predef_index = idx;

	return 0;
}

static int max96717_clk_prepare(struct clk_hw *hw)
{
	struct max96717_priv *priv = clk_hw_to_priv(hw);

	return regmap_set_bits(priv->regmap, MAX96717_REG6, MAX96717_REG6_RCLKEN);
}

static void max96717_clk_unprepare(struct clk_hw *hw)
{
	struct max96717_priv *priv = clk_hw_to_priv(hw);

	regmap_clear_bits(priv->regmap, MAX96717_REG6, MAX96717_REG6_RCLKEN);
}

static const struct clk_ops max96717_clk_ops = {
	.prepare     = max96717_clk_prepare,
	.unprepare   = max96717_clk_unprepare,
	.set_rate    = max96717_clk_set_rate,
	.recalc_rate = max96717_clk_recalc_rate,
	.determine_rate = max96717_clk_determine_rate,
};

static int max96717_register_clkout(struct max96717_priv *priv)
{
	struct device *dev = &priv->client->dev;
	struct clk_init_data init = { .ops = &max96717_clk_ops };
	int ret;

	/*
	 * Only fall back to the default pin if pinctrl has not already routed
	 * RCLKOUT, to avoid overriding the mux and slew rate chosen in DT.
	 */
	if (!priv->rclkout_muxed) {
		ret = max96717_mux_set_rclkout(priv, MAX96717_RCLK_MFP);
		if (ret)
			return ret;
	}

	init.name = kasprintf(GFP_KERNEL, "max96717.%s.clk_out", dev_name(dev));
	if (!init.name)
		return -ENOMEM;

	priv->clk_hw.init = &init;

	ret = max96717_clk_set_rate(&priv->clk_hw,
				    MAX96717_DEFAULT_CLKOUT_RATE, 0);
	if (ret)
		goto free_init_name;

	ret = devm_clk_hw_register(dev, &priv->clk_hw);
	kfree(init.name);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot register clock HW\n");

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &priv->clk_hw);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Cannot add OF clock provider\n");

	return 0;

free_init_name:
	kfree(init.name);
	return ret;
}

static int max96717_gpiochip_probe(struct max96717_priv *priv)
{
	struct device *dev = priv->dev;
	int ret;

	priv->pctldesc = (struct pinctrl_desc) {
		.owner = THIS_MODULE,
		.name = MAX96717_PINCTRL_NAME,
		.pins = max96717_pins,
		.npins = ARRAY_SIZE(max96717_pins),
		.pctlops = &max96717_ctrl_ops,
		.confops = &max96717_conf_ops,
		.pmxops = &max96717_mux_ops,
		.custom_params = max96717_cfg_params,
		.num_custom_params = ARRAY_SIZE(max96717_cfg_params),
	};

	ret = devm_pinctrl_register_and_init(dev, &priv->pctldesc, priv, &priv->pctldev);
	if (ret)
		return ret;

	ret = pinctrl_enable(priv->pctldev);
	if (ret)
		return ret;

	priv->gc = (struct gpio_chip) {
		.owner = THIS_MODULE,
		.label = MAX96717_GPIOCHIP_NAME,
		.base = -1,
		.ngpio = MAX96717_GPIO_NUM,
		.parent = dev,
		.can_sleep = true,
		.request = gpiochip_generic_request,
		.free = gpiochip_generic_free,
		.set_config = gpiochip_generic_config,
		.get_direction = max96717_gpio_get_direction,
		.direction_input = max96717_gpio_direction_input,
		.direction_output = max96717_gpio_direction_output,
		.get = max96717_gpio_get,
		.set = max96717_gpio_set,
	};

	return devm_gpiochip_add_data(dev, &priv->gc, priv);
}

static int max96717_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max96717_priv *priv;
	struct max_ser_ops *ops;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ops = devm_kzalloc(dev, sizeof(*ops), GFP_KERNEL);
	if (!ops)
		return -ENOMEM;

	priv->info = device_get_match_data(dev);
	if (!priv->info) {
		dev_err(dev, "Failed to get match data\n");
		return -ENODEV;
	}

	priv->dev = dev;
	priv->client = client;
	i2c_set_clientdata(client, priv);

	priv->regmap = devm_regmap_init_i2c(client, &max96717_i2c_regmap);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	*ops = max96717_ops;

	if (priv->info->modes & BIT(MAX_SERDES_GMSL_TUNNEL_MODE))
		ops->set_tunnel_enable = max96717_set_tunnel_enable;

	ops->modes = priv->info->modes;
	ops->num_pipes = priv->info->num_pipes;
	ops->num_dts_per_pipe = priv->info->num_dts_per_pipe;
	ops->num_phys = priv->info->num_phys;
	priv->ser.ops = ops;

	ret = max96717_wait_for_device(priv);
	if (ret)
		return ret;

	ret = max96717_gpiochip_probe(priv);
	if (ret)
		return ret;

	ret = max96717_register_clkout(priv);
	if (ret)
		return ret;

	return max_ser_probe(client, &priv->ser);
}

static void max96717_remove(struct i2c_client *client)
{
	struct max96717_priv *priv = i2c_get_clientdata(client);

	max_ser_remove(&priv->ser);
}

static const struct max96717_chip_info max9295a_info = {
	.modes = BIT(MAX_SERDES_GMSL_PIXEL_MODE),
	.num_pipes = 4,
	.num_dts_per_pipe = 2,
	.pipe_hw_ids = { 0, 1, 2, 3 },
	.num_phys = 1,
	.phy_hw_ids = { 1 },
};

static const struct max96717_chip_info max96717_info = {
	.modes = BIT(MAX_SERDES_GMSL_PIXEL_MODE) |
		 BIT(MAX_SERDES_GMSL_TUNNEL_MODE),
	.supports_3_data_lanes = true,
	.supports_pkt_cnt = true,
	.supports_noncontinuous_clock = true,
	.num_pipes = 1,
	.num_dts_per_pipe = 4,
	.pipe_hw_ids = { 2 },
	.num_phys = 1,
	.phy_hw_ids = { 1 },
};

static const struct of_device_id max96717_of_ids[] = {
	{ .compatible = "maxim,max9295a", .data = &max9295a_info },
	{ .compatible = "maxim,max96717", .data = &max96717_info },
	{ .compatible = "maxim,max96717f", .data = &max96717_info },
	{ .compatible = "maxim,max96793", .data = &max96717_info },
	{ }
};
MODULE_DEVICE_TABLE(of, max96717_of_ids);

static struct i2c_driver max96717_i2c_driver = {
	.driver	= {
		.name = MAX96717_NAME,
		.of_match_table = max96717_of_ids,
	},
	.probe = max96717_probe,
	.remove = max96717_remove,
};

module_i2c_driver(max96717_i2c_driver);

MODULE_IMPORT_NS("MAX_SERDES");
MODULE_DESCRIPTION("MAX96717 GMSL2 Serializer Driver");
MODULE_AUTHOR("Cosmin Tanislav <cosmin.tanislav@analog.com>");
MODULE_LICENSE("GPL");
