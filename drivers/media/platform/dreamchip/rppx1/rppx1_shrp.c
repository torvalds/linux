// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define SHRPCNR_VERSION_REG				0x0000

#define SHRPCNR_CTRL_REG				0x0004
#define SHRPCNR_CTRL_CAD_EN				BIT(3)
#define SHRPCNR_CTRL_DESAT_EN				BIT(2)
#define SHRPCNR_CTRL_CNR_EN				BIT(1)
#define SHRPCNR_CTRL_SHARPEN_EN				BIT(0)

#define SHRPCNR_PARAM_REG				0x0008
#define SHRPCNR_PARAM_SHARP_FACTOR_MASK			GENMASK(19, 12)
#define SHRPCNR_PARAM_CORING_THR_MASK			GENMASK(11, 0)

#define SHRPCNR_MAT_1_REG				0x000c
#define SHRPCNR_MAT_2_REG				0x0010
#define SHRPCNR_CLB_LINESIZE_REG			0x0014
#define SHRPCNR_YUV2RGB_CCOR_COEFF_0_REG		0x0018
#define SHRPCNR_YUV2RGB_CCOR_COEFF_1_REG		0x001c
#define SHRPCNR_YUV2RGB_CCOR_COEFF_2_REG		0x0020
#define SHRPCNR_YUV2RGB_CCOR_COEFF_3_REG		0x0024
#define SHRPCNR_YUV2RGB_CCOR_COEFF_4_REG		0x0028
#define SHRPCNR_YUV2RGB_CCOR_COEFF_5_REG		0x002c
#define SHRPCNR_YUV2RGB_CCOR_COEFF_6_REG		0x0030
#define SHRPCNR_YUV2RGB_CCOR_COEFF_7_REG		0x0034
#define SHRPCNR_YUV2RGB_CCOR_COEFF_8_REG		0x0038
#define SHRPCNR_YUV2RGB_CCOR_OFFSET_R_REG		0x003c
#define SHRPCNR_YUV2RGB_CCOR_OFFSET_G_REG		0x0040
#define SHRPCNR_YUV2RGB_CCOR_OFFSET_B_REG		0x0044

#define SHRPCNR_CNR_THRES_REG				0x0048
#define SHRPCNR_CNR_THRES_CNR_THRES_CR_MASK		GENMASK(27, 16)
#define SHRPCNR_CNR_THRES_CNR_THRES_CB_MASK		GENMASK(11, 0)

#define SHRPCNR_CRED_THRES_REG				0x004c
#define SHRPCNR_CRED_SLOPE_REG				0x0050
#define SHRPCNR_CAD_RESTORE_LVL_REG			0x0054
#define SHRPCNR_CAD_THRESH_V_UNEG_REG			0x0058
#define SHRPCNR_CAD_THRESH_V_UPOS_REG			0x005c
#define SHRPCNR_CAD_THRESH_U_REG			0x0060

static int rppx1_shrp_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, SHRPCNR_VERSION_REG)) {
	case 2:
		/* 12-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct rpp_module_ops rppx1_shrp_ops = {
	.probe = rppx1_shrp_probe,
};
