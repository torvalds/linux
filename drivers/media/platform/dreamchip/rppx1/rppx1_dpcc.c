// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define DPCC_VERSION_REG			0x0000

#define DPCC_MODE_REG				0x0004
#define DPCC_MODE_STAGE1_ENABLE			BIT(2)
#define DPCC_MODE_GRAYSCALE_MODE		BIT(1)
#define DPCC_MODE_DPCC_ENABLE			BIT(0)

#define DPCC_OUTPUT_MODE_REG			0x0008
#define DPCC_SET_USE_REG			0x000c
#define DPCC_METHODS_SET_1_REG			0x0010
#define DPCC_METHODS_SET_2_REG			0x0014
#define DPCC_METHODS_SET_3_REG			0x0018
#define DPCC_LINE_THRESH_1_REG			0x001c
#define DPCC_LINE_MAD_FAC_1_REG			0x0020
#define DPCC_PG_FAC_1_REG			0x0024
#define DPCC_RND_THRESH_1_REG			0x0028
#define DPCC_RG_FAC_1_REG			0x002c
#define DPCC_LINE_THRESH_2_REG			0x0030
#define DPCC_LINE_MAD_FAC_2_REG			0x0034
#define DPCC_PG_FAC_2_REG			0x0038
#define DPCC_RND_THRESH_2_REG			0x003c
#define DPCC_RG_FAC_2_REG			0x0040
#define DPCC_LINE_THRESH_3_REG			0x0044
#define DPCC_LINE_MAD_FAC_3_REG			0x0048
#define DPCC_PG_FAC_3_REG			0x004c
#define DPCC_RND_THRESH_3_REG			0x0050
#define DPCC_RG_FAC_3_REG			0x0054
#define DPCC_RO_LIMITS_REG			0x0058
#define DPCC_RND_OFFS_REG			0x005c
#define DPCC_BPT_CTRL_REG			0x0060
#define DPCC_BP_NUMBER_REG			0x0064
#define DPCC_BP_TADDR_REG			0x0068
#define DPCC_BP_POSITION_REG			0x006c

static int rppx1_dpcc_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, DPCC_VERSION_REG)) {
	case 2:
	case 4:
	case 6:
		/* 12-bit. */
		break;
	case 3:
	case 5:
	case 7:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rppx1_dpcc_start(struct rpp_module *mod,
			    const struct v4l2_mbus_framefmt *fmt)
{
	/* Bypass stage1 and DPCC. */
	rpp_module_write(mod, DPCC_MODE_REG, 0);

	return 0;
}

const struct rpp_module_ops rppx1_dpcc_ops = {
	.probe = rppx1_dpcc_probe,
	.start = rppx1_dpcc_start,
};
