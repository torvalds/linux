// SPDX-License-Identifier: GPL-2.0
/*
 * Samsung CSIS MIPI CSI-2 receiver driver.
 *
 * The Samsung CSIS IP is a MIPI CSI-2 receiver found in various NXP i.MX7 and
 * i.MX8 SoCs. The i.MX7 features version 3.3 of the IP, while i.MX8 features
 * version 3.6.3.
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
#include <linux/of_device.h>
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

#define CSIS_DRIVER_NAME			"imx-mipi-csis"

#define CSIS_PAD_SINK				0
#define CSIS_PAD_SOURCE				1
#define CSIS_PADS_NUM				2

#define MIPI_CSIS_DEF_PIX_WIDTH			640
#define MIPI_CSIS_DEF_PIX_HEIGHT		480

/* Register map definition */

/* CSIS version */
#define MIPI_CSIS_VERSION			0x00
#define MIPI_CSIS_VERSION_IMX7D			0x03030505
#define MIPI_CSIS_VERSION_IMX8MP		0x03060301

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
#define MIPI_CSIS_ISPCFG_PIXEL_MODE_SINGLE	(0 << 12)
#define MIPI_CSIS_ISPCFG_PIXEL_MODE_DUAL	(1 << 12)
#define MIPI_CSIS_ISPCFG_PIXEL_MODE_QUAD	(2 << 12)	/* i.MX8M[MNP] only */
#define MIPI_CSIS_ISPCFG_PIXEL_MASK		(3 << 12)
#define MIPI_CSIS_ISPCFG_ALIGN_32BIT		BIT(11)
#define MIPI_CSIS_ISPCFG_FMT(fmt)		((fmt) << 2)
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
#define MIPI_CSIS_DBG_INTR_MSK			0xc4
#define MIPI_CSIS_DBG_INTR_MSK_DT_NOT_SUPPORT	BIT(25)
#define MIPI_CSIS_DBG_INTR_MSK_DT_IGNORE	BIT(24)
#define MIPI_CSIS_DBG_INTR_MSK_ERR_FRAME_SIZE	BIT(20)
#define MIPI_CSIS_DBG_INTR_MSK_TRUNCATED_FRAME	BIT(16)
#define MIPI_CSIS_DBG_INTR_MSK_EARLY_FE		BIT(12)
#define MIPI_CSIS_DBG_INTR_MSK_EARLY_FS		BIT(8)
#define MIPI_CSIS_DBG_INTR_MSK_CAM_VSYNC_FALL	BIT(4)
#define MIPI_CSIS_DBG_INTR_MSK_CAM_VSYNC_RISE	BIT(0)
#define MIPI_CSIS_DBG_INTR_SRC			0xc8
#define MIPI_CSIS_DBG_INTR_SRC_DT_NOT_SUPPORT	BIT(25)
#define MIPI_CSIS_DBG_INTR_SRC_DT_IGNORE	BIT(24)
#define MIPI_CSIS_DBG_INTR_SRC_ERR_FRAME_SIZE	BIT(20)
#define MIPI_CSIS_DBG_INTR_SRC_TRUNCATED_FRAME	BIT(16)
#define MIPI_CSIS_DBG_INTR_SRC_EARLY_FE		BIT(12)
#define MIPI_CSIS_DBG_INTR_SRC_EARLY_FS		BIT(8)
#define MIPI_CSIS_DBG_INTR_SRC_CAM_VSYNC_FALL	BIT(4)
#define MIPI_CSIS_DBG_INTR_SRC_CAM_VSYNC_RISE	BIT(0)

#define MIPI_CSIS_FRAME_COUNTER_CH(n)		(0x0100 + (n) * 4)

/* Non-image packet data buffers */
#define MIPI_CSIS_PKTDATA_ODD			0x2000
#define MIPI_CSIS_PKTDATA_EVEN			0x3000
#define MIPI_CSIS_PKTDATA_SIZE			SZ_4K

#define DEFAULT_SCLK_CSIS_FREQ			166000000UL

/* MIPI CSI-2 Data Types */
#define MIPI_CSI2_DATA_TYPE_YUV420_8		0x18
#define MIPI_CSI2_DATA_TYPE_YUV420_10		0x19
#define MIPI_CSI2_DATA_TYPE_LE_YUV420_8		0x1a
#define MIPI_CSI2_DATA_TYPE_CS_YUV420_8		0x1c
#define MIPI_CSI2_DATA_TYPE_CS_YUV420_10	0x1d
#define MIPI_CSI2_DATA_TYPE_YUV422_8		0x1e
#define MIPI_CSI2_DATA_TYPE_YUV422_10		0x1f
#define MIPI_CSI2_DATA_TYPE_RGB565		0x22
#define MIPI_CSI2_DATA_TYPE_RGB666		0x23
#define MIPI_CSI2_DATA_TYPE_RGB888		0x24
#define MIPI_CSI2_DATA_TYPE_RAW6		0x28
#define MIPI_CSI2_DATA_TYPE_RAW7		0x29
#define MIPI_CSI2_DATA_TYPE_RAW8		0x2a
#define MIPI_CSI2_DATA_TYPE_RAW10		0x2b
#define MIPI_CSI2_DATA_TYPE_RAW12		0x2c
#define MIPI_CSI2_DATA_TYPE_RAW14		0x2d
#define MIPI_CSI2_DATA_TYPE_USER(x)		(0x30 + (x))

struct mipi_csis_event {
	bool debug;
	u32 mask;
	const char * const name;
	unsigned int counter;
};

