// SPDX-License-Identifier: GPL-2.0+
/*
 * Azoteq IQS269A Capacitive Touch Controller
 *
 * Copyright (C) 2020 Jeff LaBundy <jeff@labundy.com>
 *
 * This driver registers up to 3 input devices: one representing capacitive or
 * inductive keys as well as Hall-effect switches, and one for each of the two
 * axial sliders presented by the device.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define IQS269_VER_INFO				0x00
#define IQS269_VER_INFO_PROD_NUM		0x4F

#define IQS269_SYS_FLAGS			0x02
#define IQS269_SYS_FLAGS_SHOW_RESET		BIT(15)
#define IQS269_SYS_FLAGS_PWR_MODE_MASK		GENMASK(12, 11)
#define IQS269_SYS_FLAGS_PWR_MODE_SHIFT		11
#define IQS269_SYS_FLAGS_IN_ATI			BIT(10)

#define IQS269_CHx_COUNTS			0x08

#define IQS269_SLIDER_X				0x30

#define IQS269_CAL_DATA_A			0x35
#define IQS269_CAL_DATA_A_HALL_BIN_L_MASK	GENMASK(15, 12)
#define IQS269_CAL_DATA_A_HALL_BIN_L_SHIFT	12
#define IQS269_CAL_DATA_A_HALL_BIN_R_MASK	GENMASK(11, 8)
#define IQS269_CAL_DATA_A_HALL_BIN_R_SHIFT	8

#define IQS269_SYS_SETTINGS			0x80
#define IQS269_SYS_SETTINGS_CLK_DIV		BIT(15)
#define IQS269_SYS_SETTINGS_ULP_AUTO		BIT(14)
#define IQS269_SYS_SETTINGS_DIS_AUTO		BIT(13)
#define IQS269_SYS_SETTINGS_PWR_MODE_MASK	GENMASK(12, 11)
#define IQS269_SYS_SETTINGS_PWR_MODE_SHIFT	11
#define IQS269_SYS_SETTINGS_PWR_MODE_MAX	3
#define IQS269_SYS_SETTINGS_ULP_UPDATE_MASK	GENMASK(10, 8)
#define IQS269_SYS_SETTINGS_ULP_UPDATE_SHIFT	8
#define IQS269_SYS_SETTINGS_ULP_UPDATE_MAX	7
#define IQS269_SYS_SETTINGS_RESEED_OFFSET	BIT(6)
#define IQS269_SYS_SETTINGS_EVENT_MODE		BIT(5)
#define IQS269_SYS_SETTINGS_EVENT_MODE_LP	BIT(4)
#define IQS269_SYS_SETTINGS_REDO_ATI		BIT(2)
#define IQS269_SYS_SETTINGS_ACK_RESET		BIT(0)

#define IQS269_FILT_STR_LP_LTA_MASK		GENMASK(7, 6)
#define IQS269_FILT_STR_LP_LTA_SHIFT		6
#define IQS269_FILT_STR_LP_CNT_MASK		GENMASK(5, 4)
#define IQS269_FILT_STR_LP_CNT_SHIFT		4
#define IQS269_FILT_STR_NP_LTA_MASK		GENMASK(3, 2)
#define IQS269_FILT_STR_NP_LTA_SHIFT		2
#define IQS269_FILT_STR_NP_CNT_MASK		GENMASK(1, 0)
#define IQS269_FILT_STR_MAX			3

#define IQS269_EVENT_MASK_SYS			BIT(6)
#define IQS269_EVENT_MASK_DEEP			BIT(2)
#define IQS269_EVENT_MASK_TOUCH			BIT(1)
#define IQS269_EVENT_MASK_PROX			BIT(0)

#define IQS269_RATE_NP_MS_MAX			255
#define IQS269_RATE_LP_MS_MAX			255
#define IQS269_RATE_ULP_MS_MAX			4080
#define IQS269_TIMEOUT_PWR_MS_MAX		130560
#define IQS269_TIMEOUT_LTA_MS_MAX		130560

#define IQS269_MISC_A_ATI_BAND_DISABLE		BIT(15)
#define IQS269_MISC_A_ATI_LP_ONLY		BIT(14)
#define IQS269_MISC_A_ATI_BAND_TIGHTEN		BIT(13)
#define IQS269_MISC_A_FILT_DISABLE		BIT(12)
#define IQS269_MISC_A_GPIO3_SELECT_MASK		GENMASK(10, 8)
#define IQS269_MISC_A_GPIO3_SELECT_SHIFT	8
#define IQS269_MISC_A_DUAL_DIR			BIT(6)
#define IQS269_MISC_A_TX_FREQ_MASK		GENMASK(5, 4)
#define IQS269_MISC_A_TX_FREQ_SHIFT		4
#define IQS269_MISC_A_TX_FREQ_MAX		3
#define IQS269_MISC_A_GLOBAL_CAP_SIZE		BIT(0)

#define IQS269_MISC_B_RESEED_UI_SEL_MASK	GENMASK(7, 6)
#define IQS269_MISC_B_RESEED_UI_SEL_SHIFT	6
#define IQS269_MISC_B_RESEED_UI_SEL_MAX		3
#define IQS269_MISC_B_TRACKING_UI_ENABLE	BIT(4)
#define IQS269_MISC_B_FILT_STR_SLIDER		GENMASK(1, 0)

#define IQS269_CHx_SETTINGS			0x8C

#define IQS269_CHx_ENG_A_MEAS_CAP_SIZE		BIT(15)
#define IQS269_CHx_ENG_A_RX_GND_INACTIVE	BIT(13)
#define IQS269_CHx_ENG_A_LOCAL_CAP_SIZE		BIT(12)
#define IQS269_CHx_ENG_A_ATI_MODE_MASK		GENMASK(9, 8)
#define IQS269_CHx_ENG_A_ATI_MODE_SHIFT		8
#define IQS269_CHx_ENG_A_ATI_MODE_MAX		3
#define IQS269_CHx_ENG_A_INV_LOGIC		BIT(7)
#define IQS269_CHx_ENG_A_PROJ_BIAS_MASK		GENMASK(6, 5)
#define IQS269_CHx_ENG_A_PROJ_BIAS_SHIFT	5
#define IQS269_CHx_ENG_A_PROJ_BIAS_MAX		3
#define IQS269_CHx_ENG_A_SENSE_MODE_MASK	GENMASK(3, 0)
#define IQS269_CHx_ENG_A_SENSE_MODE_MAX		15

#define IQS269_CHx_ENG_B_LOCAL_CAP_ENABLE	BIT(13)
#define IQS269_CHx_ENG_B_SENSE_FREQ_MASK	GENMASK(10, 9)
#define IQS269_CHx_ENG_B_SENSE_FREQ_SHIFT	9
#define IQS269_CHx_ENG_B_SENSE_FREQ_MAX		3
#define IQS269_CHx_ENG_B_STATIC_ENABLE		BIT(8)
#define IQS269_CHx_ENG_B_ATI_BASE_MASK		GENMASK(7, 6)
#define IQS269_CHx_ENG_B_ATI_BASE_75		0x00
#define IQS269_CHx_ENG_B_ATI_BASE_100		0x40
#define IQS269_CHx_ENG_B_ATI_BASE_150		0x80
#define IQS269_CHx_ENG_B_ATI_BASE_200		0xC0
#define IQS269_CHx_ENG_B_ATI_TARGET_MASK	GENMASK(5, 0)
#define IQS269_CHx_ENG_B_ATI_TARGET_MAX		2016

#define IQS269_CHx_WEIGHT_MAX			255
#define IQS269_CHx_THRESH_MAX			255
#define IQS269_CHx_HYST_DEEP_MASK		GENMASK(7, 4)
#define IQS269_CHx_HYST_DEEP_SHIFT		4
#define IQS269_CHx_HYST_TOUCH_MASK		GENMASK(3, 0)
#define IQS269_CHx_HYST_MAX			15

#define IQS269_CHx_HALL_INACTIVE		6
#define IQS269_CHx_HALL_ACTIVE			7

#define IQS269_HALL_PAD_R			BIT(0)
#define IQS269_HALL_PAD_L			BIT(1)
#define IQS269_HALL_PAD_INV			BIT(6)

#define IQS269_HALL_UI				0xF5
#define IQS269_HALL_UI_ENABLE			BIT(15)

#define IQS269_MAX_REG				0xFF

#define IQS269_NUM_CH				8
#define IQS269_NUM_SL				2

#define IQS269_ATI_POLL_SLEEP_US		(iqs269->delay_mult * 10000)
#define IQS269_ATI_POLL_TIMEOUT_US		(iqs269->delay_mult * 500000)
#define IQS269_ATI_STABLE_DELAY_MS		(iqs269->delay_mult * 150)

#define IQS269_PWR_MODE_POLL_SLEEP_US		IQS269_ATI_POLL_SLEEP_US
#define IQS269_PWR_MODE_POLL_TIMEOUT_US		IQS269_ATI_POLL_TIMEOUT_US

#define iqs269_irq_wait()			usleep_range(100, 150)

enum iqs269_local_cap_size {
	IQS269_LOCAL_CAP_SIZE_0,
	IQS269_LOCAL_CAP_SIZE_GLOBAL_ONLY,
	IQS269_LOCAL_CAP_SIZE_GLOBAL_0pF5,
};

enum iqs269_st_offs {
	IQS269_ST_OFFS_PROX,
	IQS269_ST_OFFS_DIR,
	IQS269_ST_OFFS_TOUCH,
	IQS269_ST_OFFS_DEEP,
};

enum iqs269_th_offs {
	IQS269_TH_OFFS_PROX,
	IQS269_TH_OFFS_TOUCH,
	IQS269_TH_OFFS_DEEP,
};

enum iqs269_event_id {
	IQS269_EVENT_PROX_DN,
	IQS269_EVENT_PROX_UP,
	IQS269_EVENT_TOUCH_DN,
	IQS269_EVENT_TOUCH_UP,
	IQS269_EVENT_DEEP_DN,
	IQS269_EVENT_DEEP_UP,
};

struct iqs269_switch_desc {
	unsigned int code;
	bool enabled;
};

struct iqs269_event_desc {
	const char *name;
	enum iqs269_st_offs st_offs;
	enum iqs269_th_offs th_offs;
	bool dir_up;
	u8 mask;
};

static const struct iqs269_event_desc iqs269_events[] = {
	[IQS269_EVENT_PROX_DN] = {
		.name = "event-prox",
		.st_offs = IQS269_ST_OFFS_PROX,
		.th_offs = IQS269_TH_OFFS_PROX,
		.mask = IQS269_EVENT_MASK_PROX,
	},
	[IQS269_EVENT_PROX_UP] = {
		.name = "event-prox-alt",
		.st_offs = IQS269_ST_OFFS_PROX,
		.th_offs = IQS269_TH_OFFS_PROX,
		.dir_up = true,
		.mask = IQS269_EVENT_MASK_PROX,
	},
	[IQS269_EVENT_TOUCH_DN] = {
		.name = "event-touch",
		.st_offs = IQS269_ST_OFFS_TOUCH,
		.th_offs = IQS269_TH_OFFS_TOUCH,
		.mask = IQS269_EVENT_MASK_TOUCH,
	},
	[IQS269_EVENT_TOUCH_UP] = {
		.name = "event-touch-alt",
		.st_offs = IQS269_ST_OFFS_TOUCH,
		.th_offs = IQS269_TH_OFFS_TOUCH,
		.dir_up = true,
		.mask = IQS269_EVENT_MASK_TOUCH,
	},
	[IQS269_EVENT_DEEP_DN] = {
		.name = "event-deep",
		.st_offs = IQS269_ST_OFFS_DEEP,
		.th_offs = IQS269_TH_OFFS_DEEP,
		.mask = IQS269_EVENT_MASK_DEEP,
	},
	[IQS269_EVENT_DEEP_UP] = {
		.name = "event-deep-alt",
		.st_offs = IQS269_ST_OFFS_DEEP,
		.th_offs = IQS269_TH_OFFS_DEEP,
		.dir_up = true,
		.mask = IQS269_EVENT_MASK_DEEP,
	},
};

struct iqs269_ver_info {
	u8 prod_num;
	u8 sw_num;
	u8 hw_num;
	u8 padding;
} __packed;

struct iqs269_sys_reg {
	__be16 general;
	u8 active;
	u8 filter;
	u8 reseed;
	u8 event_mask;
	u8 rate_np;
	u8 rate_lp;
	u8 rate_ulp;
	u8 timeout_pwr;
	u8 timeout_rdy;
	u8 timeout_lta;
	__be16 misc_a;
	__be16 misc_b;
	u8 blocking;
	u8 padding;
	u8 slider_select[IQS269_NUM_SL];
	u8 timeout_tap;
	u8 timeout_swipe;
	u8 thresh_swipe;
	u8 redo_ati;
} __packed;

struct iqs269_ch_reg {
	u8 rx_enable;
	u8 tx_enable;
	__be16 engine_a;
	__be16 engine_b;
	__be16 ati_comp;
	u8 thresh[3];
	u8 hyst;
	u8 assoc_select;
	u8 assoc_weight;
} __packed;

struct iqs269_flags {
	__be16 system;
	u8 gesture;
	u8 padding;
	u8 states[4];
} __packed;

struct iqs269_private {
	struct i2c_client *client;
	struct regmap *regmap;
	struct mutex lock;
	struct iqs269_switch_desc switches[ARRAY_SIZE(iqs269_events)];
	struct iqs269_ch_reg ch_reg[IQS269_NUM_CH];
	struct iqs269_sys_reg sys_reg;
	struct input_dev *keypad;
	struct input_dev *slider[IQS269_NUM_SL];
	unsigned int keycode[ARRAY_SIZE(iqs269_events) * IQS269_NUM_CH];
	unsigned int suspend_mode;
	unsigned int delay_mult;
	unsigned int ch_num;
	bool hall_enable;
	bool ati_current;
};

static int iqs269_ati_mode_set(struct iqs269_private *iqs269,
			       unsigned int ch_num, unsigned int mode)
{
	u16 engine_a;

	if (ch_num >= IQS269_NUM_CH)
		return -EINVAL;

	if (mode > IQS269_CHx_ENG_A_ATI_MODE_MAX)
		return -EINVAL;

	mutex_lock(&iqs269->lock);

	engine_a = be16_to_cpu(iqs269->ch_reg[ch_num].engine_a);

	engine_a &= ~IQS269_CHx_ENG_A_ATI_MODE_MASK;
	engine_a |= (mode << IQS269_CHx_ENG_A_ATI_MODE_SHIFT);

	iqs269->ch_reg[ch_num].engine_a = cpu_to_be16(engine_a);
	iqs269->ati_current = false;

	mutex_unlock(&iqs269->lock);

	return 0;
}

static int iqs269_ati_mode_get(struct iqs269_private *iqs269,
			       unsigned int ch_num, unsigned int *mode)
{
	u16 engine_a;

	if (ch_num >= IQS269_NUM_CH)
		return -EINVAL;

	mutex_lock(&iqs269->lock);
	engine_a = be16_to_cpu(iqs269->ch_reg[ch_num].engine_a);
	mutex_unlock(&iqs269->lock);

	engine_a &= IQS269_CHx_ENG_A_ATI_MODE_MASK;
	*mode = (engine_a >> IQS269_CHx_ENG_A_ATI_MODE_SHIFT);

	return 0;
}

static int iqs269_ati_base_set(struct iqs269_private *iqs269,
			       unsigned int ch_num, unsigned int base)
{
	u16 engine_b;

	if (ch_num >= IQS269_NUM_CH)
		return -EINVAL;

	switch (base) {
	case 75:
		base = IQS269_CHx_ENG_B_ATI_BASE_75;
		break;

	case 100:
		base = IQS269_CHx_ENG_B_ATI_BASE_100;
		break;

	case 150:
		base = IQS269_CHx_ENG_B_ATI_BASE_150;
		break;

	case 200:
		base = IQS269_CHx_ENG_B_ATI_BASE_200;
		break;

	default:
		return -EINVAL;
	}

	mutex_lock(&iqs269->lock);

	engine_b = be16_to_cpu(iqs269->ch_reg[ch_num].engine_b);

	engine_b &= ~IQS269_CHx_ENG_B_ATI_BASE_MASK;
	engine_b |= base;

	iqs269->ch_reg[ch_num].engine_b = cpu_to_be16(engine_b);
	iqs269->ati_current = false;

	mutex_unlock(&iqs269->lock);

	return 0;
}

static int iqs269_ati_base_get(struct iqs269_private *iqs269,
			       unsigned int ch_num, unsigned int *base)
{
	u16 engine_b;

	if (ch_num >= IQS269_NUM_CH)
		return -EINVAL;

	mutex_lock(&iqs269->lock);
	engine_b = be16_to_cpu(iqs269->ch_reg[ch_num].engine_b);
	mutex_unlock(&iqs269->lock);

	switch (engine_b & IQS269_CHx_ENG_B_ATI_BASE_MASK) {
	case IQS269_CHx_ENG_B_ATI_BASE_75:
		*base = 75;
		return 0;

	case IQS269_CHx_ENG_B_ATI_BASE_100:
		*base = 100;
		return 0;

	case IQS269_CHx_ENG_B_ATI_BASE_150:
		*base = 150;
		return 0;

	case IQS269_CHx_ENG_B_ATI_BASE_200:
		*base = 200;
		return 0;

	default:
		return -EINVAL;
	}
}

static int iqs269_ati_target_set(struct iqs269_private *iqs269,
				 unsigned int ch_num, unsigned int target)
{
	u16 engine_b;

	if (ch_num >= IQS269_NUM_CH)
		return -EINVAL;

	if (target > IQS269_CHx_ENG_B_ATI_TARGET_MAX)
		return -EINVAL;

	mutex_lock(&iqs269->lock);

	engine_b = be16_to_cpu(iqs269->ch_reg[ch_num].engine_b);

	engine_b &= ~IQS269_CHx_ENG_B_ATI_TARGET_MASK;
	engine_b |= target / 32;

	iqs269->ch_reg[ch_num].engine_b = cpu_to_be16(engine_b);
	iqs269->ati_current = false;

	mutex_unlock(&iqs269->lock);

	return 0;
}

static int iqs269_ati_target_get(struct iqs269_private *iqs269,
				 unsigned int ch_num, unsigned int *target)
{
	u16 engine_b;

	if (ch_num >= IQS269_NUM_CH)
		return -EINVAL;

	mutex_lock(&iqs269->lock);
	engine_b = be16_to_cpu(iqs269->ch_reg[ch_num].engine_b);
	mutex_unlock(&iqs269->lock);

	*target = (engine_b & IQS269_CHx_ENG_B_ATI_TARGET_MASK) * 32;

	return 0;
}

static int iqs269_parse_mask(const struct fwnode_handle *fwnode,
			     const char *propname, u8 *mask)
{
	unsigned int val[IQS269_NUM_CH];
	int count, error, i;

	count = fwnode_property_count_u32(fwnode, propname);
	if (count < 0)
		return 0;

	if (count > IQS269_NUM_CH)
		return -EINVAL;

	error = fwnode_property_read_u32_array(fwnode, propname, val, count);
	if (error)
		return error;

	*mask = 0;

	for (i = 0; i < count; i++) {
		if (val[i] >= IQS269_NUM_CH)
			return -EINVAL;

		*mask |= BIT(val[i]);
	}

	return 0;
}

static int iqs269_parse_chan(struct iqs269_private *iqs269,
			     const struct fwnode_handle *ch_node)
{
	struct i2c_client *client = iqs269->client;
	struct fwnode_handle *ev_node;
	struct iqs269_ch_reg *ch_reg;
	u16 engine_a, engine_b;
	unsigned int reg, val;
	int error, i;

	error = fwnode_property_read_u32(ch_node, "reg", &reg);
	if (error) {
		dev_err(&client->dev, "Failed to read channel number: %d\n",
			error);
		return error;
	} else if (reg >= IQS269_NUM_CH) {
		dev_err(&client->dev, "Invalid channel number: %u\n", reg);
		return -EINVAL;
	}

	iqs269->sys_reg.active |= BIT(reg);
	if (!fwnode_property_present(ch_node, "azoteq,reseed-disable"))
		iqs269->sys_reg.reseed |= BIT(reg);

	if (fwnode_property_present(ch_node, "azoteq,blocking-enable"))
		iqs269->sys_reg.blocking |= BIT(reg);

	if (fwnode_property_present(ch_node, "azoteq,slider0-select"))
		iqs269->sys_reg.slider_select[0] |= BIT(reg);

	if (fwnode_property_present(ch_node, "azoteq,slider1-select"))
		iqs269->sys_reg.slider_select[1] |= BIT(reg);

	ch_reg = &iqs269->ch_reg[reg];

	error = regmap_raw_read(iqs269->regmap,
				IQS269_CHx_SETTINGS + reg * sizeof(*ch_reg) / 2,
				ch_reg, sizeof(*ch_reg));
	if (error)
		return error;

	error = iqs269_parse_mask(ch_node, "azoteq,rx-enable",
				  &ch_reg->rx_enable);
	if (error) {
		dev_err(&client->dev, "Invalid channel %u RX enable mask: %d\n",
			reg, error);
		return error;
	}

	error = iqs269_parse_mask(ch_node, "azoteq,tx-enable",
				  &ch_reg->tx_enable);
	if (error) {
		dev_err(&client->dev, "Invalid channel %u TX enable mask: %d\n",
			reg, error);
		return error;
	}

	engine_a = be16_to_cpu(ch_reg->engine_a);
	engine_b = be16_to_cpu(ch_reg->engine_b);

	engine_a |= IQS269_CHx_ENG_A_MEAS_CAP_SIZE;
	if (fwnode_property_present(ch_node, "azoteq,meas-cap-decrease"))
		engine_a &= ~IQS269_CHx_ENG_A_MEAS_CAP_SIZE;

	engine_a |= IQS269_CHx_ENG_A_RX_GND_INACTIVE;
	if (fwnode_property_present(ch_node, "azoteq,rx-float-inactive"))
		engine_a &= ~IQS269_CHx_ENG_A_RX_GND_INACTIVE;

	engine_a &= ~IQS269_CHx_ENG_A_LOCAL_CAP_SIZE;
	engine_b &= ~IQS269_CHx_ENG_B_LOCAL_CAP_ENABLE;
	if (!fwnode_property_read_u32(ch_node, "azoteq,local-cap-size", &val)) {
		switch (val) {
		case IQS269_LOCAL_CAP_SIZE_0:
			break;

		case IQS269_LOCAL_CAP_SIZE_GLOBAL_0pF5:
			engine_a |= IQS269_CHx_ENG_A_LOCAL_CAP_SIZE;

			/* fall through */

		case IQS269_LOCAL_CAP_SIZE_GLOBAL_ONLY:
			engine_b |= IQS269_CHx_ENG_B_LOCAL_CAP_ENABLE;
			break;

		default:
			dev_err(&client->dev,
				"Invalid channel %u local cap. size: %u\n", reg,
				val);
			return -EINVAL;
		}
	}

	engine_a &= ~IQS269_CHx_ENG_A_INV_LOGIC;
	if (fwnode_property_present(ch_node, "azoteq,invert-enable"))
		engine_a |= IQS269_CHx_ENG_A_INV_LOGIC;

	if (!fwnode_property_read_u32(ch_node, "azoteq,proj-bias", &val)) {
		if (val > IQS269_CHx_ENG_A_PROJ_BIAS_MAX) {
			dev_err(&client->dev,
				"Invalid channel %u bias current: %u\n", reg,
				val);
			return -EINVAL;
		}

		engine_a &= ~IQS269_CHx_ENG_A_PROJ_BIAS_MASK;
		engine_a |= (val << IQS269_CHx_ENG_A_PROJ_BIAS_SHIFT);
	}

	if (!fwnode_property_read_u32(ch_node, "azoteq,sense-mode", &val)) {
		if (val > IQS269_CHx_ENG_A_SENSE_MODE_MAX) {
			dev_err(&client->dev,
				"Invalid channel %u sensing mode: %u\n", reg,
				val);
			return -EINVAL;
		}

		engine_a &= ~IQS269_CHx_ENG_A_SENSE_MODE_MASK;
		engine_a |= val;
	}

	if (!fwnode_property_read_u32(ch_node, "azoteq,sense-freq", &val)) {
		if (val > IQS269_CHx_ENG_B_SENSE_FREQ_MAX) {
			dev_err(&client->dev,
				"Invalid channel %u sensing frequency: %u\n",
				reg, val);
			return -EINVAL;
		}

		engine_b &= ~IQS269_CHx_ENG_B_SENSE_FREQ_MASK;
		engine_b |= (val << IQS269_CHx_ENG_B_SENSE_FREQ_SHIFT);
	}

	engine_b &= ~IQS269_CHx_ENG_B_STATIC_ENABLE;
	if (fwnode_property_present(ch_node, "azoteq,static-enable"))
		engine_b |= IQS269_CHx_ENG_B_STATIC_ENABLE;

	ch_reg->engine_a = cpu_to_be16(engine_a);
	ch_reg->engine_b = cpu_to_be16(engine_b);

	if (!fwnode_property_read_u32(ch_node, "azoteq,ati-mode", &val)) {
		error = iqs269_ati_mode_set(iqs269, reg, val);
		if (error) {
			dev_err(&client->dev,
				"Invalid channel %u ATI mode: %u\n", reg, val);
			return error;
		}
	}

	if (!fwnode_property_read_u32(ch_node, "azoteq,ati-base", &val)) {
		error = iqs269_ati_base_set(iqs269, reg, val);
		if (error) {
			dev_err(&client->dev,
				"Invalid channel %u ATI base: %u\n", reg, val);
			return error;
		}
	}

	if (!fwnode_property_read_u32(ch_node, "azoteq,ati-target", &val)) {
		error = iqs269_ati_target_set(iqs269, reg, val);
		if (error) {
			dev_err(&client->dev,
				"Invalid channel %u ATI target: %u\n", reg,
				val);
			return error;
		}
	}

	error = iqs269_parse_mask(ch_node, "azoteq,assoc-select",
				  &ch_reg->assoc_select);
	if (error) {
		dev_err(&client->dev, "Invalid channel %u association: %d\n",
			reg, error);
		return error;
	}

	if (!fwnode_property_read_u32(ch_node, "azoteq,assoc-weight", &val)) {
		if (val > IQS269_CHx_WEIGHT_MAX) {
			dev_err(&client->dev,
				"Invalid channel %u associated weight: %u\n",
				reg, val);
			return -EINVAL;
		}

		ch_reg->assoc_weight = val;
	}

	for (i = 0; i < ARRAY_SIZE(iqs269_events); i++) {
		ev_node = fwnode_get_named_child_node(ch_node,
						      iqs269_events[i].name);
		if (!ev_node)
			continue;

		if (!fwnode_property_read_u32(ev_node, "azoteq,thresh", &val)) {
			if (val > IQS269_CHx_THRESH_MAX) {
				dev_err(&client->dev,
					"Invalid channel %u threshold: %u\n",
					reg, val);
				return -EINVAL;
			}

			ch_reg->thresh[iqs269_events[i].th_offs] = val;
		}

		if (!fwnode_property_read_u32(ev_node, "azoteq,hyst", &val)) {
			u8 *hyst = &ch_reg->hyst;

			if (val > IQS269_CHx_HYST_MAX) {
				dev_err(&client->dev,
					"Invalid channel %u hysteresis: %u\n",
					reg, val);
				return -EINVAL;
			}

			if (i == IQS269_EVENT_DEEP_DN ||
			    i == IQS269_EVENT_DEEP_UP) {
				*hyst &= ~IQS269_CHx_HYST_DEEP_MASK;
				*hyst |= (val << IQS269_CHx_HYST_DEEP_SHIFT);
			} else if (i == IQS269_EVENT_TOUCH_DN ||
				   i == IQS269_EVENT_TOUCH_UP) {
				*hyst &= ~IQS269_CHx_HYST_TOUCH_MASK;
				*hyst |= val;
			}
		}

		if (fwnode_property_read_u32(ev_node, "linux,code", &val))
			continue;

		switch (reg) {
		case IQS269_CHx_HALL_ACTIVE:
			if (iqs269->hall_enable) {
				iqs269->switches[i].code = val;
				iqs269->switches[i].enabled = true;
			}

			/* fall through */

		case IQS269_CHx_HALL_INACTIVE:
			if (iqs269->hall_enable)
				break;

			/* fall through */

		default:
			iqs269->keycode[i * IQS269_NUM_CH + reg] = val;
		}

		iqs269->sys_reg.event_mask &= ~iqs269_events[i].mask;
	}

	return 0;
}

