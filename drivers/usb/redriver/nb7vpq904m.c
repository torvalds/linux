// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2018-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/usb/ch9.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/regmap.h>
#include <linux/ctype.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/redriver.h>

/* priority: INT_MAX >= x >= 0 */
#define NOTIFIER_PRIORITY		1

/* Registers Address */
#define GEN_DEV_SET_REG			0x00
#define CHIP_VERSION_REG		0x17

#define REDRIVER_REG_MAX		0x1f

#define EQ_SET_REG_BASE			0x01
#define FLAT_GAIN_REG_BASE		0x18
#define OUT_COMP_AND_POL_REG_BASE	0x02
#define LOSS_MATCH_REG_BASE		0x19

#define AUX_SWITCH_REG			0x09
#define AUX_NORMAL_VAL			0
#define AUX_FLIP_VAL			1
#define AUX_DISABLE_VAL			2

/* Default Register Value */
#define GEN_DEV_SET_REG_DEFAULT		0xFB

/* Register bits */
/* General Device Settings Register Bits */
#define CHIP_EN		BIT(0)
#define CHNA_EN		BIT(4)
#define CHNB_EN		BIT(5)
#define CHNC_EN		BIT(6)
#define CHND_EN		BIT(7)

#define CHANNEL_NUM		4

#define OP_MODE_SHIFT		1

#define EQ_SETTING_MASK			0x07
#define OUTPUT_COMPRESSION_MASK		0x0b
#define LOSS_MATCH_MASK			0x03
#define FLAT_GAIN_MASK			0x03

#define EQ_SETTING_SHIFT		0x01
#define OUTPUT_COMPRESSION_SHIFT	0x01
#define LOSS_MATCH_SHIFT		0x00
#define FLAT_GAIN_SHIFT			0x00

#define CHNA_INDEX		0
#define CHNB_INDEX		1
#define CHNC_INDEX		2
#define CHND_INDEX		3

enum operation_mode {
	OP_MODE_NONE,		/* 4 lanes disabled */
	OP_MODE_USB,		/* 2 lanes for USB and 2 lanes disabled */
	OP_MODE_DP,		/* 4 lanes DP */
	OP_MODE_USB_AND_DP,	/* 2 lanes for USB and 2 lanes DP */
	OP_MODE_DEFAULT,	/* 4 lanes USB */
};

#define CHAN_MODE_USB		0
#define CHAN_MODE_DP		1
#define CHAN_MODE_NUM		2

#define CHAN_MODE_DISABLE	0xff /* when disable, not configure eq, gain ... */

#define LANES_DP		4
#define LANES_DP_AND_USB	2

#define PULLUP_WORKER_DELAY_US	500000

#define CHIP_MAX_PWR_UA		260000
#define CHIP_MIN_PWR_UV		1710000
#define CHIP_MAX_PWR_UV		1890000

struct nb7vpq904m_redriver {
	struct usb_redriver	r;
	struct device		*dev;
	struct regmap		*regmap;
	struct i2c_client	*client;
	struct regulator	*vdd;

	int typec_orientation;
	enum operation_mode op_mode;

	u8	chan_mode[CHANNEL_NUM];

	u8	eq[CHAN_MODE_NUM][CHANNEL_NUM];
	u8	output_comp[CHAN_MODE_NUM][CHANNEL_NUM];
	u8	loss_match[CHAN_MODE_NUM][CHANNEL_NUM];
	u8	flat_gain[CHAN_MODE_NUM][CHANNEL_NUM];

	u8	gen_dev_val;
	bool	lane_channel_swap;
	bool	vdd_enable;
	bool	is_set_aux;

	struct workqueue_struct *pullup_wq;
	struct work_struct	pullup_work;
	bool			work_ongoing;

	struct work_struct	host_work;

	struct dentry	*debug_root;
};

static int nb7vpq904m_channel_update(struct nb7vpq904m_redriver *redriver);
static void nb7vpq904m_debugfs_entries(struct nb7vpq904m_redriver *redriver);

static const char * const opmode_string[] = {
	[OP_MODE_NONE] = "NONE",
	[OP_MODE_USB] = "USB",
	[OP_MODE_DP] = "DP",
	[OP_MODE_USB_AND_DP] = "USB and DP",
	[OP_MODE_DEFAULT] = "DEFAULT",
};
#define OPMODESTR(x) opmode_string[x]

static int nb7vpq904m_reg_set(struct nb7vpq904m_redriver *redriver,
		u8 reg, u8 val)
{
	int ret;

	ret = regmap_write(redriver->regmap, (unsigned int)reg,
			(unsigned int)val);
	if (ret < 0) {
		dev_err(redriver->dev, "writing reg 0x%02x failure\n", reg);
		return ret;
	}

	dev_dbg(redriver->dev, "writing reg 0x%02x=0x%02x\n", reg, val);

	return 0;
}

