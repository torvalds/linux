// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip MIPI CSI2 DPHY driver
 *
 * Copyright (C) 2021 Rockchip Electronics Co., Ltd.
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
#include <linux/reset.h>
#include "phy-rockchip-csi2-dphy-common.h"

/* GRF REG OFFSET */
#define GRF_VI_CON0	(0x0340)
#define GRF_VI_CON1	(0x0344)

/*RK3588 DPHY GRF REG OFFSET */
#define GRF_DPHY_CON0	(0x0)
#define GRF_SOC_CON2	(0x0308)

/*RV1106 DPHY GRF REG OFFSET */
#define GRF_VI_MISC_CON0	(0x50000)
#define GRF_VI_CSIPHY_CON5	(0x50014)

/*GRF REG BIT DEFINE */
#define GRF_CSI2PHY_LANE_SEL_SPLIT	(0x1)
#define GRF_CSI2PHY_SEL_SPLIT_0_1	(0x0)
#define GRF_CSI2PHY_SEL_SPLIT_2_3	BIT(0)

/* PHY REG OFFSET */
#define CSI2_DPHY_CTRL_INVALID_OFFSET	(0xffff)
#define CSI2_DPHY_CTRL_PWRCTL	\
				CSI2_DPHY_CTRL_INVALID_OFFSET
#define CSI2_DPHY_CTRL_LANE_ENABLE	(0x00)
#define CSI2_DPHY_CLK1_LANE_EN		(0x2C)
#define CSI2_DPHY_DUAL_CAL_EN		(0x80)
#define CSI2_DPHY_CLK_INV		(0X84)

#define CSI2_DPHY_CLK_WR_THS_SETTLE	(0x160)
#define CSI2_DPHY_CLK_CALIB_EN		(0x168)
#define CSI2_DPHY_LANE0_WR_THS_SETTLE	(0x1e0)
#define CSI2_DPHY_LANE0_CALIB_EN	(0x1e8)
#define CSI2_DPHY_LANE1_WR_THS_SETTLE	(0x260)
#define CSI2_DPHY_LANE1_CALIB_EN	(0x268)
#define CSI2_DPHY_LANE2_WR_THS_SETTLE	(0x2e0)
#define CSI2_DPHY_LANE2_CALIB_EN	(0x2e8)
#define CSI2_DPHY_LANE3_WR_THS_SETTLE	(0x360)
#define CSI2_DPHY_LANE3_CALIB_EN	(0x368)
#define CSI2_DPHY_CLK1_WR_THS_SETTLE	(0x3e0)
#define CSI2_DPHY_CLK1_CALIB_EN		(0x3e8)

#define CSI2_DPHY_PATH0_MODE_SEL	(0x44C)
#define CSI2_DPHY_PATH0_LVDS_MODE_SEL	(0x480)
#define CSI2_DPHY_PATH1_MODE_SEL	(0x84C)
#define CSI2_DPHY_PATH1_LVDS_MODE_SEL	(0x880)

/* PHY REG BIT DEFINE */
#define CSI2_DPHY_LANE_MODE_FULL	(0x4)
#define CSI2_DPHY_LANE_MODE_SPLIT	(0x2)
#define CSI2_DPHY_LANE_SPLIT_TOP	(0x1)
#define CSI2_DPHY_LANE_SPLIT_BOT	(0x2)
#define CSI2_DPHY_LANE_SPLIT_LANE0_1	(0x3 << 2)
#define CSI2_DPHY_LANE_SPLIT_LANE2_3	(0x3 << 4)
#define CSI2_DPHY_LANE_DUAL_MODE_EN	BIT(6)
#define CSI2_DPHY_LANE_PARA_ARR_NUM	(0x2)

#define CSI2_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT	2
#define CSI2_DPHY_CTRL_DATALANE_SPLIT_LANE2_3_OFFSET_BIT	4
#define CSI2_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT	6

enum csi2_dphy_index {
	DPHY0 = 0x0,
	DPHY1,
	DPHY2,
};

enum csi2_dphy_lane {
	CSI2_DPHY_LANE_CLOCK = 0,
	CSI2_DPHY_LANE_CLOCK1,
	CSI2_DPHY_LANE_DATA0,
	CSI2_DPHY_LANE_DATA1,
	CSI2_DPHY_LANE_DATA2,
	CSI2_DPHY_LANE_DATA3
};

enum grf_reg_id {
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
	GRF_DPHY_CSI2PHY_FORCERXMODE,
	GRF_DPHY_CSI2PHY_CLKLANE_EN,
	GRF_DPHY_CSI2PHY_DATALANE_EN,
	/* rv1126 only */
	GRF_DPHY_CLK_INV_SEL,
	GRF_DPHY_SEL,
	/* rk3368 only */
	GRF_ISP_MIPI_CSI_HOST_SEL,
	/* below is for rk3399 only */
	GRF_DPHY_RX0_CLK_INV_SEL,
	GRF_DPHY_RX1_CLK_INV_SEL,
	GRF_DPHY_TX1RX1_SRC_SEL,
	/* below is for rk3568 only */
	GRF_DPHY_CSI2PHY_CLKLANE1_EN,
	GRF_DPHY_CLK1_INV_SEL,
	GRF_DPHY_ISP_CSI2PHY_SEL,
	GRF_DPHY_CIF_CSI2PHY_SEL,
	GRF_DPHY_CSI2PHY_LANE_SEL,
	GRF_DPHY_CSI2PHY1_LANE_SEL,
	GRF_DPHY_CSI2PHY_DATALANE_EN0,
	GRF_DPHY_CSI2PHY_DATALANE_EN1,
	GRF_CPHY_MODE,
	GRF_DPHY_CSIHOST2_SEL,
	GRF_DPHY_CSIHOST3_SEL,
	GRF_DPHY_CSIHOST4_SEL,
	GRF_DPHY_CSIHOST5_SEL,
	/* below is for rv1106 only */
	GRF_MIPI_HOST0_SEL,
	GRF_LVDS_HOST0_SEL,
};

