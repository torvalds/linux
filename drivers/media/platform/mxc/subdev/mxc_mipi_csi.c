/*
 * Freescale i.MX7 SoC series MIPI-CSI V3.3 receiver driver
 *
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
/*
 * Samsung S5P/EXYNOS SoC series MIPI-CSI receiver driver
 *
 * Copyright (C) 2011 - 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <media/v4l2-of.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-device.h>

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");

#define CSIS_DRIVER_NAME	"mxc_mipi-csi"
#define CSIS_SUBDEV_NAME	CSIS_DRIVER_NAME
#define CSIS_MAX_ENTITIES	2
#define CSIS0_MAX_LANES		4
#define CSIS1_MAX_LANES		2

#define MIPI_CSIS_DEF_PIX_WIDTH	640
#define MIPI_CSIS_DEF_PIX_HEIGHT	480

/* Register map definition */

/* CSIS version */
#define MIPI_CSIS_VERSION		0x00

/* CSIS common control */
#define MIPI_CSIS_CMN_CTRL			0x04
#define MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW	(1 << 16)
#define MIPI_CSIS_CMN_CTRL_INTER_MODE		(1 << 10)
#define MIPI_CSIS_CMN_CTRL_LANE_NR_OFFSET	8
#define MIPI_CSIS_CMN_CTRL_LANE_NR_MASK		(3 << 8)
#define MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW_CTRL	(1 << 2)
#define MIPI_CSIS_CMN_CTRL_RESET		(1 << 1)
#define MIPI_CSIS_CMN_CTRL_ENABLE		(1 << 0)

/* CSIS clock control */
#define MIPI_CSIS_CLK_CTRL		0x08
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH3(x)	(x << 28)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH2(x)	(x << 24)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH1(x)	(x << 20)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(x)	(x << 16)
#define MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK	(0xf << 4)
#define MIPI_CSIS_CLK_CTRL_WCLK_SRC		(1 << 0)

/* CSIS Interrupt mask */
#define MIPI_CSIS_INTMSK			0x10
#define MIPI_CSIS_INTMSK_EVEN_BEFORE	(1 << 31)
#define MIPI_CSIS_INTMSK_EVEN_AFTER	(1 << 30)
#define MIPI_CSIS_INTMSK_ODD_BEFORE	(1 << 29)
#define MIPI_CSIS_INTMSK_ODD_AFTER	(1 << 28)
#define MIPI_CSIS_INTMSK_FRAME_START	(1 << 24)
#define MIPI_CSIS_INTMSK_FRAME_END	(1 << 20)
#define MIPI_CSIS_INTMSK_ERR_SOT_HS	(1 << 16)
#define MIPI_CSIS_INTMSK_ERR_LOST_FS	(1 << 12)
#define MIPI_CSIS_INTMSK_ERR_LOST_FE	(1 << 8)
#define MIPI_CSIS_INTMSK_ERR_OVER		(1 << 4)
#define MIPI_CSIS_INTMSK_ERR_WRONG_CFG	(1 << 3)
#define MIPI_CSIS_INTMSK_ERR_ECC		(1 << 2)
#define MIPI_CSIS_INTMSK_ERR_CRC		(1 << 1)
#define MIPI_CSIS_INTMSK_ERR_UNKNOWN	(1 << 0)

/* CSIS Interrupt source */
#define MIPI_CSIS_INTSRC			0x14
#define MIPI_CSIS_INTSRC_EVEN_BEFORE	(1 << 31)
#define MIPI_CSIS_INTSRC_EVEN_AFTER	(1 << 30)
#define MIPI_CSIS_INTSRC_EVEN		(0x3 << 30)
#define MIPI_CSIS_INTSRC_ODD_BEFORE	(1 << 29)
#define MIPI_CSIS_INTSRC_ODD_AFTER	(1 << 28)
#define MIPI_CSIS_INTSRC_ODD			(0x3 << 28)
#define MIPI_CSIS_INTSRC_NON_IMAGE_DATA	(0xf << 28)
#define MIPI_CSIS_INTSRC_FRAME_START	(1 << 24)
#define MIPI_CSIS_INTSRC_FRAME_END	(1 << 20)
#define MIPI_CSIS_INTSRC_ERR_SOT_HS	(1 << 16)
#define MIPI_CSIS_INTSRC_ERR_LOST_FS	(1 << 12)
#define MIPI_CSIS_INTSRC_ERR_LOST_FE	(1 << 8)
#define MIPI_CSIS_INTSRC_ERR_OVER		(1 << 4)
#define MIPI_CSIS_INTSRC_ERR_WRONG_CFG	(1 << 3)
#define MIPI_CSIS_INTSRC_ERR_ECC		(1 << 2)
#define MIPI_CSIS_INTSRC_ERR_CRC		(1 << 1)
#define MIPI_CSIS_INTSRC_ERR_UNKNOWN	(1 << 0)
#define MIPI_CSIS_INTSRC_ERRORS		0xfffff

/* D-PHY status control */
#define MIPI_CSIS_DPHYSTATUS		0x20
#define MIPI_CSIS_DPHYSTATUS_ULPS_DAT			(1 << 8)
#define MIPI_CSIS_DPHYSTATUS_STOPSTATE_DAT		(1 << 4)
#define MIPI_CSIS_DPHYSTATUS_ULPS_CLK			(1 << 1)
#define MIPI_CSIS_DPHYSTATUS_STOPSTATE_CLK		(1 << 0)

