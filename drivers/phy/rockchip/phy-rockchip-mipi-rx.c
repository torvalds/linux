/*
 * Rockchip MIPI RX Synopsys/Innosilicon DPHY driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <media/media-entity.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>

/* GRF */
#define RK1808_GRF_PD_VI_CON_OFFSET	0x0430

#define RK3288_GRF_SOC_CON6	0x025c
#define RK3288_GRF_SOC_CON8	0x0264
#define RK3288_GRF_SOC_CON9	0x0268
#define RK3288_GRF_SOC_CON10	0x026c
#define RK3288_GRF_SOC_CON14	0x027c
#define RK3288_GRF_SOC_STATUS21	0x02d4
#define RK3288_GRF_IO_VSEL	0x0380
#define RK3288_GRF_SOC_CON15	0x03a4

#define RK3326_GRF_IO_VSEL_OFFSET	0x0180
#define RK3326_GRF_PD_VI_CON_OFFSET	0x0430

#define RK3368_GRF_SOC_CON6_OFFSET	0x0418
#define RK3368_GRF_IO_VSEL_OFFSET	0x0900

#define RK3399_GRF_SOC_CON9	0x6224
#define RK3399_GRF_SOC_CON21	0x6254
#define RK3399_GRF_SOC_CON22	0x6258
#define RK3399_GRF_SOC_CON23	0x625c
#define RK3399_GRF_SOC_CON24	0x6260
#define RK3399_GRF_SOC_CON25	0x6264
#define RK3399_GRF_SOC_STATUS1	0xe2a4
#define RK3399_GRF_IO_VSEL	0x0900

#define RV1126_GRF_CSIPHY0_CON		0x10200
#define RV1126_GRF_CSIPHY1_CON		0x10210
#define RV1126_GRF_IOFUNC_CON3		0x1026c
#define RV1126_GRF_PHY1_SEL_CIFLITE	BIT(2)
#define RV1126_GRF_PHY1_SEL_ISP		BIT(1)
#define RV1126_GRF_PHY1_SEL_CIF		BIT(0)

/* PHY */
#define RK3288_PHY_TEST_CTRL0	0x30
#define RK3288_PHY_TEST_CTRL1	0x34
#define RK3288_PHY_SHUTDOWNZ	0x08
#define RK3288_PHY_RSTZ		0x0c

#define RK3399_PHY_TEST_CTRL0	0xb4
#define RK3399_PHY_TEST_CTRL1	0xb8
#define RK3399_PHY_SHUTDOWNZ	0xa0
#define RK3399_PHY_RSTZ		0xa0

#define CLOCK_LANE_HS_RX_CONTROL		0x34
#define LANE0_HS_RX_CONTROL			0x44
#define LANE1_HS_RX_CONTROL			0x54
#define LANE2_HS_RX_CONTROL			0x84
#define LANE3_HS_RX_CONTROL			0x94
#define HS_RX_DATA_LANES_THS_SETTLE_CONTROL	0x75

/* LOW POWER MODE SET */
#define MIPI_CSI_DPHY_CTRL_INVALID_OFFSET	0xFFFF

#define RK1808_CSI_DPHY_CTRL_LANE_ENABLE	0x00
#define RK1808_CSI_DPHY_CTRL_PWRCTL	\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK1808_CSI_DPHY_CTRL_DIG_RST		0x80

#define RK3326_CSI_DPHY_CTRL_LANE_ENABLE	0x00
#define RK3326_CSI_DPHY_CTRL_PWRCTL		0x04
#define RK3326_CSI_DPHY_CTRL_DIG_RST		0x80

#define RK3368_CSI_DPHY_CTRL_LANE_ENABLE	0x00
#define RK3368_CSI_DPHY_CTRL_PWRCTL		0x04
#define RK3368_CSI_DPHY_CTRL_DIG_RST		0x80

#define RV1126_CSI_DPHY_CTRL_LANE_ENABLE	0x00
#define RV1126_CSI_DPHY_CTRL_PWRCTL	\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RV1126_CSI_DPHY_CTRL_DIG_RST		0x80

#define MIPI_CSI_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT	2
#define MIPI_CSI_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT	6

/* Configure the count time of the THS-SETTLE by protocol. */
#define RK1808_CSI_DPHY_CLK_WR_THS_SETTLE	0x160
#define RK1808_CSI_DPHY_LANE0_WR_THS_SETTLE	\
		(RK1808_CSI_DPHY_CLK_WR_THS_SETTLE + 0x80)
#define RK1808_CSI_DPHY_LANE1_WR_THS_SETTLE	\
		(RK1808_CSI_DPHY_LANE0_WR_THS_SETTLE + 0x80)
#define RK1808_CSI_DPHY_LANE2_WR_THS_SETTLE	\
		(RK1808_CSI_DPHY_LANE1_WR_THS_SETTLE + 0x80)
#define RK1808_CSI_DPHY_LANE3_WR_THS_SETTLE	\
		(RK1808_CSI_DPHY_LANE2_WR_THS_SETTLE + 0x80)

#define RK3326_CSI_DPHY_CLK_WR_THS_SETTLE	0x100
#define RK3326_CSI_DPHY_LANE0_WR_THS_SETTLE	\
		(RK3326_CSI_DPHY_CLK_WR_THS_SETTLE + 0x80)
#define RK3326_CSI_DPHY_LANE1_WR_THS_SETTLE	\
		(RK3326_CSI_DPHY_LANE0_WR_THS_SETTLE + 0x80)
#define RK3326_CSI_DPHY_LANE2_WR_THS_SETTLE	\
		(RK3326_CSI_DPHY_LANE1_WR_THS_SETTLE + 0x80)
#define RK3326_CSI_DPHY_LANE3_WR_THS_SETTLE	\
		(RK3326_CSI_DPHY_LANE2_WR_THS_SETTLE + 0x80)

#define RK3368_CSI_DPHY_CLK_WR_THS_SETTLE	0x100
#define RK3368_CSI_DPHY_LANE0_WR_THS_SETTLE	\
		(RK3368_CSI_DPHY_CLK_WR_THS_SETTLE + 0x80)
#define RK3368_CSI_DPHY_LANE1_WR_THS_SETTLE	\
		(RK3368_CSI_DPHY_LANE0_WR_THS_SETTLE + 0x80)
#define RK3368_CSI_DPHY_LANE2_WR_THS_SETTLE	\
		(RK3368_CSI_DPHY_LANE1_WR_THS_SETTLE + 0x80)
#define RK3368_CSI_DPHY_LANE3_WR_THS_SETTLE	\
		(RK3368_CSI_DPHY_LANE2_WR_THS_SETTLE + 0x80)

#define RV1126_CSI_DPHY_CLK_WR_THS_SETTLE	0x160
#define RV1126_CSI_DPHY_LANE0_WR_THS_SETTLE	0x1e0
#define RV1126_CSI_DPHY_LANE1_WR_THS_SETTLE	0x260
#define RV1126_CSI_DPHY_LANE2_WR_THS_SETTLE	0x2e0
#define RV1126_CSI_DPHY_LANE3_WR_THS_SETTLE	0x360

/* Calibration reception enable */
#define RK1808_CSI_DPHY_CLK_CALIB_EN		0x168
#define RK1808_CSI_DPHY_LANE0_CALIB_EN		0x1e8
#define RK1808_CSI_DPHY_LANE1_CALIB_EN		0x268
#define RK1808_CSI_DPHY_LANE2_CALIB_EN		0x2e8
#define RK1808_CSI_DPHY_LANE3_CALIB_EN		0x368

#define RK3326_CSI_DPHY_CLK_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3326_CSI_DPHY_LANE0_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3326_CSI_DPHY_LANE1_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3326_CSI_DPHY_LANE2_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3326_CSI_DPHY_LANE3_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET

#define RK3368_CSI_DPHY_CLK_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3368_CSI_DPHY_LANE0_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3368_CSI_DPHY_LANE1_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3368_CSI_DPHY_LANE2_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET
#define RK3368_CSI_DPHY_LANE3_CALIB_EN		\
		MIPI_CSI_DPHY_CTRL_INVALID_OFFSET

#define RV1126_CSI_DPHY_CLK_CALIB_EN		0x168
#define RV1126_CSI_DPHY_LANE0_CALIB_EN		0x1e8
#define RV1126_CSI_DPHY_LANE1_CALIB_EN		0x268
#define RV1126_CSI_DPHY_LANE2_CALIB_EN		0x2e8
#define RV1126_CSI_DPHY_LANE3_CALIB_EN		0x368

#define RV1126_CSI_DPHY_MIPI_LVDS_MODEL		0x2cc
#define RV1126_CSI_DPHY_LVDS_MODE		0x300
/*
 * CSI HOST
 */
#define PHY_TESTEN_ADDR			(0x1 << 16)
#define PHY_TESTEN_DATA			(0x0 << 16)
#define PHY_TESTCLK			(0x1 << 1)
#define PHY_TESTCLR			(0x1 << 0)
#define THS_SETTLE_COUNTER_THRESHOLD	0x04

#define HIWORD_UPDATE(val, mask, shift) \
	((val) << (shift) | (mask) << ((shift) + 16))

enum mipi_dphy_chip_id {
	CHIP_ID_RK1808 = 0,
	CHIP_ID_RK3288,
	CHIP_ID_RK3326,
	CHIP_ID_RK3368,
	CHIP_ID_RK3399,
	CHIP_ID_RK1126,
};

enum mipi_dphy_rx_pads {
	MIPI_DPHY_RX_PAD_SINK = 0,
	MIPI_DPHY_RX_PAD_SOURCE,
	MIPI_DPHY_RX_PADS_NUM,
};

