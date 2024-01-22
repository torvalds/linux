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
};

struct can_transceiver_phy {
	struct phy *generic_phy;
	struct gpio_desc *standby_gpio;
	struct gpio_desc *enable_gpio;
	struct mux_state *mux_state;
};

/* Power on function */
static int can_transceiver_phy_power_on(struct phy *phy)
{
	struct can_transceiver_phy *can_transceiver_phy = phy_get_drvdata(phy);
	int ret;

	if (can_transceiver_phy->mux_state) {
		ret = mux_state_select(can_transceiver_phy->mux_state);
		if (ret) {
			dev_err(&phy->dev, "Failed to select CAN mux: %d\n", ret);
			return ret;
		}
	}
	if (can_transceiver_phy->standby_gpio)
		gpiod_set_value_cansleep(can_transceiver_phy->standby_gpio, 0);
	if (can_transceiver_phy->enable_gpio)
		gpiod_set_value_cansleep(can_transceiver_phy->enable_gpio, 1);

	return 0;
}

/* Power off function */
static int can_transceiver_phy_power_off(struct phy *phy)
{
	struct can_transceiver_phy *can_transceiver_phy = phy_get_drvdata(phy);

	if (can_transceiver_phy->standby_gpio)
		gpiod_set_value_cansleep(can_transceiver_phy->standby_gpio, 1);
	if (can_transceiver_phy->enable_gpio)
		gpiod_set_value_cansleep(can_transceiver_phy->enable_gpio, 0);
	if (can_transceiver_phy->mux_state)
		mux_state_deselect(can_transceiver_phy->mux_state);

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
		.compatible = "nxp,tjr1443",
		.data = &tcan1043_drvdata
	},
	{ }
};
MODULE_DEVICE_TABLE(of, can_transceiver_phy_ids);

static int can_transceiver_phy_probe(struct platform_device *pdev)
{
	struct phy_provider *phy_provider;
	struct device *dev = &pdev->dev;
	struct can_transceiver_phy *can_transceiver_phy;
	const struct can_transceiver_data *drvdata;
	const struct of_device_id *match;
	struct phy *phy;
	struct gpio_desc *standby_gpio;
	struct gpio_desc *enable_gpio;
	u32 max_bitrate = 0;
	int err;

	can_transceiver_phy = devm_kzalloc(dev, sizeof(struct can_transceiver_phy), GFP_KERNEL);
	if (!can_transceiver_phy)
		return -ENOMEM;

	match = of_match_node(can_transceiver_phy_ids, pdev->dev.of_node);
	drvdata = match->data;

	if (of_property_read_bool(dev->of_node, "mux-states")) {
		struct mux_state *mux_state;

		mux_state = devm_mux_state_get(dev, NULL);
		if (IS_ERR(mux_state))
			return dev_err_probe(&pdev->dev, PTR_ERR(mux_state),
					     "failed to get mux\n");
		can_transceiver_phy->mux_state = mux_state;
	}

	phy = devm_phy_create(dev, dev->of_node,
			      &can_transceiver_phy_ops);
	if (IS_ERR(phy)) {
		dev_err(dev, "failed to create can transceiver phy\n");
		return PTR_ERR(phy);
	}

	err = device_property_read_u32(dev, "max-bitrate", &max_bitrate);
	if ((err != -EINVAL) && !max_bitrate)
		dev_warn(dev, "Invalid value for transceiver max bitrate. Ignoring bitrate limit\n");
	phy->attrs.max_link_rate = max_bitrate;

	can_transceiver_phy->generic_phy = phy;

	if (drvdata->flags & CAN_TRANSCEIVER_STB_PRESENT) {
		standby_gpio = devm_gpiod_get_optional(dev, "standby", GPIOD_OUT_HIGH);
		if (IS_ERR(standby_gpio))
			return PTR_ERR(standby_gpio);
		can_transceiver_phy->standby_gpio = standby_gpio;
	}

	if (drvdata->flags & CAN_TRANSCEIVER_EN_PRESENT) {
		enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
		if (IS_ERR(enable_gpio))
			return PTR_ERR(enable_gpio);
		can_transceiver_phy->enable_gpio = enable_gpio;
	}

	phy_set_drvdata(can_transceiver_phy->generic_phy, can_transceiver_phy);

	phy_provider = devm_of_phy_provider_register(dev, of_phy_simple_xlate);

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
