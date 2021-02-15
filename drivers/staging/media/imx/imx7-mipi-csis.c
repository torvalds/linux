// SPDX-License-Identifier: GPL-2.0
/*
 * Freescale i.MX7 SoC series MIPI-CSI V3.3 receiver driver
 *
 * Copyright (C) 2019 Linaro Ltd
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2011 - 2013 Samsung Electronics Co., Ltd.
 *
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

#define CSIS_DRIVER_NAME			"imx7-mipi-csis"
#define CSIS_SUBDEV_NAME			CSIS_DRIVER_NAME

#define CSIS_PAD_SINK				0
#define CSIS_PAD_SOURCE				1
#define CSIS_PADS_NUM				2

#define MIPI_CSIS_DEF_PIX_WIDTH			640
#define MIPI_CSIS_DEF_PIX_HEIGHT		480

/* Register map definition */

/* CSIS common control */
#define MIPI_CSIS_CMN_CTRL			0x04
#define MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW	BIT(16)
#define MIPI_CSIS_CMN_CTRL_INTER_MODE		BIT(10)
#define MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW_CTRL	BIT(2)
#define MIPI_CSIS_CMN_CTRL_RESET		BIT(1)
#define MIPI_CSIS_CMN_CTRL_ENABLE		BIT(0)

#define MIPI_CSIS_CMN_CTRL_LANE_NR_OFFSET	8
#define MIPI_CSIS_CMN_CTRL_LANE_NR_MASK		(3 << 8)

/* CSIS clock control */
#define MIPI_CSIS_CLK_CTRL			0x08
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH3(x)	((x) << 28)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH2(x)	((x) << 24)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH1(x)	((x) << 20)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(x)	((x) << 16)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK	(0xf << 4)
#define MIPI_CSIS_CLK_CTRL_WCLK_SRC		BIT(0)

/* CSIS Interrupt mask */
#define MIPI_CSIS_INT_MSK			0x10
#define MIPI_CSIS_INT_MSK_EVEN_BEFORE		BIT(31)
#define MIPI_CSIS_INT_MSK_EVEN_AFTER		BIT(30)
#define MIPI_CSIS_INT_MSK_ODD_BEFORE		BIT(29)
#define MIPI_CSIS_INT_MSK_ODD_AFTER		BIT(28)
#define MIPI_CSIS_INT_MSK_FRAME_START		BIT(24)
#define MIPI_CSIS_INT_MSK_FRAME_END		BIT(20)
#define MIPI_CSIS_INT_MSK_ERR_SOT_HS		BIT(16)
#define MIPI_CSIS_INT_MSK_ERR_LOST_FS		BIT(12)
#define MIPI_CSIS_INT_MSK_ERR_LOST_FE		BIT(8)
#define MIPI_CSIS_INT_MSK_ERR_OVER		BIT(4)
#define MIPI_CSIS_INT_MSK_ERR_WRONG_CFG		BIT(3)
#define MIPI_CSIS_INT_MSK_ERR_ECC		BIT(2)
#define MIPI_CSIS_INT_MSK_ERR_CRC		BIT(1)
#define MIPI_CSIS_INT_MSK_ERR_UNKNOWN		BIT(0)

/* CSIS Interrupt source */
#define MIPI_CSIS_INT_SRC			0x14
#define MIPI_CSIS_INT_SRC_EVEN_BEFORE		BIT(31)
#define MIPI_CSIS_INT_SRC_EVEN_AFTER		BIT(30)
#define MIPI_CSIS_INT_SRC_EVEN			BIT(30)
#define MIPI_CSIS_INT_SRC_ODD_BEFORE		BIT(29)
#define MIPI_CSIS_INT_SRC_ODD_AFTER		BIT(28)
#define MIPI_CSIS_INT_SRC_ODD			(0x3 << 28)
#define MIPI_CSIS_INT_SRC_NON_IMAGE_DATA	(0xf << 28)
#define MIPI_CSIS_INT_SRC_FRAME_START		BIT(24)
#define MIPI_CSIS_INT_SRC_FRAME_END		BIT(20)
#define MIPI_CSIS_INT_SRC_ERR_SOT_HS		BIT(16)
#define MIPI_CSIS_INT_SRC_ERR_LOST_FS		BIT(12)
#define MIPI_CSIS_INT_SRC_ERR_LOST_FE		BIT(8)
#define MIPI_CSIS_INT_SRC_ERR_OVER		BIT(4)
#define MIPI_CSIS_INT_SRC_ERR_WRONG_CFG		BIT(3)
#define MIPI_CSIS_INT_SRC_ERR_ECC		BIT(2)
#define MIPI_CSIS_INT_SRC_ERR_CRC		BIT(1)
#define MIPI_CSIS_INT_SRC_ERR_UNKNOWN		BIT(0)
#define MIPI_CSIS_INT_SRC_ERRORS		0xfffff

/* D-PHY status control */
#define MIPI_CSIS_DPHY_STATUS			0x20
#define MIPI_CSIS_DPHY_STATUS_ULPS_DAT		BIT(8)
#define MIPI_CSIS_DPHY_STATUS_STOPSTATE_DAT	BIT(4)
#define MIPI_CSIS_DPHY_STATUS_ULPS_CLK		BIT(1)
#define MIPI_CSIS_DPHY_STATUS_STOPSTATE_CLK	BIT(0)

