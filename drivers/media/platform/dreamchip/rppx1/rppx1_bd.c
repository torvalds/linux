// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define DPF_VERSION_REG			0x0000

#define DPF_MODE_REG			0x0004
#define DPF_MODE_USE_NF_GAIN		BIT(9)
#define DPF_MODE_LSC_GAIN_COMP		BIT(8)
#define DPF_MODE_NLL_SEGMENTATION	BIT(6)
#define DPF_MODE_RB_FILTER_SIZE		BIT(5)
#define DPF_MODE_R_FILTER_OFF		BIT(4)
#define DPF_MODE_GR_FILTER_OFF		BIT(3)
#define DPF_MODE_GB_FILTER_OFF		BIT(2)
#define DPF_MODE_B_FILTER_OFF		BIT(1)
#define DPF_MODE_DPF_ENABLE		BIT(0)

#define DPF_STRENGTH_R_REG		0x0008
#define DPF_STRENGTH_G_REG		0x000c
#define DPF_STRENGTH_B_REG		0x0010
#define DPF_S_WEIGHT_G_1_4_REG		0x0014
#define DPF_S_WEIGHT_G_5_6_REG		0x0018
#define DPF_S_WEIGHT_RB_1_4_REG		0x001c
#define DPF_S_WEIGHT_RB_5_6_REG		0x0020

#define DPF_NLL_G_COEFF_REG_NUM		17
#define DPF_NLL_G_COEFF_REG(n)		(0x0024 + (4 * (n)))

#define DPF_NLL_RB_COEFF_REG_NUM	17
#define DPF_NLL_RB_COEFF_REG(n)		(0x0068 + (4 * (n)))

#define DPF_NF_GAIN_R_REG		0x00ac
#define DPF_NF_GAIN_GR_REG		0x00b0
#define DPF_NF_GAIN_GB_REG		0x00b4
#define DPF_NF_GAIN_B_REG		0x00b8

static int rppx1_bd_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, DPF_VERSION_REG) != 5)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_bd_ops = {
	.probe = rppx1_bd_probe,
};
