// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Renesas R-Car MIPI CSI-2 Receiver
 *
 * Copyright (C) 2018 Renesas Electronics Corp.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/sys_soc.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-subdev.h>

struct rcar_csi2;

/* Register offsets and bits */

/* Control Timing Select */
#define TREF_REG			0x00
#define TREF_TREF			BIT(0)

/* Software Reset */
#define SRST_REG			0x04
#define SRST_SRST			BIT(0)

/* PHY Operation Control */
#define PHYCNT_REG			0x08
#define PHYCNT_SHUTDOWNZ		BIT(17)
#define PHYCNT_RSTZ			BIT(16)
#define PHYCNT_ENABLECLK		BIT(4)
#define PHYCNT_ENABLE_3			BIT(3)
#define PHYCNT_ENABLE_2			BIT(2)
#define PHYCNT_ENABLE_1			BIT(1)
#define PHYCNT_ENABLE_0			BIT(0)

/* Checksum Control */
#define CHKSUM_REG			0x0c
#define CHKSUM_ECC_EN			BIT(1)
#define CHKSUM_CRC_EN			BIT(0)

/*
 * Channel Data Type Select
 * VCDT[0-15]:  Channel 1 VCDT[16-31]:  Channel 2
 * VCDT2[0-15]: Channel 3 VCDT2[16-31]: Channel 4
 */
#define VCDT_REG			0x10
#define VCDT2_REG			0x14
#define VCDT_VCDTN_EN			BIT(15)
#define VCDT_SEL_VC(n)			(((n) & 0x3) << 8)
#define VCDT_SEL_DTN_ON			BIT(6)
#define VCDT_SEL_DT(n)			(((n) & 0x3f) << 0)

/* Frame Data Type Select */
#define FRDT_REG			0x18

/* Field Detection Control */
#define FLD_REG				0x1c
#define FLD_FLD_NUM(n)			(((n) & 0xff) << 16)
#define FLD_FLD_EN4			BIT(3)
#define FLD_FLD_EN3			BIT(2)
#define FLD_FLD_EN2			BIT(1)
#define FLD_FLD_EN			BIT(0)

/* Automatic Standby Control */
#define ASTBY_REG			0x20

/* Long Data Type Setting 0 */
#define LNGDT0_REG			0x28

/* Long Data Type Setting 1 */
#define LNGDT1_REG			0x2c

/* Interrupt Enable */
#define INTEN_REG			0x30

/* Interrupt Source Mask */
#define INTCLOSE_REG			0x34

/* Interrupt Status Monitor */
#define INTSTATE_REG			0x38
#define INTSTATE_INT_ULPS_START		BIT(7)
#define INTSTATE_INT_ULPS_END		BIT(6)

/* Interrupt Error Status Monitor */
#define INTERRSTATE_REG			0x3c

/* Short Packet Data */
#define SHPDAT_REG			0x40

/* Short Packet Count */
#define SHPCNT_REG			0x44

/* LINK Operation Control */
#define LINKCNT_REG			0x48
#define LINKCNT_MONITOR_EN		BIT(31)
#define LINKCNT_REG_MONI_PACT_EN	BIT(25)
#define LINKCNT_ICLK_NONSTOP		BIT(24)

/* Lane Swap */
#define LSWAP_REG			0x4c
#define LSWAP_L3SEL(n)			(((n) & 0x3) << 6)
#define LSWAP_L2SEL(n)			(((n) & 0x3) << 4)
#define LSWAP_L1SEL(n)			(((n) & 0x3) << 2)
#define LSWAP_L0SEL(n)			(((n) & 0x3) << 0)

/* PHY Test Interface Write Register */
#define PHTW_REG			0x50
#define PHTW_DWEN			BIT(24)
#define PHTW_TESTDIN_DATA(n)		(((n & 0xff)) << 16)
#define PHTW_CWEN			BIT(8)
#define PHTW_TESTDIN_CODE(n)		((n & 0xff))

struct phtw_value {
	u16 data;
	u16 code;
};

struct rcsi2_mbps_reg {
	u16 mbps;
	u16 reg;
};

