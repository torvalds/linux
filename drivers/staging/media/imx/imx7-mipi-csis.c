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
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>

#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#include "imx-media.h"

#define CSIS_DRIVER_NAME	"imx7-mipi-csis"
#define CSIS_SUBDEV_NAME	CSIS_DRIVER_NAME

#define CSIS_PAD_SINK		0
#define CSIS_PAD_SOURCE		1
#define CSIS_PADS_NUM		2

#define MIPI_CSIS_DEF_PIX_WIDTH		640
#define MIPI_CSIS_DEF_PIX_HEIGHT	480

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
#define MIPI_CSIS_INTMSK		0x10
#define MIPI_CSIS_INTMSK_EVEN_BEFORE	BIT(31)
#define MIPI_CSIS_INTMSK_EVEN_AFTER	BIT(30)
#define MIPI_CSIS_INTMSK_ODD_BEFORE	BIT(29)
#define MIPI_CSIS_INTMSK_ODD_AFTER	BIT(28)
#define MIPI_CSIS_INTMSK_FRAME_START	BIT(24)
#define MIPI_CSIS_INTMSK_FRAME_END	BIT(20)
#define MIPI_CSIS_INTMSK_ERR_SOT_HS	BIT(16)
#define MIPI_CSIS_INTMSK_ERR_LOST_FS	BIT(12)
#define MIPI_CSIS_INTMSK_ERR_LOST_FE	BIT(8)
#define MIPI_CSIS_INTMSK_ERR_OVER	BIT(4)
#define MIPI_CSIS_INTMSK_ERR_WRONG_CFG	BIT(3)
#define MIPI_CSIS_INTMSK_ERR_ECC	BIT(2)
#define MIPI_CSIS_INTMSK_ERR_CRC	BIT(1)
#define MIPI_CSIS_INTMSK_ERR_UNKNOWN	BIT(0)

/* CSIS Interrupt source */
#define MIPI_CSIS_INTSRC		0x14
#define MIPI_CSIS_INTSRC_EVEN_BEFORE	BIT(31)
#define MIPI_CSIS_INTSRC_EVEN_AFTER	BIT(30)
#define MIPI_CSIS_INTSRC_EVEN		BIT(30)
#define MIPI_CSIS_INTSRC_ODD_BEFORE	BIT(29)
#define MIPI_CSIS_INTSRC_ODD_AFTER	BIT(28)
#define MIPI_CSIS_INTSRC_ODD		(0x3 << 28)
#define MIPI_CSIS_INTSRC_NON_IMAGE_DATA	(0xf << 28)
#define MIPI_CSIS_INTSRC_FRAME_START	BIT(24)
#define MIPI_CSIS_INTSRC_FRAME_END	BIT(20)
#define MIPI_CSIS_INTSRC_ERR_SOT_HS	BIT(16)
#define MIPI_CSIS_INTSRC_ERR_LOST_FS	BIT(12)
#define MIPI_CSIS_INTSRC_ERR_LOST_FE	BIT(8)
#define MIPI_CSIS_INTSRC_ERR_OVER	BIT(4)
#define MIPI_CSIS_INTSRC_ERR_WRONG_CFG	BIT(3)
#define MIPI_CSIS_INTSRC_ERR_ECC	BIT(2)
#define MIPI_CSIS_INTSRC_ERR_CRC	BIT(1)
#define MIPI_CSIS_INTSRC_ERR_UNKNOWN	BIT(0)
#define MIPI_CSIS_INTSRC_ERRORS		0xfffff

/* D-PHY status control */
#define MIPI_CSIS_DPHYSTATUS			0x20
#define MIPI_CSIS_DPHYSTATUS_ULPS_DAT		BIT(8)
#define MIPI_CSIS_DPHYSTATUS_STOPSTATE_DAT	BIT(4)
#define MIPI_CSIS_DPHYSTATUS_ULPS_CLK		BIT(1)
#define MIPI_CSIS_DPHYSTATUS_STOPSTATE_CLK	BIT(0)

/* D-PHY common control */
#define MIPI_CSIS_DPHYCTRL			0x24
#define MIPI_CSIS_DPHYCTRL_HSS_MASK		(0xff << 24)
#define MIPI_CSIS_DPHYCTRL_HSS_OFFSET		24
#define MIPI_CSIS_DPHYCTRL_SCLKS_MASK		(0x3 << 22)
#define MIPI_CSIS_DPHYCTRL_SCLKS_OFFSET		22
#define MIPI_CSIS_DPHYCTRL_DPDN_SWAP_CLK	BIT(6)
#define MIPI_CSIS_DPHYCTRL_DPDN_SWAP_DAT	BIT(5)
#define MIPI_CSIS_DPHYCTRL_ENABLE_DAT		BIT(1)
#define MIPI_CSIS_DPHYCTRL_ENABLE_CLK		BIT(0)
#define MIPI_CSIS_DPHYCTRL_ENABLE		(0x1f << 0)