static int iqs269_parse_prop(struct iqs269_private *iqs269)
{
	struct iqs269_sys_reg *sys_reg = &iqs269->sys_reg;
	struct i2c_client *client = iqs269->client;
	struct fwnode_handle *ch_node;
	u16 general, misc_a, misc_b;
	unsigned int val;
	int error;

	iqs269->hall_enable = device_property_present(&client->dev,
						      "azoteq,hall-enable");

	if (!device_property_read_u32(&client->dev, "azoteq,suspend-mode",
				      &val)) {
		if (val > IQS269_SYS_SETTINGS_PWR_MODE_MAX) {
			dev_err(&client->dev, "Invalid suspend mode: %u\n",
				val);
			return -EINVAL;
		}

		iqs269->suspend_mode = val;
	}

	error = regmap_raw_read(iqs269->regmap, IQS269_SYS_SETTINGS, sys_reg,
				sizeof(*sys_reg));
	if (error)
		return error;

	if (!device_property_read_u32(&client->dev, "azoteq,filt-str-lp-lta",
				      &val)) {
		if (val > IQS269_FILT_STR_MAX) {
			dev_err(&client->dev, "Invalid filter strength: %u\n",
				val);
			return -EINVAL;
		}

		sys_reg->filter &= ~IQS269_FILT_STR_LP_LTA_MASK;
		sys_reg->filter |= (val << IQS269_FILT_STR_LP_LTA_SHIFT);
	}

	if (!device_property_read_u32(&client->dev, "azoteq,filt-str-lp-cnt",
				      &val)) {
		if (val > IQS269_FILT_STR_MAX) {
			dev_err(&client->dev, "Invalid filter strength: %u\n",
				val);
			return -EINVAL;
		}

		sys_reg->filter &= ~IQS269_FILT_STR_LP_CNT_MASK;
		sys_reg->filter |= (val << IQS269_FILT_STR_LP_CNT_SHIFT);
	}

	if (!device_property_read_u32(&client->dev, "azoteq,filt-str-np-lta",
				      &val)) {
		if (val > IQS269_FILT_STR_MAX) {
			dev_err(&client->dev, "Invalid filter strength: %u\n",
				val);
			return -EINVAL;
		}

		sys_reg->filter &= ~IQS269_FILT_STR_NP_LTA_MASK;
		sys_reg->filter |= (val << IQS269_FILT_STR_NP_LTA_SHIFT);
	}

	if (!device_property_read_u32(&client->dev, "azoteq,filt-str-np-cnt",
				      &val)) {
		if (val > IQS269_FILT_STR_MAX) {
			dev_err(&client->dev, "Invalid filter strength: %u\n",
				val);
			return -EINVAL;
		}

		sys_reg->filter &= ~IQS269_FILT_STR_NP_CNT_MASK;
		sys_reg->filter |= val;
	}

	if (!device_property_read_u32(&client->dev, "azoteq,rate-np-ms",
				      &val)) {
		if (val > IQS269_RATE_NP_MS_MAX) {
			dev_err(&client->dev, "Invalid report rate: %u\n", val);
			return -EINVAL;
		}

		sys_reg->rate_np = val;
	}

	if (!device_property_read_u32(&client->dev, "azoteq,rate-lp-ms",
				      &val)) {
		if (val > IQS269_RATE_LP_MS_MAX) {
			dev_err(&client->dev, "Invalid report rate: %u\n", val);
			return -EINVAL;
		}

		sys_reg->rate_lp = val;
	}

	if (!device_property_read_u32(&client->dev, "azoteq,rate-ulp-ms",
				      &val)) {
		if (val > IQS269_RATE_ULP_MS_MAX) {
			dev_err(&client->dev, "Invalid report rate: %u\n", val);
			return -EINVAL;
		}

		sys_reg->rate_ulp = val / 16;
	}

	if (!device_property_read_u32(&client->dev, "azoteq,timeout-pwr-ms",
				      &val)) {
		if (val > IQS269_TIMEOUT_PWR_MS_MAX) {
			dev_err(&client->dev, "Invalid timeout: %u\n", val);
			return -EINVAL;
		}

		sys_reg->timeout_pwr = val / 512;
	}

	if (!device_property_read_u32(&client->dev, "azoteq,timeout-lta-ms",
				      &val)) {
		if (val > IQS269_TIMEOUT_LTA_MS_MAX) {
			dev_err(&client->dev, "Invalid timeout: %u\n", val);
			return -EINVAL;
		}

		sys_reg->timeout_lta = val / 512;
	}

	misc_a = be16_to_cpu(sys_reg->misc_a);
	misc_b = be16_to_cpu(sys_reg->misc_b);

	misc_a &= ~IQS269_MISC_A_ATI_BAND_DISABLE;
	if (device_property_present(&client->dev, "azoteq,ati-band-disable"))
		misc_a |= IQS269_MISC_A_ATI_BAND_DISABLE;

	misc_a &= ~IQS269_MISC_A_ATI_LP_ONLY;
	if (device_property_present(&client->dev, "azoteq,ati-lp-only"))
		misc_a |= IQS269_MISC_A_ATI_LP_ONLY;

	misc_a &= ~IQS269_MISC_A_ATI_BAND_TIGHTEN;
	if (device_property_present(&client->dev, "azoteq,ati-band-tighten"))
		misc_a |= IQS269_MISC_A_ATI_BAND_TIGHTEN;

	misc_a &= ~IQS269_MISC_A_FILT_DISABLE;
	if (device_property_present(&client->dev, "azoteq,filt-disable"))
		misc_a |= IQS269_MISC_A_FILT_DISABLE;

	if (!device_property_read_u32(&client->dev, "azoteq,gpio3-select",
				      &val)) {
		if (val >= IQS269_NUM_CH) {
			dev_err(&client->dev, "Invalid GPIO3 selection: %u\n",
				val);
			return -EINVAL;
		}

		misc_a &= ~IQS269_MISC_A_GPIO3_SELECT_MASK;
		misc_a |= (val << IQS269_MISC_A_GPIO3_SELECT_SHIFT);
	}

	misc_a &= ~IQS269_MISC_A_DUAL_DIR;
	if (device_property_present(&client->dev, "azoteq,dual-direction"))
		misc_a |= IQS269_MISC_A_DUAL_DIR;

	if (!device_property_read_u32(&client->dev, "azoteq,tx-freq", &val)) {
		if (val > IQS269_MISC_A_TX_FREQ_MAX) {
			dev_err(&client->dev,
				"Invalid excitation frequency: %u\n", val);
			return -EINVAL;
		}

		misc_a &= ~IQS269_MISC_A_TX_FREQ_MASK;
		misc_a |= (val << IQS269_MISC_A_TX_FREQ_SHIFT);
	}

	misc_a &= ~IQS269_MISC_A_GLOBAL_CAP_SIZE;
	if (device_property_present(&client->dev, "azoteq,global-cap-increase"))
		misc_a |= IQS269_MISC_A_GLOBAL_CAP_SIZE;

	if (!device_property_read_u32(&client->dev, "azoteq,reseed-select",
				      &val)) {
		if (val > IQS269_MISC_B_RESEED_UI_SEL_MAX) {
			dev_err(&client->dev, "Invalid reseed selection: %u\n",
				val);
			return -EINVAL;
		}

		misc_b &= ~IQS269_MISC_B_RESEED_UI_SEL_MASK;
		misc_b |= (val << IQS269_MISC_B_RESEED_UI_SEL_SHIFT);
	}

	misc_b &= ~IQS269_MISC_B_TRACKING_UI_ENABLE;
	if (device_property_present(&client->dev, "azoteq,tracking-enable"))
		misc_b |= IQS269_MISC_B_TRACKING_UI_ENABLE;

	if (!device_property_read_u32(&client->dev, "azoteq,filt-str-slider",
				      &val)) {
		if (val > IQS269_FILT_STR_MAX) {
			dev_err(&client->dev, "Invalid filter strength: %u\n",
				val);
			return -EINVAL;
		}

		misc_b &= ~IQS269_MISC_B_FILT_STR_SLIDER;
		misc_b |= val;
	}

	sys_reg->misc_a = cpu_to_be16(misc_a);
	sys_reg->misc_b = cpu_to_be16(misc_b);

	sys_reg->active = 0;
	sys_reg->reseed = 0;

	sys_reg->blocking = 0;

	sys_reg->slider_select[0] = 0;
	sys_reg->slider_select[1] = 0;

	sys_reg->event_mask = ~((u8)IQS269_EVENT_MASK_SYS);

	device_for_each_child_node(&client->dev, ch_node) {
		error = iqs269_parse_chan(iqs269, ch_node);
		if (error) {
			fwnode_handle_put(ch_node);
			return error;
		}
	}

	/*
	 * Volunteer all active channels to participate in ATI when REDO-ATI is
	 * manually triggered.
	 */
	sys_reg->redo_ati = sys_reg->active;

	general = be16_to_cpu(sys_reg->general);

	if (device_property_present(&client->dev, "azoteq,clk-div")) {
		general |= IQS269_SYS_SETTINGS_CLK_DIV;
		iqs269->delay_mult = 4;
	} else {
		general &= ~IQS269_SYS_SETTINGS_CLK_DIV;
		iqs269->delay_mult = 1;
	}

	/*
	 * Configure the device to automatically switch between normal and low-
	 * power modes as a function of sensing activity. Ultra-low-power mode,
	 * if enabled, is reserved for suspend.
	 */
	general &= ~IQS269_SYS_SETTINGS_ULP_AUTO;
	general &= ~IQS269_SYS_SETTINGS_DIS_AUTO;
	general &= ~IQS269_SYS_SETTINGS_PWR_MODE_MASK;

	if (!device_property_read_u32(&client->dev, "azoteq,ulp-update",
				      &val)) {
		if (val > IQS269_SYS_SETTINGS_ULP_UPDATE_MAX) {
			dev_err(&client->dev, "Invalid update rate: %u\n", val);
			return -EINVAL;
		}

		general &= ~IQS269_SYS_SETTINGS_ULP_UPDATE_MASK;
		general |= (val << IQS269_SYS_SETTINGS_ULP_UPDATE_SHIFT);
	}

	general &= ~IQS269_SYS_SETTINGS_RESEED_OFFSET;
	if (device_property_present(&client->dev, "azoteq,reseed-offset"))
		general |= IQS269_SYS_SETTINGS_RESEED_OFFSET;

	general |= IQS269_SYS_SETTINGS_EVENT_MODE;

	/*
	 * As per the datasheet, enable streaming during normal-power mode if
	 * either slider is in use. In that case, the device returns to event
	 * mode during low-power mode.
	 */
	if (sys_reg->slider_select[0] || sys_reg->slider_select[1])
		general |= IQS269_SYS_SETTINGS_EVENT_MODE_LP;

	general |= IQS269_SYS_SETTINGS_REDO_ATI;
	general |= IQS269_SYS_SETTINGS_ACK_RESET;

	sys_reg->general = cpu_to_be16(general);

	return 0;
}