static const struct rcsi2_mbps_reg phtw_mbps_h3_v3h_m3n[] = {
	{ .mbps =   80, .reg = 0x86 },
	{ .mbps =   90, .reg = 0x86 },
	{ .mbps =  100, .reg = 0x87 },
	{ .mbps =  110, .reg = 0x87 },
	{ .mbps =  120, .reg = 0x88 },
	{ .mbps =  130, .reg = 0x88 },
	{ .mbps =  140, .reg = 0x89 },
	{ .mbps =  150, .reg = 0x89 },
	{ .mbps =  160, .reg = 0x8a },
	{ .mbps =  170, .reg = 0x8a },
	{ .mbps =  180, .reg = 0x8b },
	{ .mbps =  190, .reg = 0x8b },
	{ .mbps =  205, .reg = 0x8c },
	{ .mbps =  220, .reg = 0x8d },
	{ .mbps =  235, .reg = 0x8e },
	{ .mbps =  250, .reg = 0x8e },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_reg phtw_mbps_v3m_e3[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x20 },
	{ .mbps =  100, .reg = 0x40 },
	{ .mbps =  110, .reg = 0x02 },
	{ .mbps =  130, .reg = 0x22 },
	{ .mbps =  140, .reg = 0x42 },
	{ .mbps =  150, .reg = 0x04 },
	{ .mbps =  170, .reg = 0x24 },
	{ .mbps =  180, .reg = 0x44 },
	{ .mbps =  200, .reg = 0x06 },
	{ .mbps =  220, .reg = 0x26 },
	{ .mbps =  240, .reg = 0x46 },
	{ .mbps =  250, .reg = 0x08 },
	{ .mbps =  270, .reg = 0x28 },
	{ .mbps =  300, .reg = 0x0a },
	{ .mbps =  330, .reg = 0x2a },
	{ .mbps =  360, .reg = 0x4a },
	{ .mbps =  400, .reg = 0x0c },
	{ .mbps =  450, .reg = 0x2c },
	{ .mbps =  500, .reg = 0x0e },
	{ .mbps =  550, .reg = 0x2e },
	{ .mbps =  600, .reg = 0x10 },
	{ .mbps =  650, .reg = 0x30 },
	{ .mbps =  700, .reg = 0x12 },
	{ .mbps =  750, .reg = 0x32 },
	{ .mbps =  800, .reg = 0x52 },
	{ .mbps =  850, .reg = 0x72 },
	{ .mbps =  900, .reg = 0x14 },
	{ .mbps =  950, .reg = 0x34 },
	{ .mbps = 1000, .reg = 0x54 },
	{ .mbps = 1050, .reg = 0x74 },
	{ .mbps = 1125, .reg = 0x16 },
	{ /* sentinel */ },
};

/* PHY Test Interface Clear */
#define PHTC_REG			0x58
#define PHTC_TESTCLR			BIT(0)

/* PHY Frequency Control */
#define PHYPLL_REG			0x68
#define PHYPLL_HSFREQRANGE(n)		((n) << 16)

static const struct rcsi2_mbps_reg hsfreqrange_h3_v3h_m3n[] = {
	{ .mbps =   80, .reg = 0x00 },
	{ .mbps =   90, .reg = 0x10 },
	{ .mbps =  100, .reg = 0x20 },
	{ .mbps =  110, .reg = 0x30 },
	{ .mbps =  120, .reg = 0x01 },
	{ .mbps =  130, .reg = 0x11 },
	{ .mbps =  140, .reg = 0x21 },
	{ .mbps =  150, .reg = 0x31 },
	{ .mbps =  160, .reg = 0x02 },
	{ .mbps =  170, .reg = 0x12 },
	{ .mbps =  180, .reg = 0x22 },
	{ .mbps =  190, .reg = 0x32 },
	{ .mbps =  205, .reg = 0x03 },
	{ .mbps =  220, .reg = 0x13 },
	{ .mbps =  235, .reg = 0x23 },
	{ .mbps =  250, .reg = 0x33 },
	{ .mbps =  275, .reg = 0x04 },
	{ .mbps =  300, .reg = 0x14 },
	{ .mbps =  325, .reg = 0x25 },
	{ .mbps =  350, .reg = 0x35 },
	{ .mbps =  400, .reg = 0x05 },
	{ .mbps =  450, .reg = 0x16 },
	{ .mbps =  500, .reg = 0x26 },
	{ .mbps =  550, .reg = 0x37 },
	{ .mbps =  600, .reg = 0x07 },
	{ .mbps =  650, .reg = 0x18 },
	{ .mbps =  700, .reg = 0x28 },
	{ .mbps =  750, .reg = 0x39 },
	{ .mbps =  800, .reg = 0x09 },
	{ .mbps =  850, .reg = 0x19 },
	{ .mbps =  900, .reg = 0x29 },
	{ .mbps =  950, .reg = 0x3a },
	{ .mbps = 1000, .reg = 0x0a },
	{ .mbps = 1050, .reg = 0x1a },
	{ .mbps = 1100, .reg = 0x2a },
	{ .mbps = 1150, .reg = 0x3b },
	{ .mbps = 1200, .reg = 0x0b },
	{ .mbps = 1250, .reg = 0x1b },
	{ .mbps = 1300, .reg = 0x2b },
	{ .mbps = 1350, .reg = 0x3c },
	{ .mbps = 1400, .reg = 0x0c },
	{ .mbps = 1450, .reg = 0x1c },
	{ .mbps = 1500, .reg = 0x2c },
	{ /* sentinel */ },
};