static const struct mipi_csis_event mipi_csis_events[] = {
	/* Errors */
	{ false, MIPI_CSIS_INT_SRC_ERR_SOT_HS,		"SOT Error" },
	{ false, MIPI_CSIS_INT_SRC_ERR_LOST_FS,		"Lost Frame Start Error" },
	{ false, MIPI_CSIS_INT_SRC_ERR_LOST_FE,		"Lost Frame End Error" },
	{ false, MIPI_CSIS_INT_SRC_ERR_OVER,		"FIFO Overflow Error" },
	{ false, MIPI_CSIS_INT_SRC_ERR_WRONG_CFG,	"Wrong Configuration Error" },
	{ false, MIPI_CSIS_INT_SRC_ERR_ECC,		"ECC Error" },
	{ false, MIPI_CSIS_INT_SRC_ERR_CRC,		"CRC Error" },
	{ false, MIPI_CSIS_INT_SRC_ERR_UNKNOWN,		"Unknown Error" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_DT_NOT_SUPPORT,	"Data Type Not Supported" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_DT_IGNORE,	"Data Type Ignored" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_ERR_FRAME_SIZE,	"Frame Size Error" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_TRUNCATED_FRAME,	"Truncated Frame" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_EARLY_FE,	"Early Frame End" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_EARLY_FS,	"Early Frame Start" },
	/* Non-image data receive events */
	{ false, MIPI_CSIS_INT_SRC_EVEN_BEFORE,		"Non-image data before even frame" },
	{ false, MIPI_CSIS_INT_SRC_EVEN_AFTER,		"Non-image data after even frame" },
	{ false, MIPI_CSIS_INT_SRC_ODD_BEFORE,		"Non-image data before odd frame" },
	{ false, MIPI_CSIS_INT_SRC_ODD_AFTER,		"Non-image data after odd frame" },
	/* Frame start/end */
	{ false, MIPI_CSIS_INT_SRC_FRAME_START,		"Frame Start" },
	{ false, MIPI_CSIS_INT_SRC_FRAME_END,		"Frame End" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_CAM_VSYNC_FALL,	"VSYNC Falling Edge" },
	{ true, MIPI_CSIS_DBG_INTR_SRC_CAM_VSYNC_RISE,	"VSYNC Rising Edge" },
};

#define MIPI_CSIS_NUM_EVENTS ARRAY_SIZE(mipi_csis_events)

enum mipi_csis_clk {
	MIPI_CSIS_CLK_PCLK,
	MIPI_CSIS_CLK_WRAP,
	MIPI_CSIS_CLK_PHY,
	MIPI_CSIS_CLK_AXI,
};

static const char * const mipi_csis_clk_id[] = {
	"pclk",
	"wrap",
	"phy",
	"axi",
};

enum mipi_csis_version {
	MIPI_CSIS_V3_3,
	MIPI_CSIS_V3_6_3,
};

struct mipi_csis_info {
	enum mipi_csis_version version;
	unsigned int num_clocks;
};

struct mipi_csis_device {
	struct device *dev;
	void __iomem *regs;
	struct clk_bulk_data *clks;
	struct reset_control *mrst;
	struct regulator *mipi_phy_regulator;
	const struct mipi_csis_info *info;

	struct v4l2_subdev sd;
	struct media_pad pads[CSIS_PADS_NUM];
	struct v4l2_async_notifier notifier;
	struct v4l2_subdev *src_sd;

	struct v4l2_mbus_config_mipi_csi2 bus;
	u32 clk_frequency;
	u32 hs_settle;
	u32 clk_settle;

	spinlock_t slock;	/* Protect events */
	struct mipi_csis_event events[MIPI_CSIS_NUM_EVENTS];
	struct dentry *debugfs_root;
	struct {
		bool enable;
		u32 hs_settle;
		u32 clk_settle;
	} debug;
};

/* -----------------------------------------------------------------------------
 * Format helpers
 */

struct csis_pix_format {
	u32 code;
	u32 output;
	u32 data_type;
	u8 width;
};

static const struct csis_pix_format mipi_csis_formats[] = {
	/* YUV formats. */
	{
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.output = MEDIA_BUS_FMT_UYVY8_1X16,
		.data_type = MIPI_CSI2_DATA_TYPE_YUV422_8,
		.width = 16,
	},
	/* RGB formats. */
	{
		.code = MEDIA_BUS_FMT_RGB565_1X16,
		.output = MEDIA_BUS_FMT_RGB565_1X16,
		.data_type = MIPI_CSI2_DATA_TYPE_RGB565,
		.width = 16,
	}, {
		.code = MEDIA_BUS_FMT_BGR888_1X24,
		.output = MEDIA_BUS_FMT_RGB888_1X24,
		.data_type = MIPI_CSI2_DATA_TYPE_RGB888,
		.width = 24,
	},
	/* RAW (Bayer and greyscale) formats. */
	{
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.output = MEDIA_BUS_FMT_SBGGR8_1X8,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG8_1X8,
		.output = MEDIA_BUS_FMT_SGBRG8_1X8,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.output = MEDIA_BUS_FMT_SGRBG8_1X8,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB8_1X8,
		.output = MEDIA_BUS_FMT_SRGGB8_1X8,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.output = MEDIA_BUS_FMT_Y8_1X8,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW8,
		.width = 8,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.output = MEDIA_BUS_FMT_SBGGR10_1X10,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG10_1X10,
		.output = MEDIA_BUS_FMT_SGBRG10_1X10,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.output = MEDIA_BUS_FMT_SGRBG10_1X10,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB10_1X10,
		.output = MEDIA_BUS_FMT_SRGGB10_1X10,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.output = MEDIA_BUS_FMT_Y10_1X10,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW10,
		.width = 10,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR12_1X12,
		.output = MEDIA_BUS_FMT_SBGGR12_1X12,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG12_1X12,
		.output = MEDIA_BUS_FMT_SGBRG12_1X12,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG12_1X12,
		.output = MEDIA_BUS_FMT_SGRBG12_1X12,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB12_1X12,
		.output = MEDIA_BUS_FMT_SRGGB12_1X12,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_Y12_1X12,
		.output = MEDIA_BUS_FMT_Y12_1X12,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW12,
		.width = 12,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR14_1X14,
		.output = MEDIA_BUS_FMT_SBGGR14_1X14,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SGBRG14_1X14,
		.output = MEDIA_BUS_FMT_SGBRG14_1X14,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG14_1X14,
		.output = MEDIA_BUS_FMT_SGRBG14_1X14,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW14,
		.width = 14,
	}, {
		.code = MEDIA_BUS_FMT_SRGGB14_1X14,
		.output = MEDIA_BUS_FMT_SRGGB14_1X14,
		.data_type = MIPI_CSI2_DATA_TYPE_RAW14,
		.width = 14,
	},
	/* JPEG */
	{
		.code = MEDIA_BUS_FMT_JPEG_1X8,
		.output = MEDIA_BUS_FMT_JPEG_1X8,
		/*
		 * Map JPEG_1X8 to the RAW8 datatype.
		 *
		 * The CSI-2 specification suggests in Annex A "JPEG8 Data
		 * Format (informative)" to transmit JPEG data using one of the
		 * Data Types aimed to represent arbitrary data, such as the
		 * "User Defined Data Type 1" (0x30).
		 *
		 * However, when configured with a User Defined Data Type, the
		 * CSIS outputs data in quad pixel mode regardless of the mode
		 * selected in the MIPI_CSIS_ISP_CONFIG_CH register. Neither of
		 * the IP cores connected to the CSIS in i.MX SoCs (CSI bridge
		 * or ISI) support quad pixel mode, so this will never work in
		 * practice.
		 *
		 * Some sensors (such as the OV5640) send JPEG data using the
		 * RAW8 data type. This is usable and works, so map the JPEG
		 * format to RAW8. If the CSIS ends up being integrated in an
		 * SoC that can support quad pixel mode, this will have to be
		 * revisited.
		 */
		.data_type = MIPI_CSI2_DATA_TYPE_RAW8,
		.width = 8,
	}
};

