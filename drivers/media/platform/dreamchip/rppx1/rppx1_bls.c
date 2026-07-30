// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define BLS_VERSION_REG				0x0000

#define BLS_CTRL_REG				0x0004
#define BLS_CTRL_BLS_WIN2			BIT(3)
#define BLS_CTRL_BLS_WIN1			BIT(2)
#define BLS_CTRL_BLS_MODE_MEASURED		BIT(1)
#define BLS_CTRL_BLS_EN				BIT(0)

#define BLS_SAMPLES_REG				0x0008
#define BLS_H1_START_REG			0x000c
#define BLS_H1_STOP_REG				0x0010
#define BLS_V1_START_REG			0x0014
#define BLS_V1_STOP_REG				0x0018
#define BLS_H2_START_REG			0x001c
#define BLS_H2_STOP_REG				0x0020
#define BLS_V2_START_REG			0x0024
#define BLS_V2_STOP_REG				0x0028
#define BLS_A_FIXED_REG				0x002c
#define BLS_B_FIXED_REG				0x0030
#define BLS_C_FIXED_REG				0x0034
#define BLS_D_FIXED_REG				0x0038
#define BLS_A_MEASURED_REG			0x003c
#define BLS_B_MEASURED_REG			0x0040
#define BLS_C_MEASURED_REG			0x0044
#define BLS_D_MEASURED_REG			0x0048

static int rppx1_bls_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, BLS_VERSION_REG)) {
	case 3:
	case 5:
		/* 12-bit. */
		break;
	case 2:
	case 4:
		/* 20-bit. */
		break;
	case 6:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct rpp_module_ops rppx1_bls_ops = {
	.probe = rppx1_bls_probe,
};