static int iqs269_dev_init(struct iqs269_private *iqs269)
{
	struct iqs269_sys_reg *sys_reg = &iqs269->sys_reg;
	struct iqs269_ch_reg *ch_reg;
	unsigned int val;
	int error, i;

	mutex_lock(&iqs269->lock);

	error = regmap_update_bits(iqs269->regmap, IQS269_HALL_UI,
				   IQS269_HALL_UI_ENABLE,
				   iqs269->hall_enable ? ~0 : 0);
	if (error)
		goto err_mutex;

	for (i = 0; i < IQS269_NUM_CH; i++) {
		if (!(sys_reg->active & BIT(i)))
			continue;

		ch_reg = &iqs269->ch_reg[i];

		error = regmap_raw_write(iqs269->regmap,
					 IQS269_CHx_SETTINGS + i *
					 sizeof(*ch_reg) / 2, ch_reg,
					 sizeof(*ch_reg));
		if (error)
			goto err_mutex;
	}

	/*
	 * The REDO-ATI and ATI channel selection fields must be written in the
	 * same block write, so every field between registers 0x80 through 0x8B
	 * (inclusive) must be written as well.
	 */
	error = regmap_raw_write(iqs269->regmap, IQS269_SYS_SETTINGS, sys_reg,
				 sizeof(*sys_reg));
	if (error)
		goto err_mutex;

	error = regmap_read_poll_timeout(iqs269->regmap, IQS269_SYS_FLAGS, val,
					!(val & IQS269_SYS_FLAGS_IN_ATI),
					 IQS269_ATI_POLL_SLEEP_US,
					 IQS269_ATI_POLL_TIMEOUT_US);
	if (error)
		goto err_mutex;

	msleep(IQS269_ATI_STABLE_DELAY_MS);
	iqs269->ati_current = true;

err_mutex:
	mutex_unlock(&iqs269->lock);

	return error;
}