/* D-PHY Master and Slave Control register Low */
#define MIPI_CSIS_DPHYBCTRL_L		0x30
/* D-PHY Master and Slave Control register High */
#define MIPI_CSIS_DPHYBCTRL_H		0x34
/* D-PHY Slave Control register Low */
#define MIPI_CSIS_DPHYSCTRL_L		0x38
/* D-PHY Slave Control register High */
#define MIPI_CSIS_DPHYSCTRL_H		0x3c

/* ISP Configuration register */
#define MIPI_CSIS_ISPCONFIG_CH0		0x40
#define MIPI_CSIS_ISPCONFIG_CH1		0x50
#define MIPI_CSIS_ISPCONFIG_CH2		0x60
#define MIPI_CSIS_ISPCONFIG_CH3		0x70

#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP_MSK	(0xff << 24)
#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP(x)	((x) << 24)
#define MIPI_CSIS_ISPCFG_DOUBLE_CMPNT		BIT(12)
#define MIPI_CSIS_ISPCFG_ALIGN_32BIT		BIT(11)
#define MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT	(0x1e << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW8		(0x2a << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW10		(0x2b << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW12		(0x2c << 2)

/* User defined formats, x = 1...4 */
#define MIPI_CSIS_ISPCFG_FMT_USER(x)	((0x30 + (x) - 1) << 2)
#define MIPI_CSIS_ISPCFG_FMT_MASK	(0x3f << 2)

/* ISP Image Resolution register */
#define MIPI_CSIS_ISPRESOL_CH0		0x44
#define MIPI_CSIS_ISPRESOL_CH1		0x54
#define MIPI_CSIS_ISPRESOL_CH2		0x64
#define MIPI_CSIS_ISPRESOL_CH3		0x74
#define CSIS_MAX_PIX_WIDTH		0xffff
#define CSIS_MAX_PIX_HEIGHT		0xffff

/* ISP SYNC register */
#define MIPI_CSIS_ISPSYNC_CH0		0x48
#define MIPI_CSIS_ISPSYNC_CH1		0x58
#define MIPI_CSIS_ISPSYNC_CH2		0x68
#define MIPI_CSIS_ISPSYNC_CH3		0x78

#define MIPI_CSIS_ISPSYNC_HSYNC_LINTV_OFFSET	18
#define MIPI_CSIS_ISPSYNC_VSYNC_SINTV_OFFSET	12
#define MIPI_CSIS_ISPSYNC_VSYNC_EINTV_OFFSET	0

/* Non-image packet data buffers */
#define MIPI_CSIS_PKTDATA_ODD		0x2000
#define MIPI_CSIS_PKTDATA_EVEN		0x3000
#define MIPI_CSIS_PKTDATA_SIZE		SZ_4K

#define DEFAULT_SCLK_CSIS_FREQ		166000000UL

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
	{ MIPI_CSIS_INTSRC_ERR_SOT_HS,	"SOT Error" },
	{ MIPI_CSIS_INTSRC_ERR_LOST_FS,	"Lost Frame Start Error" },
	{ MIPI_CSIS_INTSRC_ERR_LOST_FE,	"Lost Frame End Error" },
	{ MIPI_CSIS_INTSRC_ERR_OVER,	"FIFO Overflow Error" },
	{ MIPI_CSIS_INTSRC_ERR_WRONG_CFG, "Wrong Configuration Error" },
	{ MIPI_CSIS_INTSRC_ERR_ECC,	"ECC Error" },
	{ MIPI_CSIS_INTSRC_ERR_CRC,	"CRC Error" },
	{ MIPI_CSIS_INTSRC_ERR_UNKNOWN,	"Unknown Error" },
	/* Non-image data receive events */
	{ MIPI_CSIS_INTSRC_EVEN_BEFORE,	"Non-image data before even frame" },
	{ MIPI_CSIS_INTSRC_EVEN_AFTER,	"Non-image data after even frame" },
	{ MIPI_CSIS_INTSRC_ODD_BEFORE,	"Non-image data before odd frame" },
	{ MIPI_CSIS_INTSRC_ODD_AFTER,	"Non-image data after odd frame" },
	/* Frame start/end */
	{ MIPI_CSIS_INTSRC_FRAME_START,	"Frame Start" },
	{ MIPI_CSIS_INTSRC_FRAME_END,	"Frame End" },
};