static const struct rcsi2_mbps_reg hsfreqrange_m3w_h3es1[] = {
	{ .mbps =   80,	.reg = 0x00 },
	{ .mbps =   90,	.reg = 0x10 },
	{ .mbps =  100,	.reg = 0x20 },
	{ .mbps =  110,	.reg = 0x30 },
	{ .mbps =  120,	.reg = 0x01 },
	{ .mbps =  130,	.reg = 0x11 },
	{ .mbps =  140,	.reg = 0x21 },
	{ .mbps =  150,	.reg = 0x31 },
	{ .mbps =  160,	.reg = 0x02 },
	{ .mbps =  170,	.reg = 0x12 },
	{ .mbps =  180,	.reg = 0x22 },
	{ .mbps =  190,	.reg = 0x32 },
	{ .mbps =  205,	.reg = 0x03 },
	{ .mbps =  220,	.reg = 0x13 },
	{ .mbps =  235,	.reg = 0x23 },
	{ .mbps =  250,	.reg = 0x33 },
	{ .mbps =  275,	.reg = 0x04 },
	{ .mbps =  300,	.reg = 0x14 },
	{ .mbps =  325,	.reg = 0x05 },
	{ .mbps =  350,	.reg = 0x15 },
	{ .mbps =  400,	.reg = 0x25 },
	{ .mbps =  450,	.reg = 0x06 },
	{ .mbps =  500,	.reg = 0x16 },
	{ .mbps =  550,	.reg = 0x07 },
	{ .mbps =  600,	.reg = 0x17 },
	{ .mbps =  650,	.reg = 0x08 },
	{ .mbps =  700,	.reg = 0x18 },
	{ .mbps =  750,	.reg = 0x09 },
	{ .mbps =  800,	.reg = 0x19 },
	{ .mbps =  850,	.reg = 0x29 },
	{ .mbps =  900,	.reg = 0x39 },
	{ .mbps =  950,	.reg = 0x0a },
	{ .mbps = 1000,	.reg = 0x1a },
	{ .mbps = 1050,	.reg = 0x2a },
	{ .mbps = 1100,	.reg = 0x3a },
	{ .mbps = 1150,	.reg = 0x0b },
	{ .mbps = 1200,	.reg = 0x1b },
	{ .mbps = 1250,	.reg = 0x2b },
	{ .mbps = 1300,	.reg = 0x3b },
	{ .mbps = 1350,	.reg = 0x0c },
	{ .mbps = 1400,	.reg = 0x1c },
	{ .mbps = 1450,	.reg = 0x2c },
	{ .mbps = 1500,	.reg = 0x3c },
	{ /* sentinel */ },
};

/* PHY ESC Error Monitor */
#define PHEERM_REG			0x74

/* PHY Clock Lane Monitor */
#define PHCLM_REG			0x78
#define PHCLM_STOPSTATECKL		BIT(0)

/* PHY Data Lane Monitor */
#define PHDLM_REG			0x7c

/* CSI0CLK Frequency Configuration Preset Register */
#define CSI0CLKFCPR_REG			0x260
#define CSI0CLKFREQRANGE(n)		((n & 0x3f) << 16)

struct rcar_csi2_format {
	u32 code;
	unsigned int datatype;
	unsigned int bpp;
};

static const struct rcar_csi2_format rcar_csi2_formats[] = {
	{ .code = MEDIA_BUS_FMT_RGB888_1X24,	.datatype = 0x24, .bpp = 24 },
	{ .code = MEDIA_BUS_FMT_UYVY8_1X16,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_YUYV8_1X16,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_UYVY8_2X8,	.datatype = 0x1e, .bpp = 16 },
	{ .code = MEDIA_BUS_FMT_YUYV10_2X10,	.datatype = 0x1e, .bpp = 20 },
};

static const struct rcar_csi2_format *rcsi2_code_to_fmt(unsigned int code)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_csi2_formats); i++)
		if (rcar_csi2_formats[i].code == code)
			return &rcar_csi2_formats[i];

	return NULL;
}

enum rcar_csi2_pads {
	RCAR_CSI2_SINK,
	RCAR_CSI2_SOURCE_VC0,
	RCAR_CSI2_SOURCE_VC1,
	RCAR_CSI2_SOURCE_VC2,
	RCAR_CSI2_SOURCE_VC3,
	NR_OF_RCAR_CSI2_PAD,
};