enum dphy_reg_id {
	GRF_DPHY_RX0_TURNDISABLE = 0,
	GRF_DPHY_RX0_FORCERXMODE,
	GRF_DPHY_RX0_FORCETXSTOPMODE,
	GRF_DPHY_RX0_ENABLE,
	GRF_DPHY_RX0_TESTCLR,
	GRF_DPHY_RX0_TESTCLK,
	GRF_DPHY_RX0_TESTEN,
	GRF_DPHY_RX0_TESTDIN,
	GRF_DPHY_RX0_TURNREQUEST,
	GRF_DPHY_RX0_TESTDOUT,
	GRF_DPHY_TX0_TURNDISABLE,
	GRF_DPHY_TX0_FORCERXMODE,
	GRF_DPHY_TX0_FORCETXSTOPMODE,
	GRF_DPHY_TX0_TURNREQUEST,
	GRF_DPHY_TX1RX1_TURNDISABLE,
	GRF_DPHY_TX1RX1_FORCERXMODE,
	GRF_DPHY_TX1RX1_FORCETXSTOPMODE,
	GRF_DPHY_TX1RX1_ENABLE,
	GRF_DPHY_TX1RX1_MASTERSLAVEZ,
	GRF_DPHY_TX1RX1_BASEDIR,
	GRF_DPHY_TX1RX1_ENABLECLK,
	GRF_DPHY_TX1RX1_TURNREQUEST,
	GRF_DPHY_RX1_SRC_SEL,
	/* rk3288 only */
	GRF_CON_DISABLE_ISP,
	GRF_CON_ISP_DPHY_SEL,
	GRF_DSI_CSI_TESTBUS_SEL,
	GRF_DVP_V18SEL,
	/* rk1808 & rk3326 & rv1126 */
	GRF_DPHY_CSIPHY_FORCERXMODE,
	GRF_DPHY_CSIPHY_CLKLANE_EN,
	GRF_DPHY_CSIPHY_DATALANE_EN,
	/* rv1126 only */
	GRF_DPHY_CLK_INV_SEL,
	GRF_DPHY_SEL,
	/* rk3368 only */
	GRF_ISP_MIPI_CSI_HOST_SEL,
	/* below is for rk3399 only */
	GRF_DPHY_RX0_CLK_INV_SEL,
	GRF_DPHY_RX1_CLK_INV_SEL,
	GRF_DPHY_TX1RX1_SRC_SEL,
};

enum csiphy_reg_id {
	CSIPHY_CTRL_LANE_ENABLE = 0,
	CSIPHY_CTRL_PWRCTL,
	CSIPHY_CTRL_DIG_RST,
	CSIPHY_CLK_THS_SETTLE,
	CSIPHY_LANE0_THS_SETTLE,
	CSIPHY_LANE1_THS_SETTLE,
	CSIPHY_LANE2_THS_SETTLE,
	CSIPHY_LANE3_THS_SETTLE,
	CSIPHY_CLK_CALIB_ENABLE,
	CSIPHY_LANE0_CALIB_ENABLE,
	CSIPHY_LANE1_CALIB_ENABLE,
	CSIPHY_LANE2_CALIB_ENABLE,
	CSIPHY_LANE3_CALIB_ENABLE,
	//rv1126 only
	CSIPHY_MIPI_LVDS_MODEL,
	CSIPHY_LVDS_MODE,
};

enum mipi_dphy_ctl_type {
	MIPI_DPHY_CTL_GRF_ONLY = 0,
	MIPI_DPHY_CTL_CSI_HOST
};

enum mipi_dphy_lane {
	MIPI_DPHY_LANE_CLOCK = 0,
	MIPI_DPHY_LANE_DATA0,
	MIPI_DPHY_LANE_DATA1,
	MIPI_DPHY_LANE_DATA2,
	MIPI_DPHY_LANE_DATA3
};

enum txrx_reg_id {
	TXRX_PHY_TEST_CTRL0 = 0,
	TXRX_PHY_TEST_CTRL1,
	TXRX_PHY_SHUTDOWNZ,
	TXRX_PHY_RSTZ,
};

struct dphy_reg {
	u32 offset;
	u32 mask;
	u32 shift;
};

struct txrx_reg {
	u32 offset;
};

struct csiphy_reg {
	u32 offset;
};

#define PHY_REG(_offset, _width, _shift) \
	{ .offset = _offset, .mask = BIT(_width) - 1, .shift = _shift, }

#define TXRX_REG(_offset) \
	{ .offset = _offset, }

#define CSIPHY_REG(_offset) \
	{ .offset = _offset, }

static const struct dphy_reg rk1808_grf_dphy_regs[] = {
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RK1808_GRF_PD_VI_CON_OFFSET, 4, 0),
	[GRF_DPHY_CSIPHY_CLKLANE_EN] = PHY_REG(RK1808_GRF_PD_VI_CON_OFFSET, 1, 8),
	[GRF_DPHY_CSIPHY_DATALANE_EN] = PHY_REG(RK1808_GRF_PD_VI_CON_OFFSET, 4, 4),
};

static const struct dphy_reg rk3288_grf_dphy_regs[] = {
	[GRF_CON_DISABLE_ISP] = PHY_REG(RK3288_GRF_SOC_CON6, 1, 0),
	[GRF_CON_ISP_DPHY_SEL] = PHY_REG(RK3288_GRF_SOC_CON6, 1, 1),
	[GRF_DSI_CSI_TESTBUS_SEL] = PHY_REG(RK3288_GRF_SOC_CON6, 1, 14),
	[GRF_DPHY_TX0_TURNDISABLE] = PHY_REG(RK3288_GRF_SOC_CON8, 4, 0),
	[GRF_DPHY_TX0_FORCERXMODE] = PHY_REG(RK3288_GRF_SOC_CON8, 4, 4),
	[GRF_DPHY_TX0_FORCETXSTOPMODE] = PHY_REG(RK3288_GRF_SOC_CON8, 4, 8),
	[GRF_DPHY_TX1RX1_TURNDISABLE] = PHY_REG(RK3288_GRF_SOC_CON9, 4, 0),
	[GRF_DPHY_TX1RX1_FORCERXMODE] = PHY_REG(RK3288_GRF_SOC_CON9, 4, 4),
	[GRF_DPHY_TX1RX1_FORCETXSTOPMODE] = PHY_REG(RK3288_GRF_SOC_CON9, 4, 8),
	[GRF_DPHY_TX1RX1_ENABLE] = PHY_REG(RK3288_GRF_SOC_CON9, 4, 12),
	[GRF_DPHY_RX0_TURNDISABLE] = PHY_REG(RK3288_GRF_SOC_CON10, 4, 0),
	[GRF_DPHY_RX0_FORCERXMODE] = PHY_REG(RK3288_GRF_SOC_CON10, 4, 4),
	[GRF_DPHY_RX0_FORCETXSTOPMODE] = PHY_REG(RK3288_GRF_SOC_CON10, 4, 8),
	[GRF_DPHY_RX0_ENABLE] = PHY_REG(RK3288_GRF_SOC_CON10, 4, 12),
	[GRF_DPHY_RX0_TESTCLR] = PHY_REG(RK3288_GRF_SOC_CON14, 1, 0),
	[GRF_DPHY_RX0_TESTCLK] = PHY_REG(RK3288_GRF_SOC_CON14, 1, 1),
	[GRF_DPHY_RX0_TESTEN] = PHY_REG(RK3288_GRF_SOC_CON14, 1, 2),
	[GRF_DPHY_RX0_TESTDIN] = PHY_REG(RK3288_GRF_SOC_CON14, 8, 3),
	[GRF_DPHY_TX1RX1_ENABLECLK] = PHY_REG(RK3288_GRF_SOC_CON14, 1, 12),
	[GRF_DPHY_RX1_SRC_SEL] = PHY_REG(RK3288_GRF_SOC_CON14, 1, 13),
	[GRF_DPHY_TX1RX1_MASTERSLAVEZ] = PHY_REG(RK3288_GRF_SOC_CON14, 1, 14),
	[GRF_DPHY_TX1RX1_BASEDIR] = PHY_REG(RK3288_GRF_SOC_CON14, 1, 15),
	[GRF_DPHY_RX0_TURNREQUEST] = PHY_REG(RK3288_GRF_SOC_CON15, 4, 0),
	[GRF_DPHY_TX1RX1_TURNREQUEST] = PHY_REG(RK3288_GRF_SOC_CON15, 4, 4),
	[GRF_DPHY_TX0_TURNREQUEST] = PHY_REG(RK3288_GRF_SOC_CON15, 3, 8),
	[GRF_DVP_V18SEL] = PHY_REG(RK3288_GRF_IO_VSEL, 1, 1),
	[GRF_DPHY_RX0_TESTDOUT] = PHY_REG(RK3288_GRF_SOC_STATUS21, 8, 0),
};

static const struct dphy_reg rk3326_grf_dphy_regs[] = {
	[GRF_DVP_V18SEL] = PHY_REG(RK3326_GRF_IO_VSEL_OFFSET, 1, 4),
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RK3326_GRF_PD_VI_CON_OFFSET, 4, 0),
	[GRF_DPHY_CSIPHY_CLKLANE_EN] = PHY_REG(RK3326_GRF_PD_VI_CON_OFFSET, 1, 8),
	[GRF_DPHY_CSIPHY_DATALANE_EN] = PHY_REG(RK3326_GRF_PD_VI_CON_OFFSET, 4, 4),
};

static const struct dphy_reg rk3368_grf_dphy_regs[] = {
	[GRF_DVP_V18SEL] = PHY_REG(RK3368_GRF_IO_VSEL_OFFSET, 1, 1),
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RK3368_GRF_SOC_CON6_OFFSET, 4, 8),
	[GRF_ISP_MIPI_CSI_HOST_SEL] = PHY_REG(RK3368_GRF_SOC_CON6_OFFSET, 1, 1),
	[GRF_CON_DISABLE_ISP] = PHY_REG(RK3368_GRF_SOC_CON6_OFFSET, 1, 0),
};

