// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define AWB_GAIN_VERSION_REG		0x0000

#define AWB_ENABLE_REG			0x0004
#define AWB_ENABLE_AWB_GAIN_EN		BIT(0)

#define AWB_GAIN_GR_REG			0x0008
#define AWB_GAIN_GB_REG			0x000c
#define AWB_GAIN_R_REG			0x0010
#define AWB_GAIN_B_REG			0x0014

static int rppx1_awbg_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, AWB_GAIN_VERSION_REG) != 3)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_awbg_ops = {
	.probe = rppx1_awbg_probe,
};
