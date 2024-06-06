// SPDX-License-Identifier: GPL-2.0+
/*
 * NXP PTN36502 Type-C driver
 *
 * Copyright (C) 2023 Luca Weiss <luca.weiss@fairphone.com>
 *
 * Based on NB7VPQ904M driver:
 * Copyright (C) 2023 Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */

#include <drm/bridge/aux-bridge.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_retimer.h>

#define PTN36502_CHIP_ID_REG				0x00
#define PTN36502_CHIP_ID				0x02

#define PTN36502_CHIP_REVISION_REG			0x01
#define PTN36502_CHIP_REVISION_BASE_MASK		GENMASK(7, 4)
#define PTN36502_CHIP_REVISION_METAL_MASK		GENMASK(3, 0)

#define PTN36502_DP_LINK_CTRL_REG			0x06
#define PTN36502_DP_LINK_CTRL_LANES_MASK		GENMASK(3, 2)
#define PTN36502_DP_LINK_CTRL_LANES_2			(2)
#define PTN36502_DP_LINK_CTRL_LANES_4			(3)
#define PTN36502_DP_LINK_CTRL_LINK_RATE_MASK		GENMASK(1, 0)
#define PTN36502_DP_LINK_CTRL_LINK_RATE_5_4GBPS		(2)

/* Registers for lane 0 (0x07) to lane 3 (0x0a) have the same layout */
#define PTN36502_DP_LANE_CTRL_REG(n)			(0x07 + (n))
#define PTN36502_DP_LANE_CTRL_RX_GAIN_MASK		GENMASK(6, 4)
#define PTN36502_DP_LANE_CTRL_RX_GAIN_3DB		(2)
#define PTN36502_DP_LANE_CTRL_TX_SWING_MASK             GENMASK(3, 2)
#define PTN36502_DP_LANE_CTRL_TX_SWING_800MVPPD         (2)
#define PTN36502_DP_LANE_CTRL_PRE_EMPHASIS_MASK		GENMASK(1, 0)
#define PTN36502_DP_LANE_CTRL_PRE_EMPHASIS_3_5DB	(1)

#define PTN36502_MODE_CTRL1_REG				0x0b
#define PTN36502_MODE_CTRL1_PLUG_ORIENT_MASK		GENMASK(5, 5)
#define PTN36502_MODE_CTRL1_PLUG_ORIENT_REVERSE		(1)
#define PTN36502_MODE_CTRL1_AUX_CROSSBAR_MASK		GENMASK(3, 3)
#define PTN36502_MODE_CTRL1_AUX_CROSSBAR_SW_ON		(1)
#define PTN36502_MODE_CTRL1_MODE_MASK			GENMASK(2, 0)
#define PTN36502_MODE_CTRL1_MODE_OFF			(0)
#define PTN36502_MODE_CTRL1_MODE_USB_ONLY		(1)
#define PTN36502_MODE_CTRL1_MODE_USB_DP			(2)
#define PTN36502_MODE_CTRL1_MODE_DP			(3)

#define PTN36502_DEVICE_CTRL_REG			0x0d
#define PTN36502_DEVICE_CTRL_AUX_MONITORING_MASK	GENMASK(7, 7)
#define PTN36502_DEVICE_CTRL_AUX_MONITORING_EN		(1)

struct ptn36502 {
	struct i2c_client *client;
	struct regulator *vdd18_supply;
	struct regmap *regmap;
	struct typec_switch_dev *sw;
	struct typec_retimer *retimer;

	struct typec_switch *typec_switch;

	struct mutex lock; /* protect non-concurrent retimer & switch */

	enum typec_orientation orientation;
	unsigned long mode;
	unsigned int svid;
};