static const struct dphy_reg rk3399_grf_dphy_regs[] = {
	[GRF_DPHY_RX0_TURNREQUEST] = PHY_REG(RK3399_GRF_SOC_CON9, 4, 0),
	[GRF_DPHY_RX0_CLK_INV_SEL] = PHY_REG(RK3399_GRF_SOC_CON9, 1, 10),
	[GRF_DPHY_RX1_CLK_INV_SEL] = PHY_REG(RK3399_GRF_SOC_CON9, 1, 11),
	[GRF_DPHY_RX0_ENABLE] = PHY_REG(RK3399_GRF_SOC_CON21, 4, 0),
	[GRF_DPHY_RX0_FORCERXMODE] = PHY_REG(RK3399_GRF_SOC_CON21, 4, 4),
	[GRF_DPHY_RX0_FORCETXSTOPMODE] = PHY_REG(RK3399_GRF_SOC_CON21, 4, 8),
	[GRF_DPHY_RX0_TURNDISABLE] = PHY_REG(RK3399_GRF_SOC_CON21, 4, 12),
	[GRF_DPHY_TX0_FORCERXMODE] = PHY_REG(RK3399_GRF_SOC_CON22, 4, 0),
	[GRF_DPHY_TX0_FORCETXSTOPMODE] = PHY_REG(RK3399_GRF_SOC_CON22, 4, 4),
	[GRF_DPHY_TX0_TURNDISABLE] = PHY_REG(RK3399_GRF_SOC_CON22, 4, 8),
	[GRF_DPHY_TX0_TURNREQUEST] = PHY_REG(RK3399_GRF_SOC_CON22, 4, 12),
	[GRF_DPHY_TX1RX1_ENABLE] = PHY_REG(RK3399_GRF_SOC_CON23, 4, 0),
	[GRF_DPHY_TX1RX1_FORCERXMODE] = PHY_REG(RK3399_GRF_SOC_CON23, 4, 4),
	[GRF_DPHY_TX1RX1_FORCETXSTOPMODE] = PHY_REG(RK3399_GRF_SOC_CON23, 4, 8),
	[GRF_DPHY_TX1RX1_TURNDISABLE] = PHY_REG(RK3399_GRF_SOC_CON23, 4, 12),
	[GRF_DPHY_TX1RX1_TURNREQUEST] = PHY_REG(RK3399_GRF_SOC_CON24, 4, 0),
	[GRF_DPHY_TX1RX1_SRC_SEL] = PHY_REG(RK3399_GRF_SOC_CON24, 1, 4),
	[GRF_DPHY_TX1RX1_BASEDIR] = PHY_REG(RK3399_GRF_SOC_CON24, 1, 5),
	[GRF_DPHY_TX1RX1_ENABLECLK] = PHY_REG(RK3399_GRF_SOC_CON24, 1, 6),
	[GRF_DPHY_TX1RX1_MASTERSLAVEZ] = PHY_REG(RK3399_GRF_SOC_CON24, 1, 7),
	[GRF_DPHY_RX0_TESTDIN] = PHY_REG(RK3399_GRF_SOC_CON25, 8, 0),
	[GRF_DPHY_RX0_TESTEN] = PHY_REG(RK3399_GRF_SOC_CON25, 1, 8),
	[GRF_DPHY_RX0_TESTCLK] = PHY_REG(RK3399_GRF_SOC_CON25, 1, 9),
	[GRF_DPHY_RX0_TESTCLR] = PHY_REG(RK3399_GRF_SOC_CON25, 1, 10),
	[GRF_DPHY_RX0_TESTDOUT] = PHY_REG(RK3399_GRF_SOC_STATUS1, 8, 0),
	[GRF_DVP_V18SEL] = PHY_REG(RK3399_GRF_IO_VSEL, 1, 1),
};

static const struct dphy_reg rv1126_grf_dphy0_regs[] = {
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RV1126_GRF_CSIPHY0_CON, 4, 0),
	[GRF_DPHY_CSIPHY_DATALANE_EN] = PHY_REG(RV1126_GRF_CSIPHY0_CON, 4, 4),
	[GRF_DPHY_CSIPHY_CLKLANE_EN] = PHY_REG(RV1126_GRF_CSIPHY0_CON, 1, 8),
	[GRF_DPHY_CLK_INV_SEL] = PHY_REG(RV1126_GRF_CSIPHY0_CON, 1, 9),
	[GRF_DPHY_SEL] = PHY_REG(RV1126_GRF_IOFUNC_CON3, 3, 9),
};

static const struct dphy_reg rv1126_grf_dphy1_regs[] = {
	[GRF_DPHY_CSIPHY_FORCERXMODE] = PHY_REG(RV1126_GRF_CSIPHY1_CON, 4, 0),
	[GRF_DPHY_CSIPHY_DATALANE_EN] = PHY_REG(RV1126_GRF_CSIPHY1_CON, 4, 4),
	[GRF_DPHY_CSIPHY_CLKLANE_EN] = PHY_REG(RV1126_GRF_CSIPHY1_CON, 1, 8),
	[GRF_DPHY_CLK_INV_SEL] = PHY_REG(RV1126_GRF_CSIPHY1_CON, 1, 9),
	[GRF_DPHY_SEL] = PHY_REG(RV1126_GRF_IOFUNC_CON3, 3, 9),
};

static const struct txrx_reg rk3288_txrx_regs[] = {
	[TXRX_PHY_TEST_CTRL0] = TXRX_REG(RK3288_PHY_TEST_CTRL0),
	[TXRX_PHY_TEST_CTRL1] = TXRX_REG(RK3288_PHY_TEST_CTRL1),
	[TXRX_PHY_SHUTDOWNZ] = TXRX_REG(RK3288_PHY_SHUTDOWNZ),
	[TXRX_PHY_RSTZ] = TXRX_REG(RK3288_PHY_RSTZ),
};

static const struct txrx_reg rk3399_txrx_regs[] = {
	[TXRX_PHY_TEST_CTRL0] = TXRX_REG(RK3399_PHY_TEST_CTRL0),
	[TXRX_PHY_TEST_CTRL1] = TXRX_REG(RK3399_PHY_TEST_CTRL1),
	[TXRX_PHY_SHUTDOWNZ] = TXRX_REG(RK3399_PHY_SHUTDOWNZ),
	[TXRX_PHY_RSTZ] = TXRX_REG(RK3399_PHY_RSTZ),
};

static const struct csiphy_reg rk1808_csiphy_regs[] = {
	[CSIPHY_CTRL_LANE_ENABLE] = CSIPHY_REG(RK1808_CSI_DPHY_CTRL_LANE_ENABLE),
	[CSIPHY_CTRL_PWRCTL] = CSIPHY_REG(RK1808_CSI_DPHY_CTRL_PWRCTL),
	[CSIPHY_CTRL_DIG_RST] = CSIPHY_REG(RK1808_CSI_DPHY_CTRL_DIG_RST),
	[CSIPHY_CLK_THS_SETTLE] = CSIPHY_REG(RK1808_CSI_DPHY_CLK_WR_THS_SETTLE),
	[CSIPHY_LANE0_THS_SETTLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE0_WR_THS_SETTLE),
	[CSIPHY_LANE1_THS_SETTLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE1_WR_THS_SETTLE),
	[CSIPHY_LANE2_THS_SETTLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE2_WR_THS_SETTLE),
	[CSIPHY_LANE3_THS_SETTLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE3_WR_THS_SETTLE),
	[CSIPHY_CLK_CALIB_ENABLE] = CSIPHY_REG(RK1808_CSI_DPHY_CLK_CALIB_EN),
	[CSIPHY_LANE0_CALIB_ENABLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE0_CALIB_EN),
	[CSIPHY_LANE1_CALIB_ENABLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE1_CALIB_EN),
	[CSIPHY_LANE2_CALIB_ENABLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE2_CALIB_EN),
	[CSIPHY_LANE3_CALIB_ENABLE] = CSIPHY_REG(RK1808_CSI_DPHY_LANE3_CALIB_EN),
};

static const struct csiphy_reg rk3326_csiphy_regs[] = {
	[CSIPHY_CTRL_LANE_ENABLE] = CSIPHY_REG(RK3326_CSI_DPHY_CTRL_LANE_ENABLE),
	[CSIPHY_CTRL_PWRCTL] = CSIPHY_REG(RK3326_CSI_DPHY_CTRL_PWRCTL),
	[CSIPHY_CTRL_DIG_RST] = CSIPHY_REG(RK3326_CSI_DPHY_CTRL_DIG_RST),
	[CSIPHY_CLK_THS_SETTLE] = CSIPHY_REG(RK3326_CSI_DPHY_CLK_WR_THS_SETTLE),
	[CSIPHY_LANE0_THS_SETTLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE0_WR_THS_SETTLE),
	[CSIPHY_LANE1_THS_SETTLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE1_WR_THS_SETTLE),
	[CSIPHY_LANE2_THS_SETTLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE2_WR_THS_SETTLE),
	[CSIPHY_LANE3_THS_SETTLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE3_WR_THS_SETTLE),
	[CSIPHY_CLK_CALIB_ENABLE] = CSIPHY_REG(RK3326_CSI_DPHY_CLK_CALIB_EN),
	[CSIPHY_LANE0_CALIB_ENABLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE0_CALIB_EN),
	[CSIPHY_LANE1_CALIB_ENABLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE1_CALIB_EN),
	[CSIPHY_LANE2_CALIB_ENABLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE2_CALIB_EN),
	[CSIPHY_LANE3_CALIB_ENABLE] = CSIPHY_REG(RK3326_CSI_DPHY_LANE3_CALIB_EN),
};

static const struct csiphy_reg rk3368_csiphy_regs[] = {
	[CSIPHY_CTRL_LANE_ENABLE] = CSIPHY_REG(RK3368_CSI_DPHY_CTRL_LANE_ENABLE),
	[CSIPHY_CTRL_PWRCTL] = CSIPHY_REG(RK3368_CSI_DPHY_CTRL_PWRCTL),
	[CSIPHY_CTRL_DIG_RST] = CSIPHY_REG(RK3368_CSI_DPHY_CTRL_DIG_RST),
	[CSIPHY_CLK_THS_SETTLE] = CSIPHY_REG(RK3368_CSI_DPHY_CLK_WR_THS_SETTLE),
	[CSIPHY_LANE0_THS_SETTLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE0_WR_THS_SETTLE),
	[CSIPHY_LANE1_THS_SETTLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE1_WR_THS_SETTLE),
	[CSIPHY_LANE2_THS_SETTLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE2_WR_THS_SETTLE),
	[CSIPHY_LANE3_THS_SETTLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE3_WR_THS_SETTLE),
	[CSIPHY_CLK_CALIB_ENABLE] = CSIPHY_REG(RK3368_CSI_DPHY_CLK_CALIB_EN),
	[CSIPHY_LANE0_CALIB_ENABLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE0_CALIB_EN),
	[CSIPHY_LANE1_CALIB_ENABLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE1_CALIB_EN),
	[CSIPHY_LANE2_CALIB_ENABLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE2_CALIB_EN),
	[CSIPHY_LANE3_CALIB_ENABLE] = CSIPHY_REG(RK3368_CSI_DPHY_LANE3_CALIB_EN),
};

