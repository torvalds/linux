// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define EXM_VERSION_REG			0x0000
#define EXM_START_REG			0x0004

#define EXM_CTRL_REG			0x0008
#define EXM_CTRL_EXM_UPDATE_ENABLE	BIT(0)

#define EXM_MODE_REG			0x000c
#define EXM_CHANNEL_SEL_REG		0x0010
#define EXM_LAST_MEAS_LINE_REG		0x0014
#define EXM_COEFF_R_REG			0x0018
#define EXM_COEFF_G_GR_REG		0x001c
#define EXM_COEFF_B_REG			0x0020
#define EXM_COEFF_GB_REG		0x0024
#define EXM_H_OFFS_REG			0x0028
#define EXM_V_OFFS_REG			0x002c
#define EXM_H_SIZE_REG			0x0030
#define EXM_V_SIZE_REG			0x0034
#define EXM_FORCED_UPD_START_LINE_REG	0x0038
#define EXM_VSTART_STATUS_REG		0x003c

#define EXM_MEAN_REG(n)			(0x0040 + (4 * (n)))

static int rppx1_exm_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, EXM_VERSION_REG)) {
	case 1:
		/* 8-bit. */
		break;
	case 3:
		/* 20-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct rpp_module_ops rppx1_exm_ops = {
	.probe = rppx1_exm_probe,
};