static void nb7vpq904m_vdd_enable(struct nb7vpq904m_redriver *redriver, bool on)
{
	int l, v, s;

	if (!redriver->vdd) {
		dev_dbg(redriver->dev, "no vdd regulator operation\n");
		return;
	}

	if (on && !redriver->vdd_enable) {
		redriver->vdd_enable = true;
		l = regulator_set_load(redriver->vdd, CHIP_MAX_PWR_UA);
		v = regulator_set_voltage(redriver->vdd, CHIP_MIN_PWR_UV, CHIP_MAX_PWR_UV);
		s = regulator_enable(redriver->vdd);
		dev_dbg(redriver->dev, "vdd regulator enable return %d-%d-%d\n", l, v, s);
	} else if (!on && redriver->vdd_enable) {
		redriver->vdd_enable = false;
		s = regulator_disable(redriver->vdd);
		v = regulator_set_voltage(redriver->vdd, 0, CHIP_MAX_PWR_UV);
		l = regulator_set_load(redriver->vdd, 0);
		dev_dbg(redriver->dev, "vdd regulator disable return %d-%d-%d\n", l, v, s);
	}
}

static void nb7vpq904m_dev_aux_set(struct nb7vpq904m_redriver *redriver)
{
	u8 aux_val = AUX_DISABLE_VAL;

	if (!redriver->is_set_aux)
		return;

	switch (redriver->op_mode) {
	case OP_MODE_DP:
	case OP_MODE_USB_AND_DP:
		if (redriver->typec_orientation == ORIENTATION_CC1)
			aux_val = AUX_NORMAL_VAL;
		else
			aux_val = AUX_FLIP_VAL;
		break;
	default:
		break;
	}

	nb7vpq904m_reg_set(redriver, AUX_SWITCH_REG, aux_val);
}

static int nb7vpq904m_gen_dev_set(struct nb7vpq904m_redriver *redriver)
{
	u8 val = 0;

	switch (redriver->op_mode) {
	case OP_MODE_DEFAULT:
		/* Enable channel A, B, C and D */
		val |= (CHNA_EN | CHNB_EN);
		val |= (CHNC_EN | CHND_EN);
		val |= (0x5 << OP_MODE_SHIFT);
		val |= CHIP_EN;
		break;
	case OP_MODE_USB:
		/* Use source side I/O mapping */
		if (redriver->typec_orientation
				== ORIENTATION_CC1) {
			/* Enable channel C and D */
			val &= ~(CHNA_EN | CHNB_EN);
			val |= (CHNC_EN | CHND_EN);
		} else if (redriver->typec_orientation
				== ORIENTATION_CC2) {
			/* Enable channel A and B*/
			val |= (CHNA_EN | CHNB_EN);
			val &= ~(CHNC_EN | CHND_EN);
		}

		/* Set to default USB Mode */
		val |= (0x5 << OP_MODE_SHIFT);
		val |= CHIP_EN;
		break;
	case OP_MODE_DP:
		/* Enable channel A, B, C and D */
		val |= (CHNA_EN | CHNB_EN);
		val |= (CHNC_EN | CHND_EN);

		/* Set to DP 4 Lane Mode (OP Mode 2) */
		val |= (0x2 << OP_MODE_SHIFT);
		val |= CHIP_EN;
		break;
	case OP_MODE_USB_AND_DP:
		/* Enable channel A, B, C and D */
		val |= (CHNA_EN | CHNB_EN);
		val |= (CHNC_EN | CHND_EN);
		val |= CHIP_EN;

		if (redriver->typec_orientation
				== ORIENTATION_CC1)
			val |= (0x1 << OP_MODE_SHIFT);
		else if (redriver->typec_orientation
				== ORIENTATION_CC2)
			val |= (0x0 << OP_MODE_SHIFT);

		break;
	default:
		val &= ~CHIP_EN;
		break;
	}

	redriver->gen_dev_val = val;

	return nb7vpq904m_reg_set(redriver, GEN_DEV_SET_REG, val);
}

