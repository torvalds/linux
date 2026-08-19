// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define FILT_VERSION_REG		0x0000

#define DEMOSAIC_REG			0x0004
#define DEMOSAIC_DEMOSAIC_BYPASS	BIT(16)
#define DEMOSAIC_DEMOSAIC_TH_MASK	GENMASK(15, 0)

#define FILT_MODE_REG			0x0008
#define FILT_MODE_FILT_LP_SELECT_MASK	GENMASK(11, 8)
#define FILT_MODE_FILT_CHR_H_MODE_MASK	GENMASK(7, 6)
#define FILT_MODE_FILT_CHR_V_MODE_MASK	GENMASK(5, 4)
#define FILT_MODE_FILT_MODE		BIT(1)
#define FILT_MODE_FILT_ENABLE		BIT(0)

#define FILT_THRESH_BL0_REG		0x000c
#define FILT_THRESH_BL1_REG		0x0010
#define FILT_THRESH_SH0_REG		0x0014
#define FILT_THRESH_SH1_REG		0x0018
#define FILT_LUM_WEIGHT_REG		0x001c
#define FILT_FAC_SH1_REG		0x0020
#define FILT_FAC_SH0_REG		0x0024
#define FILT_FAC_MID_REG		0x0028
#define FILT_FAC_BL0_REG		0x002c
#define FILT_FAC_BL1_REG		0x0030

static int rppx1_db_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, FILT_VERSION_REG) != 5)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_db_ops = {
	.probe = rppx1_db_probe,
};