/* D-PHY common control */
#define MIPI_CSIS_DPHYCTRL		0x24
#define MIPI_CSIS_DPHYCTRL_HSS_MASK			(0xff << 24)
#define MIPI_CSIS_DPHYCTRL_HSS_OFFSET		24
#define MIPI_CSIS_DPHYCTRL_SCLKS_MASK		(0x3 << 22)
#define MIPI_CSIS_DPHYCTRL_SCLKS_OFFSET		22
#define MIPI_CSIS_DPHYCTRL_DPDN_SWAP_CLK	(1 << 6)
#define MIPI_CSIS_DPHYCTRL_DPDN_SWAP_DAT	(1 << 5)
#define MIPI_CSIS_DPHYCTRL_ENABLE_DAT		(1 << 1)
#define MIPI_CSIS_DPHYCTRL_ENABLE_CLK		(1 << 0)
#define MIPI_CSIS_DPHYCTRL_ENABLE			(0x1f << 0)

/* D-PHY Master and Slave Control register Low */
#define MIPI_CSIS_DPHYBCTRL_L		0x30
/* D-PHY Master and Slave Control register High */
#define MIPI_CSIS_DPHYBCTRL_H		0x34
/* D-PHY Slave Control register Low */
#define MIPI_CSIS_DPHYSCTRL_L		0x38
/* D-PHY Slave Control register High */
#define MIPI_CSIS_DPHYSCTRL_H		0x3c


/* ISP Configuration register */
#define MIPI_CSIS_ISPCONFIG_CH0			0x40
#define MIPI_CSIS_ISPCONFIG_CH1			0x50
#define MIPI_CSIS_ISPCONFIG_CH2			0x60
#define MIPI_CSIS_ISPCONFIG_CH3			0x70

#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP_MSK    (0xff << 24)
#define MIPI_CSIS_ISPCFG_MEM_FULL_GAP(x)     (x << 24)
#define MIPI_CSIS_ISPCFG_DOUBLE_CMPNT        (1 << 12)
#define MIPI_CSIS_ISPCFG_ALIGN_32BIT         (1 << 11)
#define MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT   (0x1e << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW8		(0x2a << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW10		(0x2b << 2)
#define MIPI_CSIS_ISPCFG_FMT_RAW12		(0x2c << 2)
/* User defined formats, x = 1...4 */
#define MIPI_CSIS_ISPCFG_FMT_USER(x)	((0x30 + x - 1) << 2)
#define MIPI_CSIS_ISPCFG_FMT_MASK		(0x3f << 2)

/* ISP Image Resolution register */
#define MIPI_CSIS_ISPRESOL_CH0			0x44
#define MIPI_CSIS_ISPRESOL_CH1			0x54
#define MIPI_CSIS_ISPRESOL_CH2			0x64
#define MIPI_CSIS_ISPRESOL_CH3			0x74
#define CSIS_MAX_PIX_WIDTH		0xffff
#define CSIS_MAX_PIX_HEIGHT		0xffff

/* ISP SYNC register */
#define MIPI_CSIS_ISPSYNC_CH0			0x48
#define MIPI_CSIS_ISPSYNC_CH1			0x58
#define MIPI_CSIS_ISPSYNC_CH2			0x68
#define MIPI_CSIS_ISPSYNC_CH3			0x78

#define MIPI_CSIS_ISPSYNC_HSYNC_LINTV_OFFSET	18
#define MIPI_CSIS_ISPSYNC_VSYNC_SINTV_OFFSET 	12
#define MIPI_CSIS_ISPSYNC_VSYNC_EINTV_OFFSET	0

/* Non-image packet data buffers */
#define MIPI_CSIS_PKTDATA_ODD		0x2000
#define MIPI_CSIS_PKTDATA_EVEN		0x3000
#define MIPI_CSIS_PKTDATA_SIZE		SZ_4K

#define DEFAULT_SCLK_CSIS_FREQ	166000000UL

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

struct csis_pktbuf {
	u32 *data;
	unsigned int len;
};

struct csis_hw_reset {
	struct regmap *src;
	u8 req_src;
	u8 rst_bit;
};

/**
 * struct csi_state - the driver's internal state data structure
 * @lock: mutex serializing the subdev and power management operations,
 *        protecting @format and @flags members
 * @sd: v4l2_subdev associated with CSIS device instance
 * @index: the hardware instance index
 * @pdev: CSIS platform device
 * @phy: pointer to the CSIS generic PHY
 * @regs: mmaped I/O registers memory
 * @supplies: CSIS regulator supplies
 * @clock: CSIS clocks
 * @irq: requested s5p-mipi-csis irq number
 * @flags: the state variable for power and streaming control
 * @clock_frequency: device bus clock frequency
 * @hs_settle: HS-RX settle time
 * @clk_settle: Clk settle time
 * @num_lanes: number of MIPI-CSI data lanes used
 * @max_num_lanes: maximum number of MIPI-CSI data lanes supported
 * @wclk_ext: CSI wrapper clock: 0 - bus clock, 1 - external SCLK_CAM
 * @csis_fmt: current CSIS pixel format
 * @format: common media bus format for the source and sink pad
 * @slock: spinlock protecting structure members below
 * @pkt_buf: the frame embedded (non-image) data buffer
 * @events: MIPI-CSIS event (error) counters
 */