static int nb7vpq904m_param_config(struct nb7vpq904m_redriver *redriver,
		u8 reg_base, u8 channel, u8 chan_mode, u8 mask, u8 shift,
		u8 val, u8 (*stored_val)[CHANNEL_NUM])
{
	int i, j, ret = -EINVAL;
	u8 reg_addr, reg_val;

	if (channel == CHANNEL_NUM) {
		for (i = 0; i < CHAN_MODE_NUM; i++)
			for (j = 0; j < CHANNEL_NUM; j++) {
				if (redriver->chan_mode[j] == i) {
					reg_addr = reg_base + (j << 1);

					reg_val =  (val  << shift);
					reg_val &= (mask << shift);

					ret = nb7vpq904m_reg_set(redriver,
							reg_addr, reg_val);
					if (ret < 0)
						return ret;
				}

				stored_val[i][j] = val;
			}
	} else {
		if (redriver->chan_mode[channel] == chan_mode) {
			reg_addr = reg_base + (channel << 1);

			reg_val =  (val  << shift);
			reg_val &= (mask << shift);

			ret = nb7vpq904m_reg_set(redriver,
					reg_addr, reg_val);
			if (ret < 0)
				return ret;
		}

		stored_val[chan_mode][channel] = val;
	}

	return 0;
}

static int nb7vpq904m_eq_config(
	struct nb7vpq904m_redriver *redriver, u8 channel, u8 chan_mode, u8 val)
{
	return nb7vpq904m_param_config(redriver,
			EQ_SET_REG_BASE, channel, chan_mode,
			EQ_SETTING_MASK, EQ_SETTING_SHIFT,
			val, redriver->eq);
}

static int nb7vpq904m_flat_gain_config(
	struct nb7vpq904m_redriver *redriver, u8 channel, u8 chan_mode, u8 val)
{
	return nb7vpq904m_param_config(redriver,
			FLAT_GAIN_REG_BASE, channel, chan_mode,
			FLAT_GAIN_MASK, FLAT_GAIN_SHIFT,
			val, redriver->flat_gain);
}

static int nb7vpq904m_output_comp_config(
	struct nb7vpq904m_redriver *redriver, u8 channel, u8 chan_mode, u8 val)
{
	return nb7vpq904m_param_config(redriver,
			OUT_COMP_AND_POL_REG_BASE, channel, chan_mode,
			OUTPUT_COMPRESSION_MASK, OUTPUT_COMPRESSION_SHIFT,
			val, redriver->output_comp);
}

static int nb7vpq904m_loss_match_config(
	struct nb7vpq904m_redriver *redriver, u8 channel, u8 chan_mode, u8 val)
{
	return nb7vpq904m_param_config(redriver,
			LOSS_MATCH_REG_BASE, channel, chan_mode,
			LOSS_MATCH_MASK, LOSS_MATCH_SHIFT, val,
			redriver->loss_match);
}

