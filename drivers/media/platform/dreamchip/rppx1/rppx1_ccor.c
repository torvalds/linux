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

static int
rppx1_ccor_fill_params(struct rpp_module *mod,
		       const union rppx1_params_block *block,
		       rppx1_reg_write write, void *priv)
{
	const struct rppx1_ccor_params *cfg = &block->ccor;

	/* If the modules is disabled, configure in bypass mode. */
	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		write(priv, mod->base + CCOR_COEFF_REG(0), 0x1000);
		write(priv, mod->base + CCOR_COEFF_REG(1), 0x0000);
		write(priv, mod->base + CCOR_COEFF_REG(2), 0x0000);

		write(priv, mod->base + CCOR_COEFF_REG(3), 0x0000);
		write(priv, mod->base + CCOR_COEFF_REG(4), 0x1000);
		write(priv, mod->base + CCOR_COEFF_REG(5), 0x0000);

		write(priv, mod->base + CCOR_COEFF_REG(6), 0x0000);
		write(priv, mod->base + CCOR_COEFF_REG(7), 0x0000);
		write(priv, mod->base + CCOR_COEFF_REG(8), 0x1000);

		write(priv, mod->base + CCOR_OFFSET_R_REG, 0x00000000);
		write(priv, mod->base + CCOR_OFFSET_G_REG, 0x00000000);
		write(priv, mod->base + CCOR_OFFSET_B_REG, 0x00000000);

		return 0;
	}

	/*
	 * Coefficient n for color correction matrix.
	 *
	 * RPP coefficients are 16-bit signed fixed-point numbers with 4 bit
	 * integer and 12 bit fractional part ranging from -8 (0x8000) to
	 * +7.9996 (0x7FFF). 0 is represented by 0x0000 and a coefficient
	 * value of 1 as 0x1000.
	 */
	write(priv, mod->base + CCOR_COEFF_REG(0), cfg->coeff[0][0]);
	write(priv, mod->base + CCOR_COEFF_REG(1), cfg->coeff[0][1]);
	write(priv, mod->base + CCOR_COEFF_REG(2), cfg->coeff[0][2]);

	write(priv, mod->base + CCOR_COEFF_REG(3), cfg->coeff[1][0]);
	write(priv, mod->base + CCOR_COEFF_REG(4), cfg->coeff[1][1]);
	write(priv, mod->base + CCOR_COEFF_REG(5), cfg->coeff[1][2]);

	write(priv, mod->base + CCOR_COEFF_REG(6), cfg->coeff[2][0]);
	write(priv, mod->base + CCOR_COEFF_REG(7), cfg->coeff[2][1]);
	write(priv, mod->base + CCOR_COEFF_REG(8), cfg->coeff[2][2]);

	/*
	 * Offset for color components correction matrix.
	 *
	 * Values are a two's complement integer with one sign bit.
	 */
	write(priv, mod->base + CCOR_OFFSET_R_REG, cfg->offset[0]);
	write(priv, mod->base + CCOR_OFFSET_G_REG, cfg->offset[1]);
	write(priv, mod->base + CCOR_OFFSET_B_REG, cfg->offset[2]);

	return 0;
}

const struct rpp_module_ops rppx1_ccor_ops = {
	.probe = rppx1_ccor_probe,
	.start = rppx1_ccor_start,
	.fill_params = rppx1_ccor_fill_params,
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
