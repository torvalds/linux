// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Guochun Huang <hero.huang@rock-chips.com>
 */

#include "hal/cru_api.h"
#include <linux/kernel.h>
#include <linux/iopoll.h>
#include "rkx110_x120.h"
#include "rkx110_reg.h"
#include "serdes_combphy.h"

#define SOFT_RST		0x0000
#define HS_CLK_SOFT_RST		BIT(1)
#define CFG_CLK_SOFT_RST	BIT(0)
#define PHY_RCAL		0x0004
#define RCAL_DONE		BIT(17)
#define RCAL_OUT(x)		UPDATE(x, 16, 13)
#define RCAL_CTL(x)		UPDATE(x, 12, 5)
#define RCAL_TRIM(x)		UPDATE(x, 4, 1)
#define RCAL_EN			BIT(0)
#define ULP_RX_EN		0x0008
#define VOFFCAL_OUT		0x000c
#define CSI_CLK_VOFFCAL_DONE	BIT(29)
#define CSI_CLK_VOFFCAL_OUT(x)	UPDATE(x, 28, 24)
#define CSI_0_VOFFCAL_DONE	BIT(23)
#define CSI_0_VOFFCAL_OUT(x)	UPDATE(x, 22, 18)
#define CSI_1_VOFFCAL_DONE	BIT(17)
#define CSI_1_VOFFCAL_OUT(x)	UPDATE(x, 16, 12)
#define CSI_2_VOFFCAL_DONE	BIT(11)
#define CSI_2_VOFFCAL_OUT(x)	UPDATE(x, 10, 6)
#define CSI_3_VOFFCAL_DONE	BIT(5)
#define CSI_3_VOFFCAL_OUT(x)	UPDATE(x, 4, 0)
#define CSI_CTL01		0x0010
#define CSI_CTL1(x)		UPDATE(x, 31, 16)
#define CSI_CTL0(x)		UPDATE(x, 15, 0)
#define CSI_CTL23		0x0014
#define CSI_CTL3(x)		UPDATE(x, 31, 16)
#define CSI_CTL2(x)		UPDATE(x, 15, 0)
#define CSI_VINIT		0x001c
#define CSI_LPRX_VREF_TRIM	UPDATE(x, 23, 20)
#define CSI_CLK_LPRX_VINIT(x)	UPDATE(x, 19, 16)
#define CSI_3_LPRX_VINIT(x)	UPDATE(x, 15, 12)
#define	CSI_2_LPRX_VINIT(x)	UPDATE(x, 11, 8)
#define CSI_1_LPRX_VINIT(x)	UPDATE(x, 7, 4)
#define CSI_0_LPRX_VINIT(x)	UPDATE(x, 3, 0)
#define CLANE_PARA		0x0020
#define T_CLK_TERM_EN(x)	UPDATE(x, 15, 8)
#define T_CLK_SETTLE(x)		UPDATE(x, 7, 0)
#define T_HS_TERMEN		0x0024
#define T_D3_TERMEN(x)		UPDATE(x, 31, 24)
#define T_D2_TERMEN(x)		UPDATE(x, 23, 16)
#define T_D1_TERMEN(x)		UPDATE(x, 15, 8)
#define T_D0_TERMEN(x)		UPDATE(x, 7, 0)
#define T_HS_SETTLE		0x0028
#define T_D3_SETTLE(x)		UPDATE(x, 31, 24)
#define T_D2_SETTLE(x)		UPDATE(x, 23, 16)
#define T_D1_SETTLE(x)		UPDATE(x, 15, 8)
#define T_D0_SETTLE(x)		UPDATE(x, 7, 0)
#define T_CLANE_INIT		0x0030
#define T_CLK_INIT(x)		UPDATE(x, 23, 0)
#define T_LANE0_INIT		0x0034
#define T_D0_INIT(x)		UPDATE(x, 23, 0)
#define T_LANE1_INIT		0x0038
#define T_D1_INIT(x)		UPDATE(x, 23, 0)
#define T_LANE2_INIT		0x003c
#define T_D2_INIT(x)		UPDATE(x, 23, 0)
#define T_LANE3_INIT		0x0040
#define T_D3_INIT(x)		UPDATE(x, 23, 0)
#define TLPX_CTRL		0x0044
#define EN_TLPX_CHECK		BIT(8)
#define TLPX(x)			UPDATE(x, 7, 0)
#define NE_SWAP			0x0048
#define DPDN_SWAP_LANE(x)	UPDATE(1 << x, 11, 8)
#define LANE_SWAP_LAN3(x)	UPDATE(x, 7, 6)
#define LANE_SWAP_LANE2(x)	UPDATE(x, 5, 4)
#define LANE_SWAP_LANE1(x)	UPDATE(x, 3, 2)
#define LANE_SWAP_LANE0(x)	UPDATE(x, 1, 0)
#define MISC_INFO		0x004c
#define ULPS_LP10_SEL		BIT(1)
#define LONG_SOTSYNC_EN		BIT(0)