static int nb7vpq904m_channel_update(struct nb7vpq904m_redriver *redriver)
{
	int ret;
	u8 i, chan_mode;

	switch (redriver->op_mode) {
	case OP_MODE_DEFAULT:
		redriver->chan_mode[CHNA_INDEX] = CHAN_MODE_USB;
		redriver->chan_mode[CHNB_INDEX] = CHAN_MODE_USB;
		redriver->chan_mode[CHNC_INDEX] = CHAN_MODE_USB;
		redriver->chan_mode[CHND_INDEX] = CHAN_MODE_USB;
		break;
	case OP_MODE_USB:
		if (redriver->typec_orientation == ORIENTATION_CC1) {
			redriver->chan_mode[CHNA_INDEX] = CHAN_MODE_DISABLE;
			redriver->chan_mode[CHNB_INDEX] = CHAN_MODE_DISABLE;
			redriver->chan_mode[CHNC_INDEX] = CHAN_MODE_USB;
			redriver->chan_mode[CHND_INDEX] = CHAN_MODE_USB;
		} else {
			redriver->chan_mode[CHNA_INDEX] = CHAN_MODE_USB;
			redriver->chan_mode[CHNB_INDEX] = CHAN_MODE_USB;
			redriver->chan_mode[CHNC_INDEX] = CHAN_MODE_DISABLE;
			redriver->chan_mode[CHND_INDEX] = CHAN_MODE_DISABLE;
		}
		break;
	case OP_MODE_USB_AND_DP:
		if (redriver->typec_orientation == ORIENTATION_CC1) {
			redriver->chan_mode[CHNA_INDEX] = CHAN_MODE_DP;
			redriver->chan_mode[CHNB_INDEX] = CHAN_MODE_DP;
			redriver->chan_mode[CHNC_INDEX] = CHAN_MODE_USB;
			redriver->chan_mode[CHND_INDEX] = CHAN_MODE_USB;
		} else {
			redriver->chan_mode[CHNA_INDEX] = CHAN_MODE_USB;
			redriver->chan_mode[CHNB_INDEX] = CHAN_MODE_USB;
			redriver->chan_mode[CHNC_INDEX] = CHAN_MODE_DP;
			redriver->chan_mode[CHND_INDEX] = CHAN_MODE_DP;
		}
		break;
	case OP_MODE_DP:
		redriver->chan_mode[CHNA_INDEX] = CHAN_MODE_DP;
		redriver->chan_mode[CHNB_INDEX] = CHAN_MODE_DP;
		redriver->chan_mode[CHNC_INDEX] = CHAN_MODE_DP;
		redriver->chan_mode[CHND_INDEX] = CHAN_MODE_DP;
		break;
	default:
		return 0;
	}

	for (i = 0; i < CHANNEL_NUM; i++) {
		if (redriver->chan_mode[i] == CHAN_MODE_DISABLE)
			continue;

		chan_mode = redriver->chan_mode[i];

		ret = nb7vpq904m_eq_config(redriver, i, chan_mode,
				redriver->eq[chan_mode][i]);
		if (ret)
			goto err;

		ret = nb7vpq904m_flat_gain_config(redriver, i, chan_mode,
				redriver->flat_gain[chan_mode][i]);
		if (ret)
			goto err;

		ret = nb7vpq904m_output_comp_config(redriver, i, chan_mode,
				redriver->output_comp[chan_mode][i]);
		if (ret)
			goto err;

		ret = nb7vpq904m_loss_match_config(redriver, i, chan_mode,
				redriver->loss_match[chan_mode][i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	dev_err(redriver->dev, "channel parameters update failure(%d).\n", ret);
	return ret;
}

static int nb7vpq904m_read_configuration(struct nb7vpq904m_redriver *redriver)
{
	struct device_node *node = redriver->dev->of_node;
	int ret = 0;

	if (of_find_property(node, "eq", NULL)) {
		ret = of_property_read_u8_array(node, "eq",
				redriver->eq[0], sizeof(redriver->eq));
		if (ret)
			goto err;
	}

	if (of_find_property(node, "flat-gain", NULL)) {
		ret = of_property_read_u8_array(node,
				"flat-gain", redriver->flat_gain[0],
				sizeof(redriver->flat_gain));
		if (ret)
			goto err;
	}

	if (of_find_property(node, "output-comp", NULL)) {
		ret = of_property_read_u8_array(node,
				"output-comp", redriver->output_comp[0],
				sizeof(redriver->output_comp));
		if (ret)
			goto err;
	}

	if (of_find_property(node, "loss-match", NULL)) {
		ret = of_property_read_u8_array(node,
				"loss-match", redriver->loss_match[0],
				sizeof(redriver->loss_match));
		if (ret)
			goto err;
	}

	redriver->is_set_aux = of_property_read_bool(node, "set-aux");

	return 0;

err:
	dev_err(redriver->dev,
			"%s: error read parameters.\n", __func__);
	return ret;
}

static inline void orientation_set(struct nb7vpq904m_redriver *redriver, int ort)
{
	redriver->typec_orientation = ort;

	if (redriver->lane_channel_swap) {
		if (redriver->typec_orientation == ORIENTATION_CC1)
			redriver->typec_orientation = ORIENTATION_CC2;
		else
			redriver->typec_orientation = ORIENTATION_CC1;
	}
}

static int nb7vpq904m_notify_connect(struct usb_redriver *r, int ort)
{
	struct nb7vpq904m_redriver *redriver =
		container_of(r, struct nb7vpq904m_redriver, r);

	dev_dbg(redriver->dev, "%s: mode %s, orientation %s, %d\n", __func__,
		OPMODESTR(redriver->op_mode),
		ort == ORIENTATION_CC1 ? "CC1" : "CC2",
		redriver->lane_channel_swap);

	nb7vpq904m_vdd_enable(redriver, true);

	if (redriver->op_mode == OP_MODE_NONE)
		redriver->op_mode = OP_MODE_USB;

	orientation_set(redriver, ort);

	nb7vpq904m_gen_dev_set(redriver);
	nb7vpq904m_channel_update(redriver);

	return 0;
}

static int nb7vpq904m_notify_disconnect(struct usb_redriver *r)
{
	int ret = 0;
	struct nb7vpq904m_redriver *redriver =
		container_of(r, struct nb7vpq904m_redriver, r);

	dev_dbg(redriver->dev, "%s: mode %s\n", __func__,
		OPMODESTR(redriver->op_mode));

	if (redriver->op_mode == OP_MODE_NONE)
		return 0;

	redriver->op_mode = OP_MODE_NONE;
	ret = nb7vpq904m_reg_set(redriver, GEN_DEV_SET_REG, 0);

	if (!ret)
		nb7vpq904m_vdd_enable(redriver, false);

	return 0;
}

static int nb7vpq904m_release_usb_lanes(struct usb_redriver *r, int ort, int num)
{
	struct nb7vpq904m_redriver *redriver =
		container_of(r, struct nb7vpq904m_redriver, r);

	dev_dbg(redriver->dev, "%s: mode %s, orientation %s-%d, lanes %d\n", __func__,
		OPMODESTR(redriver->op_mode), ort == ORIENTATION_CC1 ? "CC1" : "CC2",
		redriver->lane_channel_swap, num);

	if (num == LANES_DP)
		redriver->op_mode = OP_MODE_DP;
	else if (num == LANES_DP_AND_USB)
		redriver->op_mode = OP_MODE_USB_AND_DP;

	nb7vpq904m_vdd_enable(redriver, true);

	/* in case it need aux function from redriver and the first call is release lane */
	orientation_set(redriver, ort);

	nb7vpq904m_gen_dev_set(redriver);

	nb7vpq904m_dev_aux_set(redriver);

	nb7vpq904m_channel_update(redriver);

	return 0;
}

static void nb7vpq904m_gadget_pullup_work(struct work_struct *w)
{
	struct nb7vpq904m_redriver *redriver =
		container_of(w, struct nb7vpq904m_redriver, pullup_work);
	u8 val = redriver->gen_dev_val;

	nb7vpq904m_reg_set(redriver, GEN_DEV_SET_REG, val & ~CHIP_EN);
	usleep_range(1000, 1500);
	nb7vpq904m_reg_set(redriver, GEN_DEV_SET_REG, val);

	redriver->work_ongoing = false;
}

static int nb7vpq904m_gadget_pullup_enter(struct usb_redriver *r, int is_on)
{
	struct nb7vpq904m_redriver *redriver =
		container_of(r, struct nb7vpq904m_redriver, r);
	u64 time = 0;

	dev_dbg(redriver->dev, "%s: mode %s, %d, %d\n", __func__,
		OPMODESTR(redriver->op_mode), is_on, redriver->work_ongoing);

	if (redriver->op_mode != OP_MODE_USB)
		return -EINVAL;

	if (!is_on)
		return 0;

	while (redriver->work_ongoing) {
		/*
		 * this function can work in atomic context, no sleep function here,
		 * it need wait pull down complete before pull up again.
		 */
		udelay(1);
		if (time++ > PULLUP_WORKER_DELAY_US) {
			dev_warn(redriver->dev, "pullup timeout\n");
			break;
		}
	}

	dev_dbg(redriver->dev, "pull-up disable work took %llu us\n", time);

	return 0;
}

static int nb7vpq904m_gadget_pullup_exit(struct usb_redriver *r, int is_on)
{
	struct nb7vpq904m_redriver *redriver =
		container_of(r, struct nb7vpq904m_redriver, r);

	dev_dbg(redriver->dev, "%s: mode %s, %d, %d\n", __func__,
		OPMODESTR(redriver->op_mode), is_on, redriver->work_ongoing);

	if (redriver->op_mode != OP_MODE_USB)
		return -EINVAL;

	if (is_on)
		return 0;

	redriver->work_ongoing = true;
	queue_work(redriver->pullup_wq, &redriver->pullup_work);

	return 0;
}

static void nb7vpq904m_host_work(struct work_struct *w)
{
	struct nb7vpq904m_redriver *redriver =
			container_of(w, struct nb7vpq904m_redriver, host_work);
	u8 val = redriver->gen_dev_val;

	nb7vpq904m_reg_set(redriver, GEN_DEV_SET_REG, val & ~CHIP_EN);
	/* sleep for a while to make sure xhci host detect device disconnect */
	usleep_range(2000, 2500);
	nb7vpq904m_reg_set(redriver, GEN_DEV_SET_REG, val);
}

static int nb7vpq904m_host_powercycle(struct usb_redriver *r)
{
	struct nb7vpq904m_redriver *redriver =
		container_of(r, struct nb7vpq904m_redriver, r);

	if (redriver->op_mode != OP_MODE_USB)
		return -EINVAL;

	schedule_work(&redriver->host_work);

	return 0;
}

static const struct regmap_config redriver_regmap = {
	.name = "nb7vpq904m",
	.max_register = REDRIVER_REG_MAX,
	.reg_bits = 8,
	.val_bits = 8,
};

static int nb7vpq904m_probe(struct i2c_client *client,
			       const struct i2c_device_id *dev_id)
{
	struct nb7vpq904m_redriver *redriver;
	int ret;

	redriver = devm_kzalloc(&client->dev, sizeof(struct nb7vpq904m_redriver),
			GFP_KERNEL);
	if (!redriver)
		return -ENOMEM;

	redriver->pullup_wq = alloc_workqueue("%s:pullup",
				WQ_UNBOUND | WQ_HIGHPRI, 0,
				dev_name(&client->dev));
	if (!redriver->pullup_wq) {
		dev_err(&client->dev, "Failed to create pullup workqueue\n");
		return -ENOMEM;
	}

	redriver->regmap = devm_regmap_init_i2c(client, &redriver_regmap);
	if (IS_ERR(redriver->regmap)) {
		ret = PTR_ERR(redriver->regmap);
		dev_err(&client->dev,
			"Failed to allocate register map: %d\n", ret);
		return ret;
	}

	redriver->dev = &client->dev;
	i2c_set_clientdata(client, redriver);

	ret = nb7vpq904m_read_configuration(redriver);
	if (ret < 0) {
		dev_err(&client->dev,
			"Failed to read default configuration: %d\n", ret);
		return ret;
	}

	redriver->vdd = devm_regulator_get_optional(&client->dev, "vdd");
	if (IS_ERR(redriver->vdd)) {
		ret = PTR_ERR(redriver->vdd);
		redriver->vdd = NULL;
		if (ret != -ENODEV)
			dev_err(&client->dev, "Failed to get vdd regulator %d\n", ret);
	}

	INIT_WORK(&redriver->pullup_work, nb7vpq904m_gadget_pullup_work);
	INIT_WORK(&redriver->host_work, nb7vpq904m_host_work);

	redriver->lane_channel_swap =
	    of_property_read_bool(redriver->dev->of_node, "lane-channel-swap");

	/* disable it at start, one i2c register write time is acceptable */
	redriver->op_mode = OP_MODE_NONE;
	nb7vpq904m_vdd_enable(redriver, true);
	nb7vpq904m_gen_dev_set(redriver);
	/* when private vdd present and change to none mode, it can simply disable vdd regulator,
	 * but to keep things simple and avoid if/else operation, keep one same rule as,
	 * allow original register write operation then control vdd regulator.
	 * also it will keep consistent behavior if it still need vdd control when multiple
	 * clients share the same vdd regulator.
	 */
	nb7vpq904m_vdd_enable(redriver, false);

	nb7vpq904m_debugfs_entries(redriver);

	redriver->r.of_node = redriver->dev->of_node;
	redriver->r.release_usb_lanes = nb7vpq904m_release_usb_lanes;
	redriver->r.notify_connect = nb7vpq904m_notify_connect;
	redriver->r.notify_disconnect = nb7vpq904m_notify_disconnect;
	redriver->r.gadget_pullup_enter = nb7vpq904m_gadget_pullup_enter;
	redriver->r.gadget_pullup_exit = nb7vpq904m_gadget_pullup_exit;
	redriver->r.host_powercycle = nb7vpq904m_host_powercycle;
	usb_add_redriver(&redriver->r);

	return 0;
}

static void nb7vpq904m_remove(struct i2c_client *client)
{
	struct nb7vpq904m_redriver *redriver = i2c_get_clientdata(client);

	if (usb_remove_redriver(&redriver->r))
		return;

	debugfs_remove_recursive(redriver->debug_root);
	redriver->work_ongoing = false;
	destroy_workqueue(redriver->pullup_wq);

	if (redriver->vdd)
		regulator_disable(redriver->vdd);
}

static ssize_t channel_config_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos,
		int (*config_func)(struct nb7vpq904m_redriver *redriver,
			u8 channel, u8 chan_mode, u8 val))
{
	struct seq_file *s = file->private_data;
	struct nb7vpq904m_redriver *redriver = s->private;
	char buf[40];
	char *token_chan, *token_val, *this_buf;
	u8 channel, chan_mode;
	int ret = 0;

	memset(buf, 0, sizeof(buf));

	this_buf = buf;

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (isdigit(buf[0])) {
		ret = config_func(redriver, CHANNEL_NUM, -1, buf[0] - '0');
		if (ret < 0)
			goto err;
	} else if (isalpha(buf[0])) {
		while ((token_chan = strsep(&this_buf, " ")) != NULL) {
			switch (*token_chan) {
			case 'A':
			case 'B':
			case 'C':
			case 'D':
				channel = *token_chan - 'A';
				chan_mode = CHAN_MODE_USB;
				token_val = strsep(&this_buf, " ");
				if (!isdigit(*token_val))
					goto err;
				break;
			case 'a':
			case 'b':
			case 'c':
			case 'd':
				channel = *token_chan - 'a';
				chan_mode = CHAN_MODE_DP;
				token_val = strsep(&this_buf, " ");
				if (!isdigit(*token_val))
					goto err;
				break;
			default:
				goto err;
			}

			ret = config_func(redriver, channel, chan_mode,
					*token_val - '0');
			if (ret < 0)
				goto err;
		}
	} else
		goto err;


	return count;

err:
	pr_err("Used to config redriver A/B/C/D channels' parameters\n"
		"A/B/C/D represent for re-driver parameters for USB\n"
		"a/b/c/d represent for re-driver parameters for DP\n"
		"1. Set all channels to same value(both USB and DP)\n"
		"echo n > [eq|output_comp|flat_gain|loss_match]\n"
		"- eq: Equalization, range 0-7\n"
		"- output_comp: Output Compression, range 0-3\n"
		"- loss_match: LOSS Profile Matching, range 0-3\n"
		"- flat_gain: Flat Gain, range 0-3\n"
		"Example: Set all channels to same EQ value\n"
		"echo 1 > eq\n"
		"2. Set two channels to different values leave others unchanged\n"
		"echo [A|B|C|D] n [A|B|C|D] n > [eq|output_comp|flat_gain|loss_match]\n"
		"Example2: USB mode: set channel B flat gain to 2, set channel C flat gain to 3\n"
		"echo B 2 C 3 > flat_gain\n"
		"Example3: DP mode: set channel A equalization to 6, set channel B equalization to 4\n"
		"echo a 6 b 4 > eq\n");

	return -EFAULT;
}

static int eq_status(struct seq_file *s, void *p)
{
	struct nb7vpq904m_redriver *redriver = s->private;

	seq_puts(s, "\t\t\t A(USB)\t B(USB)\t C(USB)\t D(USB)\t"
			"A(DP)\t B(DP)\t C(DP)\t D(DP)\n");
	seq_printf(s, "Equalization:\t\t %d\t %d\t %d\t %d\t"
			"%d\t %d\t %d\t %d\n",
			redriver->eq[CHAN_MODE_USB][CHNA_INDEX],
			redriver->eq[CHAN_MODE_USB][CHNB_INDEX],
			redriver->eq[CHAN_MODE_USB][CHNC_INDEX],
			redriver->eq[CHAN_MODE_USB][CHND_INDEX],
			redriver->eq[CHAN_MODE_DP][CHNA_INDEX],
			redriver->eq[CHAN_MODE_DP][CHNB_INDEX],
			redriver->eq[CHAN_MODE_DP][CHNC_INDEX],
			redriver->eq[CHAN_MODE_DP][CHND_INDEX]);
	return 0;
}

static int eq_status_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, eq_status, inode->i_private);
}