static const struct csiphy_reg rv1126_csiphy_regs[] = {
	[CSIPHY_CTRL_LANE_ENABLE] = CSIPHY_REG(RV1126_CSI_DPHY_CTRL_LANE_ENABLE),
	[CSIPHY_CTRL_PWRCTL] = CSIPHY_REG(RV1126_CSI_DPHY_CTRL_PWRCTL),
	[CSIPHY_CTRL_DIG_RST] = CSIPHY_REG(RV1126_CSI_DPHY_CTRL_DIG_RST),
	[CSIPHY_CLK_THS_SETTLE] = CSIPHY_REG(RV1126_CSI_DPHY_CLK_WR_THS_SETTLE),
	[CSIPHY_LANE0_THS_SETTLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE0_WR_THS_SETTLE),
	[CSIPHY_LANE1_THS_SETTLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE1_WR_THS_SETTLE),
	[CSIPHY_LANE2_THS_SETTLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE2_WR_THS_SETTLE),
	[CSIPHY_LANE3_THS_SETTLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE3_WR_THS_SETTLE),
	[CSIPHY_CLK_CALIB_ENABLE] = CSIPHY_REG(RV1126_CSI_DPHY_CLK_CALIB_EN),
	[CSIPHY_LANE0_CALIB_ENABLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE0_CALIB_EN),
	[CSIPHY_LANE1_CALIB_ENABLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE1_CALIB_EN),
	[CSIPHY_LANE2_CALIB_ENABLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE2_CALIB_EN),
	[CSIPHY_LANE3_CALIB_ENABLE] = CSIPHY_REG(RV1126_CSI_DPHY_LANE3_CALIB_EN),
	[CSIPHY_MIPI_LVDS_MODEL] = CSIPHY_REG(RV1126_CSI_DPHY_MIPI_LVDS_MODEL),
	[CSIPHY_LVDS_MODE] = CSIPHY_REG(RV1126_CSI_DPHY_LVDS_MODE),
};

struct hsfreq_range {
	u32 range_h;
	u8 cfg_bit;
};

struct mipidphy_priv;

struct dphy_drv_data {
	const char * const *clks;
	int num_clks;
	const struct hsfreq_range *hsfreq_ranges;
	int num_hsfreq_ranges;
	const struct dphy_reg *grf_regs;
	const struct txrx_reg *txrx_regs;
	const struct csiphy_reg *csiphy_regs;
	enum mipi_dphy_ctl_type ctl_type;
	void (*individual_init)(struct mipidphy_priv *priv);
	enum mipi_dphy_chip_id chip_id;
};

struct sensor_async_subdev {
	struct v4l2_async_subdev asd;
	struct v4l2_mbus_config mbus;
	int lanes;
};

#define MAX_DPHY_CLK		8
#define MAX_DPHY_SENSORS	2

struct mipidphy_sensor {
	struct v4l2_subdev *sd;
	struct v4l2_mbus_config mbus;
	struct v4l2_mbus_framefmt format;
	int lanes;
};

struct mipidphy_priv {
	struct device *dev;
	struct regmap *regmap_grf;
	const struct dphy_reg *grf_regs;
	const struct txrx_reg *txrx_regs;
	const struct csiphy_reg *csiphy_regs;
	void __iomem *csihost_base_addr;
	struct clk *clks[MAX_DPHY_CLK];
	const struct dphy_drv_data *drv_data;
	u64 data_rate_mbps;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev sd;
	struct mutex mutex; /* lock for updating protection */
	struct media_pad pads[MIPI_DPHY_RX_PADS_NUM];
	struct mipidphy_sensor sensors[MAX_DPHY_SENSORS];
	int num_sensors;
	int phy_index;
	bool is_streaming;
	void __iomem *txrx_base_addr;
	int (*stream_on)(struct mipidphy_priv *priv, struct v4l2_subdev *sd);
	int (*stream_off)(struct mipidphy_priv *priv, struct v4l2_subdev *sd);
};

static inline struct mipidphy_priv *to_dphy_priv(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct mipidphy_priv, sd);
}

static inline void write_grf_reg(struct mipidphy_priv *priv,
				 int index, u8 value)
{
	const struct dphy_reg *reg = &priv->grf_regs[index];
	unsigned int val = HIWORD_UPDATE(value, reg->mask, reg->shift);

	if (reg->offset)
		regmap_write(priv->regmap_grf, reg->offset, val);
}

static inline u32 read_grf_reg(struct mipidphy_priv *priv, int index)
{
	const struct dphy_reg *reg = &priv->grf_regs[index];
	unsigned int val = 0;

	if (reg->offset) {
		regmap_read(priv->regmap_grf, reg->offset, &val);
		val = (val >> reg->shift) & reg->mask;
	}
	return val;
}

static inline void write_txrx_reg(struct mipidphy_priv *priv,
				  int index, u32 value)
{
	const struct txrx_reg *reg = &priv->txrx_regs[index];

	if (reg->offset)
		writel(value, priv->txrx_base_addr + reg->offset);
}

static void mipidphy0_wr_reg(struct mipidphy_priv *priv,
			     u8 test_code, u8 test_data)
{
	/*
	 * With the falling edge on TESTCLK, the TESTDIN[7:0] signal content
	 * is latched internally as the current test code. Test data is
	 * programmed internally by rising edge on TESTCLK.
	 */
	write_grf_reg(priv, GRF_DPHY_RX0_TESTCLK, 1);
	write_grf_reg(priv, GRF_DPHY_RX0_TESTDIN, test_code);
	write_grf_reg(priv, GRF_DPHY_RX0_TESTEN, 1);
	write_grf_reg(priv, GRF_DPHY_RX0_TESTCLK, 0);
	write_grf_reg(priv, GRF_DPHY_RX0_TESTEN, 0);
	write_grf_reg(priv, GRF_DPHY_RX0_TESTDIN, test_data);
	write_grf_reg(priv, GRF_DPHY_RX0_TESTCLK, 1);
}

static void mipidphy1_wr_reg(struct mipidphy_priv *priv, unsigned char addr,
			     unsigned char data)
{
	/*
	 * TESTEN =1,TESTDIN=addr
	 * TESTCLK=0
	 * TESTEN =0,TESTDIN=data
	 * TESTCLK=1
	 */
	write_txrx_reg(priv, TXRX_PHY_TEST_CTRL1, PHY_TESTEN_ADDR | addr);
	write_txrx_reg(priv, TXRX_PHY_TEST_CTRL0, 0x00);
	write_txrx_reg(priv, TXRX_PHY_TEST_CTRL1, PHY_TESTEN_DATA | data);
	write_txrx_reg(priv, TXRX_PHY_TEST_CTRL0, 0x02);
}

static inline void write_csiphy_reg(struct mipidphy_priv *priv,
				    int index, u32 value)
{
	const struct csiphy_reg *reg = &priv->csiphy_regs[index];

	if (reg->offset != MIPI_CSI_DPHY_CTRL_INVALID_OFFSET)
		writel(value, priv->csihost_base_addr + reg->offset);
}

static inline void read_csiphy_reg(struct mipidphy_priv *priv,
				    int index, u32 *value)
{
	const struct csiphy_reg *reg = &priv->csiphy_regs[index];

	if (reg->offset != MIPI_CSI_DPHY_CTRL_INVALID_OFFSET)
		*value = readl(priv->csihost_base_addr + reg->offset);
}

static void csi_mipidphy_wr_ths_settle(struct mipidphy_priv *priv, int hsfreq,
				       enum mipi_dphy_lane lane)
{
	unsigned int val = 0;
	unsigned int offset;

	switch (lane) {
	case MIPI_DPHY_LANE_CLOCK:
		offset = CSIPHY_CLK_THS_SETTLE;
		break;
	case MIPI_DPHY_LANE_DATA0:
		offset = CSIPHY_LANE0_THS_SETTLE;
		break;
	case MIPI_DPHY_LANE_DATA1:
		offset = CSIPHY_LANE1_THS_SETTLE;
		break;
	case MIPI_DPHY_LANE_DATA2:
		offset = CSIPHY_LANE2_THS_SETTLE;
		break;
	case MIPI_DPHY_LANE_DATA3:
		offset = CSIPHY_LANE3_THS_SETTLE;
		break;
	default:
		return;
	}

	read_csiphy_reg(priv, offset, &val);
	val = (val & ~0x7f) | hsfreq;
	write_csiphy_reg(priv, offset, val);
}

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[MIPI_DPHY_RX_PAD_SINK];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_entity_remote_pad(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct mipidphy_sensor *sd_to_sensor(struct mipidphy_priv *priv,
					    struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < priv->num_sensors; ++i)
		if (priv->sensors[i].sd == sd)
			return &priv->sensors[i];

	return NULL;
}

static int mipidphy_get_sensor_data_rate(struct v4l2_subdev *sd)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct v4l2_ctrl *link_freq;
	struct v4l2_querymenu qm = { .id = V4L2_CID_LINK_FREQ, };
	int ret;

	link_freq = v4l2_ctrl_find(sensor_sd->ctrl_handler, V4L2_CID_LINK_FREQ);
	if (!link_freq) {
		v4l2_warn(sd, "No pixel rate control in subdev\n");
		return -EPIPE;
	}

	qm.index = v4l2_ctrl_g_ctrl(link_freq);
	ret = v4l2_querymenu(sensor_sd->ctrl_handler, &qm);
	if (ret < 0) {
		v4l2_err(sd, "Failed to get menu item\n");
		return ret;
	}

	if (!qm.value) {
		v4l2_err(sd, "Invalid link_freq\n");
		return -EINVAL;
	}
	priv->data_rate_mbps = qm.value * 2;
	do_div(priv->data_rate_mbps, 1000 * 1000);
	v4l2_info(sd, "data_rate_mbps %lld\n", priv->data_rate_mbps);
	return 0;
}