static int iqs269_input_init(struct iqs269_private *iqs269)
{
	struct i2c_client *client = iqs269->client;
	struct iqs269_flags flags;
	unsigned int sw_code, keycode;
	int error, i, j;
	u8 dir_mask, state;

	iqs269->keypad = devm_input_allocate_device(&client->dev);
	if (!iqs269->keypad)
		return -ENOMEM;

	iqs269->keypad->keycodemax = ARRAY_SIZE(iqs269->keycode);
	iqs269->keypad->keycode = iqs269->keycode;
	iqs269->keypad->keycodesize = sizeof(*iqs269->keycode);

	iqs269->keypad->name = "iqs269a_keypad";
	iqs269->keypad->id.bustype = BUS_I2C;

	if (iqs269->hall_enable) {
		error = regmap_raw_read(iqs269->regmap, IQS269_SYS_FLAGS,
					&flags, sizeof(flags));
		if (error) {
			dev_err(&client->dev,
				"Failed to read initial status: %d\n", error);
			return error;
		}
	}

	for (i = 0; i < ARRAY_SIZE(iqs269_events); i++) {
		dir_mask = flags.states[IQS269_ST_OFFS_DIR];
		if (!iqs269_events[i].dir_up)
			dir_mask = ~dir_mask;

		state = flags.states[iqs269_events[i].st_offs] & dir_mask;

		sw_code = iqs269->switches[i].code;

		for (j = 0; j < IQS269_NUM_CH; j++) {
			keycode = iqs269->keycode[i * IQS269_NUM_CH + j];

			/*
			 * Hall-effect sensing repurposes a pair of dedicated
			 * channels, only one of which reports events.
			 */
			switch (j) {
			case IQS269_CHx_HALL_ACTIVE:
				if (iqs269->hall_enable &&
				    iqs269->switches[i].enabled) {
					input_set_capability(iqs269->keypad,
							     EV_SW, sw_code);
					input_report_switch(iqs269->keypad,
							    sw_code,
							    state & BIT(j));
				}

				/* fall through */

			case IQS269_CHx_HALL_INACTIVE:
				if (iqs269->hall_enable)
					continue;

				/* fall through */

			default:
				if (keycode != KEY_RESERVED)
					input_set_capability(iqs269->keypad,
							     EV_KEY, keycode);
			}
		}
	}

	input_sync(iqs269->keypad);

	error = input_register_device(iqs269->keypad);
	if (error) {
		dev_err(&client->dev, "Failed to register keypad: %d\n", error);
		return error;
	}

	for (i = 0; i < IQS269_NUM_SL; i++) {
		if (!iqs269->sys_reg.slider_select[i])
			continue;

		iqs269->slider[i] = devm_input_allocate_device(&client->dev);
		if (!iqs269->slider[i])
			return -ENOMEM;

		iqs269->slider[i]->name = i ? "iqs269a_slider_1"
					    : "iqs269a_slider_0";
		iqs269->slider[i]->id.bustype = BUS_I2C;

		input_set_capability(iqs269->slider[i], EV_KEY, BTN_TOUCH);
		input_set_abs_params(iqs269->slider[i], ABS_X, 0, 255, 0, 0);

		error = input_register_device(iqs269->slider[i]);
		if (error) {
			dev_err(&client->dev,
				"Failed to register slider %d: %d\n", i, error);
			return error;
		}
	}

	return 0;
}