static ssize_t eq_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	return channel_config_write(file, ubuf, count, ppos,
			nb7vpq904m_eq_config);
}

static const struct file_operations eq_ops = {
	.open	= eq_status_open,
	.read	= seq_read,
	.write	= eq_write,
};

static int flat_gain_status(struct seq_file *s, void *p)
{
	struct nb7vpq904m_redriver *redriver = s->private;

	seq_puts(s, "\t\t\t A(USB)\t B(USB)\t C(USB)\t D(USB)\t"
			"A(DP)\t B(DP)\t C(DP)\t D(DP)\n");
	seq_printf(s, "TX/RX Flat Gain:\t %d\t %d\t %d\t %d\t"
			"%d\t %d\t %d\t %d\n",
			redriver->flat_gain[CHAN_MODE_USB][CHNA_INDEX],
			redriver->flat_gain[CHAN_MODE_USB][CHNB_INDEX],
			redriver->flat_gain[CHAN_MODE_USB][CHNC_INDEX],
			redriver->flat_gain[CHAN_MODE_USB][CHND_INDEX],
			redriver->flat_gain[CHAN_MODE_DP][CHNA_INDEX],
			redriver->flat_gain[CHAN_MODE_DP][CHNB_INDEX],
			redriver->flat_gain[CHAN_MODE_DP][CHNC_INDEX],
			redriver->flat_gain[CHAN_MODE_DP][CHND_INDEX]);
	return 0;
}

