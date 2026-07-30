// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define CCOR_VERSION_REG				0x0000

#define CCOR_COEFF_REG_NUM				9
#define CCOR_COEFF_REG(n)				(0x0004 + (4 * (n)))

#define CCOR_OFFSET_R_REG				0x0028
#define CCOR_OFFSET_G_REG				0x002c
#define CCOR_OFFSET_B_REG				0x0030

#define CCOR_CONFIG_TYPE_REG				0x0034
#define CCOR_CONFIG_TYPE_USE_OFFSETS_AS_PRE_OFFSETS	BIT(1)
#define CCOR_CONFIG_TYPE_CCOR_RANGE_AVAILABLE		BIT(0)

#define CCOR_RANGE_REG					0x0038
#define CCOR_RANGE_CCOR_C_RANGE				BIT(1)
#define CCOR_RANGE_CCOR_Y_RANGE				BIT(0)

static int rppx1_ccor_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, CCOR_VERSION_REG)) {
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

static int rppx1_ccor_start(struct rpp_module *mod,
			    const struct v4l2_mbus_framefmt *fmt)
{
	/* Configure matrix in bypass mode. */
	rpp_module_write(mod, CCOR_COEFF_REG(0), 0x1000);
	rpp_module_write(mod, CCOR_COEFF_REG(1), 0x0000);
	rpp_module_write(mod, CCOR_COEFF_REG(2), 0x0000);

	rpp_module_write(mod, CCOR_COEFF_REG(3), 0x0000);
	rpp_module_write(mod, CCOR_COEFF_REG(4), 0x1000);
	rpp_module_write(mod, CCOR_COEFF_REG(5), 0x0000);

	rpp_module_write(mod, CCOR_COEFF_REG(6), 0x0000);
	rpp_module_write(mod, CCOR_COEFF_REG(7), 0x0000);
	rpp_module_write(mod, CCOR_COEFF_REG(8), 0x1000);

	rpp_module_write(mod, CCOR_OFFSET_R_REG, 0x00000000);
	rpp_module_write(mod, CCOR_OFFSET_G_REG, 0x00000000);
	rpp_module_write(mod, CCOR_OFFSET_B_REG, 0x00000000);

	return 0;
}

const struct rpp_module_ops rppx1_ccor_ops = {
	.probe = rppx1_ccor_probe,
	.start = rppx1_ccor_start,
};

static int rppx1_ccor_csm_start(struct rpp_module *mod,
				const struct v4l2_mbus_framefmt *fmt)
{
	/* Reuse bypass matrix setup. */
	if (fmt->code == MEDIA_BUS_FMT_RGB888_1X24)
		return rppx1_ccor_start(mod, fmt);

	/* Color Transformation RGB to YUV according to ITU-R BT.709. */
	rpp_module_write(mod, CCOR_COEFF_REG(0), 0x0367);
	rpp_module_write(mod, CCOR_COEFF_REG(1), 0x0b71);
	rpp_module_write(mod, CCOR_COEFF_REG(2), 0x0128);

	rpp_module_write(mod, CCOR_COEFF_REG(3), 0xfe2b);
	rpp_module_write(mod, CCOR_COEFF_REG(4), 0xf9d5);
	rpp_module_write(mod, CCOR_COEFF_REG(5), 0x0800);

	rpp_module_write(mod, CCOR_COEFF_REG(6), 0x0800);
	rpp_module_write(mod, CCOR_COEFF_REG(7), 0xf8bc);
	rpp_module_write(mod, CCOR_COEFF_REG(8), 0xff44);

	rpp_module_write(mod, CCOR_OFFSET_R_REG, 0x00000000);
	rpp_module_write(mod, CCOR_OFFSET_G_REG, 0x00000800);
	rpp_module_write(mod, CCOR_OFFSET_B_REG, 0x00000800);

	return 0;
}

const struct rpp_module_ops rppx1_ccor_csm_ops = {
	.probe = rppx1_ccor_probe,
	.start = rppx1_ccor_csm_start,
};