struct rcar_csi2_info {
	int (*init_phtw)(struct rcar_csi2 *priv, unsigned int mbps);
	int (*confirm_start)(struct rcar_csi2 *priv);
	const struct rcsi2_mbps_reg *hsfreqrange;
	unsigned int csi0clkfreqrange;
	bool clear_ulps;
};

struct rcar_csi2 {
	struct device *dev;
	void __iomem *base;
	const struct rcar_csi2_info *info;

	struct v4l2_subdev subdev;
	struct media_pad pads[NR_OF_RCAR_CSI2_PAD];

	struct v4l2_async_notifier notifier;
	struct v4l2_async_subdev asd;
	struct v4l2_subdev *remote;

	struct v4l2_mbus_framefmt mf;

	struct mutex lock;
	int stream_count;

	unsigned short lanes;
	unsigned char lane_swap[4];
};

static inline struct rcar_csi2 *sd_to_csi2(struct v4l2_subdev *sd)
{
	return container_of(sd, struct rcar_csi2, subdev);
}

static inline struct rcar_csi2 *notifier_to_csi2(struct v4l2_async_notifier *n)
{
	return container_of(n, struct rcar_csi2, notifier);
}

static u32 rcsi2_read(struct rcar_csi2 *priv, unsigned int reg)
{
	return ioread32(priv->base + reg);
}

static void rcsi2_write(struct rcar_csi2 *priv, unsigned int reg, u32 data)
{
	iowrite32(data, priv->base + reg);
}

static void rcsi2_reset(struct rcar_csi2 *priv)
{
	rcsi2_write(priv, SRST_REG, SRST_SRST);
	usleep_range(100, 150);
	rcsi2_write(priv, SRST_REG, 0);
}

