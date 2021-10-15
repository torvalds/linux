/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Shunqing Chen csq@rock-chips.com>
 */

#ifndef _RK628_COMBTXPHY_H
#define _RK628_COMBTXPHY_H

#define COMBTXPHY_BASE		0x90000
#define COMBTXPHY_REG(x)	((x) + COMBTXPHY_BASE)

#define COMBTXPHY_CON0		COMBTXPHY_REG(0x0000)
#define SW_TX_IDLE_MASK		GENMASK(29, 20)
#define SW_TX_IDLE(x)		UPDATE(x, 29, 20)
#define SW_TX_PD_MASK		GENMASK(17, 8)
#define SW_TX_PD(x)		UPDATE(x, 17, 8)
#define SW_BUS_WIDTH_MASK	GENMASK(6, 5)
#define SW_BUS_WIDTH_7BIT	UPDATE(0x3, 6, 5)
#define SW_BUS_WIDTH_8BIT	UPDATE(0x2, 6, 5)
#define SW_BUS_WIDTH_9BIT	UPDATE(0x1, 6, 5)
#define SW_BUS_WIDTH_10BIT	UPDATE(0x0, 6, 5)
#define SW_PD_PLL_MASK		BIT(4)
#define SW_PD_PLL		BIT(4)
#define SW_GVI_LVDS_EN_MASK	BIT(3)
#define SW_GVI_LVDS_EN		BIT(3)
#define SW_MIPI_DSI_EN_MASK	BIT(2)
#define SW_MIPI_DSI_EN		BIT(2)
#define SW_MODULEB_EN_MASK	BIT(1)
#define SW_MODULEB_EN		BIT(1)
#define SW_MODULEA_EN_MASK	BIT(0)
#define SW_MODULEA_EN		BIT(0)
#define COMBTXPHY_CON1		COMBTXPHY_REG(0x0004)
#define COMBTXPHY_CON2		COMBTXPHY_REG(0x0008)
#define COMBTXPHY_CON3		COMBTXPHY_REG(0x000c)
#define COMBTXPHY_CON4		COMBTXPHY_REG(0x0010)
#define COMBTXPHY_CON5		COMBTXPHY_REG(0x0014)
#define SW_RATE(x)		UPDATE(x, 26, 24)
#define SW_REF_DIV(x)		UPDATE(x, 20, 16)
#define SW_PLL_FB_DIV(x)	UPDATE(x, 14, 10)
#define SW_PLL_FRAC_DIV(x)	UPDATE(x, 9, 0)
#define COMBTXPHY_CON6		COMBTXPHY_REG(0x0018)
#define COMBTXPHY_CON7		COMBTXPHY_REG(0x001c)
#define SW_TX_RTERM_MASK	GENMASK(22, 20)
#define SW_TX_RTERM(x)		UPDATE(x, 22, 20)
#define SW_TX_MODE_MASK		GENMASK(17, 16)
#define SW_TX_MODE(x)		UPDATE(x, 17, 16)
#define SW_TX_CTL_CON5_MASK	BIT(10)
#define SW_TX_CTL_CON5(x)	UPDATE(x, 10, 10)
#define SW_TX_CTL_CON4_MASK	GENMASK(9, 8)
#define SW_TX_CTL_CON4(x)	UPDATE(x, 9, 8)
#define COMBTXPHY_CON8		COMBTXPHY_REG(0x0020)
#define COMBTXPHY_CON9		COMBTXPHY_REG(0x0024)
#define SW_DSI_FSET_EN_MASK	BIT(29)
#define SW_DSI_FSET_EN		BIT(29)
#define SW_DSI_RCAL_EN_MASK	BIT(28)
#define SW_DSI_RCAL_EN		BIT(28)
#define COMBTXPHY_CON10		COMBTXPHY_REG(0x0028)
#define TX9_CKDRV_EN		BIT(9)
#define TX8_CKDRV_EN		BIT(8)
#define TX7_CKDRV_EN		BIT(7)
#define TX6_CKDRV_EN		BIT(6)
#define TX5_CKDRV_EN		BIT(5)
#define TX4_CKDRV_EN		BIT(4)
#define TX3_CKDRV_EN		BIT(3)
#define TX2_CKDRV_EN		BIT(2)
#define TX1_CKDRV_EN		BIT(1)
#define TX0_CKDRV_EN		BIT(0)

enum phy_mode {
	PHY_MODE_INVALID,
	PHY_MODE_VIDEO_MIPI,
	PHY_MODE_VIDEO_LVDS,
	PHY_MODE_VIDEO_GVI,
};

struct rk628_combtxphy {
	enum phy_mode mode;
	unsigned int flags;
	u8 ref_div;
	u8 fb_div;
	u16 frac_div;
	u8 rate_div;
	u32 bus_width;
};

void rk628_txphy_set_mode(struct rk628 *rk628, enum phy_mode mode);
void rk628_txphy_set_bus_width(struct rk628 *rk628, u32 bus_width);
u32 rk628_txphy_get_bus_width(struct rk628 *rk628);
void rk628_txphy_power_on(struct rk628 *rk628);
void rk628_txphy_power_off(struct rk628 *rk628);
struct rk628_combtxphy *rk628_txphy_register(struct rk628 *rk628);

#endif