static int iqs269_report(struct iqs269_private *iqs269)
{
	struct i2c_client *client = iqs269->client;
	struct iqs269_flags flags;
	unsigned int sw_code, keycode;
	int error, i, j;
	u8 slider_x[IQS269_NUM_SL];
	u8 dir_mask, state;

	error = regmap_raw_read(iqs269->regmap, IQS269_SYS_FLAGS, &flags,
				sizeof(flags));
	if (error) {
		dev_err(&client->dev, "Failed to read device status: %d\n",
			error);
		return error;
	}

	/*
	 * The device resets itself if its own watchdog bites, which can happen
	 * in the event of an I2C communication error. In this case, the device
	 * asserts a SHOW_RESET interrupt and all registers must be restored.
	 */
	if (be16_to_cpu(flags.system) & IQS269_SYS_FLAGS_SHOW_RESET) {
		dev_err(&client->dev, "Unexpected device reset\n");

		error = iqs269_dev_init(iqs269);
		if (error)
			dev_err(&client->dev,
				"Failed to re-initialize device: %d\n", error);

		return error;
	}

	error = regmap_raw_read(iqs269->regmap, IQS269_SLIDER_X, slider_x,
				sizeof(slider_x));
	if (error) {
		dev_err(&client->dev, "Failed to read slider position: %d\n",
			error);
		return error;
	}

	for (i = 0; i < IQS269_NUM_SL; i++) {
		if (!iqs269->sys_reg.slider_select[i])
			continue;

		/*
		 * Report BTN_TOUCH if any channel that participates in the
		 * slider is in a state of touch.
		 */
		if (flags.states[IQS269_ST_OFFS_TOUCH] &
		    iqs269->sys_reg.slider_select[i]) {
			input_report_key(iqs269->slider[i], BTN_TOUCH, 1);
			input_report_abs(iqs269->slider[i], ABS_X, slider_x[i]);
		} else {
			input_report_key(iqs269->slider[i], BTN_TOUCH, 0);
		}

		input_sync(iqs269->slider[i]);
	}

	for (i = 0; i < ARRAY_SIZE(iqs269_events); i++) {
		dir_mask = flags.states[IQS269_ST_OFFS_DIR];
		if (!iqs269_events[i].dir_up)
			dir_mask = ~dir_mask;

		state = flags.states[iqs269_events[i].st_offs] & dir_mask;

		sw_code = iqs269->switches[i].code;

		for (j = 0; j < IQS269_NUM_CH; j++) {
			keycode = iqs269->keycode[i * IQS269_NUM_CH + j];

			switch (j) {
			case IQS269_CHx_HALL_ACTIVE:
				if (iqs269->hall_enable &&
				    iqs269->switches[i].enabled)
					input_report_switch(iqs269->keypad,
							    sw_code,
							    state & BIT(j));

				/* fall through */

			case IQS269_CHx_HALL_INACTIVE:
				if (iqs269->hall_enable)
					continue;

				/* fall through */

			default:
				input_report_key(iqs269->keypad, keycode,
						 state & BIT(j));
			}
		}
	}

	input_sync(iqs269->keypad);

	return 0;
}

