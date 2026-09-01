// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define XYZ2LUV_VERSION_REG			0x0000
#define XYZ2LUV_U_REF_REG			0x0004
#define XYZ2LUV_V_REF_REG			0x0008
#define XYZ2LUV_LUMA_OUT_FAC_REG		0x000c
#define XYZ2LUV_CHROMA_OUT_FAC_REG		0x0010

static int rppx1_xyz2luv_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, XYZ2LUV_VERSION_REG) != 4)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_xyz2luv_ops = {
	.probe = rppx1_xyz2luv_probe,
};