static int rcsi2_wait_phy_start(struct rcar_csi2 *priv)
{
	unsigned int timeout;

	/* Wait for the clock and data lanes to enter LP-11 state. */
	for (timeout = 0; timeout <= 20; timeout++) {
		const u32 lane_mask = (1 << priv->lanes) - 1;

		if ((rcsi2_read(priv, PHCLM_REG) & PHCLM_STOPSTATECKL)  &&
		    (rcsi2_read(priv, PHDLM_REG) & lane_mask) == lane_mask)
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(priv->dev, "Timeout waiting for LP-11 state\n");

	return -ETIMEDOUT;
}

static int rcsi2_set_phypll(struct rcar_csi2 *priv, unsigned int mbps)
{
	const struct rcsi2_mbps_reg *hsfreq;

	for (hsfreq = priv->info->hsfreqrange; hsfreq->mbps != 0; hsfreq++)
		if (hsfreq->mbps >= mbps)
			break;

	if (!hsfreq->mbps) {
		dev_err(priv->dev, "Unsupported PHY speed (%u Mbps)", mbps);
		return -ERANGE;
	}

	rcsi2_write(priv, PHYPLL_REG, PHYPLL_HSFREQRANGE(hsfreq->reg));

	return 0;
}

static int rcsi2_calc_mbps(struct rcar_csi2 *priv, unsigned int bpp)
{
	struct v4l2_subdev *source;
	struct v4l2_ctrl *ctrl;
	u64 mbps;

	if (!priv->remote)
		return -ENODEV;

	source = priv->remote;

	/* Read the pixel rate control from remote. */
	ctrl = v4l2_ctrl_find(source->ctrl_handler, V4L2_CID_PIXEL_RATE);
	if (!ctrl) {
		dev_err(priv->dev, "no pixel rate control in subdev %s\n",
			source->name);
		return -EINVAL;
	}

	/*
	 * Calculate the phypll in mbps.
	 * link_freq = (pixel_rate * bits_per_sample) / (2 * nr_of_lanes)
	 * bps = link_freq * 2
	 */
	mbps = v4l2_ctrl_g_ctrl_int64(ctrl) * bpp;
	do_div(mbps, priv->lanes * 1000000);

	return mbps;
}

static int rcsi2_start(struct rcar_csi2 *priv)
{
	const struct rcar_csi2_format *format;
	u32 phycnt, vcdt = 0, vcdt2 = 0;
	unsigned int i;
	int mbps, ret;

	dev_dbg(priv->dev, "Input size (%ux%u%c)\n",
		priv->mf.width, priv->mf.height,
		priv->mf.field == V4L2_FIELD_NONE ? 'p' : 'i');

	/* Code is validated in set_fmt. */
	format = rcsi2_code_to_fmt(priv->mf.code);

	/*
	 * Enable all Virtual Channels.
	 *
	 * NOTE: It's not possible to get individual datatype for each
	 *       source virtual channel. Once this is possible in V4L2
	 *       it should be used here.
	 */
	for (i = 0; i < 4; i++) {
		u32 vcdt_part;

		vcdt_part = VCDT_SEL_VC(i) | VCDT_VCDTN_EN | VCDT_SEL_DTN_ON |
			VCDT_SEL_DT(format->datatype);

		/* Store in correct reg and offset. */
		if (i < 2)
			vcdt |= vcdt_part << ((i % 2) * 16);
		else
			vcdt2 |= vcdt_part << ((i % 2) * 16);
	}

	phycnt = PHYCNT_ENABLECLK;
	phycnt |= (1 << priv->lanes) - 1;

	mbps = rcsi2_calc_mbps(priv, format->bpp);
	if (mbps < 0)
		return mbps;

	/* Init */
	rcsi2_write(priv, TREF_REG, TREF_TREF);
	rcsi2_reset(priv);
	rcsi2_write(priv, PHTC_REG, 0);

	/* Configure */
	rcsi2_write(priv, FLD_REG, FLD_FLD_NUM(2) | FLD_FLD_EN4 |
		    FLD_FLD_EN3 | FLD_FLD_EN2 | FLD_FLD_EN);
	rcsi2_write(priv, VCDT_REG, vcdt);
	rcsi2_write(priv, VCDT2_REG, vcdt2);
	/* Lanes are zero indexed. */
	rcsi2_write(priv, LSWAP_REG,
		    LSWAP_L0SEL(priv->lane_swap[0] - 1) |
		    LSWAP_L1SEL(priv->lane_swap[1] - 1) |
		    LSWAP_L2SEL(priv->lane_swap[2] - 1) |
		    LSWAP_L3SEL(priv->lane_swap[3] - 1));

	/* Start */
	if (priv->info->init_phtw) {
		ret = priv->info->init_phtw(priv, mbps);
		if (ret)
			return ret;
	}

	if (priv->info->hsfreqrange) {
		ret = rcsi2_set_phypll(priv, mbps);
		if (ret)
			return ret;
	}

	if (priv->info->csi0clkfreqrange)
		rcsi2_write(priv, CSI0CLKFCPR_REG,
			    CSI0CLKFREQRANGE(priv->info->csi0clkfreqrange));

	rcsi2_write(priv, PHYCNT_REG, phycnt);
	rcsi2_write(priv, LINKCNT_REG, LINKCNT_MONITOR_EN |
		    LINKCNT_REG_MONI_PACT_EN | LINKCNT_ICLK_NONSTOP);
	rcsi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ);
	rcsi2_write(priv, PHYCNT_REG, phycnt | PHYCNT_SHUTDOWNZ | PHYCNT_RSTZ);

	ret = rcsi2_wait_phy_start(priv);
	if (ret)
		return ret;

	/* Confirm start */
	if (priv->info->confirm_start) {
		ret = priv->info->confirm_start(priv);
		if (ret)
			return ret;
	}

	/* Clear Ultra Low Power interrupt. */
	if (priv->info->clear_ulps)
		rcsi2_write(priv, INTSTATE_REG,
			    INTSTATE_INT_ULPS_START |
			    INTSTATE_INT_ULPS_END);
	return 0;
}

static void rcsi2_stop(struct rcar_csi2 *priv)
{
	rcsi2_write(priv, PHYCNT_REG, 0);

	rcsi2_reset(priv);

	rcsi2_write(priv, PHTC_REG, PHTC_TESTCLR);
}

static int rcsi2_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	struct v4l2_subdev *nextsd;
	int ret = 0;

	mutex_lock(&priv->lock);

	if (!priv->remote) {
		ret = -ENODEV;
		goto out;
	}

	nextsd = priv->remote;

	if (enable && priv->stream_count == 0) {
		pm_runtime_get_sync(priv->dev);

		ret = rcsi2_start(priv);
		if (ret) {
			pm_runtime_put(priv->dev);
			goto out;
		}

		ret = v4l2_subdev_call(nextsd, video, s_stream, 1);
		if (ret) {
			rcsi2_stop(priv);
			pm_runtime_put(priv->dev);
			goto out;
		}
	} else if (!enable && priv->stream_count == 1) {
		rcsi2_stop(priv);
		v4l2_subdev_call(nextsd, video, s_stream, 0);
		pm_runtime_put(priv->dev);
	}

	priv->stream_count += enable ? 1 : -1;
out:
	mutex_unlock(&priv->lock);

	return ret;
}

