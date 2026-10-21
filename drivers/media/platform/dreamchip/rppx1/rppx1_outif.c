// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define OUT_IF_VERSION_REG			0x0000

#define OUT_IF_ON_REG				0x0004
#define OUT_IF_ON_RPP_ON			BIT(0)

#define OUT_IF_OFF_REG				0x0008

#define OUT_IF_NR_FRAMES_REG			0x000c
#define OUT_IF_NR_FRAMES_NR_FRAMES		GENMASK(9, 0)

#define OUT_IF_NR_FRAMES_CNT_REG		0x0010
#define FLAGS_SHD_REG				0x0018

static int rppx1_outif_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, OUT_IF_VERSION_REG) != 1)
		return -EINVAL;

	return 0;
}

static int rppx1_outif_start(struct rpp_module *mod,
			     const struct v4l2_mbus_framefmt *fmt)
{
	rpp_module_clrset(mod, OUT_IF_NR_FRAMES_REG,
			  OUT_IF_NR_FRAMES_NR_FRAMES, 0);

	rpp_module_write(mod, OUT_IF_ON_REG, OUT_IF_ON_RPP_ON);

	return 0;
}

const struct rpp_module_ops rppx1_outif_ops = {
	.probe = rppx1_outif_probe,
	.start = rppx1_outif_start,
};