enum csi2dphy_reg_id {
	CSI2PHY_REG_CTRL_LANE_ENABLE = 0,
	CSI2PHY_CTRL_PWRCTL,
	CSI2PHY_CTRL_DIG_RST,
	CSI2PHY_CLK_THS_SETTLE,
	CSI2PHY_LANE0_THS_SETTLE,
	CSI2PHY_LANE1_THS_SETTLE,
	CSI2PHY_LANE2_THS_SETTLE,
	CSI2PHY_LANE3_THS_SETTLE,
	CSI2PHY_CLK_CALIB_ENABLE,
	CSI2PHY_LANE0_CALIB_ENABLE,
	CSI2PHY_LANE1_CALIB_ENABLE,
	CSI2PHY_LANE2_CALIB_ENABLE,
	CSI2PHY_LANE3_CALIB_ENABLE,
	//rv1126 only
	CSI2PHY_MIPI_LVDS_MODEL,
	CSI2PHY_LVDS_MODE,
	//rk3568 only
	CSI2PHY_DUAL_CLK_EN,
	CSI2PHY_CLK1_THS_SETTLE,
	CSI2PHY_CLK1_CALIB_ENABLE,
	//rk3588
	CSI2PHY_CLK_LANE_ENABLE,
	CSI2PHY_CLK1_LANE_ENABLE,
	CSI2PHY_DATA_LANE0_ENABLE,
	CSI2PHY_DATA_LANE1_ENABLE,
	CSI2PHY_DATA_LANE2_ENABLE,
	CSI2PHY_DATA_LANE3_ENABLE,
	CSI2PHY_LANE0_ERR_SOT_SYNC,
	CSI2PHY_LANE1_ERR_SOT_SYNC,
	CSI2PHY_LANE2_ERR_SOT_SYNC,
	CSI2PHY_LANE3_ERR_SOT_SYNC,
	CSI2PHY_S0C_GNR_CON1,
	CSI2PHY_COMBO_S0D0_GNR_CON1,
	CSI2PHY_COMBO_S0D1_GNR_CON1,
	CSI2PHY_COMBO_S0D2_GNR_CON1,
	CSI2PHY_S0D3_GNR_CON1,
	CSI2PHY_PATH0_MODEL,
	CSI2PHY_PATH0_LVDS_MODEL,
	CSI2PHY_PATH1_MODEL,
	CSI2PHY_PATH1_LVDS_MODEL,
	CSI2PHY_CLK_INV,
};

#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

#define GRF_REG(_offset, _width, _shift) \
	{ .offset = _offset, .mask = BIT(_width) - 1, .shift = _shift, }

#define CSI2PHY_REG(_offset) \
	{ .offset = _offset, }

struct hsfreq_range {
	u32 range_h;
	u16 cfg_bit;
};

static inline void write_sys_grf_reg(struct csi2_dphy_hw *hw,
				     int index, u8 value)
{
	const struct grf_reg *reg = &hw->grf_regs[index];
	unsigned int val = HIWORD_UPDATE(value, reg->mask, reg->shift);

	if (reg->mask)
		regmap_write(hw->regmap_sys_grf, reg->offset, val);
}

static inline void write_grf_reg(struct csi2_dphy_hw *hw,
				     int index, u8 value)
{
	const struct grf_reg *reg = &hw->grf_regs[index];
	unsigned int val = HIWORD_UPDATE(value, reg->mask, reg->shift);

	if (reg->mask)
		regmap_write(hw->regmap_grf, reg->offset, val);
}

static inline u32 read_grf_reg(struct csi2_dphy_hw *hw, int index)
{
	const struct grf_reg *reg = &hw->grf_regs[index];
	unsigned int val = 0;

	if (reg->mask) {
		regmap_read(hw->regmap_grf, reg->offset, &val);
		val = (val >> reg->shift) & reg->mask;
	}

	return val;
}

static inline void write_csi2_dphy_reg(struct csi2_dphy_hw *hw,
					    int index, u32 value)
{
	const struct csi2dphy_reg *reg = &hw->csi2dphy_regs[index];

	if ((index == CSI2PHY_REG_CTRL_LANE_ENABLE) ||
	    (index == CSI2PHY_CLK_LANE_ENABLE) ||
	    (index != CSI2PHY_REG_CTRL_LANE_ENABLE &&
	     reg->offset != 0x0))
		writel(value, hw->hw_base_addr + reg->offset);
}

static inline void write_csi2_dphy_reg_mask(struct csi2_dphy_hw *hw,
					    int index, u32 value, u32 mask)
{
	const struct csi2dphy_reg *reg = &hw->csi2dphy_regs[index];
	u32 read_val = 0;

	read_val = readl(hw->hw_base_addr + reg->offset);
	read_val &= ~mask;
	read_val |= value;
	writel(read_val, hw->hw_base_addr + reg->offset);
}

static inline void read_csi2_dphy_reg(struct csi2_dphy_hw *hw,
					   int index, u32 *value)
{
	const struct csi2dphy_reg *reg = &hw->csi2dphy_regs[index];

	if ((index == CSI2PHY_REG_CTRL_LANE_ENABLE) ||
	    (index == CSI2PHY_CLK_LANE_ENABLE) ||
	    (index != CSI2PHY_REG_CTRL_LANE_ENABLE &&
	     reg->offset != 0x0))
		*value = readl(hw->hw_base_addr + reg->offset);
}