#define GRF_MIPI_RX_CON0	0x0000
#define RXCK_RTRM(x)		HIWORD_UPDATE(x, GENMASK(15, 12), 12)
#define LVDS_RX3_PD(x)		HIWORD_UPDATE(x, BIT(10), 10)
#define LVDS_RX2_PD(x)		HIWORD_UPDATE(x, BIT(9), 9)
#define LVDS_RX1_PD(x)		HIWORD_UPDATE(x, BIT(8), 8)
#define LVDS_RX0_PD(x)		HIWORD_UPDATE(x, BIT(7), 7)
#define LVDS_RXCK_PD(x)		HIWORD_UPDATE(x, BIT(6), 6)
#define LANE3_ENABLE(x)		HIWORD_UPDATE(x, BIT(5), 5)
#define LANE2_ENABLE(x)		HIWORD_UPDATE(x, BIT(4), 4)
#define LANE1_ENABLE(x)		HIWORD_UPDATE(x, BIT(3), 3)
#define LANE0_ENABLE(x)		HIWORD_UPDATE(x, BIT(2), 2)
#define PHY_MODE(x)		HIWORD_UPDATE(x, GENMASK(1, 0), 0)

#define GRF_MIPI_RX_CON2	0x0008
#define RXCK_CTL_5(x)		HIWORD_UPDATE(x, BIT(11), 11)
#define DDR_CLK_DUTY_CYCLE(x)	HIWORD_UPDATE(x, GENMASK(10, 8), 8)
#define BUS_WIDTH_SELECTION(x)	HIWORD_UPDATE(x, GENMASK(7, 5), 5)
#define RXCK_CTL_2(x)		HIWORD_UPDATE(x, BIT(4), 4)
#define RXCK_CTL_1(x)		HIWORD_UPDATE(x, GENMASK(2, 1), 1)
#define RXCK_CTL_0(x)		HIWORD_UPDATE(x, BIT(0), 0)

#define GRF_MIPI_RX_CON4	0x0010
#define GRF_MIPI_RX_CON5	0x0014
#define GRF_MIPI_RX_CON6	0x0018
#define GRF_MIPI_RX_CON7	0x001c
#define GRF_SOC_STATUS		0x0080
#define DLL_LOCK		BIT(24)

#define LVDS1_MSBSEL(x)		HIWORD_UPDATE(x, BIT(5), 5)
#define LVDS0_MSBSEL(x)		HIWORD_UPDATE(x, BIT(2), 2)

static void
rkx110_combrxphy_dsi_timing_init(struct rk_serdes *ser, enum comb_phy_id id)
{
}

static void rkx110_combrxphy_dsi_power_on(struct rk_serdes *ser, enum comb_phy_id id)
{
	struct hwclk *hwclk = ser->chip[DEVICE_LOCAL].hwclk;
	struct rkx110_combrxphy *combrxphy = &ser->combrxphy;
	struct i2c_client *client = ser->chip[DEVICE_LOCAL].client;
	u32 val = 0;
	u32 grf_base;

	switch (id) {
	case COMBPHY_0:
		hwclk_set_rate(hwclk, RKX110_CPS_CFGCLK_MIPIRXPHY0, 100 * USEC_PER_SEC);
		dev_info(ser->dev, "RKX110_CPS_CFGCLK_MIPIRXPHY0:%d\n",
			 hwclk_get_rate(hwclk, RKX110_CPS_CFGCLK_MIPIRXPHY0));
		break;
	case COMBPHY_1:
		hwclk_set_rate(hwclk, RKX110_CPS_CFGCLK_MIPIRXPHY1, 100 * USEC_PER_SEC);
		dev_info(ser->dev, "RKX110_CPS_CFGCLK_MIPIRXPHY1:%d\n",
			 hwclk_get_rate(hwclk, RKX110_CPS_CFGCLK_MIPIRXPHY1));
		break;
	default:
		break;
	}

	serdes_combphy_get_default_config(combrxphy->rate,
					  &combrxphy->mipi_dphy_cfg);

	switch (ser->dsi_rx.lanes) {
	case 4:
		val |= LANE3_ENABLE(1);
		fallthrough;
	case 3:
		val |= LANE2_ENABLE(1);
		fallthrough;
	case 2:
		val |= LANE1_ENABLE(1);
		fallthrough;
	case 1:
	default:
		val |= LANE0_ENABLE(1);
		break;
	}

	grf_base = id ? RKX110_GRF_MIPI1_BASE : RKX110_GRF_MIPI0_BASE;
	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON0,
			   PHY_MODE(COMBRX_PHY_MODE_VIDEO_MIPI) | val);

	rkx110_combrxphy_dsi_timing_init(ser, id);
}

