// SPDX-License-Identifier: GPL-2.0
/*
 * phy-can-transceiver.c - phy driver for CAN transceivers
 *
 * Copyright (C) 2021 Texas Instruments Incorporated - https://www.ti.com
 *
 */
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/mux/consumer.h>

struct can_transceiver_data {
	u32 flags;
#define CAN_TRANSCEIVER_STB_PRESENT	BIT(0)
#define CAN_TRANSCEIVER_EN_PRESENT	BIT(1)
#define CAN_TRANSCEIVER_DUAL_CH		BIT(2)
#define CAN_TRANSCEIVER_SILENT_PRESENT	BIT(3)
};

struct can_transceiver_phy {
	struct phy *generic_phy;
	struct gpio_desc *silent_gpio;
	struct gpio_desc *standby_gpio;
	struct gpio_desc *enable_gpio;
	struct can_transceiver_priv *priv;
};

struct can_transceiver_priv {
	struct mux_state *mux_state;
	int num_ch;
	struct can_transceiver_phy can_transceiver_phy[] __counted_by(num_ch);
};

/* Power on function */
static int can_transceiver_phy_power_on(struct phy *phy)
{
	struct can_transceiver_phy *can_transceiver_phy = phy_get_drvdata(phy);
	struct can_transceiver_priv *priv = can_transceiver_phy->priv;
	int ret;

	if (priv->mux_state) {
		ret = mux_state_select(priv->mux_state);
		if (ret) {
			dev_err(&phy->dev, "Failed to select CAN mux: %d\n", ret);
			return ret;
		}
	}
	gpiod_set_value_cansleep(can_transceiver_phy->silent_gpio, 0);
	gpiod_set_value_cansleep(can_transceiver_phy->standby_gpio, 0);
	gpiod_set_value_cansleep(can_transceiver_phy->enable_gpio, 1);

	return 0;
}

/* Power off function */
static int can_transceiver_phy_power_off(struct phy *phy)
{
	struct can_transceiver_phy *can_transceiver_phy = phy_get_drvdata(phy);
	struct can_transceiver_priv *priv = can_transceiver_phy->priv;

	gpiod_set_value_cansleep(can_transceiver_phy->silent_gpio, 1);
	gpiod_set_value_cansleep(can_transceiver_phy->standby_gpio, 1);
	gpiod_set_value_cansleep(can_transceiver_phy->enable_gpio, 0);
	if (priv->mux_state)
		mux_state_deselect(priv->mux_state);

	return 0;
}

static const struct phy_ops can_transceiver_phy_ops = {
	.power_on	= can_transceiver_phy_power_on,
	.power_off	= can_transceiver_phy_power_off,
	.owner		= THIS_MODULE,
};

static const struct can_transceiver_data tcan1042_drvdata = {
	.flags = CAN_TRANSCEIVER_STB_PRESENT,
};

static const struct can_transceiver_data tcan1043_drvdata = {
	.flags = CAN_TRANSCEIVER_STB_PRESENT | CAN_TRANSCEIVER_EN_PRESENT,
};

static const struct can_transceiver_data tja1048_drvdata = {
	.flags = CAN_TRANSCEIVER_STB_PRESENT | CAN_TRANSCEIVER_DUAL_CH,
};

static const struct can_transceiver_data tja1051_drvdata = {
	.flags = CAN_TRANSCEIVER_SILENT_PRESENT | CAN_TRANSCEIVER_EN_PRESENT,
};

static const struct can_transceiver_data tja1057_drvdata = {
	.flags = CAN_TRANSCEIVER_SILENT_PRESENT,
};

static const struct of_device_id can_transceiver_phy_ids[] = {
	{
		.compatible = "ti,tcan1042",
		.data = &tcan1042_drvdata
	},
	{
		.compatible = "ti,tcan1043",
		.data = &tcan1043_drvdata
	},
	{
		.compatible = "nxp,tja1048",
		.data = &tja1048_drvdata
	},
	{
		.compatible = "nxp,tja1051",
		.data = &tja1051_drvdata
	},
	{
		.compatible = "nxp,tja1057",
		.data = &tja1057_drvdata
	},
	{
		.compatible = "nxp,tjr1443",
		.data = &tcan1043_drvdata
	},
	{ }
};
MODULE_DEVICE_TABLE(of, can_transceiver_phy_ids);

/* Temporary wrapper until the multiplexer subsystem supports optional muxes */
static inline struct mux_state *
devm_mux_state_get_optional(struct device *dev, const char *mux_name)
{
	if (!of_property_present(dev->of_node, "mux-states"))
		return NULL;

	return devm_mux_state_get(dev, mux_name);
}

