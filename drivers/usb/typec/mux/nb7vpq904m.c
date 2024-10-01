// SPDX-License-Identifier: GPL-2.0+
/*
 * OnSemi NB7VPQ904M Type-C driver
 *
 * Copyright (C) 2023 Dmitry Baryshkov <dmitry.baryshkov@linaro.org>
 */
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/bitfield.h>
#include <linux/of_graph.h>
#include <drm/bridge/aux-bridge.h>
#include <linux/usb/typec_dp.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_retimer.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#define NB7_CHNA		0
#define NB7_CHNB		1
#define NB7_CHNC		2
#define NB7_CHND		3
#define NB7_IS_CHAN_AD(channel) (channel == NB7_CHNA || channel == NB7_CHND)

#define GEN_DEV_SET_REG			0x00

#define GEN_DEV_SET_CHIP_EN		BIT(0)
#define GEN_DEV_SET_CHNA_EN		BIT(4)
#define GEN_DEV_SET_CHNB_EN		BIT(5)
#define GEN_DEV_SET_CHNC_EN		BIT(6)
#define GEN_DEV_SET_CHND_EN		BIT(7)

#define GEN_DEV_SET_OP_MODE_MASK	GENMASK(3, 1)

#define GEN_DEV_SET_OP_MODE_DP_CC2	0
#define GEN_DEV_SET_OP_MODE_DP_CC1	1
#define GEN_DEV_SET_OP_MODE_DP_4LANE	2
#define GEN_DEV_SET_OP_MODE_USB		5

#define EQ_SETTING_REG_BASE		0x01
#define EQ_SETTING_REG(n)		(EQ_SETTING_REG_BASE + (n) * 2)
#define EQ_SETTING_MASK			GENMASK(3, 1)

#define OUTPUT_COMPRESSION_AND_POL_REG_BASE	0x02
#define OUTPUT_COMPRESSION_AND_POL_REG(n)	(OUTPUT_COMPRESSION_AND_POL_REG_BASE + (n) * 2)
#define OUTPUT_COMPRESSION_MASK		GENMASK(2, 1)

#define FLAT_GAIN_REG_BASE		0x18
#define FLAT_GAIN_REG(n)		(FLAT_GAIN_REG_BASE + (n) * 2)
#define FLAT_GAIN_MASK			GENMASK(1, 0)

#define LOSS_MATCH_REG_BASE		0x19
#define LOSS_MATCH_REG(n)		(LOSS_MATCH_REG_BASE + (n) * 2)
#define LOSS_MATCH_MASK			GENMASK(1, 0)

#define AUX_CC_REG			0x09

#define CHIP_VERSION_REG		0x17

struct nb7vpq904m {
	struct i2c_client *client;
	struct gpio_desc *enable_gpio;
	struct regulator *vcc_supply;
	struct regmap *regmap;
	struct typec_switch_dev *sw;
	struct typec_retimer *retimer;

	bool swap_data_lanes;
	struct typec_switch *typec_switch;
	struct typec_mux *typec_mux;

	struct mutex lock; /* protect non-concurrent retimer & switch */

	enum typec_orientation orientation;
	unsigned long mode;
	unsigned int svid;
};

static void nb7vpq904m_set_channel(struct nb7vpq904m *nb7, unsigned int channel, bool dp)
{
	u8 eq, out_comp, flat_gain, loss_match;

	if (dp) {
		eq = NB7_IS_CHAN_AD(channel) ? 0x6 : 0x4;
		out_comp = 0x3;
		flat_gain = NB7_IS_CHAN_AD(channel) ? 0x2 : 0x1;
		loss_match = 0x3;
	} else {
		eq = 0x4;
		out_comp = 0x3;
		flat_gain = NB7_IS_CHAN_AD(channel) ? 0x3 : 0x1;
		loss_match = NB7_IS_CHAN_AD(channel) ? 0x1 : 0x3;
	}

	regmap_update_bits(nb7->regmap, EQ_SETTING_REG(channel),
			   EQ_SETTING_MASK, FIELD_PREP(EQ_SETTING_MASK, eq));
	regmap_update_bits(nb7->regmap, OUTPUT_COMPRESSION_AND_POL_REG(channel),
			   OUTPUT_COMPRESSION_MASK, FIELD_PREP(OUTPUT_COMPRESSION_MASK, out_comp));
	regmap_update_bits(nb7->regmap, FLAT_GAIN_REG(channel),
			   FLAT_GAIN_MASK, FIELD_PREP(FLAT_GAIN_MASK, flat_gain));
	regmap_update_bits(nb7->regmap, LOSS_MATCH_REG(channel),
			   LOSS_MATCH_MASK, FIELD_PREP(LOSS_MATCH_MASK, loss_match));
}