static int rcsi2_set_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);
	struct v4l2_mbus_framefmt *framefmt;

	if (!rcsi2_code_to_fmt(format->format.code))
		format->format.code = rcar_csi2_formats[0].code;

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		priv->mf = format->format;
	} else {
		framefmt = v4l2_subdev_get_try_format(sd, cfg, 0);
		*framefmt = format->format;
	}

	return 0;
}

static int rcsi2_get_pad_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_format *format)
{
	struct rcar_csi2 *priv = sd_to_csi2(sd);

	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		format->format = priv->mf;
	else
		format->format = *v4l2_subdev_get_try_format(sd, cfg, 0);

	return 0;
}

static const struct v4l2_subdev_video_ops rcar_csi2_video_ops = {
	.s_stream = rcsi2_s_stream,
};

static const struct v4l2_subdev_pad_ops rcar_csi2_pad_ops = {
	.set_fmt = rcsi2_set_pad_format,
	.get_fmt = rcsi2_get_pad_format,
};

static const struct v4l2_subdev_ops rcar_csi2_subdev_ops = {
	.video	= &rcar_csi2_video_ops,
	.pad	= &rcar_csi2_pad_ops,
};

/* -----------------------------------------------------------------------------
 * Async handling and registration of subdevices and links.
 */

static int rcsi2_notify_bound(struct v4l2_async_notifier *notifier,
			      struct v4l2_subdev *subdev,
			      struct v4l2_async_subdev *asd)
{
	struct rcar_csi2 *priv = notifier_to_csi2(notifier);
	int pad;

	pad = media_entity_get_fwnode_pad(&subdev->entity, asd->match.fwnode,
					  MEDIA_PAD_FL_SOURCE);
	if (pad < 0) {
		dev_err(priv->dev, "Failed to find pad for %s\n", subdev->name);
		return pad;
	}

	priv->remote = subdev;

	dev_dbg(priv->dev, "Bound %s pad: %d\n", subdev->name, pad);

	return media_create_pad_link(&subdev->entity, pad,
				     &priv->subdev.entity, 0,
				     MEDIA_LNK_FL_ENABLED |
				     MEDIA_LNK_FL_IMMUTABLE);
}

static void rcsi2_notify_unbind(struct v4l2_async_notifier *notifier,
				struct v4l2_subdev *subdev,
				struct v4l2_async_subdev *asd)
{
	struct rcar_csi2 *priv = notifier_to_csi2(notifier);

	priv->remote = NULL;

	dev_dbg(priv->dev, "Unbind %s\n", subdev->name);
}

static const struct v4l2_async_notifier_operations rcar_csi2_notify_ops = {
	.bound = rcsi2_notify_bound,
	.unbind = rcsi2_notify_unbind,
};

static int rcsi2_parse_v4l2(struct rcar_csi2 *priv,
			    struct v4l2_fwnode_endpoint *vep)
{
	unsigned int i;

	/* Only port 0 endpoint 0 is valid. */
	if (vep->base.port || vep->base.id)
		return -ENOTCONN;

	if (vep->bus_type != V4L2_MBUS_CSI2_DPHY) {
		dev_err(priv->dev, "Unsupported bus: %u\n", vep->bus_type);
		return -EINVAL;
	}