struct csi_state {
	struct mutex lock;
	struct device		*dev;
	struct v4l2_subdev mipi_sd;
	struct v4l2_subdev *sensor_sd;
	struct v4l2_device	v4l2_dev;

	u8 index;
	struct platform_device *pdev;
	struct phy *phy;
	void __iomem *regs;
	struct clk *mipi_clk;
	struct clk *phy_clk;
	int irq;
	u32 flags;

	u32 clk_frequency;
	u32 hs_settle;
	u32 clk_settle;
	u32 num_lanes;
	u32 max_num_lanes;
	u8 wclk_ext;

	const struct csis_pix_format *csis_fmt;
	struct v4l2_mbus_framefmt format;

	spinlock_t slock;
	struct csis_pktbuf pkt_buf;
	struct mipi_csis_event events[MIPI_CSIS_NUM_EVENTS];

	struct v4l2_async_subdev    asd;
	struct v4l2_async_notifier  subdev_notifier;
	struct v4l2_async_subdev    *async_subdevs[2];

	struct csis_hw_reset hw_reset;
	struct regulator     *mipi_phy_regulator;
};

/**
 * struct csis_pix_format - CSIS pixel format description
 * @pix_width_alignment: horizontal pixel alignment, width will be
 *                       multiple of 2^pix_width_alignment
 * @code: corresponding media bus code
 * @fmt_reg: MIPI_CSIS_CONFIG register value
 * @data_alignment: MIPI-CSI data alignment in bits
 */
struct csis_pix_format {
	unsigned int pix_width_alignment;
	u32 code;
	u32 fmt_reg;
	u8 data_alignment;
};

static const struct csis_pix_format mipi_csis_formats[] = {
	{
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_VYUY8_2X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_YCBCR422_8BIT,
		.data_alignment = 16,
	}, {
		.code = MEDIA_BUS_FMT_SBGGR8_1X8,
		.fmt_reg = MIPI_CSIS_ISPCFG_FMT_RAW8,
		.data_alignment = 8,
	}
};

#define mipi_csis_write(__csis, __r, __v) writel(__v, __csis->regs + __r)
#define mipi_csis_read(__csis, __r) readl(__csis->regs + __r)

static struct csi_state *mipi_sd_to_csi_state(struct v4l2_subdev *sdev)
{
	return container_of(sdev, struct csi_state, mipi_sd);
}

static inline struct csi_state
				*notifier_to_mipi_dev(struct v4l2_async_notifier *n)
{
	return container_of(n, struct csi_state, subdev_notifier);
}

static const struct csis_pix_format *find_csis_format(u32 code)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mipi_csis_formats); i++)
		if (code == mipi_csis_formats[i].code)
			return &mipi_csis_formats[i];
	return NULL;
}

static void mipi_csis_enable_interrupts(struct csi_state *state, bool on)
{
	u32 val = mipi_csis_read(state, MIPI_CSIS_INTMSK);
	if (on)
		val |= 0xf00fffff;
	else
		val &= ~0xf00fffff;
	mipi_csis_write(state, MIPI_CSIS_INTMSK, val);
}

static void mipi_csis_sw_reset(struct csi_state *state)
{
	u32 val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);

	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val | MIPI_CSIS_CMN_CTRL_RESET);
	udelay(10);
}

static int mipi_csis_phy_init(struct csi_state *state)
{
	int ret;

	state->mipi_phy_regulator = devm_regulator_get(state->dev,
				"mipi-phy");

	ret = regulator_set_voltage(state->mipi_phy_regulator,
			1000000, 1000000);

	return ret;
}

static int mipi_csis_phy_reset(struct csi_state *state)
{
	struct device_node *np = state->dev->of_node;
	struct device_node *node;
	phandle phandle;
	u32 out_val[3];
	int ret;

	ret = of_property_read_u32_array(np, "csis-phy-reset", out_val, 3);
	if (ret) {
		dev_dbg(state->dev, "no csis-hw-reset property found\n");
	} else {
		phandle = *out_val;

		node = of_find_node_by_phandle(phandle);
		if (!node) {
			dev_dbg(state->dev, "not find src node by phandle\n");
			ret = PTR_ERR(node);
		}
		state->hw_reset.src = syscon_node_to_regmap(node);
		if (IS_ERR(state->hw_reset.src)) {
			dev_err(state->dev, "failed to get src regmap\n");
			ret = PTR_ERR(state->hw_reset.src);
		}
		of_node_put(node);
		if (ret < 0)
			return ret;

		state->hw_reset.req_src = out_val[1];
		state->hw_reset.rst_bit = out_val[2];

		/* reset mipi phy */
		regmap_update_bits(state->hw_reset.src, state->hw_reset.req_src,
			1 << state->hw_reset.rst_bit, 1 << state->hw_reset.rst_bit);
		msleep(20);
		regmap_update_bits(state->hw_reset.src, state->hw_reset.req_src,
			1 << state->hw_reset.rst_bit, 0);

	}
	return ret;
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
		mask = (1 << (state->num_lanes + 1)) - 1;
		val |= (mask & MIPI_CSIS_DPHYCTRL_ENABLE);
	}
	mipi_csis_write(state, MIPI_CSIS_DPHYCTRL, val);
}

