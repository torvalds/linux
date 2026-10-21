// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define LTM_VERSION_REG				0x0000

#define LTM_CTRL_REG				0x0004
#define LTM_CTRL_LTM_ENABLE			BIT(0)

#define LTM_RGB_WEIGHTS_REG			0x0008
#define LTM_CLB_LINESIZE_REG			0x000c
#define LTM_TONECURVE_1_REG			0x0010
#define LTM_TONECURVE_2_REG			0x0014
#define LTM_TONECURVE_3_REG			0x0018
#define LTM_TONECURVE_4_REG			0x001c
#define LTM_TONECURVE_5_REG			0x0020
#define LTM_TONECURVE_6_REG			0x0024
#define LTM_TONECURVE_YM_REG(n)			(0x0028 + (4 * (n)))
#define LTM_L0W_REG				0x00ec
#define LTM_L0W_R_REG				0x00f0
#define LTM_L0D_REG				0x00f4
#define LTM_L0D_R_REG				0x00f8
#define LTM_KMIND_REG				0x00fc
#define LTM_KMAXD_REG				0x0100
#define LTM_KDIFFD_REG				0x0104
#define LTM_KDIFFD_R_REG			0x0108
#define LTM_KW_REG				0x010c
#define LTM_KW_R_REG				0x0110
#define LTM_CGAIN_REG				0x0114
#define LTM_LPRCH_R_HIGH_REG			0x0118
#define LTM_LPRCH_R_LOW_REG			0x011c

static int rppx1_ltm_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, LTM_VERSION_REG) != 8)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_ltm_ops = {
	.probe = rppx1_ltm_probe,
};