static const struct csis_pix_format *find_csis_format(u32 code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(mipi_csis_formats); i++)
		if (code == mipi_csis_formats[i].code)
			return &mipi_csis_formats[i];
	return NULL;
}

/* -----------------------------------------------------------------------------
 * Hardware configuration
 */

static inline u32 mipi_csis_read(struct mipi_csis_device *csis, u32 reg)
{
	return readl(csis->regs + reg);
}

static inline void mipi_csis_write(struct mipi_csis_device *csis, u32 reg,
				   u32 val)
{
	writel(val, csis->regs + reg);
}

static void mipi_csis_enable_interrupts(struct mipi_csis_device *csis, bool on)
{
	mipi_csis_write(csis, MIPI_CSIS_INT_MSK, on ? 0xffffffff : 0);
	mipi_csis_write(csis, MIPI_CSIS_DBG_INTR_MSK, on ? 0xffffffff : 0);
}

static void mipi_csis_sw_reset(struct mipi_csis_device *csis)
{
	u32 val = mipi_csis_read(csis, MIPI_CSIS_CMN_CTRL);

	mipi_csis_write(csis, MIPI_CSIS_CMN_CTRL,
			val | MIPI_CSIS_CMN_CTRL_RESET);
	usleep_range(10, 20);
}

static void mipi_csis_system_enable(struct mipi_csis_device *csis, int on)
{
	u32 val, mask;

	val = mipi_csis_read(csis, MIPI_CSIS_CMN_CTRL);
	if (on)
		val |= MIPI_CSIS_CMN_CTRL_ENABLE;
	else
		val &= ~MIPI_CSIS_CMN_CTRL_ENABLE;
	mipi_csis_write(csis, MIPI_CSIS_CMN_CTRL, val);

	val = mipi_csis_read(csis, MIPI_CSIS_DPHY_CMN_CTRL);
	val &= ~MIPI_CSIS_DPHY_CMN_CTRL_ENABLE;
	if (on) {
		mask = (1 << (csis->bus.num_data_lanes + 1)) - 1;
		val |= (mask & MIPI_CSIS_DPHY_CMN_CTRL_ENABLE);
	}
	mipi_csis_write(csis, MIPI_CSIS_DPHY_CMN_CTRL, val);
}

static void __mipi_csis_set_format(struct mipi_csis_device *csis,
				   const struct v4l2_mbus_framefmt *format,
				   const struct csis_pix_format *csis_fmt)
{
	u32 val;

	/* Color format */
	val = mipi_csis_read(csis, MIPI_CSIS_ISP_CONFIG_CH(0));
	val &= ~(MIPI_CSIS_ISPCFG_ALIGN_32BIT | MIPI_CSIS_ISPCFG_FMT_MASK
		| MIPI_CSIS_ISPCFG_PIXEL_MASK);

	/*
	 * YUV 4:2:2 can be transferred with 8 or 16 bits per clock sample
	 * (referred to in the documentation as single and dual pixel modes
	 * respectively, although the 8-bit mode transfers half a pixel per
	 * clock sample and the 16-bit mode one pixel). While both mode work
	 * when the CSIS is connected to a receiver that supports either option,
	 * single pixel mode requires clock rates twice as high. As all SoCs
	 * that integrate the CSIS can operate in 16-bit bit mode, and some do
	 * not support 8-bit mode (this is the case of the i.MX8MP), use dual
	 * pixel mode unconditionally.
	 *
	 * TODO: Verify which other formats require DUAL (or QUAD) modes.
	 */
	if (csis_fmt->data_type == MIPI_CSI2_DATA_TYPE_YUV422_8)
		val |= MIPI_CSIS_ISPCFG_PIXEL_MODE_DUAL;

	val |= MIPI_CSIS_ISPCFG_FMT(csis_fmt->data_type);
	mipi_csis_write(csis, MIPI_CSIS_ISP_CONFIG_CH(0), val);

	/* Pixel resolution */
	val = format->width | (format->height << 16);
	mipi_csis_write(csis, MIPI_CSIS_ISP_RESOL_CH(0), val);
}

