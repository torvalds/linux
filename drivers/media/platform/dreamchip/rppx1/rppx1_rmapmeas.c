// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define RMAP_MEAS_VERSION_REG			0x0000
#define RMAP_MEAS_MODE_REG			0x0004
#define RMAP_MEAS_SUBSAMPLING_REG		0x0008
#define RMAP_MEAS_RESERVED_1_REG		0x000c
#define RMAP_MEAS_MIN_THRES_SHORT_REG		0x0010
#define RMAP_MEAS_MAX_THRES_SHORT_REG		0x0014
#define RMAP_MEAS_MAX_THRES_LONG_REG		0x0018
#define RMAP_MEAS_H_OFFS_REG			0x001c
#define RMAP_MEAS_V_OFFS_REG			0x0020
#define RMAP_MEAS_H_SIZE_REG			0x0024
#define RMAP_MEAS_V_SIZE_REG			0x0028
#define RMAP_MEAS_LAST_MEAS_LINE_REG		0x002c
#define RMAP_MEAS_LS_RESULTSHORT0_REG		0x0030
#define RMAP_MEAS_LS_RESULTLONG0_REG		0x0034
#define RMAP_MEAS_RESERVED_2_REG		0x0038
#define RMAP_MEAS_RESERVED_3_REG		0x003c
#define RMAP_MEAS_LS_RESULTSHORT1_REG		0x0040
#define RMAP_MEAS_LS_RESULTLONG1_REG		0x0044
#define RMAP_MEAS_RESERVED_4_REG		0x0048
#define RMAP_MEAS_RESERVED_5_REG		0x004c

static int rppx1_rmapmeas_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, RMAP_MEAS_VERSION_REG)) {
	case 3:
		/* low: 12-bit, high: 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct rpp_module_ops rppx1_rmapmeas_ops = {
	.probe = rppx1_rmapmeas_probe,
};
