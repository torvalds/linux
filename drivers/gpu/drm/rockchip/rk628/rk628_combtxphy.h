/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 */

#ifndef RK628_COMBTXPHY_H_
#define RK628_COMBTXPHY_H_

#include <linux/phy/phy.h>

int rk628_combtxphy_set_gvi_division_mode(struct phy *phy, u8 mode);

#endif