/* D-PHY common control */
#define MIPI_CSIS_DPHY_CMN_CTRL			0x24
#define MIPI_CSIS_DPHY_CMN_CTRL_HSSETTLE(n)	((n) << 24)
#define MIPI_CSIS_DPHY_CMN_CTRL_HSSETTLE_MASK	GENMASK(31, 24)
#define MIPI_CSIS_DPHY_CMN_CTRL_CLKSETTLE(n)	((n) << 22)
#define MIPI_CSIS_DPHY_CMN_CTRL_CLKSETTLE_MASK	GENMASK(23, 22)
#define MIPI_CSIS_DPHY_CMN_CTRL_DPDN_SWAP_CLK	BIT(6)
#define MIPI_CSIS_DPHY_CMN_CTRL_DPDN_SWAP_DAT	BIT(5)
#define MIPI_CSIS_DPHY_CMN_CTRL_ENABLE_DAT	BIT(1)
#define MIPI_CSIS_DPHY_CMN_CTRL_ENABLE_CLK	BIT(0)
#define MIPI_CSIS_DPHY_CMN_CTRL_ENABLE		(0x1f << 0)

/* D-PHY Master and Slave Control register Low */
#define MIPI_CSIS_DPHY_BCTRL_L			0x30
#define MIPI_CSIS_DPHY_BCTRL_L_USER_DATA_PATTERN_LOW(n)		(((n) & 3U) << 30)
#define MIPI_CSIS_DPHY_BCTRL_L_BIAS_REF_VOLT_715MV		(0 << 28)
#define MIPI_CSIS_DPHY_BCTRL_L_BIAS_REF_VOLT_724MV		(1 << 28)
#define MIPI_CSIS_DPHY_BCTRL_L_BIAS_REF_VOLT_733MV		(2 << 28)
#define MIPI_CSIS_DPHY_BCTRL_L_BIAS_REF_VOLT_706MV		(3 << 28)
#define MIPI_CSIS_DPHY_BCTRL_L_BGR_CHOPPER_FREQ_3MHZ		(0 << 27)
#define MIPI_CSIS_DPHY_BCTRL_L_BGR_CHOPPER_FREQ_1_5MHZ		(1 << 27)
#define MIPI_CSIS_DPHY_BCTRL_L_VREG12_EXTPWR_EN_CTL		BIT(26)
#define MIPI_CSIS_DPHY_BCTRL_L_REG_12P_LVL_CTL_1_2V		(0 << 24)
#define MIPI_CSIS_DPHY_BCTRL_L_REG_12P_LVL_CTL_1_23V		(1 << 24)
#define MIPI_CSIS_DPHY_BCTRL_L_REG_12P_LVL_CTL_1_17V		(2 << 24)
#define MIPI_CSIS_DPHY_BCTRL_L_REG_12P_LVL_CTL_1_26V		(3 << 24)
#define MIPI_CSIS_DPHY_BCTRL_L_REG_1P2_LVL_SEL			BIT(23)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_HYS_LVL_80MV		(0 << 21)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_HYS_LVL_100MV		(1 << 21)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_HYS_LVL_120MV		(2 << 21)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_HYS_LVL_140MV		(3 << 21)
#define MIPI_CSIS_DPHY_BCTRL_L_VREF_SRC_SEL			BIT(20)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_VREF_LVL_715MV		(0 << 18)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_VREF_LVL_743MV		(1 << 18)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_VREF_LVL_650MV		(2 << 18)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_VREF_LVL_682MV		(3 << 18)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_RX_PULSE_REJECT		BIT(17)
#define MIPI_CSIS_DPHY_BCTRL_L_MSTRCLK_LP_SLEW_RATE_DOWN_0	(0 << 15)
#define MIPI_CSIS_DPHY_BCTRL_L_MSTRCLK_LP_SLEW_RATE_DOWN_15P	(1 << 15)
#define MIPI_CSIS_DPHY_BCTRL_L_MSTRCLK_LP_SLEW_RATE_DOWN_30P	(3 << 15)
#define MIPI_CSIS_DPHY_BCTRL_L_MSTRCLK_LP_SLEW_RATE_UP		BIT(14)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_CD_HYS_60MV			(0 << 13)
#define MIPI_CSIS_DPHY_BCTRL_L_LP_CD_HYS_70MV			(1 << 13)
#define MIPI_CSIS_DPHY_BCTRL_L_BGR_CHOPPER_EN			BIT(12)
#define MIPI_CSIS_DPHY_BCTRL_L_ERRCONTENTION_LP_EN		BIT(11)
#define MIPI_CSIS_DPHY_BCTRL_L_TXTRIGGER_CLK_EN			BIT(10)
#define MIPI_CSIS_DPHY_BCTRL_L_B_DPHYCTRL(n)			(((n) * 25 / 1000000) << 0)

/* D-PHY Master and Slave Control register High */
#define MIPI_CSIS_DPHY_BCTRL_H			0x34
/* D-PHY Slave Control register Low */
#define MIPI_CSIS_DPHY_SCTRL_L			0x38
/* D-PHY Slave Control register High */
#define MIPI_CSIS_DPHY_SCTRL_H			0x3c

/* ISP Configuration register */
#define MIPI_CSIS_ISP_CONFIG_CH(n)		(0x40 + (n) * 0x10)
#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP_MSK	(0xff << 24)
#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP(x)	((x) << 24)
#define MIPI_CSIS_ISPCFG_DOUBLE_CMPNT		BIT(12)
#define MIPI_CSIS_ISPCFG_ALIGN_32BIT		BIT(11)
#define MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT	(0x1e << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW8		(0x2a << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW10		(0x2b << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW12		(0x2c << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW14		(0x2d << 2)
/* User defined formats, x = 1...4 */
#define MIPI_CSIS_ISPCFG_FMT_USER(x)		((0x30 + (x) - 1) << 2)
#define MIPI_CSIS_ISPCFG_FMT_MASK		(0x3f << 2)

/* ISP Image Resolution register */
#define MIPI_CSIS_ISP_RESOL_CH(n)		(0x44 + (n) * 0x10)
#define CSIS_MAX_PIX_WIDTH			0xffff
#define CSIS_MAX_PIX_HEIGHT			0xffff

