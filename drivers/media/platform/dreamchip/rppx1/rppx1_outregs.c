// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define OUTREGS_VERSION_REG					0x0000

#define OUT_MODE_REG						0x0004
#define OUT_MODE_UNSELECTED_MODE_MASK				GENMASK(11, 8)
#define OUT_MODE_UNSELECTED_MODE_MAIN				(0x1 << 8)
#define OUT_MODE_UNSELECTED_MODE_PRE1				(0x2 << 8)
#define OUT_MODE_UNSELECTED_MODE_PRE2				(0x4 << 8)
#define OUT_MODE_IN_SEL_MASK					GENMASK(3, 0)
#define OUT_MODE_IN_SEL_MAIN					1
#define OUT_MODE_IN_SEL_PRE1					2
#define OUT_MODE_IN_SEL_PRE2					4

#define OUT_CONV_422_METHOD_REG					0x0008
#define OUT_CONV_422_METHOD_CONV_422_METHOD_MASK		GENMASK(1, 0)
#define OUT_CONV_422_METHOD_CONV_422_METHOD_CO_SITED1		0
#define OUT_CONV_422_METHOD_CONV_422_METHOD_CO_SITED2		1
#define OUT_CONV_422_METHOD_CONV_422_METHOD_NON_CO_SITED	2

#define OUTREGS_FORMAT_REG					0x000c
#define OUTREGS_FORMAT_OUTPUT_FORMAT_MASK			GENMASK(1, 0)
#define OUTREGS_FORMAT_OUTPUT_FORMAT_RGB			0
#define OUTREGS_FORMAT_OUTPUT_FORMAT_YUV422			1
#define OUTREGS_FORMAT_OUTPUT_FORMAT_YUV420			2

static int rppx1_outregs_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, OUTREGS_VERSION_REG) != 2)
		return -EINVAL;

	return 0;
}

static int rppx1_outregs_start(struct rpp_module *mod,
			       const struct v4l2_mbus_framefmt *fmt)
{
	u32 format;

	switch (fmt->code) {
	case MEDIA_BUS_FMT_YUYV12_1X24:
		format = OUTREGS_FORMAT_OUTPUT_FORMAT_YUV422;
		break;
	case MEDIA_BUS_FMT_RGB888_1X24:
		format = OUTREGS_FORMAT_OUTPUT_FORMAT_RGB;
		break;
	default:
		return -EINVAL;
	}

	rpp_module_clrset(mod, OUT_MODE_REG,
			  OUT_MODE_UNSELECTED_MODE_MASK | OUT_MODE_IN_SEL_MASK,
			  OUT_MODE_UNSELECTED_MODE_MASK | OUT_MODE_IN_SEL_MAIN);

	rpp_module_clrset(mod, OUT_CONV_422_METHOD_REG,
			  OUT_CONV_422_METHOD_CONV_422_METHOD_MASK,
			  OUT_CONV_422_METHOD_CONV_422_METHOD_CO_SITED1);

	rpp_module_clrset(mod, OUTREGS_FORMAT_REG,
			  OUTREGS_FORMAT_OUTPUT_FORMAT_MASK, format);

	return 0;
}

const struct rpp_module_ops rppx1_outregs_ops = {
	.probe = rppx1_outregs_probe,
	.start = rppx1_outregs_start,
};
