// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define GAMMA_OUT_VERSION_REG			0x0000

#define GAMMA_OUT_ENABLE_REG			0x0004
#define GAMMA_OUT_ENABLE_GAMMA_OUT_EN		BIT(0)

#define GAMMA_OUT_MODE_REG			0x0008
#define GAMMA_OUT_MODE_GAMMA_OUT_EQU_SEGM	BIT(0)

#define GAMMA_OUT_Y_REG_NUM			17
#define GAMMA_OUT_Y_REG(n)			(0x000c + (4 * (n)))

static int rppx1_ga_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, GAMMA_OUT_VERSION_REG)) {
	case 1:
		/* 12-bit. */
		break;
	case 2:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rppx1_ga_start(struct rpp_module *mod,
			  const struct v4l2_mbus_framefmt *fmt)
{
	/* Disable stage. */
	rpp_module_write(mod, GAMMA_OUT_ENABLE_REG, 0);

	return 0;
}

const struct rpp_module_ops rppx1_ga_ops = {
	.probe = rppx1_ga_probe,
	.start = rppx1_ga_start,
};