/* ISP SYNC register */
#define MIPI_CSIS_ISP_SYNC_CH(n)		(0x48 + (n) * 0x10)
#define MIPI_CSIS_ISP_SYNC_HSYNC_LINTV_OFFSET	18
#define MIPI_CSIS_ISP_SYNC_VSYNC_SINTV_OFFSET	12
#define MIPI_CSIS_ISP_SYNC_VSYNC_EINTV_OFFSET	0

/* ISP shadow registers */
#define MIPI_CSIS_SDW_CONFIG_CH(n)		(0x80 + (n) * 0x10)
#define MIPI_CSIS_SDW_RESOL_CH(n)		(0x84 + (n) * 0x10)
#define MIPI_CSIS_SDW_SYNC_CH(n)		(0x88 + (n) * 0x10)

/* Debug control register */
#define MIPI_CSIS_DBG_CTRL			0xc0

/* Non-image packet data buffers */
#define MIPI_CSIS_PKTDATA_ODD			0x2000
#define MIPI_CSIS_PKTDATA_EVEN			0x3000
#define MIPI_CSIS_PKTDATA_SIZE			SZ_4K

#define DEFAULT_SCLK_CSIS_FREQ			166000000UL

enum {
	ST_POWERED	= 1,
	ST_STREAMING	= 2,
	ST_SUSPENDED	= 4,
};

struct mipi_csis_event {
	u32 mask;
	const char * const name;
	unsigned int counter;
};

static const struct mipi_csis_event mipi_csis_events[] = {
	/* Errors */
	{ MIPI_CSIS_INT_SRC_ERR_SOT_HS,		"SOT Error" },
	{ MIPI_CSIS_INT_SRC_ERR_LOST_FS,	"Lost Frame Start Error" },
	{ MIPI_CSIS_INT_SRC_ERR_LOST_FE,	"Lost Frame End Error" },
	{ MIPI_CSIS_INT_SRC_ERR_OVER,		"FIFO Overflow Error" },
	{ MIPI_CSIS_INT_SRC_ERR_WRONG_CFG,	"Wrong Configuration Error" },
	{ MIPI_CSIS_INT_SRC_ERR_ECC,		"ECC Error" },
	{ MIPI_CSIS_INT_SRC_ERR_CRC,		"CRC Error" },
	{ MIPI_CSIS_INT_SRC_ERR_UNKNOWN,	"Unknown Error" },
	/* Non-image data receive events */
	{ MIPI_CSIS_INT_SRC_EVEN_BEFORE,	"Non-image data before even frame" },
	{ MIPI_CSIS_INT_SRC_EVEN_AFTER,		"Non-image data after even frame" },
	{ MIPI_CSIS_INT_SRC_ODD_BEFORE,		"Non-image data before odd frame" },
	{ MIPI_CSIS_INT_SRC_ODD_AFTER,		"Non-image data after odd frame" },
	/* Frame start/end */
	{ MIPI_CSIS_INT_SRC_FRAME_START,	"Frame Start" },
	{ MIPI_CSIS_INT_SRC_FRAME_END,		"Frame End" },
};

#define MIPI_CSIS_NUM_EVENTS ARRAY_SIZE(mipi_csis_events)

enum mipi_csis_clk {
	MIPI_CSIS_CLK_PCLK,
	MIPI_CSIS_CLK_WRAP,
	MIPI_CSIS_CLK_PHY,
};

static const char * const mipi_csis_clk_id[] = {
	"pclk",
	"wrap",
	"phy",
};

struct csis_hw_reset {
	struct regmap *src;
	u8 req_src;
	u8 rst_bit;
};

struct csi_state {
	/* lock elements below */
	struct mutex lock;
	/* lock for event handler */
	spinlock_t slock;
	struct device *dev;
	struct media_pad pads[CSIS_PADS_NUM];
	struct v4l2_subdev mipi_sd;
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *src_sd;

	u8 index;
	struct platform_device *pdev;
	struct phy *phy;
	void __iomem *regs;
	int irq;
	u32 flags;

	struct dentry *debugfs_root;
	bool debug;

	int num_clks;
	struct clk_bulk_data *clks;

	u32 clk_frequency;
	u32 hs_settle;

	struct reset_control *mrst;

	const struct csis_pix_format *csis_fmt;
	struct v4l2_mbus_framefmt format_mbus;

	struct v4l2_fwnode_bus_mipi_csi2 bus;

	struct mipi_csis_event events[MIPI_CSIS_NUM_EVENTS];

	struct csis_hw_reset hw_reset;
	struct regulator *mipi_phy_regulator;
};

struct csis_pix_format {
	u32 code;
	u32 fmt_reg;
	u8 width;
};

static const struct csis_pix_format mipi_csis_formats[] = {
	/* YUV formats. */
	{
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.width = 16,
	},
	/* RAW (Bayer and greyscale) formats. */
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_Y12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG14_1X14,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG14_1X14,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB14_1X14,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW14,
		.width = 14,
	}
};

static inline void mipi_csis_write(struct csi_state *state, u32 reg, u32 val)
{
	writel(val, state->regs + reg);
}

static inline u32 mipi_csis_read(struct csi_state *state, u32 reg)
{
	return readl(state->regs + reg);
}