#define MIPI_CSIS_NUM_EVENTS ARRAY_SIZE(mipi_csis_events)

static const char * const mipi_csis_clk_id[] = {"pclk", "wrap", "phy"};

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
	struct v4l2_subdev *src_sd;

	u8 index;
	struct platform_device *pdev;
	struct phy *phy;
	void __iomem *regs;
	struct clk *wrap_clk;
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
	bool sink_linked;
};

struct csis_pix_format {
	unsigned int pix_width_alignment;
	u32 code;
	u32 fmt_reg;
	u8 data_alignment;
};

static const struct csis_pix_format mipi_csis_formats[] = {
	{
		.code = MEDIA_BUS_FMT_SBGGR10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_VYUY8_2X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.data_alignment = 8,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_Y8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.data_alignment = 8,
	}, {
		.code = MEDIA_BUS_FMT_Y10_1X10,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW10,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_Y12_1X12,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW12,
		.data_alignment = 16,
	}
};

#define mipi_csis_write(__csis, __r, __v) writel(__v, (__csis)->regs + (__r))
#define mipi_csis_read(__csis, __r) readl((__csis)->regs + (__r))

static int mipi_csis_dump_regs(struct csi_state *state)
{
	struct device *dev = &state->pdev->dev;
	unsigned int i;
	u32 cfg;
	static const struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		{ 0x04, "CTRL" },
		{ 0x24, "DPHYCTRL" },
		{ 0x08, "CLKCTRL" },
		{ 0x20, "DPHYSTS" },
		{ 0x10, "INTMSK" },
		{ 0x40, "CONFIG_CH0" },
		{ 0x44, "RESOL_CH0" },
		{ 0xC0, "DBG_CONFIG" },
		{ 0x38, "DPHYSLAVE_L" },
		{ 0x3C, "DPHYSLAVE_H" },
	};

	dev_info(dev, "--- REGISTERS ---\n");

	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		cfg = mipi_csis_read(state, registers[i].offset);
		dev_info(dev, "%12s: 0x%08x\n", registers[i].name, cfg);
	}

	return 0;
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
	mipi_csis_write(state, MIPI_CSIS_INTMSK, on ? 0xffffffff : 0);
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

	val = mipi_csis_read(state, MIPI_CSIS_DPHYCTRL);
	val &= ~MIPI_CSIS_DPHYCTRL_ENABLE;
	if (on) {
		mask = (1 << (state->bus.num_data_lanes + 1)) - 1;
		val |= (mask & MIPI_CSIS_DPHYCTRL_ENABLE);
	}
	mipi_csis_write(state, MIPI_CSIS_DPHYCTRL, val);
}

/* Called with the state.lock mutex held */
static void __mipi_csis_set_format(struct csi_state *state)
{
	struct v4l2_mbus_framefmt *mf = &state->format_mbus;
	u32 val;

	/* Color format */
	val = mipi_csis_read(state, MIPI_CSIS_ISPCONFIG_CH0);
	val = (val & ~MIPI_CSIS_ISPCFG_FMT_MASK) | state->csis_fmt->fmt_reg;
	mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH0, val);

	/* Pixel resolution */
	val = mf->width | (mf->height << 16);
	mipi_csis_write(state, MIPI_CSIS_ISPRESOL_CH0, val);
}