static int mipidphy_update_sensor_mbus(struct v4l2_subdev *sd)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipidphy_sensor *sensor = sd_to_sensor(priv, sensor_sd);
	struct v4l2_mbus_config mbus;
	int ret;

	ret = v4l2_subdev_call(sensor_sd, pad, get_mbus_config, 0, &mbus);
	if (ret)
		return ret;

	sensor->mbus = mbus;
	switch (mbus.flags & V4L2_MBUS_CSI2_LANES) {
	case V4L2_MBUS_CSI2_1_LANE:
		sensor->lanes = 1;
		break;
	case V4L2_MBUS_CSI2_2_LANE:
		sensor->lanes = 2;
		break;
	case V4L2_MBUS_CSI2_3_LANE:
		sensor->lanes = 3;
		break;
	case V4L2_MBUS_CSI2_4_LANE:
		sensor->lanes = 4;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void rk1126_mipidphy_dphy_sel(struct v4l2_subdev *sd)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	char *model;
	u8 oldval, newval;

	model = sd->v4l2_dev->mdev->model;
	oldval = read_grf_reg(priv, GRF_DPHY_SEL);
	newval = oldval;
	if (!strncmp(model, "rkcif_lite_mipi_lvds", sizeof("rkcif_lite_mipi_lvds") - 1)) {
		if (priv->phy_index == 0)
			newval &= ~RV1126_GRF_PHY1_SEL_CIFLITE;
		else
			newval |= RV1126_GRF_PHY1_SEL_CIFLITE;
	} else if (!strncmp(model, "rkcif_mipi_lvds", sizeof("rkcif_mipi_lvds") - 1)) {
		if (priv->phy_index == 0)
			newval &= ~RV1126_GRF_PHY1_SEL_CIF;
		else
			newval |= RV1126_GRF_PHY1_SEL_CIF;
	} else {
		if (priv->phy_index == 0)
			newval &= ~RV1126_GRF_PHY1_SEL_ISP;
		else
			newval |= RV1126_GRF_PHY1_SEL_ISP;
	}

	if (newval != oldval)
		write_grf_reg(priv, GRF_DPHY_SEL, newval);
}

static int mipidphy_s_stream_start(struct v4l2_subdev *sd)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	int  ret = 0;

	if (priv->is_streaming)
		return 0;

	ret = mipidphy_get_sensor_data_rate(sd);
	if (ret < 0)
		return ret;

	if (priv->drv_data->chip_id == CHIP_ID_RK1126)
		rk1126_mipidphy_dphy_sel(sd);

	mipidphy_update_sensor_mbus(sd);
	priv->stream_on(priv, sd);

	priv->is_streaming = true;

	return 0;
}

static int mipidphy_s_stream_stop(struct v4l2_subdev *sd)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);

	if (!priv->is_streaming)
		return 0;

	if (priv->stream_off)
		priv->stream_off(priv, sd);
	priv->is_streaming = false;

	return 0;
}

static int mipidphy_s_stream(struct v4l2_subdev *sd, int on)
{
	int ret = 0;
	struct mipidphy_priv *priv = to_dphy_priv(sd);

	dev_info(priv->dev, "stream on:%d\n", on);
	mutex_lock(&priv->mutex);
	if (on)
		ret = mipidphy_s_stream_start(sd);
	else
		ret = mipidphy_s_stream_stop(sd);
	mutex_unlock(&priv->mutex);
	return ret;
}

static int mipidphy_g_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	if (sensor)
		return v4l2_subdev_call(sensor, video, g_frame_interval, fi);

	return -EINVAL;
}

static int mipidphy_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				  struct v4l2_mbus_config *config)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipidphy_sensor *sensor;

	if (!sensor_sd)
		return -ENODEV;
	sensor = sd_to_sensor(priv, sensor_sd);
	mipidphy_update_sensor_mbus(sd);
	*config = sensor->mbus;

	return 0;
}

static int mipidphy_s_power(struct v4l2_subdev *sd, int on)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);

	if (on)
		return pm_runtime_get_sync(priv->dev);
	else
		return pm_runtime_put(priv->dev);
}