static int mipi_csis_dump_regs(struct csi_state *state)
{
	struct device *dev = &state->pdev->dev;
	unsigned int i;
	u32 cfg;
	static const struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		{ MIPI_CSIS_CMN_CTRL, "CMN_CTRL" },
		{ MIPI_CSIS_CLK_CTRL, "CLK_CTRL" },
		{ MIPI_CSIS_INT_MSK, "INT_MSK" },
		{ MIPI_CSIS_DPHY_STATUS, "DPHY_STATUS" },
		{ MIPI_CSIS_DPHY_CMN_CTRL, "DPHY_CMN_CTRL" },
		{ MIPI_CSIS_DPHY_SCTRL_L, "DPHY_SCTRL_L" },
		{ MIPI_CSIS_DPHY_SCTRL_H, "DPHY_SCTRL_H" },
		{ MIPI_CSIS_ISP_CONFIG_CH(0), "ISP_CONFIG_CH0" },
		{ MIPI_CSIS_ISP_RESOL_CH(0), "ISP_RESOL_CH0" },
		{ MIPI_CSIS_SDW_CONFIG_CH(0), "SDW_CONFIG_CH0" },
		{ MIPI_CSIS_SDW_RESOL_CH(0), "SDW_RESOL_CH0" },
		{ MIPI_CSIS_DBG_CTRL, "DBG_CTRL" },
	};

	dev_info(dev, "--- REGISTERS ---\n");

	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		cfg = mipi_csis_read(state, registers[i].offset);
		dev_info(dev, "%14s: 0x%08x\n", registers[i].name, cfg);
	}

	return 0;
}

static struct csi_state *
mipi_notifier_to_csis_state(struct v4l2_async_notifier *n)
{
	return container_of(n, struct csi_state, notifier);
}

static struct csi_state *mipi_sd_to_csis_state(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi_state, mipi_sd);
}

static const struct csis_pix_format *find_csis_format(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mipi_csis_formats); i++)
		if (code == mipi_csis_formats[i].code)
			return &mipi_csis_formats[i];
	return NULL;
}

static void mipi_csis_enable_interrupts(struct csi_state *state, bool on)
{
	mipi_csis_write(state, MIPI_CSIS_INT_MSK, on ? 0xffffffff : 0);
}

static void mipi_csis_sw_reset(struct csi_state *state)
{
	u32 val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);

	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL,
			val | MIPI_CSIS_CMN_CTRL_RESET);
	usleep_range(10, 20);
}

static int mipi_csis_phy_init(struct csi_state *state)
{
	state->mipi_phy_regulator = devm_regulator_get(state->dev, "phy");
	if (IS_ERR(state->mipi_phy_regulator))
		return PTR_ERR(state->mipi_phy_regulator);

	return regulator_set_voltage(state->mipi_phy_regulator, 1000000,
				     1000000);
}

static void mipi_csis_phy_reset(struct csi_state *state)
{
	reset_control_assert(state->mrst);

	msleep(20);

	reset_control_deassert(state->mrst);
}

static void mipi_csis_system_enable(struct csi_state *state, int on)
{
	u32 val, mask;

	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	if (on)
		val |= MIPI_CSIS_CMN_CTRL_ENABLE;
	else
		val &= ~MIPI_CSIS_CMN_CTRL_ENABLE;
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val);

	val = mipi_csis_read(state, MIPI_CSIS_DPHY_CMN_CTRL);
	val &= ~MIPI_CSIS_DPHY_CMN_CTRL_ENABLE;
	if (on) {
		mask = (1 << (state->bus.num_data_lanes + 1)) - 1;
		val |= (mask & MIPI_CSIS_DPHY_CMN_CTRL_ENABLE);
	}
	mipi_csis_write(state, MIPI_CSIS_DPHY_CMN_CTRL, val);
}

/* Called with the state.lock mutex held */
static void __mipi_csis_set_format(struct csi_state *state)
{
	struct v4l2_mbus_framefmt *mf = &state->format_mbus;
	u32 val;

	/* Color format */
	val = mipi_csis_read(state, MIPI_CSIS_ISP_CONFIG_CH(0));
	val &= ~(MIPI_CSIS_ISPCFG_ALIGN_32BIT | MIPI_CSIS_ISPCFG_FMT_MASK);
	val |= state->csis_fmt->fmt_reg;
	mipi_csis_write(state, MIPI_CSIS_ISP_CONFIG_CH(0), val);

	/* Pixel resolution */
	val = mf->width | (mf->height << 16);
	mipi_csis_write(state, MIPI_CSIS_ISP_RESOL_CH(0), val);
}

static int mipi_csis_calculate_params(struct csi_state *state)
{
	s64 link_freq;
	u32 lane_rate;

	/* Calculate the line rate from the pixel rate. */
	link_freq = v4l2_get_link_freq(state->src_sd->ctrl_handler,
				       state->csis_fmt->width,
				       state->bus.num_data_lanes * 2);
	if (link_freq < 0) {
		dev_err(state->dev, "Unable to obtain link frequency: %d\n",
			(int)link_freq);
		return link_freq;
	}

	lane_rate = link_freq * 2;

	if (lane_rate < 80000000 || lane_rate > 1500000000) {
		dev_dbg(state->dev, "Out-of-bound lane rate %u\n", lane_rate);
		return -EINVAL;
	}

	/*
	 * The HSSETTLE counter value is document in a table, but can also
	 * easily be calculated.
	 */
	state->hs_settle = (lane_rate - 5000000) / 45000000;
	dev_dbg(state->dev, "lane rate %u, Ths_settle %u\n",
		lane_rate, state->hs_settle);

	return 0;
}

