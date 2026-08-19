// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define CAC_VERSION_REG			0x0000
#define CAC_CTRL_REG			0x0004
#define CAC_COUNT_START_REG		0x0008
#define CAC_A_REG			0x000c
#define CAC_B_REG			0x0010
#define CAC_C_REG			0x0014
#define CAC_X_NORM_REG			0x0018
#define CAC_Y_NORM_REG			0x001c

static int rppx1_cac_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, CAC_VERSION_REG) != 3)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_cac_ops = {
	.probe = rppx1_cac_probe,
};