static struct phy *can_transceiver_phy_xlate(struct device *dev,
					     const struct of_phandle_args *args)
{
	struct can_transceiver_priv *priv = dev_get_drvdata(dev);
	u32 idx;

	if (priv->num_ch == 1)
		return priv->can_transceiver_phy[0].generic_phy;

	if (args->args_count != 1)
		return ERR_PTR(-EINVAL);

	idx = args->args[0];
	if (idx >= priv->num_ch)
		return ERR_PTR(-EINVAL);

	return priv->can_transceiver_phy[idx].generic_phy;
}

static int can_transceiver_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct can_transceiver_phy *can_transceiver_phy;
	struct can_transceiver_priv *priv;
	const struct can_transceiver_data *drvdata;
	const struct of_device_id *match;
	struct phy *phy;
	struct gpio_desc *silent_gpio;
	struct gpio_desc *standby_gpio;
	struct gpio_desc *enable_gpio;
	struct mux_state *mux_state;
	u32 max_bitrate = 0;
	int err, i, num_ch = 1;

	match = of_match_node(can_transceiver_phy_ids, pdev->dev.of_node);
	drvdata = match->data;
	if (drvdata->flags & CAN_TRANSCEIVER_DUAL_CH)
		num_ch = 2;

	priv = devm_kzalloc(dev, struct_size(priv, can_transceiver_phy, num_ch), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->num_ch = num_ch;
	platform_set_drvdata(pdev, priv);

	mux_state = devm_mux_state_get_optional(dev, NULL);
	if (IS_ERR(mux_state))
		return PTR_ERR(mux_state);

	priv->mux_state = mux_state;

	err = device_property_read_u32(dev, "max-bitrate", &max_bitrate);
	if ((err != -EINVAL) && !max_bitrate)
		dev_warn(dev, "Invalid value for transceiver max bitrate. Ignoring bitrate limit\n");

	for (i = 0; i < num_ch; i++) {
		can_transceiver_phy = &priv->can_transceiver_phy[i];
		can_transceiver_phy->priv = priv;

		phy = devm_phy_create(dev, dev->of_node, &can_transceiver_phy_ops);
		if (IS_ERR(phy)) {
			dev_err(dev, "failed to create can transceiver phy\n");
			return PTR_ERR(phy);
		}

		phy->attrs.max_link_rate = max_bitrate;

		can_transceiver_phy->generic_phy = phy;
		can_transceiver_phy->priv = priv;

		if (drvdata->flags & CAN_TRANSCEIVER_STB_PRESENT) {
			standby_gpio = devm_gpiod_get_index_optional(dev, "standby", i,
								     GPIOD_OUT_HIGH);
			if (IS_ERR(standby_gpio))
				return PTR_ERR(standby_gpio);
			can_transceiver_phy->standby_gpio = standby_gpio;
		}

		if (drvdata->flags & CAN_TRANSCEIVER_EN_PRESENT) {
			enable_gpio = devm_gpiod_get_index_optional(dev, "enable", i,
								    GPIOD_OUT_LOW);
			if (IS_ERR(enable_gpio))
				return PTR_ERR(enable_gpio);
			can_transceiver_phy->enable_gpio = enable_gpio;
		}

		if (drvdata->flags & CAN_TRANSCEIVER_SILENT_PRESENT) {
			silent_gpio = devm_gpiod_get_index_optional(dev, "silent", i,
								    GPIOD_OUT_LOW);
			if (IS_ERR(silent_gpio))
				return PTR_ERR(silent_gpio);
			can_transceiver_phy->silent_gpio = silent_gpio;
		}

		phy_set_drvdata(can_transceiver_phy->generic_phy, can_transceiver_phy);

	}

	phy_provider = devm_of_phy_provider_register(dev, can_transceiver_phy_xlate);

	return PTR_ERR_OR_ZERO(phy_provider);
}

static struct platform_driver can_transceiver_phy_driver = {
	.probe = can_transceiver_phy_probe,
	.driver = {
		.name = "can-transceiver-phy",
		.of_match_table = can_transceiver_phy_ids,
	},
};

module_platform_driver(can_transceiver_phy_driver);

MODULE_AUTHOR("Faiz Abbas <faiz_abbas@ti.com>");
MODULE_AUTHOR("Aswath Govindraju <a-govindraju@ti.com>");
MODULE_DESCRIPTION("CAN TRANSCEIVER PHY driver");
MODULE_LICENSE("GPL v2");