static int mipidphy_runtime_suspend(struct device *dev)
{
	struct media_entity *me = dev_get_drvdata(dev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	int i, num_clks;

	num_clks = priv->drv_data->num_clks;
	for (i = num_clks - 1; i >= 0; i--)
		if (!IS_ERR(priv->clks[i]))
			clk_disable_unprepare(priv->clks[i]);

	return 0;
}

static int mipidphy_runtime_resume(struct device *dev)
{
	struct media_entity *me = dev_get_drvdata(dev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	int i, num_clks, ret;

	num_clks = priv->drv_data->num_clks;
	for (i = 0; i < num_clks; i++) {
		if (!IS_ERR(priv->clks[i])) {
			ret = clk_prepare_enable(priv->clks[i]);
			if (ret < 0)
				goto err;
		}
	}

	return 0;
err:
	while (--i >= 0)
		clk_disable_unprepare(priv->clks[i]);
	return ret;
}

/* dphy accepts all fmt/size from sensor */
static int mipidphy_get_set_fmt(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *fmt)
{
	struct mipidphy_priv *priv = to_dphy_priv(sd);
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipidphy_sensor *sensor = sd_to_sensor(priv, sensor_sd);
	int ret;
	/*
	 * Do not allow format changes and just relay whatever
	 * set currently in the sensor.
	 */
	if (!sensor_sd)
		return -ENODEV;
	ret = v4l2_subdev_call(sensor_sd, pad, get_fmt, NULL, fmt);
	if (!ret && fmt->pad == 0)
		sensor->format = fmt->format;
	return ret;
}

static int mipidphy_get_selection(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_selection *sel)
{
	struct v4l2_subdev *sensor = get_remote_sensor(sd);

	return v4l2_subdev_call(sensor, pad, get_selection, NULL, sel);
}

static const struct v4l2_subdev_pad_ops mipidphy_subdev_pad_ops = {
	.set_fmt = mipidphy_get_set_fmt,
	.get_fmt = mipidphy_get_set_fmt,
	.get_selection = mipidphy_get_selection,
	.get_mbus_config = mipidphy_g_mbus_config,
};

static const struct v4l2_subdev_core_ops mipidphy_core_ops = {
	.s_power = mipidphy_s_power,
};

static const struct v4l2_subdev_video_ops mipidphy_video_ops = {
	.g_frame_interval = mipidphy_g_frame_interval,
	.s_stream = mipidphy_s_stream,
};

static const struct v4l2_subdev_ops mipidphy_subdev_ops = {
	.core = &mipidphy_core_ops,
	.video = &mipidphy_video_ops,
	.pad = &mipidphy_subdev_pad_ops,
};

/* These tables must be sorted by .range_h ascending. */
static const struct hsfreq_range rk1808_rv1126_mipidphy_hsfreq_ranges[] = {
	{ 109, 0x02}, { 149, 0x03}, { 199, 0x06}, { 249, 0x06},
	{ 299, 0x06}, { 399, 0x08}, { 499, 0x0b}, { 599, 0x0e},
	{ 699, 0x10}, { 799, 0x12}, { 999, 0x16}, {1199, 0x1e},
	{1399, 0x23}, {1599, 0x2d}, {1799, 0x32}, {1999, 0x37},
	{2199, 0x3c}, {2399, 0x41}, {2499, 0x46}
};

static const struct hsfreq_range rk3288_mipidphy_hsfreq_ranges[] = {
	{  89, 0x00}, {  99, 0x10}, { 109, 0x20}, { 129, 0x01},
	{ 139, 0x11}, { 149, 0x21}, { 169, 0x02}, { 179, 0x12},
	{ 199, 0x22}, { 219, 0x03}, { 239, 0x13}, { 249, 0x23},
	{ 269, 0x04}, { 299, 0x14}, { 329, 0x05}, { 359, 0x15},
	{ 399, 0x25}, { 449, 0x06}, { 499, 0x16}, { 549, 0x07},
	{ 599, 0x17}, { 649, 0x08}, { 699, 0x18}, { 749, 0x09},
	{ 799, 0x19}, { 849, 0x29}, { 899, 0x39}, { 949, 0x0a},
	{ 999, 0x1a}
};

static const struct hsfreq_range rk3326_mipidphy_hsfreq_ranges[] = {
	{ 109, 0x00}, { 149, 0x01}, { 199, 0x02}, { 249, 0x03},
	{ 299, 0x04}, { 399, 0x05}, { 499, 0x06}, { 599, 0x07},
	{ 699, 0x08}, { 799, 0x09}, { 899, 0x0a}, {1099, 0x0b},
	{1249, 0x0c}, {1349, 0x0d}, {1500, 0x0e}
};

static const struct hsfreq_range rk3368_mipidphy_hsfreq_ranges[] = {
	{ 109, 0x00}, { 149, 0x01}, { 199, 0x02}, { 249, 0x03},
	{ 299, 0x04}, { 399, 0x05}, { 499, 0x06}, { 599, 0x07},
	{ 699, 0x08}, { 799, 0x09}, { 899, 0x0a}, {1099, 0x0b},
	{1249, 0x0c}, {1349, 0x0d}, {1500, 0x0e}
};

static const struct hsfreq_range rk3399_mipidphy_hsfreq_ranges[] = {
	{  89, 0x00}, {  99, 0x10}, { 109, 0x20}, { 129, 0x01},
	{ 139, 0x11}, { 149, 0x21}, { 169, 0x02}, { 179, 0x12},
	{ 199, 0x22}, { 219, 0x03}, { 239, 0x13}, { 249, 0x23},
	{ 269, 0x04}, { 299, 0x14}, { 329, 0x05}, { 359, 0x15},
	{ 399, 0x25}, { 449, 0x06}, { 499, 0x16}, { 549, 0x07},
	{ 599, 0x17}, { 649, 0x08}, { 699, 0x18}, { 749, 0x09},
	{ 799, 0x19}, { 849, 0x29}, { 899, 0x39}, { 949, 0x0a},
	{ 999, 0x1a}, {1049, 0x2a}, {1099, 0x3a}, {1149, 0x0b},
	{1199, 0x1b}, {1249, 0x2b}, {1299, 0x3b}, {1349, 0x0c},
	{1399, 0x1c}, {1449, 0x2c}, {1500, 0x3c}
};

static const char * const rk1808_mipidphy_clks[] = {
	"pclk",
};

static const char * const rk3288_mipidphy_clks[] = {
	"dphy-ref",
	"pclk",
};

static const char * const rk3326_mipidphy_clks[] = {
	"dphy-ref",
};

static const char * const rk3368_mipidphy_clks[] = {
	"pclk_dphyrx",
};

static const char * const rk3399_mipidphy_clks[] = {
	"dphy-ref",
	"dphy-cfg",
	"grf",
	"pclk_mipi_dsi",
};

static const char * const rv1126_mipidphy_clks[] = {
	"pclk",
};

static void default_mipidphy_individual_init(struct mipidphy_priv *priv)
{
}

static void rk3368_mipidphy_individual_init(struct mipidphy_priv *priv)
{
	/* isp select */
	write_grf_reg(priv, GRF_ISP_MIPI_CSI_HOST_SEL, 1);
}

static void rk3399_mipidphy_individual_init(struct mipidphy_priv *priv)
{
	/*
	 * According to the sequence of RK3399_TXRX_DPHY, the setting of isp0 mipi
	 * will affect txrx dphy in default state of grf_soc_con24.
	 */
	write_grf_reg(priv, GRF_DPHY_TX1RX1_SRC_SEL, 0);
	write_grf_reg(priv, GRF_DPHY_TX1RX1_MASTERSLAVEZ, 0);
	write_grf_reg(priv, GRF_DPHY_TX1RX1_BASEDIR, 0);

	write_grf_reg(priv, GRF_DVP_V18SEL, 0x1);
}

static void rv1126_mipidphy_individual_init(struct mipidphy_priv *priv)
{
	struct device *dev = priv->dev;
	struct device_node *parent = dev->of_node;
	struct device_node *remote = NULL;
	u8 val, sel;

	priv->grf_regs = priv->phy_index ?
		rv1126_grf_dphy1_regs : rv1126_grf_dphy0_regs;
	val = read_grf_reg(priv, GRF_DPHY_SEL);
	/* get port1 remote endpoint info */
	remote = of_graph_get_remote_node(parent, 1, 0);
	if (remote) {
		if (strstr(remote->name, "isp"))
			sel = !priv->phy_index ? 0 : RV1126_GRF_PHY1_SEL_ISP;
		else
			sel = !priv->phy_index ? 0 :
				RV1126_GRF_PHY1_SEL_CIF | RV1126_GRF_PHY1_SEL_CIFLITE;
		of_node_put(remote);
		write_grf_reg(priv, GRF_DPHY_SEL, val | sel);
	}
}

static int mipidphy_rx_stream_on(struct mipidphy_priv *priv,
				 struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipidphy_sensor *sensor = sd_to_sensor(priv, sensor_sd);
	const struct dphy_drv_data *drv_data = priv->drv_data;
	const struct hsfreq_range *hsfreq_ranges = drv_data->hsfreq_ranges;
	int num_hsfreq_ranges = drv_data->num_hsfreq_ranges;
	int i, hsfreq = 0;

	for (i = 0; i < num_hsfreq_ranges; i++) {
		if (hsfreq_ranges[i].range_h >= priv->data_rate_mbps) {
			hsfreq = hsfreq_ranges[i].cfg_bit;
			break;
		}
	}

	if (i == num_hsfreq_ranges) {
		i = num_hsfreq_ranges - 1;
		dev_warn(priv->dev, "data rate: %lld mbps, max support %d mbps",
			 priv->data_rate_mbps, hsfreq_ranges[i].range_h + 1);
		hsfreq = hsfreq_ranges[i].cfg_bit;
	}

	/* RK3288 isp connected to phy0-rx */
	write_grf_reg(priv, GRF_CON_ISP_DPHY_SEL, 0);

	/* Belowed is the sequence of mipi configuration */
	/* Step1: set RSTZ = 1'b0, phy0 controlled by isp0 */

	/* Step2: set SHUTDOWNZ = 1'b0, controlled by isp0 */

	/* Step3: set TESTCLEAR = 1'b1 */
	write_grf_reg(priv, GRF_DPHY_RX0_TESTCLK, 1);
	write_grf_reg(priv, GRF_DPHY_RX0_TESTCLR, 1);
	usleep_range(100, 150);

	/* Step4: apply REFCLK signal with the appropriate frequency */

	/* Step5: apply CFG_CLK signal with the appropriate frequency */

	/* Step6: set MASTERSLAVEZ = 1'b0 (for SLAVE), phy0 default is slave */

	/* Step7: set BASEDIR_N = 1’b1 (for SLAVE), phy0 default is slave */

	/*
	 * Step8: set all REQUEST inputs to zero, need to wait 15ns:
	 * step8.1:set lan turndisab as 1
	 * step8.2:set lan turnrequest as 0
	 */
	write_grf_reg(priv, GRF_DPHY_RX0_TURNDISABLE, 0xf);
	write_grf_reg(priv, GRF_DPHY_RX0_FORCERXMODE, 0);
	write_grf_reg(priv, GRF_DPHY_RX0_TURNREQUEST, 0);
	usleep_range(100, 150);

	/* Step9: set TESTCLR to low, need to wait 15ns */
	write_grf_reg(priv, GRF_DPHY_RX0_TESTCLR, 0);
	usleep_range(100, 150);

	/*
	 * Step10: configure Test Code 0x44 hsfreqrange according to values
	 * step10.1:set clock lane
	 * step10.2:set hsfreqrange by lane0(test code 0x44)
	 */
	hsfreq <<= 1;
	mipidphy0_wr_reg(priv, CLOCK_LANE_HS_RX_CONTROL, 0);
	mipidphy0_wr_reg(priv, LANE0_HS_RX_CONTROL, hsfreq);
	mipidphy0_wr_reg(priv, LANE1_HS_RX_CONTROL, hsfreq);
	mipidphy0_wr_reg(priv, LANE2_HS_RX_CONTROL, hsfreq);
	mipidphy0_wr_reg(priv, LANE3_HS_RX_CONTROL, hsfreq);

	/* Step11: Configure analog references: of Test Code 0x22 */

	/* Step12: Set ENABLE_N=1'b1, need to wait 5ns */
	/* set lane num */
	write_grf_reg(priv, GRF_DPHY_RX0_ENABLE, GENMASK(sensor->lanes - 1, 0));

	/* Step13: Set SHUTDOWNZ=1'b1, controlled by isp need to wait 5ns */

	/* Step14: Set RSTZ=1'b1, controlled by isp */

	/*
	 * Step15: Wait until STOPSTATEDATA_N & STOPSTATECLK
	 *         outputs are asserted
	 */

	usleep_range(100, 150);

	return 0;
}

static int mipidphy_txrx_stream_on(struct mipidphy_priv *priv,
				   struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipidphy_sensor *sensor = sd_to_sensor(priv, sensor_sd);
	const struct dphy_drv_data *drv_data = priv->drv_data;
	const struct hsfreq_range *hsfreq_ranges = drv_data->hsfreq_ranges;
	int num_hsfreq_ranges = drv_data->num_hsfreq_ranges;
	int i, hsfreq = 0;

	for (i = 0; i < num_hsfreq_ranges; i++) {
		if (hsfreq_ranges[i].range_h >= priv->data_rate_mbps) {
			hsfreq = hsfreq_ranges[i].cfg_bit;
			break;
		}
	}

	if (i == num_hsfreq_ranges) {
		i = num_hsfreq_ranges - 1;
		dev_warn(priv->dev, "data rate: %lld mbps, max support %d mbps",
			 priv->data_rate_mbps, hsfreq_ranges[i].range_h + 1);
		hsfreq = hsfreq_ranges[i].cfg_bit;
	}

	/*
	 *Config rk3288:
	 *step1:rk3288 isp connected to phy1-rx
	 *step2:rk3288 phy1-rx test bus connected to csi host
	 *step3:rk3288 phy1-rx source selected as: isp = 1'b1,csi-host = 1'b0
	 */
	write_grf_reg(priv, GRF_CON_ISP_DPHY_SEL, 1);
	write_grf_reg(priv, GRF_DSI_CSI_TESTBUS_SEL, 1);
	write_grf_reg(priv, GRF_DPHY_RX1_SRC_SEL, 1);

	/*
	 * Config rk3399：
	 * step1:rk3399 phy1-rx source selected as:1'b0=isp1,1'b1=isp0
	 */
	write_grf_reg(priv, GRF_DPHY_TX1RX1_SRC_SEL, 0);

	/* Belowed is the sequence of mipi configuration */
	/* Step1: set RSTZ = 1'b0, phy1-rx controlled by isp */

	/* Step2: set SHUTDOWNZ = 1'b0, phy1-rx controlled by isp */

	/* Step3: set TESTCLR= 1'b1,TESTCLK=1'b1 */
	write_txrx_reg(priv, TXRX_PHY_TEST_CTRL0, PHY_TESTCLR | PHY_TESTCLK);
	usleep_range(100, 150);

	/* Step4: apply REFCLK signal with the appropriate frequency */

	/* Step5: apply CFG_CLK signal with the appropriate frequency */

	/*
	 * Step6: set MASTERSLAVEZ = 1'b0 (for SLAVE),
	 *        phy1 is set as slave,controlled by isp
	 */
	write_grf_reg(priv, GRF_DPHY_TX1RX1_MASTERSLAVEZ, 0);

	/*
	 * Step7: set BASEDIR_N = 1’b1 (for SLAVE),
	 *        phy1 is set as slave,controlled by isp
	 */
	write_grf_reg(priv, GRF_DPHY_TX1RX1_BASEDIR, 1);

	/* Step8: set all REQUEST inputs to zero, need to wait 15ns */
	write_grf_reg(priv, GRF_DPHY_TX1RX1_FORCERXMODE, 0);
	write_grf_reg(priv, GRF_DPHY_TX1RX1_FORCETXSTOPMODE, 0);
	write_grf_reg(priv, GRF_DPHY_TX1RX1_TURNREQUEST, 0);
	write_grf_reg(priv, GRF_DPHY_TX1RX1_TURNDISABLE, 0xf);
	usleep_range(100, 150);

	/* Step9: set TESTCLR=1'b0,TESTCLK=1'b1 need to wait 15ns */
	write_txrx_reg(priv, TXRX_PHY_TEST_CTRL0, PHY_TESTCLK);
	usleep_range(100, 150);

	/*
	 * Step10: configure Test Code 0x44 hsfreqrange according to values
	 * step10.1:set clock lane
	 * step10.2:set hsfreqrange by lane0(test code 0x44)
	 */
	hsfreq <<= 1;
	mipidphy1_wr_reg(priv, CLOCK_LANE_HS_RX_CONTROL, 0);
	mipidphy1_wr_reg(priv, LANE0_HS_RX_CONTROL, hsfreq);
	mipidphy1_wr_reg(priv, LANE1_HS_RX_CONTROL, 0);
	mipidphy1_wr_reg(priv, LANE2_HS_RX_CONTROL, 0);
	mipidphy1_wr_reg(priv, LANE3_HS_RX_CONTROL, 0);

	/* Step11: Configure analog references: of Test Code 0x22 */

	/*
	 * Step12: Set ENABLE_N=1'b1, need to wait 5ns
	 * Set lane num:
	 * for 3288,controlled by isp,enable lanes actually
	 * is set by grf_soc_con9[12:15];
	 * for 3399,controlled by isp1,enable lanes actually
	 * is set by isp1,
	 * if run 3399 here operates grf_soc_con23[0:3]
	 */
	write_grf_reg(priv, GRF_DPHY_TX1RX1_ENABLE,
		      GENMASK(sensor->lanes - 1, 0));

	/*
	 * Step13:Set SHUTDOWNZ=1'b1, phy1-rx controlled by isp,
	 *        need to wait 5ns
	 */

	/* Step14:Set RSTZ=1'b1, phy1-rx controlled by isp*/

	/*
	 * Step15:Wait until STOPSTATEDATA_N & STOPSTATECLK
	 *        outputs are asserted
	 */

	usleep_range(100, 150);

	return 0;
}

static int csi_mipidphy_stream_on(struct mipidphy_priv *priv,
				  struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct mipidphy_sensor *sensor = sd_to_sensor(priv, sensor_sd);
	const struct dphy_drv_data *drv_data = priv->drv_data;
	const struct hsfreq_range *hsfreq_ranges = drv_data->hsfreq_ranges;
	int num_hsfreq_ranges = drv_data->num_hsfreq_ranges;
	int i, hsfreq = 0;
	u32 val = 0;

	write_grf_reg(priv, GRF_DVP_V18SEL, 0x1);

	/* phy start */
	write_csiphy_reg(priv, CSIPHY_CTRL_PWRCTL, 0xe4);

	/* set data lane num and enable clock lane */
	write_csiphy_reg(priv, CSIPHY_CTRL_LANE_ENABLE,
		((GENMASK(sensor->lanes - 1, 0) << MIPI_CSI_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT) |
		(0x1 << MIPI_CSI_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT) | 0x1));

	/* Reset dphy analog part */
	write_csiphy_reg(priv, CSIPHY_CTRL_PWRCTL, 0xe0);
	usleep_range(500, 1000);

	if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY) {
		/* Reset dphy digital part */
		write_csiphy_reg(priv, CSIPHY_CTRL_DIG_RST, 0x1e);
		write_csiphy_reg(priv, CSIPHY_CTRL_DIG_RST, 0x1f);
	} else {
		/* Disable MIPI internal logical and switch to LVDS bank */
		write_csiphy_reg(priv, CSIPHY_CTRL_DIG_RST, 0x3e);
		/* Enable LVDS mode */
		write_csiphy_reg(priv, CSIPHY_MIPI_LVDS_MODEL, 0x4);
		switch (sensor->format.code) {
		case MEDIA_BUS_FMT_Y12_1X12:
		case MEDIA_BUS_FMT_SRGGB12_1X12:
		case MEDIA_BUS_FMT_SBGGR12_1X12:
		case MEDIA_BUS_FMT_SGBRG12_1X12:
		case MEDIA_BUS_FMT_SGRBG12_1X12:
			val = 0x1f; //12bit
			break;
		case MEDIA_BUS_FMT_Y10_1X10:
		case MEDIA_BUS_FMT_SBGGR10_1X10:
		case MEDIA_BUS_FMT_SRGGB10_1X10:
		case MEDIA_BUS_FMT_SGBRG10_1X10:
		case MEDIA_BUS_FMT_SGRBG10_1X10:
			val = 0xf; //10bit
			break;
		default:
			val = 0x2f; //8bit
		}
		/* Enable LVDS internal logical and select bit mode */
		write_csiphy_reg(priv, CSIPHY_LVDS_MODE, val);
	}

	/* not into receive mode/wait stopstate */
	write_grf_reg(priv, GRF_DPHY_CSIPHY_FORCERXMODE, 0x0);

	/* enable calibration */
	if (priv->data_rate_mbps > 1500) {
		write_csiphy_reg(priv, CSIPHY_CLK_CALIB_ENABLE, 0x80);
		if (sensor->lanes > 0x00)
			write_csiphy_reg(priv, CSIPHY_LANE0_CALIB_ENABLE, 0x80);
		if (sensor->lanes > 0x01)
			write_csiphy_reg(priv, CSIPHY_LANE1_CALIB_ENABLE, 0x80);
		if (sensor->lanes > 0x02)
			write_csiphy_reg(priv, CSIPHY_LANE2_CALIB_ENABLE, 0x80);
		if (sensor->lanes > 0x03)
			write_csiphy_reg(priv, CSIPHY_LANE3_CALIB_ENABLE, 0x80);
	}

	/* set clock lane and data lane */
	for (i = 0; i < num_hsfreq_ranges; i++) {
		if (hsfreq_ranges[i].range_h >= priv->data_rate_mbps) {
			hsfreq = hsfreq_ranges[i].cfg_bit;
			break;
		}
	}

	if (i == num_hsfreq_ranges) {
		i = num_hsfreq_ranges - 1;
		dev_warn(priv->dev, "data rate: %lld mbps, max support %d mbps",
			 priv->data_rate_mbps, hsfreq_ranges[i].range_h + 1);
		hsfreq = hsfreq_ranges[i].cfg_bit;
	}

	csi_mipidphy_wr_ths_settle(priv, hsfreq, MIPI_DPHY_LANE_CLOCK);
	if (sensor->lanes > 0x00)
		csi_mipidphy_wr_ths_settle(priv, hsfreq, MIPI_DPHY_LANE_DATA0);
	if (sensor->lanes > 0x01)
		csi_mipidphy_wr_ths_settle(priv, hsfreq, MIPI_DPHY_LANE_DATA1);
	if (sensor->lanes > 0x02)
		csi_mipidphy_wr_ths_settle(priv, hsfreq, MIPI_DPHY_LANE_DATA2);
	if (sensor->lanes > 0x03)
		csi_mipidphy_wr_ths_settle(priv, hsfreq, MIPI_DPHY_LANE_DATA3);

	write_grf_reg(priv, GRF_DPHY_CLK_INV_SEL, 0x1);
	write_grf_reg(priv, GRF_DPHY_CSIPHY_CLKLANE_EN, 0x1);
	write_grf_reg(priv, GRF_DPHY_CSIPHY_DATALANE_EN,
		      GENMASK(sensor->lanes - 1, 0));
	return 0;
}

static int csi_mipidphy_stream_off(struct mipidphy_priv *priv,
				   struct v4l2_subdev *sd)
{
	/* disable all lanes */
	write_csiphy_reg(priv, CSIPHY_CTRL_LANE_ENABLE, 0x01);
	/* disable pll and ldo */
	write_csiphy_reg(priv, CSIPHY_CTRL_PWRCTL, 0xe3);
	usleep_range(500, 1000);

	return 0;
}

static const struct dphy_drv_data rk1808_mipidphy_drv_data = {
	.clks = rk1808_mipidphy_clks,
	.num_clks = ARRAY_SIZE(rk1808_mipidphy_clks),
	.hsfreq_ranges = rk1808_rv1126_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk1808_rv1126_mipidphy_hsfreq_ranges),
	.grf_regs = rk1808_grf_dphy_regs,
	.csiphy_regs = rk1808_csiphy_regs,
	.ctl_type = MIPI_DPHY_CTL_CSI_HOST,
	.individual_init = default_mipidphy_individual_init,
	.chip_id = CHIP_ID_RK1808,
};