/* Called with the state.lock mutex held */
static void __mipi_csis_set_format(struct csi_state *state)
{
	struct v4l2_mbus_framefmt *mf = &state->format;
	u32 val;

	v4l2_dbg(1, debug, &state->mipi_sd, "fmt: %#x, %d x %d\n",
		 mf->code, mf->width, mf->height);

	/* Color format */
	val = mipi_csis_read(state, MIPI_CSIS_ISPCONFIG_CH0);
	val = (val & ~MIPI_CSIS_ISPCFG_FMT_MASK) | state->csis_fmt->fmt_reg;
	mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH0, val);

	/* Pixel resolution */
	val = mf->width | (mf->height << 16);
	mipi_csis_write(state, MIPI_CSIS_ISPRESOL_CH0, val);
}

static void mipi_csis_set_hsync_settle(struct csi_state *state,
								int hs_settle, int clk_settle)
{
	u32 val = mipi_csis_read(state, MIPI_CSIS_DPHYCTRL);

	val = (val & ~MIPI_CSIS_DPHYCTRL_HSS_MASK) |
				(hs_settle << 24) | (clk_settle << 22);

	mipi_csis_write(state, MIPI_CSIS_DPHYCTRL, val);
}

static void mipi_csis_set_params(struct csi_state *state)
{
	u32 val;

	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	val &= ~MIPI_CSIS_CMN_CTRL_LANE_NR_MASK;
	val |= (state->num_lanes - 1) << MIPI_CSIS_CMN_CTRL_LANE_NR_OFFSET;
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val);

	__mipi_csis_set_format(state);

	mipi_csis_set_hsync_settle(state, state->hs_settle, state->clk_settle);

	val = mipi_csis_read(state, MIPI_CSIS_ISPCONFIG_CH0);
	if (state->csis_fmt->data_alignment == 32)
		val |= MIPI_CSIS_ISPCFG_ALIGN_32BIT;
	else /* Normal output */
		val &= ~MIPI_CSIS_ISPCFG_ALIGN_32BIT;
	mipi_csis_write(state, MIPI_CSIS_ISPCONFIG_CH0, val);

	val = (0 << MIPI_CSIS_ISPSYNC_HSYNC_LINTV_OFFSET) |
		(0 << MIPI_CSIS_ISPSYNC_VSYNC_SINTV_OFFSET) |
		(0 << MIPI_CSIS_ISPSYNC_VSYNC_EINTV_OFFSET);
	mipi_csis_write(state, MIPI_CSIS_ISPSYNC_CH0, val);

	val = mipi_csis_read(state, MIPI_CSIS_CLK_CTRL);
	val &= ~MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	if (state->wclk_ext)
		val |= MIPI_CSIS_CLK_CTRL_WCLK_SRC;
	val |= MIPI_CSIS_CLK_CTRL_CLKGATE_TRAIL_CH0(15);
	val &= ~MIPI_CSIS_CLK_CTRL_CLKGATE_EN_MSK;
	mipi_csis_write(state, MIPI_CSIS_CLK_CTRL, val);

	mipi_csis_write(state, MIPI_CSIS_DPHYBCTRL_L, 0x1f4);
	mipi_csis_write(state, MIPI_CSIS_DPHYBCTRL_H, 0);

	/* Update the shadow register. */
	val = mipi_csis_read(state, MIPI_CSIS_CMN_CTRL);
	mipi_csis_write(state, MIPI_CSIS_CMN_CTRL, val | MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW |
					MIPI_CSIS_CMN_CTRL_UPDATE_SHADOW_CTRL);
}

static void mipi_csis_clk_enable(struct csi_state *state)
{
	clk_prepare_enable(state->mipi_clk);
	clk_prepare_enable(state->phy_clk);
}

static void mipi_csis_clk_disable(struct csi_state *state)
{
	clk_disable_unprepare(state->mipi_clk);
	clk_disable_unprepare(state->phy_clk);
}

static int mipi_csis_clk_get(struct csi_state *state)
{
	struct device *dev = &state->pdev->dev;
	int ret = true;

	state->mipi_clk = devm_clk_get(dev, "mipi_clk");
	if (IS_ERR(state->mipi_clk)) {
		dev_err(dev, "Could not get mipi csi clock\n");
		return -ENODEV;
	}

	state->phy_clk = devm_clk_get(dev, "phy_clk");
	if (IS_ERR(state->phy_clk)) {
		dev_err(dev, "Could not get mipi phy clock\n");
		return -ENODEV;
	}

	/* Set clock rate */
	if (state->clk_frequency)
		ret = clk_set_rate(state->mipi_clk,
				   state->clk_frequency);
	else
		dev_WARN(dev, "No clock frequency specified!\n");
	if (ret < 0) {
		dev_err(dev, "set rate filed, rate=%d\n", state->clk_frequency);
		return -EINVAL;
	}

	return ret;
}