static int flat_gain_status_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, flat_gain_status, inode->i_private);
}

static ssize_t flat_gain_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	return channel_config_write(file, ubuf, count, ppos,
			nb7vpq904m_flat_gain_config);
}

static const struct file_operations flat_gain_ops = {
	.open	= flat_gain_status_open,
	.read	= seq_read,
	.write	= flat_gain_write,
};

static int output_comp_status(struct seq_file *s, void *p)
{
	struct nb7vpq904m_redriver *redriver = s->private;

	seq_puts(s, "\t\t\t A(USB)\t B(USB)\t C(USB)\t D(USB)\t"
			"A(DP)\t B(DP)\t C(DP)\t D(DP)\n");
	seq_printf(s, "Output Compression:\t %d\t %d\t %d\t %d\t"
			"%d\t %d\t %d\t %d\n",
			redriver->output_comp[CHAN_MODE_USB][CHNA_INDEX],
			redriver->output_comp[CHAN_MODE_USB][CHNB_INDEX],
			redriver->output_comp[CHAN_MODE_USB][CHNC_INDEX],
			redriver->output_comp[CHAN_MODE_USB][CHND_INDEX],
			redriver->output_comp[CHAN_MODE_DP][CHNA_INDEX],
			redriver->output_comp[CHAN_MODE_DP][CHNB_INDEX],
			redriver->output_comp[CHAN_MODE_DP][CHNC_INDEX],
			redriver->output_comp[CHAN_MODE_DP][CHND_INDEX]);
	return 0;
}