static int mipi_csis_calculate_params(struct mipi_csis_device *csis,
				      const struct csis_pix_format *csis_fmt)
{
	s64 link_freq;
	u32 lane_rate;

	/* Calculate the line rate from the pixel rate. */
	link_freq = v4l2_get_link_freq(csis->src_sd->ctrl_handler,
				       csis_fmt->width,
				       csis->bus.num_data_lanes * 2);
	if (link_freq < 0) {
		dev_err(csis->dev, "Unable to obtain link frequency: %d\n",
			(int)link_freq);
		return link_freq;
	}

	lane_rate = link_freq * 2;

	if (lane_rate < 80000000 || lane_rate > 1500000000) {
		dev_dbg(csis->dev, "Out-of-bound lane rate %u\n", lane_rate);
		return -EINVAL;
	}

	/*
	 * The HSSETTLE counter value is document in a table, but can also
	 * easily be calculated. Hardcode the CLKSETTLE value to 0 for now
	 * (which is documented as corresponding to CSI-2 v0.87 to v1.00) until
	 * we figure out how to compute it correctly.
	 */
	csis->hs_settle = (lane_rate - 5000000) / 45000000;
	csis->clk_settle = 0;

	dev_dbg(csis->dev, "lane rate %u, Tclk_settle %u, Ths_settle %u\n",
		lane_rate, csis->clk_settle, csis->hs_settle);

	if (csis->debug.hs_settle < 0xff) {
		dev_dbg(csis->dev, "overriding Ths_settle with %u\n",
			csis->debug.hs_settle);
		csis->hs_settle = csis->debug.hs_settle;
	}

	if (csis->debug.clk_settle < 4) {
		dev_dbg(csis->dev, "overriding Tclk_settle with %u\n",
			csis->debug.clk_settle);
		csis->clk_settle = csis->debug.clk_settle;
	}

	return 0;
}

static void mipi_csis_set_params(struct mipi_csis_device *csis,
				 const struct v4l2_mbus_framefmt *format,
				 const struct csis_pix_format *csis_fmt)
{
	int lanes = csis->bus.num_data_lanes;
	u32 val;

	val = mipi_csis_read(csis, MIPI_CSIS_CMN_CTRL);
	val &= ~MIPI_CSIS_CMN_CTRL_LANE_NR_MASK;
	val |= (lanes - 1) << MIPI_CSIS_CMN_CTRL_LANE_NR_OFFSET;
	if (csis->info->version == MIPI_CSIS_V3_3)
		val |= MIPI_CSIS_CMN_CTRL_INTER_MODE;
	mipi_csis_write(csis, MIPI_CSIS_CMN_CTRL, val);

	__mipi_csis_set_format(csis, format, csis_fmt);

	mipi_csis_write(csis, MIPI_CSIS_DPHY_CMN_CTRL,
			MIPI_CSIS_DPHY_CMN_CTRL_HSSETTLE(csis->hs_settle) |
			MIPI_CSIS_DPHY_CMN_CTRL_CLKSETTLE(csis->clk_settle));

	val = (0 << MIPI_CSIS_ISP_SYNC_HSYNC_LINTV_OFFSET)
	    | (0 << MIPI_CSIS_ISP_SYNC_VSYNC_SINTV_OFFSET)
	    | (0 << MIPI_CSIS_ISP_SYNC_VSYNC_EINTV_OFFSET);
	mipi_csis_write(csis, MIPI_CSIS_ISP_SYNC_CH(0), val);

	val = mipi_csis_read(csis, MIPI_CSIS_CLK_CTRL);
	val |= MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	val |= MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(15);
	val &= ~MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK;
	mipi_csis_write(csis, MIPI_CSIS_CLK_CTRL, val);

	mipi_csis_write(csis, MIPI_CSIS_DPHY_BCTRL_L,
			MIPI_CSIS_DPHY_BCTRL_L_BIAS_REF_VOLT_715MV |
			MIPI_CSIS_DPHY_BCTRL_L_BGR_CHOPPER_FREQ_3MHZ |
			MIPI_CSIS_DPHY_BCTRL_L_REG_12P_LVL_CTL_1_2V |
			MIPI_CSIS_DPHY_BCTRL_L_LP_RX_HYS_LVL_80MV |
			MIPI_CSIS_DPHY_BCTRL_L_LP_RX_VREF_LVL_715MV |
			MIPI_CSIS_DPHY_BCTRL_L_LP_CD_HYS_60MV |
			MIPI_CSIS_DPHY_BCTRL_L_B_DPHYCTRL(20000000));
	mipi_csis_write(csis, MIPI_CSIS_DPHY_BCTRL_H, 0);

	/* Update the shadow register. */
	val = mipi_csis_read(csis, MIPI_CSIS_CMN_CTRL);
	mipi_csis_write(csis, MIPI_CSIS_CMN_CTRL,
			val | MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW |
			MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW_CTRL);
}

static int mipi_csis_clk_enable(struct mipi_csis_device *csis)
{
	return clk_bulk_prepare_enable(csis->info->num_clocks, csis->clks);
}

static void mipi_csis_clk_disable(struct mipi_csis_device *csis)
{
	clk_bulk_disable_unprepare(csis->info->num_clocks, csis->clks);
}

static int mipi_csis_clk_get(struct mipi_csis_device *csis)
{
	unsigned int i;
	int ret;

	csis->clks = devm_kcalloc(csis->dev, csis->info->num_clocks,
				  sizeof(*csis->clks), GFP_KERNEL);

	if (!csis->clks)
		return -ENOMEM;

	for (i = 0; i < csis->info->num_clocks; i++)
		csis->clks[i].id = mipi_csis_clk_id[i];

	ret = devm_clk_bulk_get(csis->dev, csis->info->num_clocks,
				csis->clks);
	if (ret < 0)
		return ret;

	/* Set clock rate */
	ret = clk_set_rate(csis->clks[MIPI_CSIS_CLK_WRAP].clk,
			   csis->clk_frequency);
	if (ret < 0)
		dev_err(csis->dev, "set rate=%d failed: %d\n",
			csis->clk_frequency, ret);

	return ret;
}