static void dump_regs(struct csi_state *state, const char *label)
{
	struct {
		u32 offset;
		const char * const name;
	} registers[] = {
		{ 0x00, "CTRL" },
		{ 0x04, "DPHYCTRL" },
		{ 0x08, "CONFIG" },
		{ 0x0c, "DPHYSTS" },
		{ 0x10, "INTMSK" },
		{ 0x2c, "RESOL" },
		{ 0x38, "SDW_CONFIG" },
	};
	u32 i;

	v4l2_info(&state->mipi_sd, "--- %s ---\n", label);

	for (i = 0; i < ARRAY_SIZE(registers); i++) {
		u32 cfg = mipi_csis_read(state, registers[i].offset);
		v4l2_info(&state->mipi_sd, "%10s: 0x%08x\n", registers[i].name, cfg);
	}
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
	int i;

	spin_lock_irqsave(&state->slock, flags);
	for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++)
		state->events[i].counter = 0;
	spin_unlock_irqrestore(&state->slock, flags);
}

static void mipi_csis_log_counters(struct csi_state *state, bool non_errors)
{
	int i = non_errors ? MIPI_CSIS_NUM_EVENTS : MIPI_CSIS_NUM_EVENTS - 4;
	unsigned long flags;

	spin_lock_irqsave(&state->slock, flags);

	for (i--; i >= 0; i--) {
		if (state->events[i].counter > 0 || debug)
			v4l2_info(&state->mipi_sd, "%s events: %d\n",
				  state->events[i].name,
				  state->events[i].counter);
	}
	spin_unlock_irqrestore(&state->slock, flags);
}

/*
 * V4L2 subdev operations
 */
static int mipi_csis_s_power(struct v4l2_subdev *mipi_sd, int on)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct device *dev = &state->pdev->dev;

	v4l2_subdev_call(state->sensor_sd, core, s_power, on);

	if (on)
		return pm_runtime_get_sync(dev);

	return pm_runtime_put_sync(dev);
}

static int mipi_csis_s_stream(struct v4l2_subdev *mipi_sd, int enable)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	int ret = 0;

	v4l2_dbg(1, debug, mipi_sd, "%s: %d, state: 0x%x\n",
		 __func__, enable, state->flags);

	if (enable) {
		mipi_csis_clear_counters(state);
		ret = pm_runtime_get_sync(&state->pdev->dev);
		if (ret && ret != 1)
			return ret;
	}

	mutex_lock(&state->lock);
	if (enable) {
		if (state->flags & ST_SUSPENDED) {
			ret = -EBUSY;
			goto unlock;
		}
		mipi_csis_start_stream(state);
		v4l2_subdev_call(state->sensor_sd, video, s_stream, true);
		state->flags |= ST_STREAMING;
	} else {
		v4l2_subdev_call(state->sensor_sd, video, s_stream, false);
		mipi_csis_stop_stream(state);
		state->flags &= ~ST_STREAMING;
		if (debug > 0)
			mipi_csis_log_counters(state, true);
	}
unlock:
	mutex_unlock(&state->lock);
	if (!enable)
		pm_runtime_put(&state->pdev->dev);

	return ret == 1 ? 0 : ret;
}

static int mipi_csis_enum_mbus_fmt(struct v4l2_subdev *mipi_sd, unsigned int index,
				u32 *code)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;
	struct csis_pix_format const *csis_fmt;
	int ret;

	ret = v4l2_subdev_call(sensor_sd, video, enum_mbus_fmt, index, code);
	if (ret < 0)
		return -EINVAL;

	csis_fmt = find_csis_format(*code);
	if (csis_fmt == NULL) {
		dev_err(state->dev, "format not match\n");
		return -EINVAL;
	}

	return ret;
}

static struct csis_pix_format const *mipi_csis_try_format(
	struct v4l2_subdev *mipi_sd, struct v4l2_mbus_framefmt *mf)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;
	struct csis_pix_format const *csis_fmt;

	csis_fmt = find_csis_format(mf->code);
	if (csis_fmt == NULL)
		csis_fmt = &mipi_csis_formats[0];

	v4l2_subdev_call(sensor_sd, video, s_mbus_fmt, mf);

	mf->code = csis_fmt->code;
	v4l_bound_align_image(&mf->width, 1, CSIS_MAX_PIX_WIDTH,
			      csis_fmt->pix_width_alignment,
			      &mf->height, 1, CSIS_MAX_PIX_HEIGHT, 1,
			      0);

	state->format.code = mf->code;
	state->format.width = mf->width;
	state->format.height = mf->height;

	return csis_fmt;
}

static int mipi_csis_set_fmt(struct v4l2_subdev *mipi_sd,
		struct v4l2_mbus_framefmt *fmt)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct csis_pix_format const *csis_fmt;

	csis_fmt = mipi_csis_try_format(mipi_sd, fmt);
	if (csis_fmt) {
		mutex_lock(&state->lock);
		state->csis_fmt = csis_fmt;
		mutex_unlock(&state->lock);
	}
	return 0;
}

static int mipi_csis_get_fmt(struct v4l2_subdev *mipi_sd,
		struct v4l2_mbus_framefmt *fmt)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;

	v4l2_subdev_call(sensor_sd, video, g_mbus_fmt, fmt);
	if (!fmt)
		return -EINVAL;

	return 0;
}

