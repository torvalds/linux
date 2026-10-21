/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _PHY_K3_COMMON_H
#define _PHY_K3_COMMON_H

#include <linux/phy/phy.h>

struct k3_phy_lane_group_data {
	u32 lanes;
	u8 config;
	u8 mask;
	u32 offsets[] __counted_by(lanes);
};

struct k3_lane_group {
	const struct k3_phy_lane_group_data *data;
	void __iomem *base;
	struct phy *phy;
	bool is_pcie;
};

extern const struct phy_ops k3_pcie_phy_ops;
extern const struct phy_ops k3_usb3_phy_ops;

int k3_phy_calibrate(struct regmap *apb_spare);

#endif