static void mipi_csis_set_params(struct csi_state *state)
{
	int lanes = state->bus.num_data_lanes;
	u32 val;

	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	val &= ~MIPI_CSIS_CMN_CTRL_LANE_NR_MASK;
	val |= (lanes - 1) << MIPI_CSIS_CMN_CTRL_LANE_NR_OFFSET;
	val |= MIPI_CSIS_CMN_CTRL_INTER_MODE;
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val);

	__mipi_csis_set_format(state);

	mipi_csis_write(state, MIPI_CSIS_DPHY_CMN_CTRL,
			MIPI_CSIS_DPHY_CMN_CTRL_HSSETTLE(state->hs_settle));

	val = (0 << MIPI_CSIS_ISP_SYNC_HSYNC_LINTV_OFFSET)
	    | (0 << MIPI_CSIS_ISP_SYNC_VSYNC_SINTV_OFFSET)
	    | (0 << MIPI_CSIS_ISP_SYNC_VSYNC_EINTV_OFFSET);
	mipi_csis_write(state, MIPI_CSIS_ISP_SYNC_CH(0), val);

	val = mipi_csis_read(state, MIPI_CSIS_CLK_CTRL);
	val |= MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	val |= MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(15);
	val &= ~MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK;
	mipi_csis_write(state, MIPI_CSIS_CLK_CTRL, val);

	mipi_csis_write(state, MIPI_CSIS_DPHY_BCTRL_L,
			MIPI_CSIS_DPHY_BCTRL_L_BIAS_REF_VOLT_715MV |
			MIPI_CSIS_DPHY_BCTRL_L_BGR_CHOPPER_FREQ_3MHZ |
			MIPI_CSIS_DPHY_BCTRL_L_REG_12P_LVL_CTL_1_2V |
			MIPI_CSIS_DPHY_BCTRL_L_LP_RX_HYS_LVL_80MV |
			MIPI_CSIS_DPHY_BCTRL_L_LP_RX_VREF_LVL_715MV |
			MIPI_CSIS_DPHY_BCTRL_L_LP_CD_HYS_60MV |
			MIPI_CSIS_DPHY_BCTRL_L_B_DPHYCTRL(20000000));
	mipi_csis_write(state, MIPI_CSIS_DPHY_BCTRL_H, 0);

	/* Update the shadow register. */
	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL,
			val | MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW |
			MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW_CTRL);
}

static int mipi_csis_clk_enable(struct csi_state *state)
{
	return clk_bulk_prepare_enable(state->num_clks, state->clks);
}

static void mipi_csis_clk_disable(struct csi_state *state)
{
	clk_bulk_disable_unprepare(state->num_clks, state->clks);
}

static int mipi_csis_clk_get(struct csi_state *state)
{
	struct device *dev = &state->pdev->dev;
	unsigned int i;
	int ret;

	state->num_clks = ARRAY_SIZE(mipi_csis_clk_id);
	state->clks = devm_kcalloc(dev, state->num_clks, sizeof(*state->clks),
				   GFP_KERNEL);

	if (!state->clks)
		return -ENOMEM;

	for (i = 0; i < state->num_clks; i++)
		state->clks[i].id = mipi_csis_clk_id[i];

	ret = devm_clk_bulk_get(dev, state->num_clks, state->clks);
	if (ret < 0)
		return ret;

	/* Set clock rate */
	ret = clk_set_rate(state->clks[MIPI_CSIS_CLK_WRAP].clk,
			   state->clk_frequency);
	if (ret < 0)
		dev_err(dev, "set rate=%d failed: %d\n", state->clk_frequency,
			ret);

	return ret;
}

static void mipi_csis_start_stream(struct csi_state *state)
{
	mipi_csis_sw_reset(state);
	mipi_csis_set_params(state);
	mipi_csis_system_enable(state, true);
	mipi_csis_enable_interrupts(state, true);
}

static void mipi_csis_stop_stream(struct csi_state *state)
{
	mipi_csis_enable_interrupts(state, false);
	mipi_csis_system_enable(state, false);
}

static void mipi_csis_clear_counters(struct csi_state *state)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&state->slock, flags);
	for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++)
		state->events[i].counter = 0;
	spin_unlock_irqrestore(&state->slock, flags);
}

static void mipi_csis_log_counters(struct csi_state *state, bool non_errors)
{
	int i = non_errors ? MIPI_CSIS_NUM_EVENTS : MIPI_CSIS_NUM_EVENTS - 4;
	struct device *dev = &state->pdev->dev;
	unsigned long flags;

	spin_lock_irqsave(&state->slock, flags);

	for (i--; i >= 0; i--) {
		if (state->events[i].counter > 0 || state->debug)
			dev_info(dev, "%s events: %d\n", state->events[i].name,
				 state->events[i].counter);
	}
	spin_unlock_irqrestore(&state->slock, flags);
}

/*
 * V4L2 subdev operations
 */
static int mipi_csis_s_stream(struct v4l2_subdev *mipi_sd, int enable)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	int ret;

	if (enable) {
		ret = mipi_csis_calculate_params(state);
		if (ret < 0)
			return ret;

		mipi_csis_clear_counters(state);
		ret = pm_runtime_get_sync(&state->pdev->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&state->pdev->dev);
			return ret;
		}
		ret = v4l2_subdev_call(state->src_sd, core, s_power, 1);
		if (ret < 0 && ret != -ENOIOCTLCMD)
			return ret;
	}

	mutex_lock(&state->lock);
	if (enable) {
		if (state->flags & ST_SUSPENDED) {
			ret = -EBUSY;
			goto unlock;
		}

		mipi_csis_start_stream(state);
		ret = v4l2_subdev_call(state->src_sd, video, s_stream, 1);
		if (ret < 0)
			goto unlock;

		mipi_csis_log_counters(state, true);

		state->flags |= ST_STREAMING;
	} else {
		v4l2_subdev_call(state->src_sd, video, s_stream, 0);
		ret = v4l2_subdev_call(state->src_sd, core, s_power, 0);
		if (ret == -ENOIOCTLCMD)
			ret = 0;
		mipi_csis_stop_stream(state);
		state->flags &= ~ST_STREAMING;
		if (state->debug)
			mipi_csis_log_counters(state, true);
	}

unlock:
	mutex_unlock(&state->lock);
	if (!enable)
		pm_runtime_put(&state->pdev->dev);

	return ret;
}