static int mipi_csis_s_rx_buffer(struct v4l2_subdev *mipi_sd, void *buf,
			       unsigned int *size)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	unsigned long flags;

	*size = min_t(unsigned int, *size, MIPI_CSIS_PKTDATA_SIZE);

	spin_lock_irqsave(&state->slock, flags);
	state->pkt_buf.data = buf;
	state->pkt_buf.len = *size;
	spin_unlock_irqrestore(&state->slock, flags);

	return 0;
}

static int mipi_csis_try_fmt(struct v4l2_subdev *mipi_sd,
			  struct v4l2_mbus_framefmt *mf)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;

	return v4l2_subdev_call(sensor_sd, video, try_mbus_fmt, mf);
}

static int mipi_csis_s_parm(struct v4l2_subdev *mipi_sd, struct v4l2_streamparm *a)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;

	return v4l2_subdev_call(sensor_sd, video, s_parm, a);
}

static int mipi_csis_g_parm(struct v4l2_subdev *mipi_sd, struct v4l2_streamparm *a)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;

	return v4l2_subdev_call(sensor_sd, video, g_parm, a);
}

static int mipi_csis_enum_framesizes(struct v4l2_subdev *mipi_sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_size_enum *fse)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;

	return v4l2_subdev_call(sensor_sd, pad, enum_frame_size, NULL, fse);
}

static int mipi_csis_enum_frameintervals(struct v4l2_subdev *mipi_sd,
		struct v4l2_subdev_pad_config *cfg,
		struct v4l2_subdev_frame_interval_enum *fie)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);
	struct v4l2_subdev *sensor_sd = state->sensor_sd;

	return v4l2_subdev_call(sensor_sd, pad, enum_frame_interval, NULL, fie);
}

static int mipi_csis_log_status(struct v4l2_subdev *mipi_sd)
{
	struct csi_state *state = mipi_sd_to_csi_state(mipi_sd);

	mutex_lock(&state->lock);
	mipi_csis_log_counters(state, true);
	if (debug && (state->flags & ST_POWERED))
		dump_regs(state, __func__);
	mutex_unlock(&state->lock);
	return 0;
}

static struct v4l2_subdev_core_ops mipi_csis_core_ops = {
	.s_power = mipi_csis_s_power,
	.log_status = mipi_csis_log_status,
};

static struct v4l2_subdev_video_ops mipi_csis_video_ops = {
	.s_rx_buffer = mipi_csis_s_rx_buffer,
	.s_stream = mipi_csis_s_stream,

	.enum_mbus_fmt = mipi_csis_enum_mbus_fmt,
	.try_mbus_fmt	= mipi_csis_try_fmt,
	.g_mbus_fmt = mipi_csis_get_fmt,
	.s_mbus_fmt = mipi_csis_set_fmt,

	.s_parm = mipi_csis_s_parm,
	.g_parm = mipi_csis_g_parm,
};

static const struct v4l2_subdev_pad_ops mipi_csis_pad_ops = {
	.enum_frame_size       = mipi_csis_enum_framesizes,
	.enum_frame_interval   = mipi_csis_enum_frameintervals,
};

static struct v4l2_subdev_ops mipi_csis_subdev_ops = {
	.core = &mipi_csis_core_ops,
	.video = &mipi_csis_video_ops,
	.pad = &mipi_csis_pad_ops,
};

static irqreturn_t mipi_csis_irq_handler(int irq, void *dev_id)
{
	struct csi_state *state = dev_id;
	struct csis_pktbuf *pktbuf = &state->pkt_buf;
	unsigned long flags;
	u32 status;

	status = mipi_csis_read(state, MIPI_CSIS_INTSRC);

	spin_lock_irqsave(&state->slock, flags);

	if ((status & MIPI_CSIS_INTSRC_NON_IMAGE_DATA) && pktbuf->data) {
		u32 offset;

		if (status & MIPI_CSIS_INTSRC_EVEN)
			offset = MIPI_CSIS_PKTDATA_EVEN;
		else
			offset = MIPI_CSIS_PKTDATA_ODD;

		memcpy(pktbuf->data, state->regs + offset, pktbuf->len);
		pktbuf->data = NULL;
		rmb();
	}

	/* Update the event/error counters */
	if ((status & MIPI_CSIS_INTSRC_ERRORS) || debug) {
		int i;
		for (i = 0; i < MIPI_CSIS_NUM_EVENTS; i++) {
			if (!(status & state->events[i].mask))
				continue;
			state->events[i].counter++;
			v4l2_dbg(2, debug, &state->mipi_sd, "%s: %d\n",
				 state->events[i].name,
				 state->events[i].counter);
		}
		v4l2_dbg(2, debug, &state->mipi_sd, "status: %08x\n", status);
	}
	spin_unlock_irqrestore(&state->slock, flags);

	mipi_csis_write(state, MIPI_CSIS_INTSRC, status);
	return IRQ_HANDLED;
}

static int subdev_notifier_bound(struct v4l2_async_notifier *notifier,
			    struct v4l2_subdev *subdev,
			    struct v4l2_async_subdev *asd)
{
	struct csi_state *state = notifier_to_mipi_dev(notifier);

	/* Find platform data for this sensor subdev */
	if (state->asd.match.of.node == subdev->dev->of_node)
		state->sensor_sd = subdev;