static void mipi_csis_start_stream(struct mipi_csis_device *csis,
				   const struct v4l2_mbus_framefmt *format,
				   const struct csis_pix_format *csis_fmt)
{
	mipi_csis_sw_reset(csis);
	mipi_csis_set_params(csis, format, csis_fmt);
	mipi_csis_system_enable(csis, true);
	mipi_csis_enable_interrupts(csis, true);
}

static void mipi_csis_stop_stream(struct mipi_csis_device *csis)
{
	mipi_csis_enable_interrupts(csis, false);
	mipi_csis_system_enable(csis, false);
}

static irqreturn_t mipi_csis_irq_handler(int irq, void *dev_id)
{
	struct mipi_csis_device *csis = dev_id;
	unsigned long flags;
	unsigned int i;
	u32 status;
	u32 dbg_status;

	status = mipi_csis_read(csis, MIPI_CSIS_INT_SRC);
	dbg_status = mipi_csis_read(csis, MIPI_CSIS_DBG_INTR_SRC);

	spin_lock_irqsave(&csis->slock, flags);

	/* Update the event/error counters */
	if ((status & MIPI_CSIS_INT_SRC_ERRORS) || csis->debug.enable) {
		for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++) {
			struct mipi_csis_event *event = &csis->events[i];

			if ((!event->debug && (status & event->mask)) ||
			    (event->debug && (dbg_status & event->mask)))
				event->counter++;
		}
	}
	spin_unlock_irqrestore(&csis->slock, flags);

	mipi_csis_write(csis, MIPI_CSIS_INT_SRC, status);
	mipi_csis_write(csis, MIPI_CSIS_DBG_INTR_SRC, dbg_status);

	return IRQ_HANDLED;
}

/* -----------------------------------------------------------------------------
 * PHY regulator and reset
 */

static int mipi_csis_phy_enable(struct mipi_csis_device *csis)
{
	if (csis->info->version != MIPI_CSIS_V3_3)
		return 0;

	return regulator_enable(csis->mipi_phy_regulator);
}

static int mipi_csis_phy_disable(struct mipi_csis_device *csis)
{
	if (csis->info->version != MIPI_CSIS_V3_3)
		return 0;

	return regulator_disable(csis->mipi_phy_regulator);
}

static void mipi_csis_phy_reset(struct mipi_csis_device *csis)
{
	if (csis->info->version != MIPI_CSIS_V3_3)
		return;

	reset_control_assert(csis->mrst);
	msleep(20);
	reset_control_deassert(csis->mrst);
}

static int mipi_csis_phy_init(struct mipi_csis_device *csis)
{
	if (csis->info->version != MIPI_CSIS_V3_3)
		return 0;

	/* Get MIPI PHY reset and regulator. */
	csis->mrst = devm_reset_control_get_exclusive(csis->dev, NULL);
	if (IS_ERR(csis->mrst))
		return PTR_ERR(csis->mrst);

	csis->mipi_phy_regulator = devm_regulator_get(csis->dev, "phy");
	if (IS_ERR(csis->mipi_phy_regulator))
		return PTR_ERR(csis->mipi_phy_regulator);

	return regulator_set_voltage(csis->mipi_phy_regulator, 1000000,
				     1000000);
}

/* -----------------------------------------------------------------------------
 * Debug
 */

static void mipi_csis_clear_counters(struct mipi_csis_device *csis)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&csis->slock, flags);
	for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++)
		csis->events[i].counter = 0;
	spin_unlock_irqrestore(&csis->slock, flags);
}

static void mipi_csis_log_counters(struct mipi_csis_device *csis, bool non_errors)
{
	unsigned int num_events = non_errors ? MIPI_CSIS_NUM_EVENTS
				: MIPI_CSIS_NUM_EVENTS - 8;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&csis->slock, flags);

	for (i = 0; i < num_events; ++i) {
		if (csis->events[i].counter > 0 || csis->debug.enable)
			dev_info(csis->dev, "%s events: %d\n",
				 csis->events[i].name,
				 csis->events[i].counter);
	}
	spin_unlock_irqrestore(&csis->slock, flags);
}

static int mipi_csis_dump_regs(struct mipi_csis_device *csis)
{
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
		{ MIPI_CSIS_FRAME_COUNTER_CH(0), "FRAME_COUNTER_CH0" },
	};

	unsigned int i;
	u32 cfg;

	if (!pm_runtime_get_if_in_use(csis->dev))
		return 0;

	dev_info(csis->dev, "--- REGISTERS ---\n");

	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		cfg = mipi_csis_read(csis, registers[i].offset);
		dev_info(csis->dev, "%14s: 0x%08x\n", registers[i].name, cfg);
	}

	pm_runtime_put(csis->dev);

	return 0;
}

static int mipi_csis_dump_regs_show(struct seq_file *m, void *private)
{
	struct mipi_csis_device *csis = m->private;

	return mipi_csis_dump_regs(csis);
}
DEFINE_SHOW_ATTRIBUTE(mipi_csis_dump_regs);

static void mipi_csis_debugfs_init(struct mipi_csis_device *csis)
{
	csis->debug.hs_settle = UINT_MAX;
	csis->debug.clk_settle = UINT_MAX;

	csis->debugfs_root = debugfs_create_dir(dev_name(csis->dev), NULL);

	debugfs_create_bool("debug_enable", 0600, csis->debugfs_root,
			    &csis->debug.enable);
	debugfs_create_file("dump_regs", 0600, csis->debugfs_root, csis,
			    &mipi_csis_dump_regs_fops);
	debugfs_create_u32("tclk_settle", 0600, csis->debugfs_root,
			   &csis->debug.clk_settle);
	debugfs_create_u32("ths_settle", 0600, csis->debugfs_root,
			   &csis->debug.hs_settle);
}