static void mipi_csis_set_hsync_settle(struct csi_state *state, int hs_settle)
{
	u32 val = mipi_csis_read(state, MIPI_CSIS_DPHYCTRL);

	val = (val & ~MIPI_CSIS_DPHYCTRL_HSS_MASK) | (hs_settle << 24);

	mipi_csis_write(state, MIPI_CSIS_DPHYCTRL, val);
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

	mipi_csis_set_hsync_settle(state, state->hs_settle);

	val = mipi_csis_read(state, MIPI_CSIS_ISPCONFIG_CH0);
	if (state->csis_fmt->data_alignment == 32)
		val |= MIPI_CSIS_ISPCFG_ALIGN_32BIT;
	else
		val &= ~MIPI_CSIS_ISPCFG_ALIGN_32BIT;
	mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH0, val);

	val = (0 << MIPI_CSIS_ISPSYNC_HSYNC_LINTV_OFFSET) |
		(0 << MIPI_CSIS_ISPSYNC_VSYNC_SINTV_OFFSET) |
		(0 << MIPI_CSIS_ISPSYNC_VSYNC_EINTV_OFFSET);
	mipi_csis_write(state, MIPI_CSIS_ISPSYNC_CH0, val);

	val = mipi_csis_read(state, MIPI_CSIS_CLK_CTRL);
	val &= ~MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	if (state->wrap_clk)
		val |= MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	else
		val &= ~MIPI_CSIS_CLK_CTRL_WCLK_SRC;

	val |= MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(15);
	val &= ~MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK;
	mipi_csis_write(state, MIPI_CSIS_CLK_CTRL, val);

	mipi_csis_write(state, MIPI_CSIS_DPHYBCTRL_L, 0x1f4);
	mipi_csis_write(state, MIPI_CSIS_DPHYBCTRL_H, 0);

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

	state->wrap_clk = devm_clk_get(dev, "wrap");
	if (IS_ERR(state->wrap_clk))
		return PTR_ERR(state->wrap_clk);

	/* Set clock rate */
	ret = clk_set_rate(state->wrap_clk, state->clk_frequency);
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
	int ret = 0;

	if (enable) {
		mipi_csis_clear_counters(state);
		ret = pm_runtime_get_sync(&state->pdev->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&state->pdev->dev);
			return ret;
		}
		ret = v4l2_subdev_call(state->src_sd, core, s_power, 1);
		if (ret < 0)
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

	remote_sd = media_entity_to_v4l2_subdev(remote_pad->entity);

	mutex_lock(&state->lock);

	if (local_pad->flags & MEDIA_PAD_FL_SOURCE) {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (state->sink_linked) {
				ret = -EBUSY;
				goto out;
			}
			state->sink_linked = true;
		} else {
			state->sink_linked = false;
		}
	} else {
		if (flags & MEDIA_LNK_FL_ENABLED) {
			if (state->src_sd) {
				ret = -EBUSY;
				goto out;
			}
			state->src_sd = remote_sd;
		} else {
			state->src_sd = NULL;
		}
	}

out:
	mutex_unlock(&state->lock);
	return ret;
}

static int mipi_csis_init_cfg(struct v4l2_subdev *mipi_sd,
			      struct v4l2_subdev_pad_config *cfg)
{
	struct v4l2_mbus_framefmt *mf;
	unsigned int i;
	int ret;