static irqreturn_t iqs269_irq(int irq, void *context)
{
	struct iqs269_private *iqs269 = context;

	if (iqs269_report(iqs269))
		return IRQ_NONE;

	/*
	 * The device does not deassert its interrupt (RDY) pin until shortly
	 * after receiving an I2C stop condition; the following delay ensures
	 * the interrupt handler does not return before this time.
	 */
	iqs269_irq_wait();

	return IRQ_HANDLED;
}

static ssize_t counts_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	struct i2c_client *client = iqs269->client;
	__le16 counts;
	int error;

	if (!iqs269->ati_current || iqs269->hall_enable)
		return -EPERM;

	/*
	 * Unsolicited I2C communication prompts the device to assert its RDY
	 * pin, so disable the interrupt line until the operation is finished
	 * and RDY has been deasserted.
	 */
	disable_irq(client->irq);

	error = regmap_raw_read(iqs269->regmap,
				IQS269_CHx_COUNTS + iqs269->ch_num * 2,
				&counts, sizeof(counts));

	iqs269_irq_wait();
	enable_irq(client->irq);

	if (error)
		return error;

	return scnprintf(buf, PAGE_SIZE, "%u\n", le16_to_cpu(counts));
}

static ssize_t hall_bin_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	struct i2c_client *client = iqs269->client;
	unsigned int val;
	int error;

	disable_irq(client->irq);

	error = regmap_read(iqs269->regmap, IQS269_CAL_DATA_A, &val);

	iqs269_irq_wait();
	enable_irq(client->irq);

	if (error)
		return error;

	switch (iqs269->ch_reg[IQS269_CHx_HALL_ACTIVE].rx_enable &
		iqs269->ch_reg[IQS269_CHx_HALL_INACTIVE].rx_enable) {
	case IQS269_HALL_PAD_R:
		val &= IQS269_CAL_DATA_A_HALL_BIN_R_MASK;
		val >>= IQS269_CAL_DATA_A_HALL_BIN_R_SHIFT;
		break;

	case IQS269_HALL_PAD_L:
		val &= IQS269_CAL_DATA_A_HALL_BIN_L_MASK;
		val >>= IQS269_CAL_DATA_A_HALL_BIN_L_SHIFT;
		break;

	default:
		return -EINVAL;
	}

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t hall_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", iqs269->hall_enable);
}

