// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define HIST256_VERSION_REG			0x0000
#define HIST256_MODE_REG			0x0004
#define HIST256_MODE_HIST256_MODE		BIT(0)

#define HIST256_CHANNEL_SEL_REG			0x0008
#define HIST256_CHANNEL_SEL_CHANNEL_SELECT	GENMASK(2, 0)

#define HIST256_H_OFFS_REG			0x000c
#define HIST256_V_OFFS_REG			0x0010
#define HIST256_H_SIZE_REG			0x0014
#define HIST256_V_SIZE_REG			0x0018
#define HIST256_SAMPLE_OFFSET_REG		0x001c
#define HIST256_SAMPLE_SCALE_REG		0x0020
#define HIST256_MEAS_RESULT_ADDR_AUTOINCR_REG	0x0024
#define HIST256_MEAS_RESULT_ADDR_REG		0x0028
#define HIST256_MEAS_RESULT_DATA_REG		0x002c

#define HIST256_LOG_ENABLE_REG			0x0030
#define HIST256_LOG_ENABLE_HIST256_LOG_EN	BIT(0)

#define HIST256_LOG_DX_LO_REG			0x0034
#define HIST256_LOG_DX_HI_REG			0x0038

#define HIST256_Y_REG_NUM			17
#define HIST256_Y_REG(n)			(0x0040 + (4 * (n)))

static int rppx1_hist256_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, HIST256_VERSION_REG) != 2)
		return -EINVAL;

	return 0;
}

const struct rpp_module_ops rppx1_hist256_ops = {
	.probe = rppx1_hist256_probe,
};