static void csi_mipidphy_wr_ths_settle(struct csi2_dphy_hw *hw,
					      int hsfreq,
					      enum csi2_dphy_lane lane)
{
	unsigned int val = 0;
	unsigned int offset;

	switch (lane) {
	case CSI2_DPHY_LANE_CLOCK:
		offset = CSI2PHY_CLK_THS_SETTLE;
		break;
	case CSI2_DPHY_LANE_CLOCK1:
		offset = CSI2PHY_CLK1_THS_SETTLE;
		break;
	case CSI2_DPHY_LANE_DATA0:
		offset = CSI2PHY_LANE0_THS_SETTLE;
		break;
	case CSI2_DPHY_LANE_DATA1:
		offset = CSI2PHY_LANE1_THS_SETTLE;
		break;
	case CSI2_DPHY_LANE_DATA2:
		offset = CSI2PHY_LANE2_THS_SETTLE;
		break;
	case CSI2_DPHY_LANE_DATA3:
		offset = CSI2PHY_LANE3_THS_SETTLE;
		break;
	default:
		return;
	}

	read_csi2_dphy_reg(hw, offset, &val);
	val = (val & ~0x7f) | hsfreq;
	write_csi2_dphy_reg(hw, offset, val);
}

static const struct grf_reg rk3568_grf_dphy_regs[] = {
	[GRF_DPHY_CSI2PHY_FORCERXMODE] = GRF_REG(GRF_VI_CON0, 4, 0),
	[GRF_DPHY_CSI2PHY_DATALANE_EN] = GRF_REG(GRF_VI_CON0, 4, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN0] = GRF_REG(GRF_VI_CON0, 2, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN1] = GRF_REG(GRF_VI_CON0, 2, 6),
	[GRF_DPHY_CSI2PHY_CLKLANE_EN] = GRF_REG(GRF_VI_CON0, 1, 8),
	[GRF_DPHY_CLK_INV_SEL] = GRF_REG(GRF_VI_CON0, 1, 9),
	[GRF_DPHY_CSI2PHY_CLKLANE1_EN] = GRF_REG(GRF_VI_CON0, 1, 10),
	[GRF_DPHY_CLK1_INV_SEL] = GRF_REG(GRF_VI_CON0, 1, 11),
	[GRF_DPHY_ISP_CSI2PHY_SEL] = GRF_REG(GRF_VI_CON1, 1, 12),
	[GRF_DPHY_CIF_CSI2PHY_SEL] = GRF_REG(GRF_VI_CON1, 1, 11),
	[GRF_DPHY_CSI2PHY_LANE_SEL] = GRF_REG(GRF_VI_CON1, 1, 7),
};

static const struct csi2dphy_reg rk3568_csi2dphy_regs[] = {
	[CSI2PHY_REG_CTRL_LANE_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CTRL_LANE_ENABLE),
	[CSI2PHY_DUAL_CLK_EN] = CSI2PHY_REG(CSI2_DPHY_DUAL_CAL_EN),
	[CSI2PHY_CLK_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK_WR_THS_SETTLE),
	[CSI2PHY_CLK_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK_CALIB_EN),
	[CSI2PHY_LANE0_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_WR_THS_SETTLE),
	[CSI2PHY_LANE0_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_CALIB_EN),
	[CSI2PHY_LANE1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_WR_THS_SETTLE),
	[CSI2PHY_LANE1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_CALIB_EN),
	[CSI2PHY_LANE2_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_WR_THS_SETTLE),
	[CSI2PHY_LANE2_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_CALIB_EN),
	[CSI2PHY_LANE3_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_WR_THS_SETTLE),
	[CSI2PHY_LANE3_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_CALIB_EN),
	[CSI2PHY_CLK1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_WR_THS_SETTLE),
	[CSI2PHY_CLK1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_CALIB_EN),
};

static const struct grf_reg rk3588_grf_dphy_regs[] = {
	[GRF_DPHY_CSI2PHY_FORCERXMODE] = GRF_REG(GRF_DPHY_CON0, 4, 0),
	[GRF_DPHY_CSI2PHY_DATALANE_EN] = GRF_REG(GRF_DPHY_CON0, 4, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN0] = GRF_REG(GRF_DPHY_CON0, 2, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN1] = GRF_REG(GRF_DPHY_CON0, 2, 6),
	[GRF_DPHY_CSI2PHY_CLKLANE_EN] = GRF_REG(GRF_DPHY_CON0, 1, 8),
	[GRF_DPHY_CLK_INV_SEL] = GRF_REG(GRF_DPHY_CON0, 1, 9),
	[GRF_DPHY_CSI2PHY_CLKLANE1_EN] = GRF_REG(GRF_DPHY_CON0, 1, 10),
	[GRF_DPHY_CLK1_INV_SEL] = GRF_REG(GRF_DPHY_CON0, 1, 11),
	[GRF_DPHY_CSI2PHY_LANE_SEL] = GRF_REG(GRF_SOC_CON2, 1, 6),
	[GRF_DPHY_CSI2PHY1_LANE_SEL] = GRF_REG(GRF_SOC_CON2, 1, 7),
	[GRF_DPHY_CSIHOST2_SEL] = GRF_REG(GRF_SOC_CON2, 1, 8),
	[GRF_DPHY_CSIHOST3_SEL] = GRF_REG(GRF_SOC_CON2, 1, 9),
	[GRF_DPHY_CSIHOST4_SEL] = GRF_REG(GRF_SOC_CON2, 1, 10),
	[GRF_DPHY_CSIHOST5_SEL] = GRF_REG(GRF_SOC_CON2, 1, 11),
};