static ssize_t hall_enable_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	mutex_lock(&iqs269->lock);

	iqs269->hall_enable = val;
	iqs269->ati_current = false;

	mutex_unlock(&iqs269->lock);

	return count;
}

static ssize_t ch_number_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", iqs269->ch_num);
}

static ssize_t ch_number_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	if (val >= IQS269_NUM_CH)
		return -EINVAL;

	iqs269->ch_num = val;

	return count;
}

static ssize_t rx_enable_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 iqs269->ch_reg[iqs269->ch_num].rx_enable);
}

static ssize_t rx_enable_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	if (val > 0xFF)
		return -EINVAL;

	mutex_lock(&iqs269->lock);

	iqs269->ch_reg[iqs269->ch_num].rx_enable = val;
	iqs269->ati_current = false;

	mutex_unlock(&iqs269->lock);

	return count;
}

static ssize_t ati_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = iqs269_ati_mode_get(iqs269, iqs269->ch_num, &val);
	if (error)
		return error;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t ati_mode_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	error = iqs269_ati_mode_set(iqs269, iqs269->ch_num, val);
	if (error)
		return error;

	return count;
}

static ssize_t ati_base_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = iqs269_ati_base_get(iqs269, iqs269->ch_num, &val);
	if (error)
		return error;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t ati_base_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	error = iqs269_ati_base_set(iqs269, iqs269->ch_num, val);
	if (error)
		return error;

	return count;
}

static ssize_t ati_target_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = iqs269_ati_target_get(iqs269, iqs269->ch_num, &val);
	if (error)
		return error;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t ati_target_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	error = iqs269_ati_target_set(iqs269, iqs269->ch_num, val);
	if (error)
		return error;

	return count;
}

static ssize_t ati_trigger_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%u\n", iqs269->ati_current);
}

static ssize_t ati_trigger_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	struct i2c_client *client = iqs269->client;
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	if (!val)
		return count;

	disable_irq(client->irq);

	error = iqs269_dev_init(iqs269);

	iqs269_irq_wait();
	enable_irq(client->irq);

	if (error)
		return error;

	return count;
}

