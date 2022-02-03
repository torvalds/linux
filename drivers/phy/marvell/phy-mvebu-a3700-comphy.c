// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Marvell
 *
 * Authors:
 *   Evan Wang <xswang@marvell.com>
 *   Miquèl Raynal <miquel.raynal@bootlin.com>
 *
 * Structure inspired from phy-mvebu-cp110-comphy.c written by Antoine Tenart.
 * SMC call initial support done by Grzegorz Jaszczyk.
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>

#define MVEBU_A3700_COMPHY_LANES		3

/* COMPHY Fast SMC function identifiers */
#define COMPHY_SIP_POWER_ON			0x82000001
#define COMPHY_SIP_POWER_OFF			0x82000002
#define COMPHY_SIP_PLL_LOCK			0x82000003

#define COMPHY_FW_MODE_SATA			0x1
#define COMPHY_FW_MODE_SGMII			0x2
#define COMPHY_FW_MODE_2500BASEX		0x3
#define COMPHY_FW_MODE_USB3H			0x4
#define COMPHY_FW_MODE_USB3D			0x5
#define COMPHY_FW_MODE_PCIE			0x6
#define COMPHY_FW_MODE_USB3			0xa

#define COMPHY_FW_SPEED_1_25G			0 /* SGMII 1G */
#define COMPHY_FW_SPEED_2_5G			1
#define COMPHY_FW_SPEED_3_125G			2 /* 2500BASE-X */
#define COMPHY_FW_SPEED_5G			3
#define COMPHY_FW_SPEED_MAX			0x3F

#define COMPHY_FW_MODE(mode)			((mode) << 12)
#define COMPHY_FW_NET(mode, idx, speed)		(COMPHY_FW_MODE(mode) | \
						 ((idx) << 8) |	\
						 ((speed) << 2))
#define COMPHY_FW_PCIE(mode, speed, width)	(COMPHY_FW_NET(mode, 0, speed) | \
						 ((width) << 18))

struct mvebu_a3700_comphy_conf {
	unsigned int lane;
	enum phy_mode mode;
	int submode;
	u32 fw_mode;
};

#define MVEBU_A3700_COMPHY_CONF(_lane, _mode, _smode, _fw)		\
	{								\
		.lane = _lane,						\
		.mode = _mode,						\
		.submode = _smode,					\
		.fw_mode = _fw,						\
	}

#define MVEBU_A3700_COMPHY_CONF_GEN(_lane, _mode, _fw) \
	MVEBU_A3700_COMPHY_CONF(_lane, _mode, PHY_INTERFACE_MODE_NA, _fw)

#define MVEBU_A3700_COMPHY_CONF_ETH(_lane, _smode, _fw) \
	MVEBU_A3700_COMPHY_CONF(_lane, PHY_MODE_ETHERNET, _smode, _fw)

static const struct mvebu_a3700_comphy_conf mvebu_a3700_comphy_modes[] = {
	/* lane 0 */
	MVEBU_A3700_COMPHY_CONF_GEN(0, PHY_MODE_USB_HOST_SS,
				    COMPHY_FW_MODE_USB3H),
	MVEBU_A3700_COMPHY_CONF_ETH(0, PHY_INTERFACE_MODE_SGMII,
				    COMPHY_FW_MODE_SGMII),
	MVEBU_A3700_COMPHY_CONF_ETH(0, PHY_INTERFACE_MODE_2500BASEX,
				    COMPHY_FW_MODE_2500BASEX),
	/* lane 1 */
	MVEBU_A3700_COMPHY_CONF_GEN(1, PHY_MODE_PCIE, COMPHY_FW_MODE_PCIE),
	MVEBU_A3700_COMPHY_CONF_ETH(1, PHY_INTERFACE_MODE_SGMII,
				    COMPHY_FW_MODE_SGMII),
	MVEBU_A3700_COMPHY_CONF_ETH(1, PHY_INTERFACE_MODE_2500BASEX,
				    COMPHY_FW_MODE_2500BASEX),
	/* lane 2 */
	MVEBU_A3700_COMPHY_CONF_GEN(2, PHY_MODE_SATA, COMPHY_FW_MODE_SATA),
	MVEBU_A3700_COMPHY_CONF_GEN(2, PHY_MODE_USB_HOST_SS,
				    COMPHY_FW_MODE_USB3H),
};