static int output_comp_status_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, output_comp_status, inode->i_private);
}

static ssize_t output_comp_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	return channel_config_write(file, ubuf, count, ppos,
			nb7vpq904m_output_comp_config);
}

static const struct file_operations output_comp_ops = {
	.open	= output_comp_status_open,
	.read	= seq_read,
	.write	= output_comp_write,
};

static int loss_match_status(struct seq_file *s, void *p)
{
	struct nb7vpq904m_redriver *redriver = s->private;

	seq_puts(s, "\t\t\t A(USB)\t B(USB)\t C(USB)\t D(USB)\t"
			"A(DP)\t B(DP)\t C(DP)\t D(DP)\n");
	seq_printf(s, "Loss Profile Match:\t %d\t %d\t %d\t %d\t"
			"%d\t %d\t %d\t %d\n",
			redriver->loss_match[CHAN_MODE_USB][CHNA_INDEX],
			redriver->loss_match[CHAN_MODE_USB][CHNB_INDEX],
			redriver->loss_match[CHAN_MODE_USB][CHNC_INDEX],
			redriver->loss_match[CHAN_MODE_USB][CHND_INDEX],
			redriver->loss_match[CHAN_MODE_DP][CHNA_INDEX],
			redriver->loss_match[CHAN_MODE_DP][CHNB_INDEX],
			redriver->loss_match[CHAN_MODE_DP][CHNC_INDEX],
			redriver->loss_match[CHAN_MODE_DP][CHND_INDEX]);
	return 0;
}

