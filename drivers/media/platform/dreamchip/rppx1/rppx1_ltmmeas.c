// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define LTM_MEAS_VERSION_REG		0x0000

#define LTM_MEAS_CTRL_REG		0x0004
#define LTM_MEAS_CTRL_LTM_MEAS_ENABLE	BIT(0)

#define LTM_MEAS_RGB_WEIGHTS_REG	0x0008
#define LTM_MEAS_H_OFFS_REG		0x000c
#define LTM_MEAS_V_OFFS_REG		0x0010
#define LTM_MEAS_H_SIZE_REG		0x0014
#define LTM_MEAS_V_SIZE_REG		0x0018

#define LTM_MEAS_PRC_THRESH_NUM		8
#define LTM_MEAS_PRC_THRESH_REG(n)	(0x001c + (4 * (n)))

#define LTM_MEAS_PRC_REG_NUM		8
#define LTM_MEAS_PRC_REG(n)		(0x003c + (4 * (n)))

#define LTM_MEAS_L_MIN_REG		0x005c
#define LTM_MEAS_L_MAX_REG		0x0060
#define LTM_MEAS_L_GMEAN_REG		0x0064

static int rppx1_ltmmeas_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, LTM_MEAS_VERSION_REG) != 1)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_ltmmeas_ops = {
	.probe = rppx1_ltmmeas_probe,
};