static void mipi_csis_debugfs_exit(struct mipi_csis_device *csis)
{
	debugfs_remove_recursive(csis->debugfs_root);
}

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static struct mipi_csis_device *sd_to_mipi_csis_device(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct mipi_csis_device, sd);
}

static int mipi_csis_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct mipi_csis_device *csis = sd_to_mipi_csis_device(sd);
	const struct v4l2_mbus_framefmt *format;
	const struct csis_pix_format *csis_fmt;
	struct v4l2_subdev_state *state;
	int ret;

	if (!enable) {
		v4l2_subdev_call(csis->src_sd, video, s_stream, 0);

		mipi_csis_stop_stream(csis);
		if (csis->debug.enable)
			mipi_csis_log_counters(csis, true);

		pm_runtime_put(csis->dev);

		return 0;
	}

	state = v4l2_subdev_lock_and_get_active_state(sd);

	format = v4l2_subdev_get_pad_format(sd, state, CSIS_PAD_SINK);
	csis_fmt = find_csis_format(format->code);

	ret = mipi_csis_calculate_params(csis, csis_fmt);
	if (ret < 0)
		goto err_unlock;

	mipi_csis_clear_counters(csis);

	ret = pm_runtime_resume_and_get(csis->dev);
	if (ret < 0)
		goto err_unlock;

	mipi_csis_start_stream(csis, format, csis_fmt);

	ret = v4l2_subdev_call(csis->src_sd, video, s_stream, 1);
	if (ret < 0)
		goto err_stop;

	mipi_csis_log_counters(csis, true);

	v4l2_subdev_unlock_state(state);

	return 0;

err_stop:
	mipi_csis_stop_stream(csis);
	pm_runtime_put(csis->dev);
err_unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int mipi_csis_enum_mbus_code(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *sd_state,
				    struct v4l2_subdev_mbus_code_enum *code)
{
	/*
	 * The CSIS can't transcode in any way, the source format is identical
	 * to the sink format.
	 */
	if (code->pad == CSIS_PAD_SOURCE) {
		struct v4l2_mbus_framefmt *fmt;

		if (code->index > 0)
			return -EINVAL;

		fmt = v4l2_subdev_get_pad_format(sd, sd_state, code->pad);
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

static int mipi_csis_set_fmt(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *sdformat)
{
	struct csis_pix_format const *csis_fmt;
	struct v4l2_mbus_framefmt *fmt;
	unsigned int align;

	/*
	 * The CSIS can't transcode in any way, the source format can't be
	 * modified.
	 */
	if (sdformat->pad == CSIS_PAD_SOURCE)
		return v4l2_subdev_get_fmt(sd, sd_state, sdformat);

	if (sdformat->pad != CSIS_PAD_SINK)
		return -EINVAL;

	/*
	 * Validate the media bus code and clamp and align the size.
	 *
	 * The total number of bits per line must be a multiple of 8. We thus
	 * need to align the width for formats that are not multiples of 8
	 * bits.
	 */
	csis_fmt = find_csis_format(sdformat->format.code);
	if (!csis_fmt)
		csis_fmt = &mipi_csis_formats[0];

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
	default:
		/* 1, 3, 5, 7 */
		align = 3;
		break;
	}

	v4l_bound_align_image(&sdformat->format.width, 1,
			      CSIS_MAX_PIX_WIDTH, align,
			      &sdformat->format.height, 1,
			      CSIS_MAX_PIX_HEIGHT, 0, 0);

	fmt = v4l2_subdev_get_pad_format(sd, sd_state, sdformat->pad);

	fmt->code = csis_fmt->code;
	fmt->width = sdformat->format.width;
	fmt->height = sdformat->format.height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = sdformat->format.colorspace;
	fmt->quantization = sdformat->format.quantization;
	fmt->xfer_func = sdformat->format.xfer_func;
	fmt->ycbcr_enc = sdformat->format.ycbcr_enc;

	sdformat->format = *fmt;

	/* Propagate the format from sink to source. */
	fmt = v4l2_subdev_get_pad_format(sd, sd_state, CSIS_PAD_SOURCE);
	*fmt = sdformat->format;

	/* The format on the source pad might change due to unpacking. */
	fmt->code = csis_fmt->output;

	return 0;
}

static int mipi_csis_get_frame_desc(struct v4l2_subdev *sd, unsigned int pad,
				    struct v4l2_mbus_frame_desc *fd)
{
	struct v4l2_mbus_frame_desc_entry *entry = &fd->entry[0];
	const struct csis_pix_format *csis_fmt;
	const struct v4l2_mbus_framefmt *fmt;
	struct v4l2_subdev_state *state;

	if (pad != CSIS_PAD_SOURCE)
		return -EINVAL;

	state = v4l2_subdev_lock_and_get_active_state(sd);
	fmt = v4l2_subdev_get_pad_format(sd, state, CSIS_PAD_SOURCE);
	csis_fmt = find_csis_format(fmt->code);
	v4l2_subdev_unlock_state(state);

	if (!csis_fmt)
		return -EPIPE;

	fd->type = V4L2_MBUS_FRAME_DESC_TYPE_PARALLEL;
	fd->num_entries = 1;

	memset(entry, 0, sizeof(*entry));

	entry->flags = 0;
	entry->pixelcode = csis_fmt->code;
	entry->bus.csi2.vc = 0;
	entry->bus.csi2.dt = csis_fmt->data_type;

	return 0;
}

static int mipi_csis_init_cfg(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.pad = CSIS_PAD_SINK,
	};

	fmt.format.code = mipi_csis_formats[0].code;
	fmt.format.width = MIPI_CSIS_DEF_PIX_WIDTH;
	fmt.format.height = MIPI_CSIS_DEF_PIX_HEIGHT;

	fmt.format.colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt.format.xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt.format.colorspace);
	fmt.format.ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt.format.colorspace);
	fmt.format.quantization =
		V4L2_MAP_QUANTIZATION_DEFAULT(false, fmt.format.colorspace,
					      fmt.format.ycbcr_enc);

	return mipi_csis_set_fmt(sd, sd_state, &fmt);
}