static int ptn36502_set(struct ptn36502 *ptn)
{
	bool reverse = (ptn->orientation == TYPEC_ORIENTATION_REVERSE);
	unsigned int ctrl1_val = 0;
	unsigned int lane_ctrl_val = 0;
	unsigned int link_ctrl_val = 0;

	switch (ptn->mode) {
	case TYPEC_STATE_SAFE:
		/* Deep power saving state */
		regmap_write(ptn->regmap, PTN36502_MODE_CTRL1_REG,
			     FIELD_PREP(PTN36502_MODE_CTRL1_MODE_MASK,
					PTN36502_MODE_CTRL1_MODE_OFF));
		return 0;

	case TYPEC_STATE_USB:
		/*
		 * Normal Orientation (CC1)
		 * A -> USB RX
		 * B -> USB TX
		 * C -> X
		 * D -> X
		 * Flipped Orientation (CC2)
		 * A -> X
		 * B -> X
		 * C -> USB TX
		 * D -> USB RX
		 */

		/* USB 3.1 Gen 1 only */
		ctrl1_val = FIELD_PREP(PTN36502_MODE_CTRL1_MODE_MASK,
				       PTN36502_MODE_CTRL1_MODE_USB_ONLY);
		if (reverse)
			ctrl1_val |= FIELD_PREP(PTN36502_MODE_CTRL1_PLUG_ORIENT_MASK,
						PTN36502_MODE_CTRL1_PLUG_ORIENT_REVERSE);

		regmap_write(ptn->regmap, PTN36502_MODE_CTRL1_REG, ctrl1_val);
		return 0;

	default:
		if (ptn->svid != USB_TYPEC_DP_SID)
			return -EINVAL;

		break;
	}

	/* DP Altmode Setup */

	switch (ptn->mode) {
	case TYPEC_DP_STATE_C:
	case TYPEC_DP_STATE_E:
		/*
		 * Normal Orientation (CC1)
		 * A -> DP3
		 * B -> DP2
		 * C -> DP1
		 * D -> DP0
		 * Flipped Orientation (CC2)
		 * A -> DP0
		 * B -> DP1
		 * C -> DP2
		 * D -> DP3
		 */

		/* 4-lane DP */
		ctrl1_val |= FIELD_PREP(PTN36502_MODE_CTRL1_MODE_MASK,
					PTN36502_MODE_CTRL1_MODE_DP);
		link_ctrl_val |= FIELD_PREP(PTN36502_DP_LINK_CTRL_LANES_MASK,
					    PTN36502_DP_LINK_CTRL_LANES_4);
		break;

	case TYPEC_DP_STATE_D:
	case TYPEC_DP_STATE_F: /* State F is deprecated */
		/*
		 * Normal Orientation (CC1)
		 * A -> USB RX
		 * B -> USB TX
		 * C -> DP1
		 * D -> DP0
		 * Flipped Orientation (CC2)
		 * A -> DP0
		 * B -> DP1
		 * C -> USB TX
		 * D -> USB RX
		 */

		/* USB 3.1 Gen 1 and 2-lane DP */
		ctrl1_val |= FIELD_PREP(PTN36502_MODE_CTRL1_MODE_MASK,
					PTN36502_MODE_CTRL1_MODE_USB_DP);
		link_ctrl_val |= FIELD_PREP(PTN36502_DP_LINK_CTRL_LANES_MASK,
					    PTN36502_DP_LINK_CTRL_LANES_2);
		break;

	default:
		return -EOPNOTSUPP;
	}

	/* Enable AUX monitoring */
	regmap_write(ptn->regmap, PTN36502_DEVICE_CTRL_REG,
		     FIELD_PREP(PTN36502_DEVICE_CTRL_AUX_MONITORING_MASK,
				PTN36502_DEVICE_CTRL_AUX_MONITORING_EN));

	/* Enable AUX switch path */
	ctrl1_val |= FIELD_PREP(PTN36502_MODE_CTRL1_AUX_CROSSBAR_MASK,
				PTN36502_MODE_CTRL1_AUX_CROSSBAR_SW_ON);
	if (reverse)
		ctrl1_val |= FIELD_PREP(PTN36502_MODE_CTRL1_PLUG_ORIENT_MASK,
					PTN36502_MODE_CTRL1_PLUG_ORIENT_REVERSE);
	regmap_write(ptn->regmap, PTN36502_MODE_CTRL1_REG, ctrl1_val);

	/* DP Link rate: 5.4 Gbps (HBR2) */
	link_ctrl_val |= FIELD_PREP(PTN36502_DP_LINK_CTRL_LINK_RATE_MASK,
				    PTN36502_DP_LINK_CTRL_LINK_RATE_5_4GBPS);
	regmap_write(ptn->regmap, PTN36502_DP_LINK_CTRL_REG, link_ctrl_val);

	/*
	 * For all lanes:
	 * - Rx equivalization gain: 3 dB
	 * - TX output swing control: 800 mVppd
	 * - Pre-emphasis control: 3.5 dB
	 */
	lane_ctrl_val = FIELD_PREP(PTN36502_DP_LANE_CTRL_RX_GAIN_MASK,
				   PTN36502_DP_LANE_CTRL_RX_GAIN_3DB) |
			FIELD_PREP(PTN36502_DP_LANE_CTRL_TX_SWING_MASK,
				   PTN36502_DP_LANE_CTRL_TX_SWING_800MVPPD) |
			FIELD_PREP(PTN36502_DP_LANE_CTRL_PRE_EMPHASIS_MASK,
				   PTN36502_DP_LANE_CTRL_PRE_EMPHASIS_3_5DB);
	regmap_write(ptn->regmap, PTN36502_DP_LANE_CTRL_REG(0), lane_ctrl_val);
	regmap_write(ptn->regmap, PTN36502_DP_LANE_CTRL_REG(1), lane_ctrl_val);
	regmap_write(ptn->regmap, PTN36502_DP_LANE_CTRL_REG(2), lane_ctrl_val);
	regmap_write(ptn->regmap, PTN36502_DP_LANE_CTRL_REG(3), lane_ctrl_val);

	return 0;
}