	if (subdev == NULL)
		return -EINVAL;

	v4l2_info(&state->v4l2_dev, "Registered sensor subdevice: %s\n",
		  subdev->name);

	return 0;
}

static int mipi_csis_parse_dt(struct platform_device *pdev,
			    struct csi_state *state)
{
	struct device_node *node = pdev->dev.of_node;

	if (of_property_read_u32(node, "clock-frequency",
				 &state->clk_frequency))
		state->clk_frequency = DEFAULT_SCLK_CSIS_FREQ;
	if (of_property_read_u32(node, "bus-width",
				 &state->max_num_lanes))
		return -EINVAL;

	node = of_graph_get_next_endpoint(node, NULL);
	if (!node) {
		dev_err(&pdev->dev, "No port node at %s\n",
				pdev->dev.of_node->full_name);
		return -EINVAL;
	}

	/* Get MIPI CSI-2 bus configration from the endpoint node. */
	of_property_read_u32(node, "csis-hs-settle",
					&state->hs_settle);

	of_property_read_u32(node, "csis-clk-settle",
					&state->clk_settle);
	state->wclk_ext = of_property_read_bool(node,
					"csis-wclk");

	of_property_read_u32(node, "data-lanes",
					&state->num_lanes);
	of_node_put(node);

	return 0;
}

static int mipi_csis_pm_resume(struct device *dev, bool runtime);
static const struct of_device_id mipi_csis_of_match[];

/* register parent dev */
static int mipi_csis_subdev_host(struct csi_state *state)
{
	struct device_node *parent = state->dev->of_node;
	struct device_node *node, *port, *rem;
	int ret;

	/* Attach sensors linked to csi receivers */
	for_each_available_child_of_node(parent, node) {
		if (of_node_cmp(node->name, "port"))
			continue;

		/* The csi node can have only port subnode. */
		port = of_get_next_child(node, NULL);
		if (!port)
			continue;
		rem = of_graph_get_remote_port_parent(port);
		of_node_put(port);
		if (rem == NULL) {
			v4l2_info(&state->v4l2_dev,
						"Remote device at %s not found\n",
						port->full_name);
			return -1;
		}

		state->asd.match_type = V4L2_ASYNC_MATCH_OF;
		state->asd.match.of.node = rem;
		state->async_subdevs[0] = &state->asd;

		of_node_put(rem);
		break;
	}

	state->subdev_notifier.subdevs = state->async_subdevs;
	state->subdev_notifier.num_subdevs = 1;
	state->subdev_notifier.bound = subdev_notifier_bound;

	ret = v4l2_async_notifier_register(&state->v4l2_dev,
					&state->subdev_notifier);
	if (ret)
		dev_err(state->dev,
					"Error register async notifier regoster\n");

	return ret;
}

/* init subdev */
static int mipi_csis_subdev_init(struct v4l2_subdev *mipi_sd,
		struct platform_device *pdev,
		const struct v4l2_subdev_ops *ops)
{
	struct csi_state *state = platform_get_drvdata(pdev);
	int ret = 0;

	v4l2_subdev_init(mipi_sd, ops);
	mipi_sd->owner = THIS_MODULE;
	snprintf(mipi_sd->name, sizeof(mipi_sd->name), "%s.%d",
		 CSIS_SUBDEV_NAME, state->index);
	mipi_sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	mipi_sd->dev = &pdev->dev;

	state->csis_fmt = &mipi_csis_formats[0];
	state->format.code = mipi_csis_formats[0].code;
	state->format.width = MIPI_CSIS_DEF_PIX_WIDTH;
	state->format.height = MIPI_CSIS_DEF_PIX_HEIGHT;

	/* This allows to retrieve the platform device id by the host driver */
	v4l2_set_subdevdata(mipi_sd, pdev);

	ret = v4l2_async_register_subdev(mipi_sd);
	if (ret < 0)
		dev_err(&pdev->dev, "%s--Async register faialed, ret=%d\n", __func__, ret);

	return ret;
}