static DEVICE_ATTR_RO(counts);
static DEVICE_ATTR_RO(hall_bin);
static DEVICE_ATTR_RW(hall_enable);
static DEVICE_ATTR_RW(ch_number);
static DEVICE_ATTR_RW(rx_enable);
static DEVICE_ATTR_RW(ati_mode);
static DEVICE_ATTR_RW(ati_base);
static DEVICE_ATTR_RW(ati_target);
static DEVICE_ATTR_RW(ati_trigger);

static struct attribute *iqs269_attrs[] = {
	&dev_attr_counts.attr,
	&dev_attr_hall_bin.attr,
	&dev_attr_hall_enable.attr,
	&dev_attr_ch_number.attr,
	&dev_attr_rx_enable.attr,
	&dev_attr_ati_mode.attr,
	&dev_attr_ati_base.attr,
	&dev_attr_ati_target.attr,
	&dev_attr_ati_trigger.attr,
	NULL,
};

static const struct attribute_group iqs269_attr_group = {
	.attrs = iqs269_attrs,
};

static const struct regmap_config iqs269_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = IQS269_MAX_REG,
};

static int iqs269_probe(struct i2c_client *client)
{
	struct iqs269_ver_info ver_info;
	struct iqs269_private *iqs269;
	int error;

	iqs269 = devm_kzalloc(&client->dev, sizeof(*iqs269), GFP_KERNEL);
	if (!iqs269)
		return -ENOMEM;

	i2c_set_clientdata(client, iqs269);
	iqs269->client = client;

	iqs269->regmap = devm_regmap_init_i2c(client, &iqs269_regmap_config);
	if (IS_ERR(iqs269->regmap)) {
		error = PTR_ERR(iqs269->regmap);
		dev_err(&client->dev, "Failed to initialize register map: %d\n",
			error);
		return error;
	}

	mutex_init(&iqs269->lock);

	error = regmap_raw_read(iqs269->regmap, IQS269_VER_INFO, &ver_info,
				sizeof(ver_info));
	if (error)
		return error;

	if (ver_info.prod_num != IQS269_VER_INFO_PROD_NUM) {
		dev_err(&client->dev, "Unrecognized product number: 0x%02X\n",
			ver_info.prod_num);
		return -EINVAL;
	}

	error = iqs269_parse_prop(iqs269);
	if (error)
		return error;

	error = iqs269_dev_init(iqs269);
	if (error) {
		dev_err(&client->dev, "Failed to initialize device: %d\n",
			error);
		return error;
	}

	error = iqs269_input_init(iqs269);
	if (error)
		return error;

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, iqs269_irq, IRQF_ONESHOT,
					  client->name, iqs269);
	if (error) {
		dev_err(&client->dev, "Failed to request IRQ: %d\n", error);
		return error;
	}

	error = devm_device_add_group(&client->dev, &iqs269_attr_group);
	if (error)
		dev_err(&client->dev, "Failed to add attributes: %d\n", error);

	return error;
}

static int __maybe_unused iqs269_suspend(struct device *dev)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	struct i2c_client *client = iqs269->client;
	unsigned int val;
	int error;

	if (!iqs269->suspend_mode)
		return 0;

	disable_irq(client->irq);

	/*
	 * Automatic power mode switching must be disabled before the device is
	 * forced into any particular power mode. In this case, the device will
	 * transition into normal-power mode.
	 */
	error = regmap_update_bits(iqs269->regmap, IQS269_SYS_SETTINGS,
				   IQS269_SYS_SETTINGS_DIS_AUTO, ~0);
	if (error)
		goto err_irq;

	/*
	 * The following check ensures the device has completed its transition
	 * into normal-power mode before a manual mode switch is performed.
	 */
	error = regmap_read_poll_timeout(iqs269->regmap, IQS269_SYS_FLAGS, val,
					!(val & IQS269_SYS_FLAGS_PWR_MODE_MASK),
					 IQS269_PWR_MODE_POLL_SLEEP_US,
					 IQS269_PWR_MODE_POLL_TIMEOUT_US);
	if (error)
		goto err_irq;

	error = regmap_update_bits(iqs269->regmap, IQS269_SYS_SETTINGS,
				   IQS269_SYS_SETTINGS_PWR_MODE_MASK,
				   iqs269->suspend_mode <<
				   IQS269_SYS_SETTINGS_PWR_MODE_SHIFT);
	if (error)
		goto err_irq;

	/*
	 * This last check ensures the device has completed its transition into
	 * the desired power mode to prevent any spurious interrupts from being
	 * triggered after iqs269_suspend has already returned.
	 */
	error = regmap_read_poll_timeout(iqs269->regmap, IQS269_SYS_FLAGS, val,
					 (val & IQS269_SYS_FLAGS_PWR_MODE_MASK)
					 == (iqs269->suspend_mode <<
					     IQS269_SYS_FLAGS_PWR_MODE_SHIFT),
					 IQS269_PWR_MODE_POLL_SLEEP_US,
					 IQS269_PWR_MODE_POLL_TIMEOUT_US);

err_irq:
	iqs269_irq_wait();
	enable_irq(client->irq);

	return error;
}

static int __maybe_unused iqs269_resume(struct device *dev)
{
	struct iqs269_private *iqs269 = dev_get_drvdata(dev);
	struct i2c_client *client = iqs269->client;
	unsigned int val;
	int error;

	if (!iqs269->suspend_mode)
		return 0;

	disable_irq(client->irq);

	error = regmap_update_bits(iqs269->regmap, IQS269_SYS_SETTINGS,
				   IQS269_SYS_SETTINGS_PWR_MODE_MASK, 0);
	if (error)
		goto err_irq;

	/*
	 * This check ensures the device has returned to normal-power mode
	 * before automatic power mode switching is re-enabled.
	 */
	error = regmap_read_poll_timeout(iqs269->regmap, IQS269_SYS_FLAGS, val,
					!(val & IQS269_SYS_FLAGS_PWR_MODE_MASK),
					 IQS269_PWR_MODE_POLL_SLEEP_US,
					 IQS269_PWR_MODE_POLL_TIMEOUT_US);
	if (error)
		goto err_irq;

	error = regmap_update_bits(iqs269->regmap, IQS269_SYS_SETTINGS,
				   IQS269_SYS_SETTINGS_DIS_AUTO, 0);
	if (error)
		goto err_irq;

	/*
	 * This step reports any events that may have been "swallowed" as a
	 * result of polling PWR_MODE (which automatically acknowledges any
	 * pending interrupts).
	 */
	error = iqs269_report(iqs269);

err_irq:
	iqs269_irq_wait();
	enable_irq(client->irq);

	return error;
}

static SIMPLE_DEV_PM_OPS(iqs269_pm, iqs269_suspend, iqs269_resume);

static const struct of_device_id iqs269_of_match[] = {
	{ .compatible = "azoteq,iqs269a" },
	{ }
};
MODULE_DEVICE_TABLE(of, iqs269_of_match);

static struct i2c_driver iqs269_i2c_driver = {
	.driver = {
		.name = "iqs269a",
		.of_match_table = iqs269_of_match,
		.pm = &iqs269_pm,
	},
	.probe_new = iqs269_probe,
};
module_i2c_driver(iqs269_i2c_driver);

MODULE_AUTHOR("Jeff LaBundy <jeff@labundy.com>");
MODULE_DESCRIPTION("Azoteq IQS269A Capacitive Touch Controller");
MODULE_LICENSE("GPL");