static int ptn36502_sw_set(struct typec_switch_dev *sw, enum typec_orientation orientation)
{
	struct ptn36502 *ptn = typec_switch_get_drvdata(sw);
	int ret;

	ret = typec_switch_set(ptn->typec_switch, orientation);
	if (ret)
		return ret;

	mutex_lock(&ptn->lock);

	if (ptn->orientation != orientation) {
		ptn->orientation = orientation;

		ret = ptn36502_set(ptn);
	}

	mutex_unlock(&ptn->lock);

	return ret;
}

static int ptn36502_retimer_set(struct typec_retimer *retimer, struct typec_retimer_state *state)
{
	struct ptn36502 *ptn = typec_retimer_get_drvdata(retimer);
	int ret = 0;

	mutex_lock(&ptn->lock);

	if (ptn->mode != state->mode) {
		ptn->mode = state->mode;

		if (state->alt)
			ptn->svid = state->alt->svid;
		else
			ptn->svid = 0; // No SVID

		ret = ptn36502_set(ptn);
	}

	mutex_unlock(&ptn->lock);

	return ret;
}

static int ptn36502_detect(struct ptn36502 *ptn)
{
	struct device *dev = &ptn->client->dev;
	unsigned int reg_val;
	int ret;

	ret = regmap_read(ptn->regmap, PTN36502_CHIP_ID_REG,
			  &reg_val);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read chip ID\n");

	if (reg_val != PTN36502_CHIP_ID)
		return dev_err_probe(dev, -ENODEV, "Unexpected chip ID: %x\n", reg_val);

	ret = regmap_read(ptn->regmap, PTN36502_CHIP_REVISION_REG,
			  &reg_val);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to read chip revision\n");

	dev_dbg(dev, "Chip revision: base layer version %lx, metal layer version %lx\n",
		FIELD_GET(PTN36502_CHIP_REVISION_BASE_MASK, reg_val),
		FIELD_GET(PTN36502_CHIP_REVISION_METAL_MASK, reg_val));

	return 0;
}