static void rkx110_combrxphy_dsi_power_off(struct rk_serdes *ser, enum comb_phy_id id)
{
	struct i2c_client *client = ser->chip[DEVICE_LOCAL].client;
	u32 grf_base;

	grf_base = id ? RKX110_GRF_MIPI1_BASE : RKX110_GRF_MIPI0_BASE;
	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON0,
			    LANE3_ENABLE(0) | LANE2_ENABLE(0) |
			    LANE1_ENABLE(0) | LANE0_ENABLE(0));
}

static void rkx110_combrxphy_lvds_power_on(struct rk_serdes *ser, enum comb_phy_id id)
{
	struct i2c_client *client = ser->chip[DEVICE_LOCAL].client;
	u32 grf_base = id ? RKX110_GRF_MIPI1_BASE : RKX110_GRF_MIPI0_BASE;
	u32 val;
	int ret;

	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON0,
			   LVDS_RXCK_PD(0) | PHY_MODE(COMBRX_PHY_MODE_VIDEO_LVDS));
	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON2,
			   BUS_WIDTH_SELECTION(7) | RXCK_CTL_2(1) |
			   RXCK_CTL_1(1) | RXCK_CTL_0(0));
	ser->i2c_write_reg(client, SER_GRF_SOC_CON2,
			   id ? LVDS1_MSBSEL(0) : LVDS0_MSBSEL(0));
	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON4, 0xffff0f08);
	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON5, 0xffff0f08);
	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON6, 0xffff0f08);
	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON7, 0xffff0f08);

	ret = read_poll_timeout(ser->i2c_read_reg, ret,
				!(ret < 0) && (val & DLL_LOCK),
				0, MSEC_PER_SEC, false, client,
				grf_base + GRF_SOC_STATUS, &val);
	if (ret < 0)
		dev_err(ser->dev, "DLL is not locked\n");

	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON0,
			   LVDS_RX3_PD(0) | LVDS_RX2_PD(0) |
			   LVDS_RX1_PD(0) | LVDS_RX0_PD(0));
}

static void rkx110_combrxphy_lvds_power_off(struct rk_serdes *ser, enum comb_phy_id id)
{
	struct i2c_client *client = ser->chip[DEVICE_LOCAL].client;
	u32 grf_base = id ? RKX110_GRF_MIPI1_BASE : RKX110_GRF_MIPI0_BASE;

	ser->i2c_write_reg(client, grf_base + GRF_MIPI_RX_CON0,
			   LVDS_RX3_PD(1) | LVDS_RX2_PD(1) |
			   LVDS_RX1_PD(1) | LVDS_RX0_PD(1));
}

static void rkx110_combrxphy_lvds_camera_power_on(struct rk_serdes *ser, enum comb_phy_id id)
{
}

static void rkx110_combrxphy_lvds_camera_power_off(struct rk_serdes *ser, enum comb_phy_id id)
{
}

void rkx110_combrxphy_power_on(struct rk_serdes *ser, enum comb_phy_id id)
{
	struct rkx110_combrxphy *combrxphy = &ser->combrxphy;

	switch (combrxphy->mode) {
	case COMBRX_PHY_MODE_VIDEO_MIPI:
		rkx110_combrxphy_dsi_power_on(ser, id);
		break;
	case COMBRX_PHY_MODE_VIDEO_LVDS:
		rkx110_combrxphy_lvds_power_on(ser, id);
		break;
	case COMBRX_PHY_MODE_LVDS_CAMERA:
		rkx110_combrxphy_lvds_camera_power_on(ser, id);
		break;
	default:
		break;
	}
}

void rkx110_combrxphy_power_off(struct rk_serdes *ser, enum comb_phy_id id)
{
	struct rkx110_combrxphy *combrxphy = &ser->combrxphy;

	switch (combrxphy->mode) {
	case COMBRX_PHY_MODE_VIDEO_MIPI:
		rkx110_combrxphy_dsi_power_off(ser, id);
		break;
	case COMBRX_PHY_MODE_VIDEO_LVDS:
		rkx110_combrxphy_lvds_power_off(ser, id);
		break;
	case COMBRX_PHY_MODE_LVDS_CAMERA:
		rkx110_combrxphy_lvds_camera_power_off(ser, id);
		break;
	default:
		break;
	}
}

void rkx110_combrxphy_set_rate(struct rk_serdes *ser, u64 rate)
{
	struct rkx110_combrxphy *combrxphy = &ser->combrxphy;

	combrxphy->rate = rate;
}

void rkx110_combrxphy_set_mode(struct rk_serdes *ser, enum combrx_phy_mode mode)
{
	struct rkx110_combrxphy *combrxphy = &ser->combrxphy;

	combrxphy->mode = mode;
}