static int nb7vpq904m_set(struct nb7vpq904m *nb7)
{
	bool reverse = (nb7->orientation == TYPEC_ORIENTATION_REVERSE);

	switch (nb7->mode) {
	case TYPEC_STATE_SAFE:
		regmap_write(nb7->regmap, GEN_DEV_SET_REG,
			     GEN_DEV_SET_CHIP_EN |
			     GEN_DEV_SET_CHNA_EN |
			     GEN_DEV_SET_CHNB_EN |
			     GEN_DEV_SET_CHNC_EN |
			     GEN_DEV_SET_CHND_EN |
			     FIELD_PREP(GEN_DEV_SET_OP_MODE_MASK,
					GEN_DEV_SET_OP_MODE_USB));
		nb7vpq904m_set_channel(nb7, NB7_CHNA, false);
		nb7vpq904m_set_channel(nb7, NB7_CHNB, false);
		nb7vpq904m_set_channel(nb7, NB7_CHNC, false);
		nb7vpq904m_set_channel(nb7, NB7_CHND, false);
		regmap_write(nb7->regmap, AUX_CC_REG, 0x2);

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
		 *
		 * Reversed if data lanes are swapped
		 */
		if (reverse ^ nb7->swap_data_lanes) {
			regmap_write(nb7->regmap, GEN_DEV_SET_REG,
				     GEN_DEV_SET_CHIP_EN |
				     GEN_DEV_SET_CHNA_EN |
				     GEN_DEV_SET_CHNB_EN |
				     FIELD_PREP(GEN_DEV_SET_OP_MODE_MASK,
						GEN_DEV_SET_OP_MODE_USB));
			nb7vpq904m_set_channel(nb7, NB7_CHNA, false);
			nb7vpq904m_set_channel(nb7, NB7_CHNB, false);
		} else {
			regmap_write(nb7->regmap, GEN_DEV_SET_REG,
				     GEN_DEV_SET_CHIP_EN |
				     GEN_DEV_SET_CHNC_EN |
				     GEN_DEV_SET_CHND_EN |
				     FIELD_PREP(GEN_DEV_SET_OP_MODE_MASK,
						GEN_DEV_SET_OP_MODE_USB));
			nb7vpq904m_set_channel(nb7, NB7_CHNC, false);
			nb7vpq904m_set_channel(nb7, NB7_CHND, false);
		}
		regmap_write(nb7->regmap, AUX_CC_REG, 0x2);

		return 0;

	default:
		if (nb7->svid != USB_TYPEC_DP_SID)
			return -EINVAL;

		break;
	}

	/* DP Altmode Setup */

	regmap_write(nb7->regmap, AUX_CC_REG, reverse ? 0x1 : 0x0);

	switch (nb7->mode) {
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
		regmap_write(nb7->regmap, GEN_DEV_SET_REG,
			     GEN_DEV_SET_CHIP_EN |
			     GEN_DEV_SET_CHNA_EN |
			     GEN_DEV_SET_CHNB_EN |
			     GEN_DEV_SET_CHNC_EN |
			     GEN_DEV_SET_CHND_EN |
			     FIELD_PREP(GEN_DEV_SET_OP_MODE_MASK,
					GEN_DEV_SET_OP_MODE_DP_4LANE));
		nb7vpq904m_set_channel(nb7, NB7_CHNA, true);
		nb7vpq904m_set_channel(nb7, NB7_CHNB, true);
		nb7vpq904m_set_channel(nb7, NB7_CHNC, true);
		nb7vpq904m_set_channel(nb7, NB7_CHND, true);
		break;

	case TYPEC_DP_STATE_D:
	case TYPEC_DP_STATE_F:
		regmap_write(nb7->regmap, GEN_DEV_SET_REG,
			     GEN_DEV_SET_CHIP_EN |
			     GEN_DEV_SET_CHNA_EN |
			     GEN_DEV_SET_CHNB_EN |
			     GEN_DEV_SET_CHNC_EN |
			     GEN_DEV_SET_CHND_EN |
			     FIELD_PREP(GEN_DEV_SET_OP_MODE_MASK,
					reverse ^ nb7->swap_data_lanes ?
						GEN_DEV_SET_OP_MODE_DP_CC2
						: GEN_DEV_SET_OP_MODE_DP_CC1));

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
		 *
		 * Reversed if data lanes are swapped
		 */
		if (nb7->swap_data_lanes) {
			nb7vpq904m_set_channel(nb7, NB7_CHNA, !reverse);
			nb7vpq904m_set_channel(nb7, NB7_CHNB, !reverse);
			nb7vpq904m_set_channel(nb7, NB7_CHNC, reverse);
			nb7vpq904m_set_channel(nb7, NB7_CHND, reverse);
		} else {
			nb7vpq904m_set_channel(nb7, NB7_CHNA, reverse);
			nb7vpq904m_set_channel(nb7, NB7_CHNB, reverse);
			nb7vpq904m_set_channel(nb7, NB7_CHNC, !reverse);
			nb7vpq904m_set_channel(nb7, NB7_CHND, !reverse);
		}
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nb7vpq904m_sw_set(struct typec_switch_dev *sw, enum typec_orientation orientation)
{
	struct nb7vpq904m *nb7 = typec_switch_get_drvdata(sw);
	int ret;

	ret = typec_switch_set(nb7->typec_switch, orientation);
	if (ret)
		return ret;

	mutex_lock(&nb7->lock);

	if (nb7->orientation != orientation) {
		nb7->orientation = orientation;

		ret = nb7vpq904m_set(nb7);
	}

	mutex_unlock(&nb7->lock);

	return ret;
}

static int nb7vpq904m_retimer_set(struct typec_retimer *retimer, struct typec_retimer_state *state)
{
	struct nb7vpq904m *nb7 = typec_retimer_get_drvdata(retimer);
	struct typec_mux_state mux_state;
	int ret = 0;

	mutex_lock(&nb7->lock);

	if (nb7->mode != state->mode) {
		nb7->mode = state->mode;

		if (state->alt)
			nb7->svid = state->alt->svid;
		else
			nb7->svid = 0; // No SVID

		ret = nb7vpq904m_set(nb7);
	}

	mutex_unlock(&nb7->lock);

	if (ret)
		return ret;

	mux_state.alt = state->alt;
	mux_state.data = state->data;
	mux_state.mode = state->mode;

	return typec_mux_set(nb7->typec_mux, &mux_state);
}

static const struct regmap_config nb7_regmap = {
	.max_register = 0x1f,
	.reg_bits = 8,
	.val_bits = 8,
};

enum {
	NORMAL_LANE_MAPPING,
	INVERT_LANE_MAPPING,
};

#define DATA_LANES_COUNT	4

static const int supported_data_lane_mapping[][DATA_LANES_COUNT] = {
	[NORMAL_LANE_MAPPING] = { 0, 1, 2, 3 },
	[INVERT_LANE_MAPPING] = { 3, 2, 1, 0 },
};

static int nb7vpq904m_parse_data_lanes_mapping(struct nb7vpq904m *nb7)
{
	struct device_node *ep;
	u32 data_lanes[4];
	int ret, i, j;

	ep = of_graph_get_endpoint_by_regs(nb7->client->dev.of_node, 1, 0);

	if (!ep)
		return 0;


	ret = of_property_count_u32_elems(ep, "data-lanes");
	if (ret == -EINVAL)
		/* Property isn't here, consider default mapping */
		goto out_done;
	if (ret < 0)
		goto out_error;

	if (ret != DATA_LANES_COUNT) {
		dev_err(&nb7->client->dev, "expected 4 data lanes\n");
		ret = -EINVAL;
		goto out_error;
	}

	ret = of_property_read_u32_array(ep, "data-lanes", data_lanes, DATA_LANES_COUNT);
	if (ret)
		goto out_error;

	for (i = 0; i < ARRAY_SIZE(supported_data_lane_mapping); i++) {
		for (j = 0; j < DATA_LANES_COUNT; j++) {
			if (data_lanes[j] != supported_data_lane_mapping[i][j])
				break;
		}

		if (j == DATA_LANES_COUNT)
			break;
	}

	switch (i) {
	case NORMAL_LANE_MAPPING:
		break;
	case INVERT_LANE_MAPPING:
		nb7->swap_data_lanes = true;
		dev_info(&nb7->client->dev, "using inverted data lanes mapping\n");
		break;
	default:
		dev_err(&nb7->client->dev, "invalid data lanes mapping\n");
		ret = -EINVAL;
		goto out_error;
	}

out_done:
	ret = 0;

out_error:
	of_node_put(ep);

	return ret;
}

static int nb7vpq904m_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct typec_switch_desc sw_desc = { };
	struct typec_retimer_desc retimer_desc = { };
	struct nb7vpq904m *nb7;
	int ret;

	nb7 = devm_kzalloc(dev, sizeof(*nb7), GFP_KERNEL);
	if (!nb7)
		return -ENOMEM;

	nb7->client = client;

	nb7->regmap = devm_regmap_init_i2c(client, &nb7_regmap);
	if (IS_ERR(nb7->regmap)) {
		dev_err(&client->dev, "Failed to allocate register map\n");
		return PTR_ERR(nb7->regmap);
	}

	nb7->mode = TYPEC_STATE_SAFE;
	nb7->orientation = TYPEC_ORIENTATION_NONE;

	mutex_init(&nb7->lock);

	nb7->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(nb7->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(nb7->enable_gpio),
				     "unable to acquire enable gpio\n");

	nb7->vcc_supply = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(nb7->vcc_supply))
		return PTR_ERR(nb7->vcc_supply);

	nb7->typec_switch = fwnode_typec_switch_get(dev->fwnode);
	if (IS_ERR(nb7->typec_switch))
		return dev_err_probe(dev, PTR_ERR(nb7->typec_switch),
				     "failed to acquire orientation-switch\n");

	nb7->typec_mux = fwnode_typec_mux_get(dev->fwnode);
	if (IS_ERR(nb7->typec_mux)) {
		ret = dev_err_probe(dev, PTR_ERR(nb7->typec_mux),
				    "Failed to acquire mode-switch\n");
		goto err_switch_put;
	}

	ret = nb7vpq904m_parse_data_lanes_mapping(nb7);
	if (ret)
		goto err_mux_put;

	ret = regulator_enable(nb7->vcc_supply);
	if (ret)
		dev_warn(dev, "Failed to enable vcc: %d\n", ret);

	gpiod_set_value(nb7->enable_gpio, 1);

	ret = drm_aux_bridge_register(dev);
	if (ret)
		goto err_disable_gpio;

	sw_desc.drvdata = nb7;
	sw_desc.fwnode = dev->fwnode;
	sw_desc.set = nb7vpq904m_sw_set;

	nb7->sw = typec_switch_register(dev, &sw_desc);
	if (IS_ERR(nb7->sw)) {
		ret = dev_err_probe(dev, PTR_ERR(nb7->sw),
				    "Error registering typec switch\n");
		goto err_disable_gpio;
	}

	retimer_desc.drvdata = nb7;
	retimer_desc.fwnode = dev->fwnode;
	retimer_desc.set = nb7vpq904m_retimer_set;

	nb7->retimer = typec_retimer_register(dev, &retimer_desc);
	if (IS_ERR(nb7->retimer)) {
		ret = dev_err_probe(dev, PTR_ERR(nb7->retimer),
				    "Error registering typec retimer\n");
		goto err_switch_unregister;
	}

	return 0;

err_switch_unregister:
	typec_switch_unregister(nb7->sw);

err_disable_gpio:
	gpiod_set_value(nb7->enable_gpio, 0);
	regulator_disable(nb7->vcc_supply);

err_mux_put:
	typec_mux_put(nb7->typec_mux);

err_switch_put:
	typec_switch_put(nb7->typec_switch);

	return ret;
}