static const struct dphy_drv_data rk3288_mipidphy_drv_data = {
	.clks = rk3288_mipidphy_clks,
	.num_clks = ARRAY_SIZE(rk3288_mipidphy_clks),
	.hsfreq_ranges = rk3288_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3288_mipidphy_hsfreq_ranges),
	.grf_regs = rk3288_grf_dphy_regs,
	.txrx_regs = rk3288_txrx_regs,
	.ctl_type = MIPI_DPHY_CTL_GRF_ONLY,
	.individual_init = default_mipidphy_individual_init,
	.chip_id = CHIP_ID_RK3288,
};

static const struct dphy_drv_data rk3326_mipidphy_drv_data = {
	.clks = rk3326_mipidphy_clks,
	.num_clks = ARRAY_SIZE(rk3326_mipidphy_clks),
	.hsfreq_ranges = rk3326_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3326_mipidphy_hsfreq_ranges),
	.grf_regs = rk3326_grf_dphy_regs,
	.csiphy_regs = rk3326_csiphy_regs,
	.ctl_type = MIPI_DPHY_CTL_CSI_HOST,
	.individual_init = default_mipidphy_individual_init,
	.chip_id = CHIP_ID_RK3326,
};

static const struct dphy_drv_data rk3368_mipidphy_drv_data = {
	.clks = rk3368_mipidphy_clks,
	.num_clks = ARRAY_SIZE(rk3368_mipidphy_clks),
	.hsfreq_ranges = rk3368_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3368_mipidphy_hsfreq_ranges),
	.grf_regs = rk3368_grf_dphy_regs,
	.csiphy_regs = rk3368_csiphy_regs,
	.ctl_type = MIPI_DPHY_CTL_CSI_HOST,
	.individual_init = rk3368_mipidphy_individual_init,
	.chip_id = CHIP_ID_RK3368,
};

static const struct dphy_drv_data rk3399_mipidphy_drv_data = {
	.clks = rk3399_mipidphy_clks,
	.num_clks = ARRAY_SIZE(rk3399_mipidphy_clks),
	.hsfreq_ranges = rk3399_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3399_mipidphy_hsfreq_ranges),
	.grf_regs = rk3399_grf_dphy_regs,
	.txrx_regs = rk3399_txrx_regs,
	.ctl_type = MIPI_DPHY_CTL_GRF_ONLY,
	.individual_init = rk3399_mipidphy_individual_init,
	.chip_id = CHIP_ID_RK3399,
};

