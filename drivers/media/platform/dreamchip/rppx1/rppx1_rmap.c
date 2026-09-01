// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define RMAP_DATA_VERSION_REG		0x0000

#define RMAP_CTRL_REG			0x0004
#define RMAP_CTRL_BYPASS_LONG		BIT(2)

#define RMAP_WBTHRESHOLD_LONG_REG	0x0008
#define RMAP_WBTHRESHOLD_SHORT_REG	0x000c
#define RMAP_RESERVED_1_REG		0x0010
#define RMAP_WBGAIN_LONG_RED_REG	0x0014
#define RMAP_WBGAIN_LONG_BLUE_REG	0x0018
#define RMAP_WBGAIN_SHORT_RED_REG	0x001c
#define RMAP_WBGAIN_SHORT_BLUE_REG	0x0020
#define RMAP_RESERVED_2_REG		0x0024
#define RMAP_RESERVED_3_REG		0x0028
#define RMAP_MAP_FAC_SHORT_REG		0x002c
#define RMAP_RESERVED_4_REG		0x0030
#define RMAP_MIN_THRES_SHORT_REG	0x0034
#define RMAP_MAX_THRES_SHORT_REG	0x0038
#define RMAP_STEPSIZE_SHORT_REG		0x003c
#define RMAP_MIN_THRES_LONG_REG		0x0040
#define RMAP_MAX_THRES_LONG_REG		0x0044
#define RMAP_STEPSIZE_LONG_REG		0x0048
#define RMAP_CLB_LINESIZE_REG		0x004c

static int rppx1_rmap_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, RMAP_DATA_VERSION_REG)) {
	case 8:
		/* low: 12-bit, high: 20-bit. */
		break;
	case 9:
		/* low: 12-bit, high: 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rppx1_rmap_start(struct rpp_module *mod,
			    const struct v4l2_mbus_framefmt *fmt)
{
	/* Bypass radiance mapping and use the long exposure channel (PRE1). */
	rpp_module_write(mod, RMAP_CTRL_REG, RMAP_CTRL_BYPASS_LONG);

	return 0;
}

const struct rpp_module_ops rppx1_rmap_ops = {
	.probe = rppx1_rmap_probe,
	.start = rppx1_rmap_start,
};