static int mipi_csis_link_setup(struct media_entity *entity,
				const struct media_pad *local_pad,
				const struct media_pad *remote_pad, u32 flags)
{
	struct v4l2_subdev *mipi_sd = media_entity_to_v4l2_subdev(entity);
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	struct v4l2_subdev *remote_sd;
	int ret = 0;

	dev_dbg(state->dev, "link setup %s -> %s", remote_pad->entity->name,
		local_pad->entity->name);

	/* We only care about the link to the source. */
	if (!(local_pad->flags & MEDIA_PAD_FL_SINK))
		return 0;

	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	mutex_lock(&state->lock);

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (state->src_sd) {
			ret = -EBUSY;
			goto out;
		}

		state->src_sd = remote_sd;
	} else {
		state->src_sd = NULL;
	}

out:
	mutex_unlock(&state->lock);
	return ret;
}

static struct v4l2_mbus_framefmt *
mipi_csis_get_format(struct csi_state *state,
		     struct v4l2_subdev_pad_config *cfg,
		     enum v4l2_subdev_format_whence which,
		     unsigned int pad)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return v4l2_subdev_get_try_format(&state->mipi_sd, cfg, pad);

	return &state->format_mbus;
}

static int mipi_csis_init_cfg(struct v4l2_subdev *mipi_sd,
			      struct v4l2_subdev_pad_config *cfg)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	struct v4l2_mbus_framefmt *fmt_sink;
	struct v4l2_mbus_framefmt *fmt_source;
	enum v4l2_subdev_format_whence which;

	which = cfg ? V4L2_SUBDEV_FORMAT_TRY : V4L2_SUBDEV_FORMAT_ACTIVE;
	fmt_sink = mipi_csis_get_format(state, cfg, which, CSIS_PAD_SINK);

	fmt_sink->code = MEDIA_BUS_FMT_UYVY8_1X16;
	fmt_sink->width = MIPI_CSIS_DEF_PIX_WIDTH;
	fmt_sink->height = MIPI_CSIS_DEF_PIX_HEIGHT;
	fmt_sink->field = V4L2_FIELD_NONE;

	fmt_sink->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt_sink->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt_sink->colorspace);
	fmt_sink->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt_sink->colorspace);
	fmt_sink->quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(false, fmt_sink->colorspace,
					      fmt_sink->ycbcr_enc);

	/*
	 * When called from mipi_csis_subdev_init() to initialize the active
	 * configuration, cfg is NULL, which indicates there's no source pad
	 * configuration to set.
	 */
	if (!cfg)
		return 0;

	fmt_source = mipi_csis_get_format(state, cfg, which, CSIS_PAD_SOURCE);
	*fmt_source = *fmt_sink;

	return 0;
}

static int mipi_csis_get_fmt(struct v4l2_subdev *mipi_sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *sdformat)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	struct v4l2_mbus_framefmt *fmt;

	mutex_lock(&state->lock);
	fmt = mipi_csis_get_format(state, cfg, sdformat->which, sdformat->pad);
	sdformat->format = *fmt;
	mutex_unlock(&state->lock);

	return 0;
}

static int mipi_csis_enum_mbus_code(struct v4l2_subdev *mipi_sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);

	/*
	 * The CSIS can't transcode in any way, the source format is identical
	 * to the sink format.
	 */
	if (code->pad == CSIS_PAD_SOURCE) {
		struct v4l2_mbus_framefmt *fmt;

		if (code->index > 0)
			return -EINVAL;

		fmt = mipi_csis_get_format(state, cfg, code->which, code->pad);
		code->code = fmt->code;
		return 0;
	}

	if (code->pad != CSIS_PAD_SINK)
		return -EINVAL;

	if (code->index >= ARRAY_SIZE(mipi_csis_formats))
		return -EINVAL;

	code->code = mipi_csis_formats[code->index].code;

	return 0;
}

static int mipi_csis_set_fmt(struct v4l2_subdev *mipi_sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *sdformat)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	struct csis_pix_format const *csis_fmt;
	struct v4l2_mbus_framefmt *fmt;
	unsigned int align;

	/*
	 * The CSIS can't transcode in any way, the source format can't be
	 * modified.
	 */
	if (sdformat->pad == CSIS_PAD_SOURCE)
		return mipi_csis_get_fmt(mipi_sd, cfg, sdformat);

	if (sdformat->pad != CSIS_PAD_SINK)
		return -EINVAL;

	fmt = mipi_csis_get_format(state, cfg, sdformat->which, sdformat->pad);

	mutex_lock(&state->lock);

	/* Validate the media bus code and clamp the size. */
	csis_fmt = find_csis_format(sdformat->format.code);
	if (!csis_fmt)
		csis_fmt = &mipi_csis_formats[0];

	fmt->code = csis_fmt->code;
	fmt->width = sdformat->format.width;
	fmt->height = sdformat->format.height;

	/*
	 * The total number of bits per line must be a multiple of 8. We thus
	 * need to align the width for formats that are not multiples of 8
	 * bits.
	 */
	switch (csis_fmt->width % 8) {
	case 0:
		align = 0;
		break;
	case 4:
		align = 1;
		break;
	case 2:
	case 6:
		align = 2;
		break;
	case 1:
	case 3:
	case 5:
	case 7:
		align = 3;
		break;
	}

	v4l_bound_align_image(&fmt->width, 1, CSIS_MAX_PIX_WIDTH, align,
			      &fmt->height, 1, CSIS_MAX_PIX_HEIGHT, 0, 0);

	sdformat->format = *fmt;

	/* Propagate the format from sink to source. */
	fmt = mipi_csis_get_format(state, cfg, sdformat->which,
				   CSIS_PAD_SOURCE);
	*fmt = sdformat->format;

	/* Store the CSIS format descriptor for active formats. */
	if (sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		state->csis_fmt = csis_fmt;

	mutex_unlock(&state->lock);

	return 0;
}

