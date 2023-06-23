/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2020 Kévin L'hôpital <kevin.lhopital@bootlin.com>
 * Copyright 2020-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#ifndef _SUN8I_A83T_DPHY_H_
#define _SUN8I_A83T_DPHY_H_

#include "sun8i_a83t_mipi_csi2.h"

#define SUN8I_A83T_DPHY_CTRL_REG		0x10
#define SUN8I_A83T_DPHY_CTRL_INIT_VALUE		0xb8df698e
#define SUN8I_A83T_DPHY_CTRL_RESET_N		BIT(31)
#define SUN8I_A83T_DPHY_CTRL_SHUTDOWN_N		BIT(15)
#define SUN8I_A83T_DPHY_CTRL_DEBUG		BIT(8)
#define SUN8I_A83T_DPHY_STATUS_REG		0x14
#define SUN8I_A83T_DPHY_STATUS_CLK_STOP		BIT(10)
#define SUN8I_A83T_DPHY_STATUS_CLK_ULPS		BIT(9)
#define SUN8I_A83T_DPHY_STATUS_HSCLK		BIT(8)
#define SUN8I_A83T_DPHY_STATUS_D3_STOP		BIT(7)
#define SUN8I_A83T_DPHY_STATUS_D2_STOP		BIT(6)
#define SUN8I_A83T_DPHY_STATUS_D1_STOP		BIT(5)
#define SUN8I_A83T_DPHY_STATUS_D0_STOP		BIT(4)
#define SUN8I_A83T_DPHY_STATUS_D3_ULPS		BIT(3)
#define SUN8I_A83T_DPHY_STATUS_D2_ULPS		BIT(2)
#define SUN8I_A83T_DPHY_STATUS_D1_ULPS		BIT(1)
#define SUN8I_A83T_DPHY_STATUS_D0_ULPS		BIT(0)

#define SUN8I_A83T_DPHY_ANA0_REG		0x30
#define SUN8I_A83T_DPHY_ANA0_REXT_EN		BIT(31)
#define SUN8I_A83T_DPHY_ANA0_REXT		BIT(30)
#define SUN8I_A83T_DPHY_ANA0_RINT(v)		(((v) << 28) & GENMASK(29, 28))
#define SUN8I_A83T_DPHY_ANA0_SNK(v)		(((v) << 20) & GENMASK(22, 20))

int sun8i_a83t_dphy_register(struct sun8i_a83t_mipi_csi2_device *csi2_dev);

#endif