static int mipi_csis_log_status(struct v4l2_subdev *sd)
{
	struct mipi_csis_device *csis = sd_to_mipi_csis_device(sd);

	mipi_csis_log_counters(csis, true);
	if (csis->debug.enable)
		mipi_csis_dump_regs(csis);

	return 0;
}

static const struct v4l2_subdev_core_ops mipi_csis_core_ops = {
	.log_status	= mipi_csis_log_status,
};

static const struct v4l2_subdev_video_ops mipi_csis_video_ops = {
	.s_stream	= mipi_csis_s_stream,
};

static const struct v4l2_subdev_pad_ops mipi_csis_pad_ops = {
	.init_cfg		= mipi_csis_init_cfg,
	.enum_mbus_code		= mipi_csis_enum_mbus_code,
	.get_fmt		= v4l2_subdev_get_fmt,
	.set_fmt		= mipi_csis_set_fmt,
	.get_frame_desc		= mipi_csis_get_frame_desc,
};

static const struct v4l2_subdev_ops mipi_csis_subdev_ops = {
	.core	= &mipi_csis_core_ops,
	.video	= &mipi_csis_video_ops,
	.pad	= &mipi_csis_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Media entity operations
 */

static int mipi_csis_link_setup(struct media_entity *entity,
				const struct media_pad *local_pad,
				const struct media_pad *remote_pad, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct mipi_csis_device *csis = sd_to_mipi_csis_device(sd);
	struct v4l2_subdev *remote_sd;

	dev_dbg(csis->dev, "link setup %s -> %s", remote_pad->entity->name,
		local_pad->entity->name);

	/* We only care about the link to the source. */
	if (!(local_pad->flags & MEDIA_PAD_FL_SINK))
		return 0;

	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	if (flags & MEDIA_LNK_FL_ENABLED) {
		if (csis->src_sd)
			return -EBUSY;

		csis->src_sd = remote_sd;
	} else {
		csis->src_sd = NULL;
	}

	return 0;
}

static const struct media_entity_operations mipi_csis_entity_ops = {
	.link_setup	= mipi_csis_link_setup,
	.link_validate	= v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,
};

/* -----------------------------------------------------------------------------
 * Async subdev notifier
 */

static struct mipi_csis_device *
mipi_notifier_to_csis_state(struct v4l2_async_notifier *n)
{
	return container_of(n, struct mipi_csis_device, notifier);
}

static int mipi_csis_notify_bound(struct v4l2_async_notifier *notifier,
				  struct v4l2_subdev *sd,
				  struct v4l2_async_subdev *asd)
{
	struct mipi_csis_device *csis = mipi_notifier_to_csis_state(notifier);
	struct media_pad *sink = &csis->sd.entity.pads[CSIS_PAD_SINK];

	return v4l2_create_fwnode_links_to_pad(sd, sink, 0);
}

static const struct v4l2_async_notifier_operations mipi_csis_notify_ops = {
	.bound = mipi_csis_notify_bound,
};

static int mipi_csis_async_register(struct mipi_csis_device *csis)
{
	struct v4l2_fwnode_endpoint vep = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct v4l2_async_subdev *asd;
	struct fwnode_handle *ep;
	unsigned int i;
	int ret;

	v4l2_async_nf_init(&csis->notifier);

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(csis->dev), 0, 0,
					     FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!ep)
		return -ENOTCONN;

	ret = v4l2_fwnode_endpoint_parse(ep, &vep);
	if (ret)
		goto err_parse;

	for (i = 0; i < vep.bus.mipi_csi2.num_data_lanes; ++i) {
		if (vep.bus.mipi_csi2.data_lanes[i] != i + 1) {
			dev_err(csis->dev,
				"data lanes reordering is not supported");
			ret = -EINVAL;
			goto err_parse;
		}
	}

	csis->bus = vep.bus.mipi_csi2;

	dev_dbg(csis->dev, "data lanes: %d\n", csis->bus.num_data_lanes);
	dev_dbg(csis->dev, "flags: 0x%08x\n", csis->bus.flags);

	asd = v4l2_async_nf_add_fwnode_remote(&csis->notifier, ep,
					      struct v4l2_async_subdev);
	if (IS_ERR(asd)) {
		ret = PTR_ERR(asd);
		goto err_parse;
	}

	fwnode_handle_put(ep);

	csis->notifier.ops = &mipi_csis_notify_ops;

	ret = v4l2_async_subdev_nf_register(&csis->sd, &csis->notifier);
	if (ret)
		return ret;

	return v4l2_async_register_subdev(&csis->sd);

err_parse:
	fwnode_handle_put(ep);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Suspend/resume
 */

static int __maybe_unused mipi_csis_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct mipi_csis_device *csis = sd_to_mipi_csis_device(sd);
	int ret;

	ret = mipi_csis_phy_disable(csis);
	if (ret)
		return -EAGAIN;

	mipi_csis_clk_disable(csis);

	return 0;
}

static int __maybe_unused mipi_csis_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct mipi_csis_device *csis = sd_to_mipi_csis_device(sd);
	int ret;

	ret = mipi_csis_phy_enable(csis);
	if (ret)
		return -EAGAIN;

	ret = mipi_csis_clk_enable(csis);
	if (ret) {
		mipi_csis_phy_disable(csis);
		return ret;
	}

	return 0;
}

static const struct dev_pm_ops mipi_csis_pm_ops = {
	SET_RUNTIME_PM_OPS(mipi_csis_runtime_suspend, mipi_csis_runtime_resume,
			   NULL)
};

/* -----------------------------------------------------------------------------
 * Probe/remove & platform driver
 */