	for (i = 0; i < CSIS_PADS_NUM; i++) {
		mf = v4l2_subdev_get_try_format(mipi_sd, cfg, i);

		ret = imx_media_init_mbus_fmt(mf, MIPI_CSIS_DEF_PIX_HEIGHT,
					      MIPI_CSIS_DEF_PIX_WIDTH, 0,
					      V4L2_FIELD_NONE, NULL);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static struct csis_pix_format const *
mipi_csis_try_format(struct v4l2_subdev *mipi_sd, struct v4l2_mbus_framefmt *mf)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	struct csis_pix_format const *csis_fmt;

	csis_fmt = find_csis_format(mf->code);
	if (!csis_fmt)
		csis_fmt = &mipi_csis_formats[0];

	v4l_bound_align_image(&mf->width, 1, CSIS_MAX_PIX_WIDTH,
			      csis_fmt->pix_width_alignment,
			      &mf->height, 1, CSIS_MAX_PIX_HEIGHT, 1,
			      0);

	state->format_mbus.code = csis_fmt->code;
	state->format_mbus.width = mf->width;
	state->format_mbus.height = mf->height;

	return csis_fmt;
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

static int mipi_csis_set_fmt(struct v4l2_subdev *mipi_sd,
			     struct v4l2_subdev_pad_config *cfg,
			     struct v4l2_subdev_format *sdformat)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	struct csis_pix_format const *csis_fmt;
	struct v4l2_mbus_framefmt *fmt;

	if (sdformat->pad >= CSIS_PADS_NUM)
		return -EINVAL;

	fmt = mipi_csis_get_format(state, cfg, sdformat->which, sdformat->pad);

	mutex_lock(&state->lock);
	if (sdformat->pad == CSIS_PAD_SOURCE) {
		sdformat->format = *fmt;
		goto unlock;
	}

	csis_fmt = mipi_csis_try_format(mipi_sd, &sdformat->format);

	sdformat->format = *fmt;

	if (csis_fmt && sdformat->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		state->csis_fmt = csis_fmt;
	else
		cfg->try_fmt = sdformat->format;

unlock:
	mutex_unlock(&state->lock);

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

	status = mipi_csis_read(state, MIPI_CSIS_INTSRC);

	spin_lock_irqsave(&state->slock, flags);

	/* Update the event/error counters */
	if ((status & MIPI_CSIS_INTSRC_ERRORS) || state->debug) {
		for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++) {
			if (!(status & state->events[i].mask))
				continue;
			state->events[i].counter++;
		}
	}
	spin_unlock_irqrestore(&state->slock, flags);

	mipi_csis_write(state, MIPI_CSIS_INTSRC, status);

	return IRQ_HANDLED;
}

static const struct v4l2_subdev_core_ops mipi_csis_core_ops = {
	.log_status	= mipi_csis_log_status,
};

static const struct media_entity_operations mipi_csis_entity_ops = {
	.link_setup	= mipi_csis_link_setup,
	.link_validate	= v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_video_ops mipi_csis_video_ops = {
	.s_stream	= mipi_csis_s_stream,
};

static const struct v4l2_subdev_pad_ops mipi_csis_pad_ops = {
	.init_cfg		= mipi_csis_init_cfg,
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
	state->mrst = devm_reset_control_get_exclusive(&pdev->dev, "mrst");
	if (IS_ERR(state->mrst))
		return PTR_ERR(state->mrst);

	/* Get MIPI CSI-2 bus configuration from the endpoint node. */
	of_property_read_u32(node, "fsl,csis-hs-settle", &state->hs_settle);

	return 0;
}

static int mipi_csis_pm_resume(struct device *dev, bool runtime);

static int mipi_csis_parse_endpoint(struct device *dev,
				    struct v4l2_fwnode_endpoint *ep,
				    struct v4l2_async_subdev *asd)
{
	struct v4l2_subdev *mipi_sd = dev_get_drvdata(dev);
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);

	if (ep->bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(dev, "invalid bus type, must be MIPI CSI2\n");
		return -EINVAL;
	}

	state->bus = ep->bus.mipi_csi2;

	dev_dbg(state->dev, "data lanes: %d\n", state->bus.num_data_lanes);
	dev_dbg(state->dev, "flags: 0x%08x\n", state->bus.flags);

	return 0;
}

static int mipi_csis_subdev_init(struct v4l2_subdev *mipi_sd,
				 struct platform_device *pdev,
				 const struct v4l2_subdev_ops *ops)
{
	struct csi_state *state = mipi_sd_to_csis_state(mipi_sd);
	unsigned int sink_port = 0;
	int ret;

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
	state->format_mbus.code = mipi_csis_formats[0].code;
	state->format_mbus.width = MIPI_CSIS_DEF_PIX_WIDTH;
	state->format_mbus.height = MIPI_CSIS_DEF_PIX_HEIGHT;
	state->format_mbus.field = V4L2_FIELD_NONE;

	v4l2_set_subdevdata(mipi_sd, &pdev->dev);

	state->pads[CSIS_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	state->pads[CSIS_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&mipi_sd->entity, CSIS_PADS_NUM,
				     state->pads);
	if (ret)
		return ret;

	ret = v4l2_async_register_fwnode_subdev(mipi_sd,
						sizeof(struct v4l2_async_subdev),
						&sink_port, 1,
						mipi_csis_parse_endpoint);
	if (ret < 0)
		dev_err(&pdev->dev, "async fwnode register failed: %d\n", ret);

	return ret;
}

static int mipi_csis_dump_regs_show(struct seq_file *m, void *private)
{
	struct csi_state *state = m->private;

	return mipi_csis_dump_regs(state);
}
DEFINE_SHOW_ATTRIBUTE(mipi_csis_dump_regs);

static int mipi_csis_debugfs_init(struct csi_state *state)
{
	struct dentry *d;

	if (!debugfs_initialized())
		return -ENODEV;

	state->debugfs_root = debugfs_create_dir(dev_name(state->dev), NULL);
	if (!state->debugfs_root)
		return -ENOMEM;

	d = debugfs_create_bool("debug_enable", 0600, state->debugfs_root,
				&state->debug);
	if (!d)
		goto remove_debugfs;

	d = debugfs_create_file("dump_regs", 0600, state->debugfs_root,
				state, &mipi_csis_dump_regs_fops);
	if (!d)
		goto remove_debugfs;

	return 0;

remove_debugfs:
	debugfs_remove_recursive(state->debugfs_root);

	return -ENOMEM;
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

	memcpy(state->events, mipi_csis_events, sizeof(state->events));

	mipi_csis_debugfs_init(state);
	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mipi_csis_pm_resume(dev, true);
		if (ret < 0)
			goto unregister_all;
	}

	dev_info(&pdev->dev, "lanes: %d, hs_settle: %d, wclk: %d, freq: %u\n",
		 state->bus.num_data_lanes, state->hs_settle,
		 state->wrap_clk ? 1 : 0, state->clk_frequency);

	return 0;

unregister_all:
	mipi_csis_debugfs_exit(state);
	media_entity_cleanup(&state->mipi_sd.entity);
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
