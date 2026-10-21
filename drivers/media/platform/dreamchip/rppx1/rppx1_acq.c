// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define ACQ_VERSION_REG				0x0000

#define ACQ_CTRL_REG				0x0004
#define ACQ_CTRL_ALTERNATIVE_CFG_MODE_ENABLE	BIT(8)
#define ACQ_CTRL_RPP_MODE_MASK			GENMASK(3, 1)
#define ACQ_CTRL_RPP_MODE_RAWBT601		(0 << 1)
#define ACQ_CTRL_RPP_MODE_BT656			(1 << 1)
#define ACQ_CTRL_RPP_MODE_BT601			(2 << 1)
#define ACQ_CTRL_RPP_MODE_BAYER			(3 << 1)
#define ACQ_CTRL_RPP_MODE_DATA			(4 << 1)
#define ACQ_CTRL_RPP_MODE_BAYERRGB		(5 << 1)
#define ACQ_CTRL_RPP_MODE_RAWBT656		(6 << 1)
#define ACQ_CTRL_INFORM_EN_ENABLE		BIT(0)

#define ACQ_PROP_REG				0x0008

#define ACQ_PROP_SENSOR_IN_LSB_ALIGNED_IN_LSB	BIT(30)
#define ACQ_PROP_YUV_OUT_SEL			BIT(25)
#define ACQ_PROP_MUX_DMA_SEL			BIT(24)
#define ACQ_PROP_SECOND_INPUT_TYPE		BIT(18)
#define ACQ_PROP_LATENCY_FIFO_INPUT_SELECTION	BIT(15)
#define ACQ_PROP_INPUT_SELECTION_MASK		GENMASK(14, 12)
#define ACQ_PROP_INPUT_SELECTION_8BIT		(0 << 12)
#define ACQ_PROP_INPUT_SELECTION_10BIT		(1 << 12)
#define ACQ_PROP_INPUT_SELECTION_12BIT		(2 << 12)
#define ACQ_PROP_BAYER_PAT_MASK			GENMASK(4, 3)
#define ACQ_PROP_BAYER_PAT_RGRG			(0 << 3)
#define ACQ_PROP_BAYER_PAT_GRGR			(1 << 3)
#define ACQ_PROP_BAYER_PAT_GBGB			(2 << 3)
#define ACQ_PROP_BAYER_PAT_BGBG			(3 << 3)
#define ACQ_PROP_VSYNC_POL			BIT(2)
#define ACQ_PROP_HSYNC_POL			BIT(1)
#define ACQ_PROP_SAMPLE_EDGE			BIT(0)

#define ACQ_H_OFFS_REG				0x000c
#define ACQ_V_OFFS_REG				0x0010
#define ACQ_H_SIZE_REG				0x0014
#define ACQ_V_SIZE_REG				0x0018
#define ACQ_OUT_H_OFFS_REG			0x001c
#define ACQ_OUT_V_OFFS_REG			0x0020
#define ACQ_OUT_H_SIZE_REG			0x0024
#define ACQ_OUT_V_SIZE_REG			0x0028
#define FLAGS_SHD_REG				0x002c
#define ACQ_OUT_H_OFFS_SHD_REG			0x0030
#define ACQ_OUT_V_OFFS_SHD_REG			0x0034
#define ACQ_OUT_H_SIZE_SHD_REG			0x0038
#define ACQ_OUT_V_SIZE_SHD_REG			0x003c

static int rppx1_acq_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, ACQ_VERSION_REG) != 0x0b)
		return -EINVAL;

	return 0;
}

static int rppx1_acq_start(struct rpp_module *mod,
			   const struct v4l2_mbus_framefmt *fmt)
{
	u32 bayerpat, selection;

	rpp_module_clrset(mod, ACQ_CTRL_REG, ACQ_CTRL_RPP_MODE_MASK,
			  ACQ_CTRL_RPP_MODE_BAYER);

	rpp_module_write(mod, ACQ_H_OFFS_REG, 0);
	rpp_module_write(mod, ACQ_V_OFFS_REG, 0);
	rpp_module_write(mod, ACQ_H_SIZE_REG, fmt->width);
	rpp_module_write(mod, ACQ_V_SIZE_REG, fmt->height);
	rpp_module_write(mod, ACQ_OUT_H_OFFS_REG, 0);
	rpp_module_write(mod, ACQ_OUT_V_OFFS_REG, 0);
	rpp_module_write(mod, ACQ_OUT_H_SIZE_REG, fmt->width);
	rpp_module_write(mod, ACQ_OUT_V_SIZE_REG, fmt->height);

	switch (fmt->code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
		mod->info.acq.raw_pattern = RPP_BGGR;
		bayerpat = ACQ_PROP_BAYER_PAT_BGBG;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
		mod->info.acq.raw_pattern = RPP_GBRG;
		bayerpat = ACQ_PROP_BAYER_PAT_GBGB;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
		mod->info.acq.raw_pattern = RPP_GRBG;
		bayerpat = ACQ_PROP_BAYER_PAT_GRGR;
		break;
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		mod->info.acq.raw_pattern = RPP_RGGB;
		bayerpat = ACQ_PROP_BAYER_PAT_RGRG;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt->code) {
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SRGGB8_1X8:
		selection = ACQ_PROP_INPUT_SELECTION_8BIT;
		break;
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
		selection = ACQ_PROP_INPUT_SELECTION_10BIT;
		break;
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
		selection = ACQ_PROP_INPUT_SELECTION_12BIT;
		break;
	default:
		return -EINVAL;
	}

	rpp_module_write(mod, ACQ_PROP_REG, bayerpat | selection |
			 ACQ_PROP_SENSOR_IN_LSB_ALIGNED_IN_LSB);

	rpp_module_clrset(mod, ACQ_CTRL_REG, ACQ_CTRL_INFORM_EN_ENABLE,
			  ACQ_CTRL_INFORM_EN_ENABLE);

	return 0;
}

const struct rpp_module_ops rppx1_acq_ops = {
	.probe = rppx1_acq_probe,
	.start = rppx1_acq_start,
};