static int mipi_csis_subdev_init(struct mipi_csis_device *csis)
{
	struct v4l2_subdev *sd = &csis->sd;
	int ret;

	v4l2_subdev_init(sd, &mipi_csis_subdev_ops);
	sd->owner = THIS_MODULE;
	snprintf(sd->name, sizeof(sd->name), "csis-%s",
		 dev_name(csis->dev));

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->ctrl_handler = NULL;

	sd->entity.function = MEDIA_ENT_F_VID_IF_BRIDGE;
	sd->entity.ops = &mipi_csis_entity_ops;

	sd->dev = csis->dev;

	sd->fwnode = fwnode_graph_get_endpoint_by_id(dev_fwnode(csis->dev),
						     1, 0, 0);
	if (!sd->fwnode) {
		dev_err(csis->dev, "Unable to retrieve endpoint for port@1\n");
		return -ENOENT;
	}

	csis->pads[CSIS_PAD_SINK].flags = MEDIA_PAD_FL_SINK
					 | MEDIA_PAD_FL_MUST_CONNECT;
	csis->pads[CSIS_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE
					   | MEDIA_PAD_FL_MUST_CONNECT;
	ret = media_entity_pads_init(&sd->entity, CSIS_PADS_NUM, csis->pads);
	if (ret)
		return ret;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret) {
		media_entity_cleanup(&sd->entity);
		return ret;
	}

	return 0;
}

static int mipi_csis_parse_dt(struct mipi_csis_device *csis)
{
	struct device_node *node = csis->dev->of_node;

	if (of_property_read_u32(node, "clock-frequency",
				 &csis->clk_frequency))
		csis->clk_frequency = DEFAULT_SCLK_CSIS_FREQ;

	return 0;
}

static int mipi_csis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mipi_csis_device *csis;
	int irq;
	int ret;

	csis = devm_kzalloc(dev, sizeof(*csis), GFP_KERNEL);
	if (!csis)
		return -ENOMEM;

	spin_lock_init(&csis->slock);

	csis->dev = dev;
	csis->info = of_device_get_match_data(dev);

	memcpy(csis->events, mipi_csis_events, sizeof(csis->events));

	/* Parse DT properties. */
	ret = mipi_csis_parse_dt(csis);
	if (ret < 0) {
		dev_err(dev, "Failed to parse device tree: %d\n", ret);
		return ret;
	}

	/* Acquire resources. */
	csis->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(csis->regs))
		return PTR_ERR(csis->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = mipi_csis_phy_init(csis);
	if (ret < 0)
		return ret;

	ret = mipi_csis_clk_get(csis);
	if (ret < 0)
		return ret;

	/* Reset PHY and enable the clocks. */
	mipi_csis_phy_reset(csis);

	ret = mipi_csis_clk_enable(csis);
	if (ret < 0) {
		dev_err(csis->dev, "failed to enable clocks: %d\n", ret);
		return ret;
	}

	/* Now that the hardware is initialized, request the interrupt. */
	ret = devm_request_irq(dev, irq, mipi_csis_irq_handler, 0,
			       dev_name(dev), csis);
	if (ret) {
		dev_err(dev, "Interrupt request failed\n");
		goto err_disable_clock;
	}

	/* Initialize and register the subdev. */
	ret = mipi_csis_subdev_init(csis);
	if (ret < 0)
		goto err_disable_clock;

	platform_set_drvdata(pdev, &csis->sd);

	ret = mipi_csis_async_register(csis);
	if (ret < 0) {
		dev_err(dev, "async register failed: %d\n", ret);
		goto err_cleanup;
	}

	/* Initialize debugfs. */
	mipi_csis_debugfs_init(csis);

	/* Enable runtime PM. */
	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mipi_csis_runtime_resume(dev);
		if (ret < 0)
			goto err_unregister_all;
	}

	dev_info(dev, "lanes: %d, freq: %u\n",
		 csis->bus.num_data_lanes, csis->clk_frequency);

	return 0;

err_unregister_all:
	mipi_csis_debugfs_exit(csis);
err_cleanup:
	v4l2_subdev_cleanup(&csis->sd);
	media_entity_cleanup(&csis->sd.entity);
	v4l2_async_nf_unregister(&csis->notifier);
	v4l2_async_nf_cleanup(&csis->notifier);
	v4l2_async_unregister_subdev(&csis->sd);
err_disable_clock:
	mipi_csis_clk_disable(csis);
	fwnode_handle_put(csis->sd.fwnode);

	return ret;
}

static void mipi_csis_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct mipi_csis_device *csis = sd_to_mipi_csis_device(sd);

	mipi_csis_debugfs_exit(csis);
	v4l2_async_nf_unregister(&csis->notifier);
	v4l2_async_nf_cleanup(&csis->notifier);
	v4l2_async_unregister_subdev(&csis->sd);

	pm_runtime_disable(&pdev->dev);
	mipi_csis_runtime_suspend(&pdev->dev);
	mipi_csis_clk_disable(csis);
	v4l2_subdev_cleanup(&csis->sd);
	media_entity_cleanup(&csis->sd.entity);
	fwnode_handle_put(csis->sd.fwnode);
	pm_runtime_set_suspended(&pdev->dev);
}

static const struct of_device_id mipi_csis_of_match[] = {
	{
		.compatible = "fsl,imx7-mipi-csi2",
		.data = &(const struct mipi_csis_info){
			.version = MIPI_CSIS_V3_3,
			.num_clocks = 3,
		},
	}, {
		.compatible = "fsl,imx8mm-mipi-csi2",
		.data = &(const struct mipi_csis_info){
			.version = MIPI_CSIS_V3_6_3,
			.num_clocks = 4,
		},
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mipi_csis_of_match);

static struct platform_driver mipi_csis_driver = {
	.probe		= mipi_csis_probe,
	.remove_new	= mipi_csis_remove,
	.driver		= {
		.of_match_table = mipi_csis_of_match,
		.name		= CSIS_DRIVER_NAME,
		.pm		= &mipi_csis_pm_ops,
	},
};

module_platform_driver(mipi_csis_driver);

MODULE_DESCRIPTION("i.MX7 & i.MX8 MIPI CSI-2 receiver driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-mipi-csi2");
