/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_COMBO_PHY_H__
#define __INTEL_COMBO_PHY_H__

#include <linux/types.h>

enum phy;
struct intel_display;

void intel_combo_phy_init(struct intel_display *display);
void intel_combo_phy_uninit(struct intel_display *display);
void intel_combo_phy_power_up_lanes(struct intel_display *display,
				    enum phy phy, bool is_dsi,
				    int lane_count, bool lane_reversal);

#endif /* __INTEL_COMBO_PHY_H__ */