static const struct csi2dphy_reg rk3588_csi2dphy_regs[] = {
	[CSI2PHY_REG_CTRL_LANE_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CTRL_LANE_ENABLE),
	[CSI2PHY_DUAL_CLK_EN] = CSI2PHY_REG(CSI2_DPHY_DUAL_CAL_EN),
	[CSI2PHY_CLK_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK_WR_THS_SETTLE),
	[CSI2PHY_CLK_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK_CALIB_EN),
	[CSI2PHY_LANE0_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_WR_THS_SETTLE),
	[CSI2PHY_LANE0_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_CALIB_EN),
	[CSI2PHY_LANE1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_WR_THS_SETTLE),
	[CSI2PHY_LANE1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_CALIB_EN),
	[CSI2PHY_LANE2_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_WR_THS_SETTLE),
	[CSI2PHY_LANE2_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_CALIB_EN),
	[CSI2PHY_LANE3_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_WR_THS_SETTLE),
	[CSI2PHY_LANE3_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_CALIB_EN),
	[CSI2PHY_CLK1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_WR_THS_SETTLE),
	[CSI2PHY_CLK1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_CALIB_EN),
	[CSI2PHY_CLK1_LANE_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_LANE_EN),
};

static const struct grf_reg rv1106_grf_dphy_regs[] = {
	[GRF_DPHY_CSI2PHY_FORCERXMODE] = GRF_REG(GRF_VI_CSIPHY_CON5, 4, 0),
	[GRF_DPHY_CSI2PHY_CLKLANE_EN] = GRF_REG(GRF_VI_CSIPHY_CON5, 1, 8),
	[GRF_DPHY_CSI2PHY_DATALANE_EN] = GRF_REG(GRF_VI_CSIPHY_CON5, 4, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN0] = GRF_REG(GRF_VI_CSIPHY_CON5, 2, 4),
	[GRF_DPHY_CSI2PHY_DATALANE_EN1] = GRF_REG(GRF_VI_CSIPHY_CON5, 2, 6),
	[GRF_DPHY_CLK_INV_SEL] = GRF_REG(GRF_VI_CSIPHY_CON5, 1, 9),
	[GRF_DPHY_CSI2PHY_CLKLANE1_EN] = GRF_REG(GRF_VI_CSIPHY_CON5, 1, 10),
	[GRF_DPHY_CLK1_INV_SEL] = GRF_REG(GRF_VI_CSIPHY_CON5, 1, 11),
	[GRF_MIPI_HOST0_SEL] = GRF_REG(GRF_VI_MISC_CON0, 1, 0),
	[GRF_LVDS_HOST0_SEL] = GRF_REG(GRF_VI_MISC_CON0, 1, 2),
};

static const struct csi2dphy_reg rv1106_csi2dphy_regs[] = {
	[CSI2PHY_REG_CTRL_LANE_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CTRL_LANE_ENABLE),
	[CSI2PHY_DUAL_CLK_EN] = CSI2PHY_REG(CSI2_DPHY_DUAL_CAL_EN),
	[CSI2PHY_CLK_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK_WR_THS_SETTLE),
	[CSI2PHY_CLK_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK_CALIB_EN),
	[CSI2PHY_LANE0_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_WR_THS_SETTLE),
	[CSI2PHY_LANE0_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE0_CALIB_EN),
	[CSI2PHY_LANE1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_WR_THS_SETTLE),
	[CSI2PHY_LANE1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE1_CALIB_EN),
	[CSI2PHY_LANE2_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_WR_THS_SETTLE),
	[CSI2PHY_LANE2_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE2_CALIB_EN),
	[CSI2PHY_LANE3_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_WR_THS_SETTLE),
	[CSI2PHY_LANE3_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_LANE3_CALIB_EN),
	[CSI2PHY_CLK1_THS_SETTLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_WR_THS_SETTLE),
	[CSI2PHY_CLK1_CALIB_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_CALIB_EN),
	[CSI2PHY_CLK1_LANE_ENABLE] = CSI2PHY_REG(CSI2_DPHY_CLK1_LANE_EN),
	[CSI2PHY_PATH0_MODEL] = CSI2PHY_REG(CSI2_DPHY_PATH0_MODE_SEL),
	[CSI2PHY_PATH0_LVDS_MODEL] = CSI2PHY_REG(CSI2_DPHY_PATH0_LVDS_MODE_SEL),
	[CSI2PHY_PATH1_MODEL] = CSI2PHY_REG(CSI2_DPHY_PATH1_MODE_SEL),
	[CSI2PHY_PATH1_LVDS_MODEL] = CSI2PHY_REG(CSI2_DPHY_PATH1_LVDS_MODE_SEL),
	[CSI2PHY_CLK_INV] = CSI2PHY_REG(CSI2_DPHY_CLK_INV),
};

/* These tables must be sorted by .range_h ascending. */
static const struct hsfreq_range rk3568_csi2_dphy_hw_hsfreq_ranges[] = {
	{ 109, 0x02}, { 149, 0x03}, { 199, 0x06}, { 249, 0x06},
	{ 299, 0x06}, { 399, 0x08}, { 499, 0x0b}, { 599, 0x0e},
	{ 699, 0x10}, { 799, 0x12}, { 999, 0x16}, {1199, 0x1e},
	{1399, 0x23}, {1599, 0x2d}, {1799, 0x32}, {1999, 0x37},
	{2199, 0x3c}, {2399, 0x41}, {2499, 0x46}
};

static struct v4l2_subdev *get_remote_sensor(struct v4l2_subdev *sd)
{
	struct media_pad *local, *remote;
	struct media_entity *sensor_me;

	local = &sd->entity.pads[CSI2_DPHY_RX_PAD_SINK];
	remote = media_entity_remote_pad(local);
	if (!remote) {
		v4l2_warn(sd, "No link between dphy and sensor\n");
		return NULL;
	}