struct mvebu_a3700_comphy_lane {
	struct device *dev;
	unsigned int id;
	enum phy_mode mode;
	int submode;
};

static int mvebu_a3700_comphy_smc(unsigned long function, unsigned long lane,
				  unsigned long mode)
{
	struct arm_smccc_res res;
	s32 ret;

	arm_smccc_smc(function, lane, mode, 0, 0, 0, 0, 0, &res);
	ret = res.a0;

	switch (ret) {
	case SMCCC_RET_SUCCESS:
		return 0;
	case SMCCC_RET_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
}

static int mvebu_a3700_comphy_get_fw_mode(int lane,
					  enum phy_mode mode,
					  int submode)
{
	int i, n = ARRAY_SIZE(mvebu_a3700_comphy_modes);

	/* Unused PHY mux value is 0x0 */
	if (mode == PHY_MODE_INVALID)
		return -EINVAL;

	for (i = 0; i < n; i++) {
		if (mvebu_a3700_comphy_modes[i].lane == lane &&
		    mvebu_a3700_comphy_modes[i].mode == mode &&
		    mvebu_a3700_comphy_modes[i].submode == submode)
			break;
	}

	if (i == n)
		return -EINVAL;

	return mvebu_a3700_comphy_modes[i].fw_mode;
}

static int mvebu_a3700_comphy_set_mode(struct phy *phy, enum phy_mode mode,
				       int submode)
{
	struct mvebu_a3700_comphy_lane *lane = phy_get_drvdata(phy);
	int fw_mode;

	if (submode == PHY_INTERFACE_MODE_1000BASEX)
		submode = PHY_INTERFACE_MODE_SGMII;

	fw_mode = mvebu_a3700_comphy_get_fw_mode(lane->id, mode,
						 submode);
	if (fw_mode < 0) {
		dev_err(lane->dev, "invalid COMPHY mode\n");
		return fw_mode;
	}

	/* Just remember the mode, ->power_on() will do the real setup */
	lane->mode = mode;
	lane->submode = submode;

	return 0;
}

static int mvebu_a3700_comphy_power_on(struct phy *phy)
{
	struct mvebu_a3700_comphy_lane *lane = phy_get_drvdata(phy);
	u32 fw_param;
	int fw_mode;
	int fw_port;
	int ret;

	fw_mode = mvebu_a3700_comphy_get_fw_mode(lane->id,
						 lane->mode, lane->submode);
	if (fw_mode < 0) {
		dev_err(lane->dev, "invalid COMPHY mode\n");
		return fw_mode;
	}

	switch (lane->mode) {
	case PHY_MODE_USB_HOST_SS:
		dev_dbg(lane->dev, "set lane %d to USB3 host mode\n", lane->id);
		fw_param = COMPHY_FW_MODE(fw_mode);
		break;
	case PHY_MODE_SATA:
		dev_dbg(lane->dev, "set lane %d to SATA mode\n", lane->id);
		fw_param = COMPHY_FW_MODE(fw_mode);
		break;
	case PHY_MODE_ETHERNET:
		fw_port = (lane->id == 0) ? 1 : 0;
		switch (lane->submode) {
		case PHY_INTERFACE_MODE_SGMII:
			dev_dbg(lane->dev, "set lane %d to SGMII mode\n",
				lane->id);
			fw_param = COMPHY_FW_NET(fw_mode, fw_port,
						 COMPHY_FW_SPEED_1_25G);
			break;
		case PHY_INTERFACE_MODE_2500BASEX:
			dev_dbg(lane->dev, "set lane %d to 2500BASEX mode\n",
				lane->id);
			fw_param = COMPHY_FW_NET(fw_mode, fw_port,
						 COMPHY_FW_SPEED_3_125G);
			break;
		default:
			dev_err(lane->dev, "unsupported PHY submode (%d)\n",
				lane->submode);
			return -ENOTSUPP;
		}
		break;
	case PHY_MODE_PCIE:
		dev_dbg(lane->dev, "set lane %d to PCIe mode\n", lane->id);
		fw_param = COMPHY_FW_PCIE(fw_mode, COMPHY_FW_SPEED_5G,
					  phy->attrs.bus_width);
		break;
	default:
		dev_err(lane->dev, "unsupported PHY mode (%d)\n", lane->mode);
		return -ENOTSUPP;
	}

	ret = mvebu_a3700_comphy_smc(COMPHY_SIP_POWER_ON, lane->id, fw_param);
	if (ret == -EOPNOTSUPP)
		dev_err(lane->dev,
			"unsupported SMC call, try updating your firmware\n");

	return ret;
}

static int mvebu_a3700_comphy_power_off(struct phy *phy)
{
	struct mvebu_a3700_comphy_lane *lane = phy_get_drvdata(phy);

	return mvebu_a3700_comphy_smc(COMPHY_SIP_POWER_OFF, lane->id, 0);
}

static const struct phy_ops mvebu_a3700_comphy_ops = {
	.power_on	= mvebu_a3700_comphy_power_on,
	.power_off	= mvebu_a3700_comphy_power_off,
	.set_mode	= mvebu_a3700_comphy_set_mode,
	.owner		= THIS_MODULE,
};

static struct phy *mvebu_a3700_comphy_xlate(struct device *dev,
					    struct of_phandle_args *args)
{
	struct mvebu_a3700_comphy_lane *lane;
	unsigned int port;
	struct phy *phy;

	phy = of_phy_simple_xlate(dev, args);
	if (IS_ERR(phy))
		return phy;

	lane = phy_get_drvdata(phy);

	port = args->args[0];
	if (port != 0 && (port != 1 || lane->id != 0)) {
		dev_err(lane->dev, "invalid port number %u\n", port);
		return ERR_PTR(-EINVAL);
	}

	return phy;
}

static int mvebu_a3700_comphy_probe(struct platform_device *pdev)
{
	struct phy_provider *provider;
	struct device_node *child;

	for_each_available_child_of_node(pdev->dev.of_node, child) {
		struct mvebu_a3700_comphy_lane *lane;
		struct phy *phy;
		int ret;
		u32 lane_id;

		ret = of_property_read_u32(child, "reg", &lane_id);
		if (ret < 0) {
			dev_err(&pdev->dev, "missing 'reg' property (%d)\n",
				ret);
			continue;
		}

		if (lane_id >= MVEBU_A3700_COMPHY_LANES) {
			dev_err(&pdev->dev, "invalid 'reg' property\n");
			continue;
		}

		lane = devm_kzalloc(&pdev->dev, sizeof(*lane), GFP_KERNEL);
		if (!lane) {
			of_node_put(child);
			return -ENOMEM;
		}

		phy = devm_phy_create(&pdev->dev, child,
				      &mvebu_a3700_comphy_ops);
		if (IS_ERR(phy)) {
			of_node_put(child);
			return PTR_ERR(phy);
		}

		lane->dev = &pdev->dev;
		lane->mode = PHY_MODE_INVALID;
		lane->submode = PHY_INTERFACE_MODE_NA;
		lane->id = lane_id;
		phy_set_drvdata(phy, lane);
	}

	provider = devm_of_phy_provider_register(&pdev->dev,
						 mvebu_a3700_comphy_xlate);
	return PTR_ERR_OR_ZERO(provider);
}

static const struct of_device_id mvebu_a3700_comphy_of_match_table[] = {
	{ .compatible = "marvell,comphy-a3700" },
	{ },
};
MODULE_DEVICE_TABLE(of, mvebu_a3700_comphy_of_match_table);

static struct platform_driver mvebu_a3700_comphy_driver = {
	.probe	= mvebu_a3700_comphy_probe,
	.driver	= {
		.name = "mvebu-a3700-comphy",
		.of_match_table = mvebu_a3700_comphy_of_match_table,
	},
};
module_platform_driver(mvebu_a3700_comphy_driver);

MODULE_AUTHOR("Miquèl Raynal <miquel.raynal@bootlin.com>");
MODULE_DESCRIPTION("Common PHY driver for A3700");
MODULE_LICENSE("GPL v2");