static const struct regmap_config ptn36502_regmap = {
	.max_register = 0x0d,
	.reg_bits = 8,
	.val_bits = 8,
};

static int ptn36502_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct typec_switch_desc sw_desc = { };
	struct typec_retimer_desc retimer_desc = { };
	struct ptn36502 *ptn;
	int ret;

	ptn = devm_kzalloc(dev, sizeof(*ptn), GFP_KERNEL);
	if (!ptn)
		return -ENOMEM;

	ptn->client = client;

	ptn->regmap = devm_regmap_init_i2c(client, &ptn36502_regmap);
	if (IS_ERR(ptn->regmap)) {
		dev_err(&client->dev, "Failed to allocate register map\n");
		return PTR_ERR(ptn->regmap);
	}

	ptn->mode = TYPEC_STATE_SAFE;
	ptn->orientation = TYPEC_ORIENTATION_NONE;

	mutex_init(&ptn->lock);

	ptn->vdd18_supply = devm_regulator_get_optional(dev, "vdd18");
	if (IS_ERR(ptn->vdd18_supply))
		return PTR_ERR(ptn->vdd18_supply);

	ptn->typec_switch = fwnode_typec_switch_get(dev->fwnode);
	if (IS_ERR(ptn->typec_switch))
		return dev_err_probe(dev, PTR_ERR(ptn->typec_switch),
				     "Failed to acquire orientation-switch\n");

	ret = regulator_enable(ptn->vdd18_supply);
	if (ret) {
		ret = dev_err_probe(dev, ret, "Failed to enable vdd18\n");
		goto err_switch_put;
	}

	ret = ptn36502_detect(ptn);
	if (ret)
		goto err_disable_regulator;

	ret = drm_aux_bridge_register(dev);
	if (ret)
		goto err_disable_regulator;

	sw_desc.drvdata = ptn;
	sw_desc.fwnode = dev->fwnode;
	sw_desc.set = ptn36502_sw_set;

	ptn->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(ptn->sw)) {
		ret = dev_err_probe(dev, PTR_ERR(ptn->sw),
				    "Failed to register typec switch\n");
		goto err_disable_regulator;
	}

	retimer_desc.drvdata = ptn;
	retimer_desc.fwnode = dev->fwnode;
	retimer_desc.set = ptn36502_retimer_set;

	ptn->retimer = typec_retimer_register(dev, &retimer_desc);
	if (IS_ERR(ptn->retimer)) {
		ret = dev_err_probe(dev, PTR_ERR(ptn->retimer),
				    "Failed to register typec retimer\n");
		goto err_switch_unregister;
	}

	return 0;

err_switch_unregister:
	typec_switch_unregister(ptn->sw);

err_disable_regulator:
	regulator_disable(ptn->vdd18_supply);

err_switch_put:
	typec_switch_put(ptn->typec_switch);

	return ret;
}

static void ptn36502_remove(struct i2c_client *client)
{
	struct ptn36502 *ptn = i2c_get_clientdata(client);

	typec_retimer_unregister(ptn->retimer);
	typec_switch_unregister(ptn->sw);

	regulator_disable(ptn->vdd18_supply);

	typec_switch_put(ptn->typec_switch);
}

static const struct i2c_device_id ptn36502_table[] = {
	{ "ptn36502" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ptn36502_table);

static const struct of_device_id ptn36502_of_table[] = {
	{ .compatible = "nxp,ptn36502" },
	{ }
};
MODULE_DEVICE_TABLE(of, ptn36502_of_table);

static struct i2c_driver ptn36502_driver = {
	.driver = {
		.name = "ptn36502",
		.of_match_table = ptn36502_of_table,
	},
	.probe		= ptn36502_probe,
	.remove		= ptn36502_remove,
	.id_table	= ptn36502_table,
};
module_i2c_driver(ptn36502_driver);

MODULE_AUTHOR("Luca Weiss <luca.weiss@fairphone.com>");
MODULE_DESCRIPTION("NXP PTN36502 Type-C driver");
MODULE_LICENSE("GPL");
