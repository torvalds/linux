// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

/* NOTE: The module is called LIN the registers GAMMA_IN. */
#define LIN_VERSION_REG				0x0000

#define LIN_ENABLE_REG				0x0004
#define LIN_ENABLE_GAMMA_IN_EN			BIT(0)

#define LIN_DX_LO_REG				0x0008
#define LIN_DX_HI_REG				0x000c

#define LIN_R_Y_REG_NUM				17
#define LIN_R_Y_REG(n)				(0x0010 + (4 * (n)))

#define LIN_G_Y_REG_NUM				17
#define LIN_G_Y_REG(n)				(0x0054 + (4 * (n)))

#define LIN_B_Y_REG_NUM				17
#define LIN_B_Y_REG(n)				(0x0098 + (4 * (n)))

#define LIN_PRE1_DEGAMMA_CURVE_MASK		GENMASK(23, 0)
#define LIN_PRE1_SAMPLE_POINTS_MASK		GENMASK(3, 0)
#define LIN_PRE2_DEGAMMA_CURVE_MASK		GENMASK(11, 0)
#define LIN_PRE2_SAMPLE_POINTS_MASK		GENMASK(2, 0)

static int rppx1_lin_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, LIN_VERSION_REG)) {
	case 7:
		/* 12-bit. */
		break;
	case 8:
		/* 20-bit. */
		break;
	case 9:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rppx1_lin_start(struct rpp_module *mod,
			   const struct v4l2_mbus_framefmt *fmt)
{
	rpp_module_clrset(mod, LIN_ENABLE_REG, LIN_ENABLE_GAMMA_IN_EN, 0);

	return 0;
}

static int rppx1_lin_fill_params(struct rpp_module *mod,
				 const union rppx1_params_block *block,
				 rppx1_reg_write write, void *priv)
{
	const struct rppx1_lin_params *cfg = &block->lin;
	u8 sample_mask;
	u32 dx_lo = 0;
	u32 dx_hi = 0;
	u32 mask;

	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		write(priv, mod->base + LIN_ENABLE_REG, 0);
		return 0;
	}

	switch (cfg->header.type) {
	case RPPX1_PARAMS_BLOCK_TYPE_LIN_PRE1:
		mask = LIN_PRE1_DEGAMMA_CURVE_MASK;
		sample_mask = LIN_PRE1_SAMPLE_POINTS_MASK;
		break;
	case RPPX1_PARAMS_BLOCK_TYPE_LIN_PRE2:
		mask = LIN_PRE2_DEGAMMA_CURVE_MASK;
		sample_mask = LIN_PRE2_SAMPLE_POINTS_MASK;
		break;
	default:
		return -EINVAL;
	}

	for (unsigned int i = 0; i < 8; ++i) {
		dx_lo |= (cfg->dx[i] & sample_mask) << 4 * i;
		dx_hi |= (cfg->dx[i + 8] & sample_mask) << 4 * i;
	}

	write(priv, mod->base + LIN_DX_LO_REG, dx_lo);
	write(priv, mod->base + LIN_DX_HI_REG, dx_hi);

	for (unsigned int i = 0; i < RPPX1_LIN_DEGAMMA_CURVE_NUM; i++) {
		write(priv, mod->base + LIN_R_Y_REG(i), cfg->curve_r[i] & mask);
		write(priv, mod->base + LIN_G_Y_REG(i), cfg->curve_g[i] & mask);
		write(priv, mod->base + LIN_B_Y_REG(i), cfg->curve_b[i] & mask);
	}

	write(priv, mod->base + LIN_ENABLE_REG, LIN_ENABLE_GAMMA_IN_EN);

	return 0;
}

const struct rpp_module_ops rppx1_lin_ops = {
	.probe = rppx1_lin_probe,
	.start = rppx1_lin_start,
	.fill_params = rppx1_lin_fill_params,
};