static const struct dphy_drv_data rv1126_mipidphy_drv_data = {
	.clks = rv1126_mipidphy_clks,
	.num_clks = ARRAY_SIZE(rv1126_mipidphy_clks),
	.hsfreq_ranges = rk1808_rv1126_mipidphy_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk1808_rv1126_mipidphy_hsfreq_ranges),
	.csiphy_regs = rv1126_csiphy_regs,
	.ctl_type = MIPI_DPHY_CTL_CSI_HOST,
	.individual_init = rv1126_mipidphy_individual_init,
	.chip_id = CHIP_ID_RK1126,
};

static const struct of_device_id rockchip_mipidphy_match_id[] = {
	{
		.compatible = "rockchip,rk1808-mipi-dphy-rx",
		.data = &rk1808_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3288-mipi-dphy",
		.data = &rk3288_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3326-mipi-dphy",
		.data = &rk3326_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3368-mipi-dphy",
		.data = &rk3368_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rk3399-mipi-dphy",
		.data = &rk3399_mipidphy_drv_data,
	},
	{
		.compatible = "rockchip,rv1126-csi-dphy",
		.data = &rv1126_mipidphy_drv_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_mipidphy_match_id);

/* The .bound() notifier callback when a match is found */
static int
rockchip_mipidphy_notifier_bound(struct v4l2_async_notifier *notifier,
				 struct v4l2_subdev *sd,
				 struct v4l2_async_subdev *asd)
{
	struct mipidphy_priv *priv = container_of(notifier,
						  struct mipidphy_priv,
						  notifier);
	struct sensor_async_subdev *s_asd = container_of(asd,
					struct sensor_async_subdev, asd);
	struct mipidphy_sensor *sensor;
	unsigned int pad, ret;

	if (priv->num_sensors == ARRAY_SIZE(priv->sensors))
		return -EBUSY;

	sensor = &priv->sensors[priv->num_sensors++];
	sensor->lanes = s_asd->lanes;
	sensor->mbus = s_asd->mbus;
	sensor->sd = sd;
	dev_info(priv->dev, "match %s:bus type %d\n", sd->name, s_asd->mbus.type);

	for (pad = 0; pad < sensor->sd->entity.num_pads; pad++)
		if (sensor->sd->entity.pads[pad].flags & MEDIA_PAD_FL_SOURCE)
			break;

	if (pad == sensor->sd->entity.num_pads) {
		dev_err(priv->dev,
			"failed to find src pad for %s\n",
			sensor->sd->name);

		return -ENXIO;
	}

	ret = media_create_pad_link(
			&sensor->sd->entity, pad,
			&priv->sd.entity, MIPI_DPHY_RX_PAD_SINK,
			priv->num_sensors != 1 ? 0 : MEDIA_LNK_FL_ENABLED);
	if (ret) {
		dev_err(priv->dev,
			"failed to create link for %s\n",
			sensor->sd->name);
		return ret;
	}

	return 0;
}

/* The .unbind callback */
static void
rockchip_mipidphy_notifier_unbind(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct mipidphy_priv *priv = container_of(notifier,
						  struct mipidphy_priv,
						  notifier);
	struct mipidphy_sensor *sensor = sd_to_sensor(priv, sd);

	sensor->sd = NULL;
}

static const struct
v4l2_async_notifier_operations rockchip_mipidphy_async_ops = {
	.bound = rockchip_mipidphy_notifier_bound,
	.unbind = rockchip_mipidphy_notifier_unbind,
};

static int rockchip_mipidphy_fwnode_parse(struct device *dev,
					  struct v4l2_fwnode_endpoint *vep,
					  struct v4l2_async_subdev *asd)
{
	struct sensor_async_subdev *s_asd =
			container_of(asd, struct sensor_async_subdev, asd);
	struct v4l2_mbus_config *config = &s_asd->mbus;

	if (vep->base.port != 0) {
		dev_err(dev, "The PHY has only port 0\n");
		return -EINVAL;
	}

	if (vep->bus_type == V4L2_MBUS_CSI2_DPHY) {
		config->type = V4L2_MBUS_CSI2_DPHY;
		config->flags = vep->bus.mipi_csi2.flags;
		s_asd->lanes = vep->bus.mipi_csi2.num_data_lanes;
	} else if (vep->bus_type == V4L2_MBUS_CCP2) {
		/* V4L2_MBUS_CCP2 for lvds */
		config->type = V4L2_MBUS_CCP2;
		s_asd->lanes = vep->bus.mipi_csi1.data_lane;
	} else {
		dev_err(dev, "Only CSI2 and CCP2 bus type is currently supported\n");
		return -EINVAL;
	}

	switch (s_asd->lanes) {
	case 1:
		config->flags |= V4L2_MBUS_CSI2_1_LANE;
		break;
	case 2:
		config->flags |= V4L2_MBUS_CSI2_2_LANE;
		break;
	case 3:
		config->flags |= V4L2_MBUS_CSI2_3_LANE;
		break;
	case 4:
		config->flags |= V4L2_MBUS_CSI2_4_LANE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rockchip_mipidphy_media_init(struct mipidphy_priv *priv)
{
	int ret;

	priv->pads[MIPI_DPHY_RX_PAD_SOURCE].flags =
		MEDIA_PAD_FL_SOURCE | MEDIA_PAD_FL_MUST_CONNECT;
	priv->pads[MIPI_DPHY_RX_PAD_SINK].flags =
		MEDIA_PAD_FL_SINK | MEDIA_PAD_FL_MUST_CONNECT;
	priv->sd.entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	ret = media_entity_pads_init(&priv->sd.entity,
				MIPI_DPHY_RX_PADS_NUM, priv->pads);
	if (ret < 0)
		return ret;

	v4l2_async_notifier_init(&priv->notifier);

	ret = v4l2_async_notifier_parse_fwnode_endpoints_by_port(
		priv->dev, &priv->notifier,
		sizeof(struct sensor_async_subdev), 0,
		rockchip_mipidphy_fwnode_parse);
	if (ret < 0)
		return ret;

	priv->sd.subdev_notifier = &priv->notifier;
	priv->notifier.ops = &rockchip_mipidphy_async_ops;
	ret = v4l2_async_subdev_notifier_register(&priv->sd, &priv->notifier);
	if (ret) {
		dev_err(priv->dev,
			"failed to register async notifier : %d\n", ret);
		v4l2_async_notifier_cleanup(&priv->notifier);
		return ret;
	}

	return v4l2_async_register_subdev(&priv->sd);
}

static int rockchip_mipidphy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *sd;
	struct mipidphy_priv *priv;
	struct regmap *grf;
	struct resource *res;
	const struct of_device_id *of_id;
	const struct dphy_drv_data *drv_data;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;

	of_id = of_match_device(rockchip_mipidphy_match_id, dev);
	if (!of_id)
		return -EINVAL;

	grf = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(grf)) {
		grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						      "rockchip,grf");
		if (IS_ERR(grf)) {
			dev_err(dev, "Can't find GRF syscon\n");
			return -ENODEV;
		}
	}
	priv->regmap_grf = grf;

	priv->phy_index = of_alias_get_id(dev->of_node, "dphy");
	if (priv->phy_index < 0)
		priv->phy_index = 0;

	drv_data = of_id->data;
	for (i = 0; i < drv_data->num_clks; i++) {
		priv->clks[i] = devm_clk_get(dev, drv_data->clks[i]);

		if (IS_ERR(priv->clks[i]))
			dev_dbg(dev, "Failed to get %s\n", drv_data->clks[i]);
	}

	priv->grf_regs = drv_data->grf_regs;
	priv->txrx_regs = drv_data->txrx_regs;
	priv->csiphy_regs = drv_data->csiphy_regs;
	priv->drv_data = drv_data;
	if (drv_data->ctl_type == MIPI_DPHY_CTL_CSI_HOST) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		priv->csihost_base_addr = devm_ioremap_resource(dev, res);
		priv->stream_on = csi_mipidphy_stream_on;
		priv->stream_off = csi_mipidphy_stream_off;
	} else {
		priv->stream_on = mipidphy_txrx_stream_on;
		priv->txrx_base_addr = NULL;
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		priv->txrx_base_addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(priv->txrx_base_addr))
			priv->stream_on = mipidphy_rx_stream_on;
		priv->stream_off = NULL;
	}

	sd = &priv->sd;
	mutex_init(&priv->mutex);
	v4l2_subdev_init(sd, &mipidphy_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(sd->name, sizeof(sd->name), "rockchip-mipi-dphy-rx");
	sd->dev = dev;

	platform_set_drvdata(pdev, &sd->entity);

	ret = rockchip_mipidphy_media_init(priv);
	if (ret < 0)
		goto destroy_mutex;

	pm_runtime_enable(&pdev->dev);
	drv_data->individual_init(priv);
	return 0;

destroy_mutex:
	mutex_destroy(&priv->mutex);
	return 0;
}

static int rockchip_mipidphy_remove(struct platform_device *pdev)
{
	struct media_entity *me = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(me);
	struct mipidphy_priv *priv = platform_get_drvdata(pdev);

	media_entity_cleanup(&sd->entity);

	pm_runtime_disable(&pdev->dev);
	mutex_destroy(&priv->mutex);
	return 0;
}

static const struct dev_pm_ops rockchip_mipidphy_pm_ops = {
	SET_RUNTIME_PM_OPS(mipidphy_runtime_suspend,
			   mipidphy_runtime_resume, NULL)
};

static struct platform_driver rockchip_isp_mipidphy_driver = {
	.probe = rockchip_mipidphy_probe,
	.remove = rockchip_mipidphy_remove,
	.driver = {
			.name = "rockchip-mipi-dphy-rx",
			.pm = &rockchip_mipidphy_pm_ops,
			.of_match_table = rockchip_mipidphy_match_id,
	},
};

module_platform_driver(rockchip_isp_mipidphy_driver);
MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip MIPI RX DPHY driver");
MODULE_LICENSE("Dual BSD/GPL");