static int mipi_csis_log_status(struct v4l2_subdev *mipi_sd)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);

	mutex_lock(&state->lock);
	mipi_csis_log_counters(state, true);
	if (state->debug && (state->flags & ST_POWERED))
		mipi_csis_dump_regs(state);
	mutex_unlock(&state->lock);

	return 0;
}

static irqreturn_t mipi_csis_irq_handler(int irq, void *dev_id)
{
	struct csi_state *state = dev_id;
	unsigned long flags;
	unsigned int i;
	u32 status;

	status = mipi_csis_read(state, MIPI_CSIS_INT_SRC);

	spin_lock_irqsave(&state->slock, flags);

	/* Update the event/error counters */
	if ((status & MIPI_CSIS_INT_SRC_ERRORS) || state->debug) {
		for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++) {
			if (!(status & state->events[i].mask))
				continue;
			state->events[i].counter++;
		}
	}
	spin_unlock_irqrestore(&state->slock, flags);

	mipi_csis_write(state, MIPI_CSIS_INT_SRC, status);

	return IRQ_HANDLED;
}

static const struct v4l2_subdev_core_ops mipi_csis_core_ops = {
	.log_status	= mipi_csis_log_status,
};

static const struct media_entity_operations mipi_csis_entity_ops = {
	.link_setup	= mipi_csis_link_setup,
	.link_validate	= v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

static const struct v4l2_subdev_video_ops mipi_csis_video_ops = {
	.s_stream	= mipi_csis_s_stream,
};

static const struct v4l2_subdev_pad_ops mipi_csis_pad_ops = {
	.init_cfg		= mipi_csis_init_cfg,
	.enum_mbus_code		= mipi_csis_enum_mbus_code,
	.get_fmt		= mipi_csis_get_fmt,
	.set_fmt		= mipi_csis_set_fmt,
};

static const struct v4l2_subdev_ops mipi_csis_subdev_ops = {
	.core	= &mipi_csis_core_ops,
	.video	= &mipi_csis_video_ops,
	.pad	= &mipi_csis_pad_ops,
};

static int mipi_csis_parse_dt(struct platform_device *pdev,
			      struct csi_state *state)
{
	struct device_node *node = pdev->dev.of_node;

	if (of_property_read_u32(node, "clock-frequency",
				 &state->clk_frequency))
		state->clk_frequency = DEFAULT_SCLK_CSIS_FREQ;

	/* Get MIPI PHY resets */
	state->mrst = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(state->mrst))
		return PTR_ERR(state->mrst);

	return 0;
}

static int mipi_csis_pm_resume(struct device *dev, bool runtime);

static int mipi_csis_notify_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct csi_state *state = mipi_notifier_to_csis_state(notifier);
	struct media_pad *sink = &state->mipi_sd.entity.pads[CSIS_PAD_SINK];

	return v4l2_create_fwnode_links_to_pad(sd, sink, 0);
}

static const struct v4l2_async_notifier_operations mipi_csis_notify_ops = {
	.bound = mipi_csis_notify_bound,
};

static int mipi_csis_subdev_init(struct v4l2_subdev *mipi_sd,
				 struct platform_device *pdev,
				 const struct v4l2_subdev_ops *ops)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);

	v4l2_subdev_init(mipi_sd, ops);
	mipi_sd->owner = THIS_MODULE;
	snprintf(mipi_sd->name, sizeof(mipi_sd->name), "%s.%d",
		 CSIS_SUBDEV_NAME, state->index);

	mipi_sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	mipi_sd->ctrl_handler = NULL;

	mipi_sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	mipi_sd->entity.ops = &mipi_csis_entity_ops;

	mipi_sd->dev = &pdev->dev;

	state->csis_fmt = &mipi_csis_formats[0];
	mipi_csis_init_cfg(mipi_sd, NULL);

	v4l2_set_subdevdata(mipi_sd, &pdev->dev);

	state->pads[CSIS_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	state->pads[CSIS_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	return media_entity_pads_init(&mipi_sd->entity, CSIS_PADS_NUM,
				      state->pads);
}

static int mipi_csis_async_register(struct csi_state *state)
{
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *ep;
	int ret;

	v4l2_async_notifier_init(&state->notifier);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(state->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return -ENOTCONN;

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret)
		goto err_parse;

	state->bus = vep.bus.mipi_csi2;

	dev_dbg(state->dev, "data lanes: %d\n", state->bus.num_data_lanes);
	dev_dbg(state->dev, "flags: 0x%08x\n", state->bus.flags);

	asd = v4l2_async_notifier_add_fwnode_remote_subdev(
		&state->notifier, ep, struct v4l2_async_subdev);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		goto err_parse;
	}

	fwnode_handle_put(ep);

	state->notifier.ops = &mipi_csis_notify_ops;

	ret = v4l2_async_subdev_notifier_register(&state->mipi_sd,
						  &state->notifier);
	if (ret)
		return ret;

	return v4l2_async_register_subdev(&state->mipi_sd);

err_parse:
	fwnode_handle_put(ep);

	return ret;
}

static int mipi_csis_dump_regs_show(struct seq_file *m, void *private)
{
	struct csi_state *state = m->private;

	return mipi_csis_dump_regs(state);
}
DEFINE_SHOW_ATTRIBUTE(mipi_csis_dump_regs);

static void mipi_csis_debugfs_init(struct csi_state *state)
{
	state->debugfs_root = debugfs_create_dir(dev_name(state->dev), NULL);

	debugfs_create_bool("debug_enable", 0600, state->debugfs_root,
			    &state->debug);
	debugfs_create_file("dump_regs", 0600, state->debugfs_root, state,
			    &mipi_csis_dump_regs_fops);
}

static void mipi_csis_debugfs_exit(struct csi_state *state)
{
	debugfs_remove_recursive(state->debugfs_root);
}