	sensor_me = media_entity_remote_pad(local)->entity;
	return media_entity_to_v4l2_subdev(sensor_me);
}

static struct csi2_sensor *sd_to_sensor(struct csi2_dphy *dphy,
					   struct v4l2_subdev *sd)
{
	int i;

	for (i = 0; i < dphy->num_sensors; ++i)
		if (dphy->sensors[i].sd == sd)
			return &dphy->sensors[i];

	return NULL;
}

static unsigned char get_lvds_data_width(u32 pixelformat)
{
	switch (pixelformat) {
	/* csi raw8 */
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		return 0x2;
	/* csi raw10 */
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		return 0x0;
	/* csi raw12 */
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		return 0x1;
	/* csi uyvy 422 */
	case MEDIA_BUS_FMT_UYVY8_2X8:
	case MEDIA_BUS_FMT_VYUY8_2X8:
	case MEDIA_BUS_FMT_YUYV8_2X8:
	case MEDIA_BUS_FMT_YVYU8_2X8:
	case MEDIA_BUS_FMT_RGB888_1X24:
		return 0x2;

	default:
		return 0x2;
	}
}

static void csi2_dphy_hw_do_reset(struct csi2_dphy_hw *hw)
{
	if (hw->rsts_bulk)
		reset_control_assert(hw->rsts_bulk);

	udelay(5);

	if (hw->rsts_bulk)
		reset_control_deassert(hw->rsts_bulk);
}

static void csi2_dphy_config_dual_mode(struct csi2_dphy *dphy,
					       struct csi2_sensor *sensor)
{
	struct csi2_dphy_hw *hw = dphy->dphy_hw;
	struct v4l2_subdev *sd = &dphy->sd;
	bool is_cif = false;
	char *model;
	u32 val;

	model = sd->v4l2_dev->mdev->model;
	if (!strncmp(model, "rkcif_mipi_lvds", sizeof("rkcif_mipi_lvds") - 1))
		is_cif = true;
	else
		is_cif = false;

	if (hw->lane_mode == LANE_MODE_FULL) {
		val = ~GRF_CSI2PHY_LANE_SEL_SPLIT;
		if (dphy->phy_index < 3) {
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN, 0x1);
			if (hw->drv_data->chip_id < CHIP_ID_RK3588)
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL, val);
			else
				write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL, val);
		} else {
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN, 0x1);
			if (hw->drv_data->chip_id < CHIP_ID_RK3588)
				write_grf_reg(hw, GRF_DPHY_CSI2PHY1_LANE_SEL, val);
			else
				write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY1_LANE_SEL, val);
		}
	} else {
		val = GRF_CSI2PHY_LANE_SEL_SPLIT;

		switch (dphy->phy_index) {
		case 1:
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN0,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN, 0x1);
			if (hw->drv_data->chip_id < CHIP_ID_RK3588) {
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL, val);
				if (is_cif)
					write_grf_reg(hw, GRF_DPHY_CIF_CSI2PHY_SEL,
						      GRF_CSI2PHY_SEL_SPLIT_0_1);
				else
					write_grf_reg(hw, GRF_DPHY_ISP_CSI2PHY_SEL,
						      GRF_CSI2PHY_SEL_SPLIT_0_1);
			} else if (hw->drv_data->chip_id == CHIP_ID_RK3588) {
				write_sys_grf_reg(hw, GRF_DPHY_CSIHOST2_SEL, 0x0);
				write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL, val);
			} else if (hw->drv_data->chip_id == CHIP_ID_RV1106) {
				if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY)
					write_grf_reg(hw, GRF_MIPI_HOST0_SEL, 0x1);
				else
					write_grf_reg(hw, GRF_LVDS_HOST0_SEL, 0x1);
			}
			break;
		case 2:
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN1,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE1_EN, 0x1);
			if (hw->drv_data->chip_id < CHIP_ID_RK3588) {
				write_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL, val);
				if (is_cif)
					write_grf_reg(hw, GRF_DPHY_CIF_CSI2PHY_SEL,
						GRF_CSI2PHY_SEL_SPLIT_2_3);
				else
					write_grf_reg(hw, GRF_DPHY_ISP_CSI2PHY_SEL,
						GRF_CSI2PHY_SEL_SPLIT_2_3);
			} else if (hw->drv_data->chip_id == CHIP_ID_RK3588) {
				write_sys_grf_reg(hw, GRF_DPHY_CSIHOST3_SEL, 0x1);
				write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY_LANE_SEL, val);
			}
			break;
		case 4:
			write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY1_LANE_SEL, val);
			write_sys_grf_reg(hw, GRF_DPHY_CSIHOST4_SEL, 0x0);
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN0,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE_EN, 0x1);
			break;
		case 5:
			write_sys_grf_reg(hw, GRF_DPHY_CSI2PHY1_LANE_SEL, val);
			write_sys_grf_reg(hw, GRF_DPHY_CSIHOST5_SEL, 0x1);
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_DATALANE_EN1,
				      GENMASK(sensor->lanes - 1, 0));
			write_grf_reg(hw, GRF_DPHY_CSI2PHY_CLKLANE1_EN, 0x1);
			break;
		default:
			break;
		};
	}
}

static int csi2_dphy_hw_stream_on(struct csi2_dphy *dphy,
					struct v4l2_subdev *sd)
{
	struct v4l2_subdev *sensor_sd = get_remote_sensor(sd);
	struct csi2_sensor *sensor = sd_to_sensor(dphy, sensor_sd);
	struct csi2_dphy_hw *hw = dphy->dphy_hw;
	const struct dphy_hw_drv_data *drv_data = hw->drv_data;
	const struct hsfreq_range *hsfreq_ranges = drv_data->hsfreq_ranges;
	int num_hsfreq_ranges = drv_data->num_hsfreq_ranges;
	int i, hsfreq = 0;
	u32 val = 0, pre_val;
	u8 lvds_width = 0;

