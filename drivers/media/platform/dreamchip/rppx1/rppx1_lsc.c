// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define LSC_VERSION_REG		0x0000

#define LSC_CTRL_REG		0x0004
#define LSC_CTRL_LSC_EN		BIT(0)

#define LSC_R_TABLE_ADDR_REG	0x0008
#define LSC_GR_TABLE_ADDR_REG	0x000c
#define LSC_B_TABLE_ADDR_REG	0x0010
#define LSC_GB_TABLE_ADDR_REG	0x0014
#define LSC_R_TABLE_DATA_REG	0x0018
#define LSC_GR_TABLE_DATA_REG	0x001c
#define LSC_B_TABLE_DATA_REG	0x0020
#define LSC_GB_TABLE_DATA_REG	0x0024
#define LSC_XGRAD_01_REG	0x0028
#define LSC_XGRAD_23_REG	0x002c
#define LSC_XGRAD_45_REG	0x0030
#define LSC_XGRAD_67_REG	0x0034
#define LSC_XGRAD_89_REG	0x0038
#define LSC_XGRAD_1011_REG	0x003c
#define LSC_XGRAD_1213_REG	0x0040
#define LSC_XGRAD_1415_REG	0x0044
#define LSC_YGRAD_01_REG	0x0048
#define LSC_YGRAD_23_REG	0x004c
#define LSC_YGRAD_45_REG	0x0050
#define LSC_YGRAD_67_REG	0x0054
#define LSC_YGRAD_89_REG	0x0058
#define LSC_YGRAD_1011_REG	0x005c
#define LSC_YGRAD_1213_REG	0x0060
#define LSC_YGRAD_1415_REG	0x0064
#define LSC_XSIZE_01_REG	0x0068
#define LSC_XSIZE_23_REG	0x006c
#define LSC_XSIZE_45_REG	0x0070
#define LSC_XSIZE_67_REG	0x0074
#define LSC_XSIZE_89_REG	0x0078
#define LSC_XSIZE_1011_REG	0x007c
#define LSC_XSIZE_1213_REG	0x0080
#define LSC_XSIZE_1415_REG	0x0084
#define LSC_YSIZE_01_REG	0x0088
#define LSC_YSIZE_23_REG	0x008c
#define LSC_YSIZE_45_REG	0x0090
#define LSC_YSIZE_67_REG	0x0094
#define LSC_YSIZE_89_REG	0x0098
#define LSC_YSIZE_1011_REG	0x009c
#define LSC_YSIZE_1213_REG	0x00a0
#define LSC_YSIZE_1415_REG	0x00a4
#define LSC_TABLE_SEL_REG	0x00a8
#define LSC_STATUS_REG		0x00ac

static int rppx1_lsc_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, LSC_VERSION_REG) != 0x04)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_lsc_ops = {
	.probe = rppx1_lsc_probe,
};
