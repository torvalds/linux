// SPDX-License-Identifier: GPL-2.0-only
/*
 * phy-k3-combo.c - SpacemiT K3 PCIe/USB3 Combo PHY Driver
 *
 * Copyright (c) 2025 SpacemiT Technology Co. Ltd
 */

#include <linux/bitfield.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/phy/phy.h>

#include <dt-bindings/phy/phy.h>

#include "phy-k3-common.h"

/*
 * The PCIE/USB Subsystem on SpacemiT K3 have 3 single lane PIPE3 PHYs
 * (PHY2/3/4) shared by PCIE PortC/D and USB3 PortB/C/D.
 *
 * PMUA_PCIE_SUBSYS_MGMT[4:0]
 *
 *   bit4 = 0 : PCIe A X8 mode, all 8 lanes dedicated to PCIe Port A
 *          1 : PHY lanes shared between PCIe or USB according to [3:0]
 *
 * All PHY matrix combinations according to [4:0]:
 *
 *   0x0X : PCIe-A X8
 *   0x10 : PCIe-C x2 (PHY2+PHY3) + PCIe-D x1 (PHY4)
 *   0x11 : PCIe-C x2 (PHY2+PHY3) + USB-D (PHY4)
 *   0x12 : PCIe-C x1 (PHY2)      + USB-C (PHY3)
 *   0x13 : PCIe-C x1 (PHY2)      + USB-C (PHY3) + USB-D (PHY4)
 *   0x14 : PCIe-C x1 (PHY3)      + USB-B (PHY2)
 *   0x15 : PCIe-C x1 (PHY3)      + USB-B (PHY2) + USB-D (PHY4)
 *   0x16 : USB-B (PHY2) + USB-C (PHY3) + PCIe D x1 (PHY4)
 *   0x17 : USB-B (PHY2) + USB-C (PHY3) + USB-D (PHY4)
 *
 * So any USB Port B/C/D operation requires PCIe A X8 mode to be disabled.
 */
#define PMUA_PCIE_SUBSYS_MGMT		0x1d8
#define PU_MATRIX_CONF_MASK		GENMASK(4, 0)

#define COMBPHY_MAX_SUBPHYS		6

struct k3_combo_phy {
	struct device *dev;
	struct k3_lane_group groups[COMBPHY_MAX_SUBPHYS];
	void __iomem *base;
	struct regmap *apb_spare;
};

static const struct k3_phy_lane_group_data k3_combphy_lane_group0 = {
	.lanes		= 2,
	.config		= 0xff,
	.mask		= 0x00,
	.offsets	= {
		0x0, 0x400
	},
};

static const struct k3_phy_lane_group_data k3_combphy_lane_group1 = {
	.lanes		= 2,
	.config		= 0xff,
	.mask		= 0x00,
	.offsets	= {
		0x100000, 0x100400
	},
};

static const struct k3_phy_lane_group_data k3_combphy_lane_group2 = {
	.lanes		= 1,
	.config		= 0x14,
	.mask		= 0x14,
	.offsets	= {
		0x200000
	},
};

static const struct k3_phy_lane_group_data k3_combphy_lane_group3 = {
	.lanes		= 1,
	.config		= 0x12,
	.mask		= 0x12,
	.offsets	= {
		0x300000
	},
};

static const struct k3_phy_lane_group_data k3_combphy_lane_group4 = {
	.lanes		= 1,
	.config		= 0x11,
	.mask		= 0x11,
	.offsets	= {
		0x400000
	},
};

static const struct k3_phy_lane_group_data k3_combphy_lane_group5 = {
	.lanes		= 1,
	.config		= 0xff,
	.mask		= 0x00,
	.offsets	= {
		0x500000
	},
};

static const struct k3_phy_lane_group_data *k3_combphy_lane_datas[] = {
	&k3_combphy_lane_group0,
	&k3_combphy_lane_group1,
	&k3_combphy_lane_group2,
	&k3_combphy_lane_group3,
	&k3_combphy_lane_group4,
	&k3_combphy_lane_group5,
};

