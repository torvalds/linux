// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define AWB_MEAS_VERSION_REG			0x0000

#define AWB_MEAS_PROP_REG			0x0004
#define AWB_MEAS_PROP_MEAS_MODE_RGB		BIT(16) /* 0: YCbCr 1: RGB */
#define AWB_MEAS_PROP_YMAX			BIT(2)
#define AWB_MEAS_PROP_AWB_MODE_ON		BIT(1)

#define AWB_MEAS_H_OFFS_REG			0x0008
#define AWB_MEAS_V_OFFS_REG			0x000c
#define AWB_MEAS_H_SIZE_REG			0x0010
#define AWB_MEAS_V_SIZE_REG			0x0014
#define AWB_MEAS_FRAMES_REG			0x0018
#define AWB_MEAS_REF_CB_MAX_B_REG		0x001c
#define AWB_MEAS_REF_CR_MAX_R_REG		0x0020
#define AWB_MEAS_MAX_Y_REG			0x0024
#define AWB_MEAS_MIN_Y_MAX_G_REG		0x0028
#define AWB_MEAS_MAX_CSUM_REG			0x002c
#define AWB_MEAS_MIN_C_REG			0x0030
#define AWB_MEAS_WHITE_CNT_REG			0x0034
#define AWB_MEAS_MEAN_Y_G_REG			0x0038
#define AWB_MEAS_MEAN_CB_B_REG			0x003c
#define AWB_MEAS_MEAN_CR_R_REG			0x0040

#define AWB_MEAS_CCOR_COEFF_NUM			9
#define AWB_MEAS_CCOR_COEFF_REG(n)		(0x0044 + (4 * (n)))

#define AWB_MEAS_CCOR_OFFSET_R_REG		0x0068
#define AWB_MEAS_CCOR_OFFSET_G_REG		0x006c
#define AWB_MEAS_CCOR_OFFSET_B_REG		0x0070

static int rppx1_wbmeas_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, AWB_MEAS_VERSION_REG)) {
	case 1:
		/* 8-bit. */
		break;
	case 2:
		/* 20-bit. */
		break;
	case 3:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct rpp_module_ops rppx1_wbmeas_ops = {
	.probe = rppx1_wbmeas_probe,
};