	mutex_lock(&hw->mutex);

	/* set data lane num and enable clock lane */
	/*
	 * for rk356x: dphy0 is used just for full mode,
	 *             dphy1 is used just for split mode,uses lane0_1,
	 *             dphy2 is used just for split mode,uses lane2_3
	 */
	read_csi2_dphy_reg(hw, CSI2PHY_REG_CTRL_LANE_ENABLE, &pre_val);
	if (hw->lane_mode == LANE_MODE_FULL) {
		val |= (GENMASK(sensor->lanes - 1, 0) <<
			CSI2_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT) |
			(0x1 << CSI2_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT);
	} else {
		if (!(pre_val & (0x1 << CSI2_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT)))
			val |= (0x1 << CSI2_DPHY_CTRL_CLKLANE_ENABLE_OFFSET_BIT);

		if (dphy->phy_index % 3 == DPHY1)
			val |= (GENMASK(sensor->lanes - 1, 0) <<
				CSI2_DPHY_CTRL_DATALANE_ENABLE_OFFSET_BIT);

		if (dphy->phy_index % 3 == DPHY2) {
			val |= (GENMASK(sensor->lanes - 1, 0) <<
				CSI2_DPHY_CTRL_DATALANE_SPLIT_LANE2_3_OFFSET_BIT);
			if (hw->drv_data->chip_id >= CHIP_ID_RK3588)
				write_csi2_dphy_reg(hw, CSI2PHY_CLK1_LANE_ENABLE, BIT(6));
		}
	}
	val |= pre_val;
	write_csi2_dphy_reg(hw, CSI2PHY_REG_CTRL_LANE_ENABLE, val);

	/* Reset dphy digital part */
	if (hw->lane_mode == LANE_MODE_FULL) {
		write_csi2_dphy_reg(hw, CSI2PHY_DUAL_CLK_EN, 0x1e);
		write_csi2_dphy_reg(hw, CSI2PHY_DUAL_CLK_EN, 0x1f);
	} else {
		read_csi2_dphy_reg(hw, CSI2PHY_DUAL_CLK_EN, &val);
		if (!(val & CSI2_DPHY_LANE_DUAL_MODE_EN)) {
			write_csi2_dphy_reg(hw, CSI2PHY_DUAL_CLK_EN, 0x5e);
			write_csi2_dphy_reg(hw, CSI2PHY_DUAL_CLK_EN, 0x5f);
		}
	}
	csi2_dphy_config_dual_mode(dphy, sensor);

	/* not into receive mode/wait stopstate */
	write_grf_reg(hw, GRF_DPHY_CSI2PHY_FORCERXMODE, 0x0);

	/* enable calibration */
	if (dphy->data_rate_mbps > 1500) {
		if (hw->lane_mode == LANE_MODE_FULL) {
			write_csi2_dphy_reg(hw, CSI2PHY_CLK_CALIB_ENABLE, 0x80);
			if (sensor->lanes > 0x00)
				write_csi2_dphy_reg(hw, CSI2PHY_LANE0_CALIB_ENABLE, 0x80);
			if (sensor->lanes > 0x01)
				write_csi2_dphy_reg(hw, CSI2PHY_LANE1_CALIB_ENABLE, 0x80);
			if (sensor->lanes > 0x02)
				write_csi2_dphy_reg(hw, CSI2PHY_LANE2_CALIB_ENABLE, 0x80);
			if (sensor->lanes > 0x03)
				write_csi2_dphy_reg(hw, CSI2PHY_LANE3_CALIB_ENABLE, 0x80);
		} else {
			if (dphy->phy_index % 3 == DPHY1) {
				write_csi2_dphy_reg(hw, CSI2PHY_CLK_CALIB_ENABLE, 0x80);
				if (sensor->lanes > 0x00)
					write_csi2_dphy_reg(hw, CSI2PHY_LANE0_CALIB_ENABLE, 0x80);
				if (sensor->lanes > 0x01)
					write_csi2_dphy_reg(hw, CSI2PHY_LANE1_CALIB_ENABLE, 0x80);
			}

			if (dphy->phy_index % 3 == DPHY2) {
				write_csi2_dphy_reg(hw, CSI2PHY_CLK1_CALIB_ENABLE, 0x80);
				if (sensor->lanes > 0x00)
					write_csi2_dphy_reg(hw, CSI2PHY_LANE2_CALIB_ENABLE, 0x80);
				if (sensor->lanes > 0x01)
					write_csi2_dphy_reg(hw, CSI2PHY_LANE3_CALIB_ENABLE, 0x80);
			}
		}
	}

	/* set clock lane and data lane */
	for (i = 0; i < num_hsfreq_ranges; i++) {
		if (hsfreq_ranges[i].range_h >= dphy->data_rate_mbps) {
			hsfreq = hsfreq_ranges[i].cfg_bit;
			break;
		}
	}

	if (i == num_hsfreq_ranges) {
		i = num_hsfreq_ranges - 1;
		dev_warn(dphy->dev, "data rate: %lld mbps, max support %d mbps",
			 dphy->data_rate_mbps, hsfreq_ranges[i].range_h + 1);
		hsfreq = hsfreq_ranges[i].cfg_bit;
	}