static int k3_combo_phy_init_lanes(struct k3_combo_phy *phy, unsigned int config)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(k3_combphy_lane_datas); i++) {
		const struct k3_phy_lane_group_data *data = k3_combphy_lane_datas[i];
		struct k3_lane_group *lg = &phy->groups[i];
		const struct phy_ops *ops;
		bool is_usb;

		is_usb = (data->mask & config) == data->config;
		if (is_usb)
			ops = &k3_usb3_phy_ops;
		else
			ops = &k3_pcie_phy_ops;

		dev_dbg(phy->dev, "phy %d is %s\n", i, is_usb ? "usb" : "pcie");

		lg->phy = devm_phy_create(phy->dev, NULL, ops);
		if (IS_ERR(lg->phy))
			return PTR_ERR(lg->phy);

		lg->is_pcie = !is_usb;
		lg->data = data;
		lg->base = phy->base;
		phy_set_drvdata(lg->phy, lg);
	}

	return 0;
}

static int k3_combo_phy_update_config(struct regmap *apmu, unsigned int config)
{
	if (config & ~PU_MATRIX_CONF_MASK)
		return -EINVAL;

	return regmap_update_bits(apmu, PMUA_PCIE_SUBSYS_MGMT, PU_MATRIX_CONF_MASK, config);
}

static struct phy *k3_combo_phy_xlate(struct device *dev, const struct of_phandle_args *args)
{
	struct k3_combo_phy *phy = dev_get_drvdata(dev);
	struct k3_lane_group *lg;

	if (args->args_count != 2) {
		dev_err(dev, "Invalid number of arguments\n");
		return ERR_PTR(-EINVAL);
	}

	if (args->args[0] >= ARRAY_SIZE(k3_combphy_lane_datas)) {
		dev_err(dev, "Invalid PHY id\n");
		return ERR_PTR(-EINVAL);
	}

	lg = &phy->groups[args->args[0]];

	if ((lg->is_pcie && args->args[1] != PHY_TYPE_PCIE) ||
	    (!lg->is_pcie && args->args[1] != PHY_TYPE_USB3)) {
		dev_err(dev, "Invalid PHY mode\n");
		return ERR_PTR(-EINVAL);
	}

	return lg->phy;
}

static int k3_combo_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct phy_provider *provider;
	struct k3_combo_phy *phy;
	struct regmap *apmu;
	u32 config = 0;
	int ret;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(phy->base))
		return PTR_ERR(phy->base);

	phy->apb_spare = syscon_regmap_lookup_by_phandle(node, "spacemit,apb-spare");
	if (IS_ERR(phy->apb_spare))
		return dev_err_probe(dev, PTR_ERR(phy->apb_spare),
				     "Failed to find APB SPARE syscon");

	apmu = syscon_regmap_lookup_by_phandle_args(node, "spacemit,apmu", 1, &config);
	if (IS_ERR(apmu))
		return dev_err_probe(dev, PTR_ERR(apmu),
				     "Failed to find APMU syscon");

	ret = k3_combo_phy_update_config(apmu, config);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to set lane configuration");

	phy->dev = dev;
	platform_set_drvdata(pdev, phy);

	ret = k3_phy_calibrate(phy->apb_spare);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to calibrate phy");

	ret = k3_combo_phy_init_lanes(phy, config);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to init lanes");

	provider = devm_of_phy_provider_register(dev, k3_combo_phy_xlate);
	if (IS_ERR(provider))
		return dev_err_probe(dev, PTR_ERR(provider),
				     "Failed to register provider\n");

	return 0;
}

static const struct of_device_id k3_combo_phy_of_match[] = {
	{ .compatible = "spacemit,k3-combo-phy" },
	{ },
};
MODULE_DEVICE_TABLE(of, k3_combo_phy_of_match);

static struct platform_driver k3_combo_phy_driver = {
	.probe = k3_combo_phy_probe,
	.driver = {
		.name = "spacemit,k3-combo-phy",
		.of_match_table = k3_combo_phy_of_match,
	},
};
module_platform_driver(k3_combo_phy_driver);

MODULE_DESCRIPTION("SpacemiT K3 USB3/PCIe combo PHY driver");
MODULE_LICENSE("GPL");