static int mipi_csis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct csi_state *state;
	int ret;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	spin_lock_init(&state->slock);

	state->pdev = pdev;
	state->dev = dev;

	ret = mipi_csis_parse_dt(pdev, state);
	if (ret < 0) {
		dev_err(dev, "Failed to parse device tree: %d\n", ret);
		return ret;
	}

	ret = mipi_csis_phy_init(state);
	if (ret < 0)
		return ret;

	mipi_csis_phy_reset(state);

	state->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(state->regs))
		return PTR_ERR(state->regs);

	state->irq = platform_get_irq(pdev, 0);
	if (state->irq < 0)
		return state->irq;

	ret = mipi_csis_clk_get(state);
	if (ret < 0)
		return ret;

	ret = mipi_csis_clk_enable(state);
	if (ret < 0) {
		dev_err(state->dev, "failed to enable clocks: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(dev, state->irq, mipi_csis_irq_handler,
			       0, dev_name(dev), state);
	if (ret) {
		dev_err(dev, "Interrupt request failed\n");
		goto disable_clock;
	}

	platform_set_drvdata(pdev, &state->mipi_sd);

	mutex_init(&state->lock);
	ret = mipi_csis_subdev_init(&state->mipi_sd, pdev,
				    &mipi_csis_subdev_ops);
	if (ret < 0)
		goto disable_clock;

	ret = mipi_csis_async_register(state);
	if (ret < 0) {
		dev_err(&pdev->dev, "async register failed: %d\n", ret);
		goto cleanup;
	}

	memcpy(state->events, mipi_csis_events, sizeof(state->events));

	mipi_csis_debugfs_init(state);
	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mipi_csis_pm_resume(dev, true);
		if (ret < 0)
			goto unregister_all;
	}

	dev_info(&pdev->dev, "lanes: %d, freq: %u\n",
		 state->bus.num_data_lanes, state->clk_frequency);

	return 0;

unregister_all:
	mipi_csis_debugfs_exit(state);
cleanup:
	media_entity_cleanup(&state->mipi_sd.entity);
	v4l2_async_notifier_unregister(&state->notifier);
	v4l2_async_notifier_cleanup(&state->notifier);
	v4l2_async_unregister_subdev(&state->mipi_sd);
disable_clock:
	mipi_csis_clk_disable(state);
	mutex_destroy(&state->lock);

	return ret;
}

static int mipi_csis_pm_suspend(struct device *dev, bool runtime)
{
	struct v4l2_subdev *mipi_sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	int ret = 0;

	mutex_lock(&state->lock);
	if (state->flags & ST_POWERED) {
		mipi_csis_stop_stream(state);
		ret = regulator_disable(state->mipi_phy_regulator);
		if (ret)
			goto unlock;
		mipi_csis_clk_disable(state);
		state->flags &= ~ST_POWERED;
		if (!runtime)
			state->flags |= ST_SUSPENDED;
	}

unlock:
	mutex_unlock(&state->lock);

	return ret ? -EAGAIN : 0;
}

static int mipi_csis_pm_resume(struct device *dev, bool runtime)
{
	struct v4l2_subdev *mipi_sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	int ret = 0;

	mutex_lock(&state->lock);
	if (!runtime && !(state->flags & ST_SUSPENDED))
		goto unlock;

	if (!(state->flags & ST_POWERED)) {
		ret = regulator_enable(state->mipi_phy_regulator);
		if (ret)
			goto unlock;

		state->flags |= ST_POWERED;
		mipi_csis_clk_enable(state);
	}
	if (state->flags & ST_STREAMING)
		mipi_csis_start_stream(state);

	state->flags &= ~ST_SUSPENDED;

unlock:
	mutex_unlock(&state->lock);

	return ret ? -EAGAIN : 0;
}

static int __maybe_unused mipi_csis_suspend(struct device *dev)
{
	return mipi_csis_pm_suspend(dev, false);
}

static int __maybe_unused mipi_csis_resume(struct device *dev)
{
	return mipi_csis_pm_resume(dev, false);
}

static int __maybe_unused mipi_csis_runtime_suspend(struct device *dev)
{
	return mipi_csis_pm_suspend(dev, true);
}

static int __maybe_unused mipi_csis_runtime_resume(struct device *dev)
{
	return mipi_csis_pm_resume(dev, true);
}

static int mipi_csis_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *mipi_sd = platform_get_drvdata(pdev);
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);

	mipi_csis_debugfs_exit(state);
	v4l2_async_notifier_unregister(&state->notifier);
	v4l2_async_notifier_cleanup(&state->notifier);
	v4l2_async_unregister_subdev(&state->mipi_sd);

	pm_runtime_disable(&pdev->dev);
	mipi_csis_pm_suspend(&pdev->dev, true);
	mipi_csis_clk_disable(state);
	media_entity_cleanup(&state->mipi_sd.entity);
	mutex_destroy(&state->lock);
	pm_runtime_set_suspended(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops mipi_csis_pm_ops = {
	SET_RUNTIME_PM_OPS(mipi_csis_runtime_suspend, mipi_csis_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(mipi_csis_suspend, mipi_csis_resume)
};

static const struct of_device_id mipi_csis_of_match[] = {
	{ .compatible = "fsl,imx7-mipi-csi2", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mipi_csis_of_match);

static struct platform_driver mipi_csis_driver = {
	.probe		= mipi_csis_probe,
	.remove		= mipi_csis_remove,
	.driver		= {
		.of_match_table = mipi_csis_of_match,
		.name		= CSIS_DRIVER_NAME,
		.pm		= &mipi_csis_pm_ops,
	},
};

module_platform_driver(mipi_csis_driver);

MODULE_DESCRIPTION("i.MX7 MIPI CSI-2 Receiver driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx7-mipi-csi2");