	priv->lanes = vep->bus.mipi_csi2.num_data_lanes;
	if (priv->lanes != 1 && priv->lanes != 2 && priv->lanes != 4) {
		dev_err(priv->dev, "Unsupported number of data-lanes: %u\n",
			priv->lanes);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(priv->lane_swap); i++) {
		priv->lane_swap[i] = i < priv->lanes ?
			vep->bus.mipi_csi2.data_lanes[i] : i;

		/* Check for valid lane number. */
		if (priv->lane_swap[i] < 1 || priv->lane_swap[i] > 4) {
			dev_err(priv->dev, "data-lanes must be in 1-4 range\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int rcsi2_parse_dt(struct rcar_csi2 *priv)
{
	struct device_node *ep;
	struct v4l2_fwnode_endpoint v4l2_ep = { .bus_type = 0 };
	int ret;

	ep = of_graph_get_endpoint_by_regs(priv->dev->of_node, 0, 0);
	if (!ep) {
		dev_err(priv->dev, "Not connected to subdevice\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &v4l2_ep);
	if (ret) {
		dev_err(priv->dev, "Could not parse v4l2 endpoint\n");
		of_node_put(ep);
		return -EINVAL;
	}

	ret = rcsi2_parse_v4l2(priv, &v4l2_ep);
	if (ret) {
		of_node_put(ep);
		return ret;
	}

	priv->asd.match.fwnode =
		fwnode_graph_get_remote_endpoint(of_fwnode_handle(ep));
	priv->asd.match_type = V4L2_ASYNC_MATCH_FWNODE;

	of_node_put(ep);

	v4l2_async_notifier_init(&priv->notifier);

	ret = v4l2_async_notifier_add_subdev(&priv->notifier, &priv->asd);
	if (ret) {
		fwnode_handle_put(priv->asd.match.fwnode);
		return ret;
	}

	priv->notifier.ops = &rcar_csi2_notify_ops;

	dev_dbg(priv->dev, "Found '%pOF'\n",
		to_of_node(priv->asd.match.fwnode));

	ret = v4l2_async_subdev_notifier_register(&priv->subdev,
						  &priv->notifier);
	if (ret)
		v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

/* -----------------------------------------------------------------------------
 * PHTW initialization sequences.
 *
 * NOTE: Magic values are from the datasheet and lack documentation.
 */

static int rcsi2_phtw_write(struct rcar_csi2 *priv, u16 data, u16 code)
{
	unsigned int timeout;

	rcsi2_write(priv, PHTW_REG,
		    PHTW_DWEN | PHTW_TESTDIN_DATA(data) |
		    PHTW_CWEN | PHTW_TESTDIN_CODE(code));

	/* Wait for DWEN and CWEN to be cleared by hardware. */
	for (timeout = 0; timeout <= 20; timeout++) {
		if (!(rcsi2_read(priv, PHTW_REG) & (PHTW_DWEN | PHTW_CWEN)))
			return 0;

		usleep_range(1000, 2000);
	}

	dev_err(priv->dev, "Timeout waiting for PHTW_DWEN and/or PHTW_CWEN\n");

	return -ETIMEDOUT;
}

static int rcsi2_phtw_write_array(struct rcar_csi2 *priv,
				  const struct phtw_value *values)
{
	const struct phtw_value *value;
	int ret;

	for (value = values; value->data || value->code; value++) {
		ret = rcsi2_phtw_write(priv, value->data, value->code);
		if (ret)
			return ret;
	}

	return 0;
}

static int rcsi2_phtw_write_mbps(struct rcar_csi2 *priv, unsigned int mbps,
				 const struct rcsi2_mbps_reg *values, u16 code)
{
	const struct rcsi2_mbps_reg *value;

	for (value = values; value->mbps; value++)
		if (value->mbps >= mbps)
			break;

	if (!value->mbps) {
		dev_err(priv->dev, "Unsupported PHY speed (%u Mbps)", mbps);
		return -ERANGE;
	}

	return rcsi2_phtw_write(priv, value->reg, code);
}

static int rcsi2_init_phtw_h3_v3h_m3n(struct rcar_csi2 *priv, unsigned int mbps)
{
	static const struct phtw_value step1[] = {
		{ .data = 0xcc, .code = 0xe2 },
		{ .data = 0x01, .code = 0xe3 },
		{ .data = 0x11, .code = 0xe4 },
		{ .data = 0x01, .code = 0xe5 },
		{ .data = 0x10, .code = 0x04 },
		{ /* sentinel */ },
	};

	static const struct phtw_value step2[] = {
		{ .data = 0x38, .code = 0x08 },
		{ .data = 0x01, .code = 0x00 },
		{ .data = 0x4b, .code = 0xac },
		{ .data = 0x03, .code = 0x00 },
		{ .data = 0x80, .code = 0x07 },
		{ /* sentinel */ },
	};

	int ret;

	ret = rcsi2_phtw_write_array(priv, step1);
	if (ret)
		return ret;

	if (mbps <= 250) {
		ret = rcsi2_phtw_write(priv, 0x39, 0x05);
		if (ret)
			return ret;

		ret = rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_h3_v3h_m3n,
					    0xf1);
		if (ret)
			return ret;
	}

	return rcsi2_phtw_write_array(priv, step2);
}

static int rcsi2_init_phtw_v3m_e3(struct rcar_csi2 *priv, unsigned int mbps)
{
	return rcsi2_phtw_write_mbps(priv, mbps, phtw_mbps_v3m_e3, 0x44);
}

static int rcsi2_confirm_start_v3m_e3(struct rcar_csi2 *priv)
{
	static const struct phtw_value step1[] = {
		{ .data = 0xed, .code = 0x34 },
		{ .data = 0xed, .code = 0x44 },
		{ .data = 0xed, .code = 0x54 },
		{ .data = 0xed, .code = 0x84 },
		{ .data = 0xed, .code = 0x94 },
		{ /* sentinel */ },
	};

	return rcsi2_phtw_write_array(priv, step1);
}

/* -----------------------------------------------------------------------------
 * Platform Device Driver.
 */

static const struct media_entity_operations rcar_csi2_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int rcsi2_probe_resources(struct rcar_csi2 *priv,
				 struct platform_device *pdev)
{
	struct resource *res;
	int irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	return 0;
}

static const struct rcar_csi2_info rcar_csi2_info_r8a7795 = {
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.clear_ulps = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7795es1 = {
	.hsfreqrange = hsfreqrange_m3w_h3es1,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a7796 = {
	.hsfreqrange = hsfreqrange_m3w_h3es1,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77965 = {
	.init_phtw = rcsi2_init_phtw_h3_v3h_m3n,
	.hsfreqrange = hsfreqrange_h3_v3h_m3n,
	.csi0clkfreqrange = 0x20,
	.clear_ulps = true,
};

static const struct rcar_csi2_info rcar_csi2_info_r8a77970 = {
	.init_phtw = rcsi2_init_phtw_v3m_e3,
	.confirm_start = rcsi2_confirm_start_v3m_e3,
};

static const struct of_device_id rcar_csi2_of_table[] = {
	{
		.compatible = "renesas,r8a7795-csi2",
		.data = &rcar_csi2_info_r8a7795,
	},
	{
		.compatible = "renesas,r8a7796-csi2",
		.data = &rcar_csi2_info_r8a7796,
	},
	{
		.compatible = "renesas,r8a77965-csi2",
		.data = &rcar_csi2_info_r8a77965,
	},
	{
		.compatible = "renesas,r8a77970-csi2",
		.data = &rcar_csi2_info_r8a77970,
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, rcar_csi2_of_table);

static const struct soc_device_attribute r8a7795es1[] = {
	{
		.soc_id = "r8a7795", .revision = "ES1.*",
		.data = &rcar_csi2_info_r8a7795es1,
	},
	{ /* sentinel */ },
};

static int rcsi2_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *attr;
	struct rcar_csi2 *priv;
	unsigned int i;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->info = of_device_get_match_data(&pdev->dev);

	/*
	 * r8a7795 ES1.x behaves differently than the ES2.0+ but doesn't
	 * have it's own compatible string.
	 */
	attr = soc_device_match(r8a7795es1);
	if (attr)
		priv->info = attr->data;

	priv->dev = &pdev->dev;

	mutex_init(&priv->lock);
	priv->stream_count = 0;

	ret = rcsi2_probe_resources(priv, pdev);
	if (ret) {
		dev_err(priv->dev, "Failed to get resources\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	ret = rcsi2_parse_dt(priv);
	if (ret)
		return ret;

	priv->subdev.owner = THIS_MODULE;
	priv->subdev.dev = &pdev->dev;
	v4l2_subdev_init(&priv->subdev, &rcar_csi2_subdev_ops);
	v4l2_set_subdevdata(&priv->subdev, &pdev->dev);
	snprintf(priv->subdev.name, V4L2_SUBDEV_NAME_SIZE, "%s %s",
		 KBUILD_MODNAME, dev_name(&pdev->dev));
	priv->subdev.flags = V4L2_SUBDEV_FL_HAS_DEVNODE;

	priv->subdev.entity.function = MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER;
	priv->subdev.entity.ops = &rcar_csi2_entity_ops;

	priv->pads[RCAR_CSI2_SINK].flags = MEDIA_PAD_FL_SINK;
	for (i = RCAR_CSI2_SOURCE_VC0; i < NR_OF_RCAR_CSI2_PAD; i++)
		priv->pads[i].flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&priv->subdev.entity, NR_OF_RCAR_CSI2_PAD,
				     priv->pads);
	if (ret)
		goto error;

	pm_runtime_enable(&pdev->dev);

	ret = v4l2_async_register_subdev(&priv->subdev);
	if (ret < 0)
		goto error;

	dev_info(priv->dev, "%d lanes found\n", priv->lanes);

	return 0;

error:
	v4l2_async_notifier_unregister(&priv->notifier);
	v4l2_async_notifier_cleanup(&priv->notifier);

	return ret;
}

static int rcsi2_remove(struct platform_device *pdev)
{
	struct rcar_csi2 *priv = platform_get_drvdata(pdev);

	v4l2_async_notifier_unregister(&priv->notifier);
	v4l2_async_notifier_cleanup(&priv->notifier);
	v4l2_async_unregister_subdev(&priv->subdev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static struct platform_driver rcar_csi2_pdrv = {
	.remove	= rcsi2_remove,
	.probe	= rcsi2_probe,
	.driver	= {
		.name	= "rcar-csi2",
		.of_match_table	= rcar_csi2_of_table,
	},
};

module_platform_driver(rcar_csi2_pdrv);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Renesas R-Car MIPI CSI-2 receiver driver");
MODULE_LICENSE("GPL");