static void nb7vpq904m_remove(struct i2c_client *client)
{
	struct nb7vpq904m *nb7 = i2c_get_clientdata(client);

	typec_retimer_unregister(nb7->retimer);
	typec_switch_unregister(nb7->sw);

	gpiod_set_value(nb7->enable_gpio, 0);

	regulator_disable(nb7->vcc_supply);

	typec_mux_put(nb7->typec_mux);
	typec_switch_put(nb7->typec_switch);
}

static const struct i2c_device_id nb7vpq904m_table[] = {
	{ "nb7vpq904m" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, nb7vpq904m_table);

static const struct of_device_id nb7vpq904m_of_table[] = {
	{ .compatible = "onnn,nb7vpq904m" },
	{ }
};
MODULE_DEVICE_TABLE(of, nb7vpq904m_of_table);

static struct i2c_driver nb7vpq904m_driver = {
	.driver = {
		.name = "nb7vpq904m",
		.of_match_table = nb7vpq904m_of_table,
	},
	.probe		= nb7vpq904m_probe,
	.remove		= nb7vpq904m_remove,
	.id_table	= nb7vpq904m_table,
};

module_i2c_driver(nb7vpq904m_driver);

MODULE_AUTHOR("Dmitry Baryshkov <dmitry.baryshkov@linaro.org>");
MODULE_DESCRIPTION("OnSemi NB7VPQ904M Type-C driver");
MODULE_LICENSE("GPL");