	if (hw->lane_mode == LANE_MODE_FULL) {
		csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_CLOCK);
		if (sensor->lanes > 0x00)
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA0);
		if (sensor->lanes > 0x01)
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA1);
		if (sensor->lanes > 0x02)
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA2);
		if (sensor->lanes > 0x03)
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA3);
	} else {
		if (dphy->phy_index % 3 == DPHY1) {
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_CLOCK);
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA0);
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA1);
		}

		if (dphy->phy_index % 3 == DPHY2) {
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_CLOCK1);
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA2);
			csi_mipidphy_wr_ths_settle(hw, hsfreq, CSI2_DPHY_LANE_DATA3);
		}
	}

	if (hw->drv_data->chip_id == CHIP_ID_RV1106) {
		if (dphy->phy_index % 3 == DPHY0 ||
		    dphy->phy_index % 3 == DPHY1) {
			if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY) {
				write_csi2_dphy_reg(hw, CSI2PHY_PATH0_MODEL, 0x2);
			} else {
				write_csi2_dphy_reg(hw, CSI2PHY_PATH0_MODEL, 0x4);
				lvds_width = get_lvds_data_width(sensor->format.code);
				write_csi2_dphy_reg(hw, CSI2PHY_PATH0_LVDS_MODEL, (lvds_width << 4) | 0X0f);
			}
		} else {
			if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY) {
				write_csi2_dphy_reg(hw, CSI2PHY_PATH1_MODEL, 0x2);
			} else {
				write_csi2_dphy_reg(hw, CSI2PHY_PATH1_MODEL, 0x4);
				lvds_width = get_lvds_data_width(sensor->format.code);
				write_csi2_dphy_reg(hw, CSI2PHY_PATH1_LVDS_MODEL, (lvds_width << 4) | 0X0f);
			}
		}
		if (sensor->mbus.type == V4L2_MBUS_CSI2_DPHY) {
			if (hw->lane_mode == LANE_MODE_FULL)
				write_csi2_dphy_reg(hw, CSI2PHY_CLK_INV, 0x04);
			else
				write_csi2_dphy_reg(hw, CSI2PHY_CLK_INV, 0x14);
		}
	}

	atomic_inc(&hw->stream_cnt);

	mutex_unlock(&hw->mutex);

	return 0;
}

static int csi2_dphy_hw_stream_off(struct csi2_dphy *dphy,
					  struct v4l2_subdev *sd)
{
	struct csi2_dphy_hw *hw = dphy->dphy_hw;

	if (atomic_dec_return(&hw->stream_cnt))
		return 0;

	mutex_lock(&hw->mutex);

	write_csi2_dphy_reg(hw, CSI2PHY_REG_CTRL_LANE_ENABLE, 0x01);
	csi2_dphy_hw_do_reset(hw);
	usleep_range(500, 1000);

	mutex_unlock(&hw->mutex);

	return 0;
}

static int csi2_dphy_hw_ttl_mode_enable(struct csi2_dphy_hw *hw)
{
	int ret = 0;

	ret = clk_bulk_prepare_enable(hw->num_clks, hw->clks_bulk);
	if (ret) {
		dev_err(hw->dev, "failed to enable clks\n");
		return ret;
	}

	write_csi2_dphy_reg(hw, CSI2PHY_REG_CTRL_LANE_ENABLE, 0x7d);
	write_csi2_dphy_reg(hw, CSI2PHY_DUAL_CLK_EN, 0x5f);
	write_csi2_dphy_reg(hw, CSI2PHY_PATH0_MODEL, 0x1);
	write_csi2_dphy_reg(hw, CSI2PHY_PATH1_MODEL, 0x1);
	return ret;
}

static void csi2_dphy_hw_ttl_mode_disable(struct csi2_dphy_hw *hw)
{
	write_csi2_dphy_reg(hw, CSI2PHY_REG_CTRL_LANE_ENABLE, 0x01);
	clk_bulk_disable_unprepare(hw->num_clks, hw->clks_bulk);
}

static void rk3568_csi2_dphy_hw_individual_init(struct csi2_dphy_hw *hw)
{
	hw->grf_regs = rk3568_grf_dphy_regs;
}

static void rk3588_csi2_dphy_hw_individual_init(struct csi2_dphy_hw *hw)
{
	hw->grf_regs = rk3588_grf_dphy_regs;
}

static void rv1106_csi2_dphy_hw_individual_init(struct csi2_dphy_hw *hw)
{
	hw->grf_regs = rv1106_grf_dphy_regs;
}

static const struct dphy_hw_drv_data rk3568_csi2_dphy_hw_drv_data = {
	.hsfreq_ranges = rk3568_csi2_dphy_hw_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3568_csi2_dphy_hw_hsfreq_ranges),
	.csi2dphy_regs = rk3568_csi2dphy_regs,
	.grf_regs = rk3568_grf_dphy_regs,
	.individual_init = rk3568_csi2_dphy_hw_individual_init,
	.chip_id = CHIP_ID_RK3568,
	.stream_on = csi2_dphy_hw_stream_on,
	.stream_off = csi2_dphy_hw_stream_off,
};

static const struct dphy_hw_drv_data rk3588_csi2_dphy_hw_drv_data = {
	.hsfreq_ranges = rk3568_csi2_dphy_hw_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3568_csi2_dphy_hw_hsfreq_ranges),
	.csi2dphy_regs = rk3588_csi2dphy_regs,
	.grf_regs = rk3588_grf_dphy_regs,
	.individual_init = rk3588_csi2_dphy_hw_individual_init,
	.chip_id = CHIP_ID_RK3588,
	.stream_on = csi2_dphy_hw_stream_on,
	.stream_off = csi2_dphy_hw_stream_off,
};