static int mipi_csis_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct v4l2_subdev *mipi_sd;
	struct resource *mem_res;
	struct csi_state *state;
	int ret = -ENOMEM;

	state = devm_kzalloc(dev, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;

	mutex_init(&state->lock);
	spin_lock_init(&state->slock);

	state->pdev = pdev;
	mipi_sd = &state->mipi_sd;
	state->dev = dev;

	ret = mipi_csis_parse_dt(pdev, state);
	if (ret < 0)
		return ret;

	if (state->num_lanes == 0 || state->num_lanes > state->max_num_lanes) {
		dev_err(dev, "Unsupported number of data lanes: %d (max. %d)\n",
			state->num_lanes, state->max_num_lanes);
		return -EINVAL;
	}

	mipi_csis_phy_init(state);
	mipi_csis_phy_reset(state);

	mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	state->regs = devm_ioremap_resource(dev, mem_res);
	if (IS_ERR(state->regs))
		return PTR_ERR(state->regs);

	state->irq = platform_get_irq(pdev, 0);
	if (state->irq < 0) {
		dev_err(dev, "Failed to get irq\n");
		return state->irq;
	}

	ret = mipi_csis_clk_get(state);
	if (ret < 0)
		return ret;

	mipi_csis_clk_enable(state);

	ret = devm_request_irq(dev, state->irq, mipi_csis_irq_handler,
			       0, dev_name(dev), state);
	if (ret) {
		dev_err(dev, "Interrupt request failed\n");
		goto e_clkdis;
	}

	/* First register a v4l2 device */
	ret = v4l2_device_register(dev, &state->v4l2_dev);
	if (ret) {
		v4l2_err(dev->driver,
			"Unable to register v4l2 device.\n");
		goto e_clkdis;
	}
	v4l2_info(&state->v4l2_dev, "mipi csi v4l2 device registered\n");

	/* .. and a pointer to the subdev. */
	platform_set_drvdata(pdev, state);

	ret = mipi_csis_subdev_init(&state->mipi_sd, pdev, &mipi_csis_subdev_ops);
	if (ret < 0)
		goto e_sd_mipi;

	memcpy(state->events, mipi_csis_events, sizeof(state->events));

	/* subdev host register */
	ret = mipi_csis_subdev_host(state);
	if (ret < 0)
		goto e_sd_host;

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mipi_csis_pm_resume(dev, true);
		if (ret < 0)
			goto e_sd_host;
	}

	dev_info(&pdev->dev,
			"lanes: %d, hs_settle: %d, clk_settle: %d, wclk: %d, freq: %u\n",
		 state->num_lanes, state->hs_settle, state->clk_settle,
		 state->wclk_ext, state->clk_frequency);
	return 0;

e_sd_host:
	v4l2_async_notifier_unregister(&state->subdev_notifier);
	v4l2_device_unregister(&state->v4l2_dev);
e_sd_mipi:
	v4l2_async_unregister_subdev(&state->mipi_sd);
e_clkdis:
	mipi_csis_clk_disable(state);
	return ret;
}

static int mipi_csis_pm_suspend(struct device *dev, bool runtime)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct csi_state *state = platform_get_drvdata(pdev);
	struct v4l2_subdev *mipi_sd = &state->mipi_sd;
	int ret = 0;

	v4l2_dbg(1, debug, mipi_sd, "%s: flags: 0x%x\n",
		 __func__, state->flags);

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
	struct platform_device *pdev = to_platform_device(dev);
	struct csi_state *state = platform_get_drvdata(pdev);
	struct v4l2_subdev *mipi_sd = &state->mipi_sd;
	int ret = 0;

	v4l2_dbg(1, debug, mipi_sd, "%s: flags: 0x%x\n",
		 __func__, state->flags);

	mutex_lock(&state->lock);
	if (!runtime && !(state->flags & ST_SUSPENDED))
		goto unlock;

	if (!(state->flags & ST_POWERED)) {
		ret = regulator_enable(state->mipi_phy_regulator);
		if (!ret) {
			state->flags |= ST_POWERED;
		} else {
			goto unlock;
		}
		mipi_csis_clk_enable(state);
	}
	if (state->flags & ST_STREAMING)
		mipi_csis_start_stream(state);

	state->flags &= ~ST_SUSPENDED;
 unlock:
	mutex_unlock(&state->lock);
	return ret ? -EAGAIN : 0;
}

#ifdef CONFIG_PM_SLEEP
static int mipi_csis_suspend(struct device *dev)
{
	return mipi_csis_pm_suspend(dev, false);
}

static int mipi_csis_resume(struct device *dev)
{
	return mipi_csis_pm_resume(dev, false);
}
#endif

static int mipi_csis_runtime_suspend(struct device *dev)
{
	return mipi_csis_pm_suspend(dev, true);
}

static int mipi_csis_runtime_resume(struct device *dev)
{
	return mipi_csis_pm_resume(dev, true);
}

static int mipi_csis_remove(struct platform_device *pdev)
{
	struct csi_state *state = platform_get_drvdata(pdev);

	v4l2_async_unregister_subdev(&state->mipi_sd);
	v4l2_async_notifier_unregister(&state->subdev_notifier);
	v4l2_device_unregister(&state->v4l2_dev);

	pm_runtime_disable(&pdev->dev);
	mipi_csis_pm_suspend(&pdev->dev, true);
	mipi_csis_clk_disable(state);
	pm_runtime_set_suspended(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops mipi_csis_pm_ops = {
	SET_RUNTIME_PM_OPS(mipi_csis_runtime_suspend, mipi_csis_runtime_resume,
			   NULL)
	SET_SYSTEM_SLEEP_PM_OPS(mipi_csis_suspend, mipi_csis_resume)
};

static const struct of_device_id mipi_csis_of_match[] = {
	{	.compatible = "fsl,imx7d-mipi-csi",},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mipi_csis_of_match);

static struct platform_driver mipi_csis_driver = {
	.probe		= mipi_csis_probe,
	.remove		= mipi_csis_remove,
	.driver		= {
		.of_match_table = mipi_csis_of_match,
		.name		= CSIS_DRIVER_NAME,
		.owner		= THIS_MODULE,
		.pm		= &mipi_csis_pm_ops,
	},
};

module_platform_driver(mipi_csis_driver);

MODULE_DESCRIPTION("Freescale MIPI-CSI2 receiver driver");
MODULE_LICENSE("GPL");