static int loss_match_status_open(struct inode *inode,
		struct file *file)
{
	return single_open(file, loss_match_status, inode->i_private);
}

static ssize_t loss_match_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	return channel_config_write(file, ubuf, count, ppos,
			nb7vpq904m_loss_match_config);
}

static const struct file_operations loss_match_ops = {
	.open	= loss_match_status_open,
	.read	= seq_read,
	.write	= loss_match_write,
};

static void nb7vpq904m_debugfs_entries(
		struct nb7vpq904m_redriver *redriver)
{
	redriver->debug_root = debugfs_create_dir("nb7vpq904m_redriver", NULL);
	if (!redriver->debug_root) {
		dev_warn(redriver->dev, "Couldn't create debug dir\n");
		return;
	}

	debugfs_create_file("eq", 0600,
			redriver->debug_root, redriver, &eq_ops);

	debugfs_create_file("flat_gain", 0600,
			redriver->debug_root, redriver, &flat_gain_ops);

	debugfs_create_file("output_comp", 0600,
			redriver->debug_root, redriver, &output_comp_ops);

	debugfs_create_file("loss_match", 0600,
			redriver->debug_root, redriver, &loss_match_ops);

	debugfs_create_bool("lane-channel-swap", 0644,
			redriver->debug_root,  &redriver->lane_channel_swap);
}

static void nb7vpq904m_shutdown(struct i2c_client *client)
{
	struct nb7vpq904m_redriver *redriver = i2c_get_clientdata(client);
	int ret;

	/* Set back to USB mode with four channel enabled */
	ret = nb7vpq904m_reg_set(redriver, GEN_DEV_SET_REG,
			GEN_DEV_SET_REG_DEFAULT);
	if (ret < 0)
		dev_err(&client->dev,
			"%s: fail to set USB mode with 4 channel enabled.\n",
			__func__);
	else
		dev_dbg(&client->dev,
			"%s: successfully set back to USB mode.\n",
			__func__);
}

static const struct of_device_id nb7vpq904m_match_table[] = {
	{ .compatible = "onnn,redriver" },
	{ }
};

static struct i2c_driver nb7vpq904m_driver = {
	.driver = {
		.name	= "ssusb-redriver",
		.of_match_table	= nb7vpq904m_match_table,
	},

	.probe		= nb7vpq904m_probe,
	.remove		= nb7vpq904m_remove,
	.shutdown	= nb7vpq904m_shutdown,
};

module_i2c_driver(nb7vpq904m_driver);

MODULE_DESCRIPTION("USB Super Speed Linear Re-Driver");
MODULE_LICENSE("GPL");