static const struct dphy_hw_drv_data rv1106_csi2_dphy_hw_drv_data = {
	.hsfreq_ranges = rk3568_csi2_dphy_hw_hsfreq_ranges,
	.num_hsfreq_ranges = ARRAY_SIZE(rk3568_csi2_dphy_hw_hsfreq_ranges),
	.csi2dphy_regs = rv1106_csi2dphy_regs,
	.grf_regs = rv1106_grf_dphy_regs,
	.individual_init = rv1106_csi2_dphy_hw_individual_init,
	.chip_id = CHIP_ID_RV1106,
	.stream_on = csi2_dphy_hw_stream_on,
	.stream_off = csi2_dphy_hw_stream_off,
};

static const struct of_device_id rockchip_csi2_dphy_hw_match_id[] = {
	{
		.compatible = "rockchip,rk3568-csi2-dphy-hw",
		.data = &rk3568_csi2_dphy_hw_drv_data,
	},
	{
		.compatible = "rockchip,rk3588-csi2-dphy-hw",
		.data = &rk3588_csi2_dphy_hw_drv_data,
	},
	{
		.compatible = "rockchip,rv1106-csi2-dphy-hw",
		.data = &rv1106_csi2_dphy_hw_drv_data,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_csi2_dphy_hw_match_id);

static int rockchip_csi2_dphy_hw_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct csi2_dphy_hw *dphy_hw;
	struct regmap *grf;
	struct resource *res;
	const struct of_device_id *of_id;
	const struct dphy_hw_drv_data *drv_data;

	dphy_hw = devm_kzalloc(dev, sizeof(*dphy_hw), GFP_KERNEL);
	if (!dphy_hw)
		return -ENOMEM;
	dphy_hw->dev = dev;

	of_id = of_match_device(rockchip_csi2_dphy_hw_match_id, dev);
	if (!of_id)
		return -EINVAL;

	drv_data = of_id->data;

	grf = syscon_regmap_lookup_by_phandle(dev->of_node,
					      "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(dev, "Can't find GRF syscon\n");
		return -ENODEV;
	}
	dphy_hw->regmap_grf = grf;

	if (drv_data->chip_id == CHIP_ID_RK3588) {
		grf = syscon_regmap_lookup_by_phandle(dev->of_node,
						      "rockchip,sys_grf");
		if (IS_ERR(grf)) {
			dev_err(dev, "Can't find SYS GRF syscon\n");
			return -ENODEV;
		}
		dphy_hw->regmap_sys_grf = grf;
	}

	dphy_hw->num_clks = devm_clk_bulk_get_all(dev, &dphy_hw->clks_bulk);
	if (dphy_hw->num_clks < 0)
		dev_err(dev, "failed to get csi2 clks\n");

	dphy_hw->rsts_bulk = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(dphy_hw->rsts_bulk))
		dev_err_probe(dev, PTR_ERR(dphy_hw->rsts_bulk), "failed to get dphy reset\n");

	dphy_hw->dphy_dev_num = 0;
	dphy_hw->drv_data = drv_data;
	dphy_hw->lane_mode = LANE_MODE_UNDEF;
	dphy_hw->grf_regs = drv_data->grf_regs;
	dphy_hw->txrx_regs = drv_data->txrx_regs;
	dphy_hw->csi2dphy_regs = drv_data->csi2dphy_regs;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dphy_hw->hw_base_addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(dphy_hw->hw_base_addr)) {
		resource_size_t offset = res->start;
		resource_size_t size = resource_size(res);

		dphy_hw->hw_base_addr = devm_ioremap(dev, offset, size);
		if (IS_ERR(dphy_hw->hw_base_addr)) {
			dev_err(dev, "Can't find csi2 dphy hw addr!\n");
			return -ENODEV;
		}
	}
	dphy_hw->stream_on = drv_data->stream_on;
	dphy_hw->stream_off = drv_data->stream_off;

	if (drv_data->chip_id == CHIP_ID_RV1106) {
		dphy_hw->ttl_mode_enable = csi2_dphy_hw_ttl_mode_enable;
		dphy_hw->ttl_mode_disable = csi2_dphy_hw_ttl_mode_disable;
	} else {
		dphy_hw->ttl_mode_enable = NULL;
		dphy_hw->ttl_mode_disable = NULL;
	}

	atomic_set(&dphy_hw->stream_cnt, 0);

	mutex_init(&dphy_hw->mutex);

	platform_set_drvdata(pdev, dphy_hw);

	pm_runtime_enable(&pdev->dev);

	dev_info(dev, "csi2 dphy hw probe successfully!\n");

	return 0;
}

static int rockchip_csi2_dphy_hw_remove(struct platform_device *pdev)
{
	struct csi2_dphy_hw *hw = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	mutex_destroy(&hw->mutex);

	return 0;
}

static struct platform_driver rockchip_csi2_dphy_hw_driver = {
	.probe = rockchip_csi2_dphy_hw_probe,
	.remove = rockchip_csi2_dphy_hw_remove,
	.driver = {
		.name = "rockchip-csi2-dphy-hw",
		.of_match_table = rockchip_csi2_dphy_hw_match_id,
	},
};
#if defined(CONFIG_VIDEO_ROCKCHIP_THUNDER_BOOT_ISP) && !defined(CONFIG_INITCALL_ASYNC)
static int __init rockchip_csi2_dphy_hw_init(void)
{
	return platform_driver_register(&rockchip_csi2_dphy_hw_driver);
}

subsys_initcall(rockchip_csi2_dphy_hw_init);
#else
module_platform_driver(rockchip_csi2_dphy_hw_driver);
#endif

MODULE_AUTHOR("Rockchip Camera/ISP team");
MODULE_DESCRIPTION("Rockchip MIPI CSI2 DPHY HW driver");
MODULE_LICENSE("GPL v2");
