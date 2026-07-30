// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define HIST_VERSION_REG			0x0000

#define HIST_CTRL_REG				0x0004
#define HIST_CTRL_HIST_UPDATE_ENABLE		BIT(0)

#define HIST_MODE_REG				0x0008
#define HIST_MODE_HIST_MODE_MASK		GENMASK(2, 0)
#define HIST_MODE_HIST_MODE_DISABLE		0
#define HIST_MODE_HIST_MODE_YRGB		1
#define HIST_MODE_HIST_MODE_R			2
#define HIST_MODE_HIST_MODE_GR			3
#define HIST_MODE_HIST_MODE_B			4
#define HIST_MODE_HIST_MODE_GB			5

#define HIST_CHANNEL_SEL_REG			0x000c
#define HIST_CHANNEL_SEL_CHANNEL_SELECT_MASK	GENMASK(2, 0)

#define HIST_LAST_MEAS_LINE_REG			0x0010
#define HIST_SUBSAMPLING_REG			0x0014
#define HIST_COEFF_R_REG			0x0018
#define HIST_COEFF_G_REG			0x001c
#define HIST_COEFF_B_REG			0x0020
#define HIST_H_OFFS_REG				0x0024
#define HIST_V_OFFS_REG				0x0028
#define HIST_H_SIZE_REG				0x002c
#define HIST_V_SIZE_REG				0x0030

#define HIST_SAMPLE_RANGE_REG			0x0034
#define HIST_SAMPLE_RANGE_SAMPLE_SHIFT_MASK	GENMASK(28, 24)
#define HIST_SAMPLE_RANGE_SAMPLE_OFFSET_MASK	GENMASK(23, 0)

#define HIST_WEIGHT_00TO30_REG			0x0038
#define HIST_WEIGHT_40TO21_REG			0x003c
#define HIST_WEIGHT_31TO12_REG			0x0040
#define HIST_WEIGHT_22TO03_REG			0x0044
#define HIST_WEIGHT_13TO43_REG			0x0048
#define HIST_WEIGHT_04TO34_REG			0x004c
#define HIST_WEIGHT_44_REG			0x0050
#define HIST_FORCED_UPD_START_LINE_REG		0x0054
#define HIST_FORCED_UPDATE_REG			0x0058
#define HIST_VSTART_STATUS_REG			0x005c

#define HIST_BIN_REG_NUM			32
#define HIST_BIN_REG(n)				(0x0060 + (4 * (n)))

static int rppx1_hist_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, HIST_VERSION_REG)) {
	case 3:
		/* 12-bit. */
		break;
	case 4:
		/* 20-bit. */
		break;
	case 5:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

const struct rpp_module_ops rppx1_hist_ops = {
	.probe = rppx1_hist_probe,
};
