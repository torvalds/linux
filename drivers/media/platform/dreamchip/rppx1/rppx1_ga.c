// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define GAMMA_OUT_VERSION_REG			0x0000

#define GAMMA_OUT_ENABLE_REG			0x0004
#define GAMMA_OUT_ENABLE_GAMMA_OUT_EN		BIT(0)

#define GAMMA_OUT_MODE_REG			0x0008
#define GAMMA_OUT_MODE_GAMMA_OUT_EQU_SEGM	BIT(0)

#define GAMMA_OUT_Y_REG(n)			(0x000c + (4 * (n)))

#define GAMMA_OUT_HV_GAMMA_CURVE_MASK		GENMASK(11, 0)
#define GAMMA_OUT_MV_GAMMA_CURVE_MASK		GENMASK(23, 0)

static int rppx1_ga_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, GAMMA_OUT_VERSION_REG)) {
	case 1:
		/* 12-bit. */
		break;
	case 2:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int rppx1_ga_start(struct rpp_module *mod,
			  const struct v4l2_mbus_framefmt *fmt)
{
	/* Disable stage. */
	rpp_module_write(mod, GAMMA_OUT_ENABLE_REG, 0);

	return 0;
}

static int
rppx1_ga_fill_params(struct rpp_module *mod,
		     const union rppx1_params_block *block,
		     rppx1_reg_write write, void *priv)
{
	const struct rppx1_ga_params *cfg = &block->ga;
	u32 mask;

	/* If the modules is disabled, simply bypass it. */
	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		write(priv, mod->base + GAMMA_OUT_ENABLE_REG, 0);
		return 0;
	}

	switch (cfg->header.type) {
	case RPPX1_PARAMS_BLOCK_TYPE_GA_HV:
		mask = GAMMA_OUT_HV_GAMMA_CURVE_MASK;
		break;
	case RPPX1_PARAMS_BLOCK_TYPE_GA_MV:
		mask = GAMMA_OUT_MV_GAMMA_CURVE_MASK;
		break;
	default:
		return -EINVAL;
	}

	write(priv, mod->base + GAMMA_OUT_MODE_REG, cfg->mode);

	for (unsigned int i = 0; i < RPPX1_GA_MAX_SAMPLES; i++)
		write(priv, mod->base + GAMMA_OUT_Y_REG(i),
		      cfg->gamma_y[i] & mask);

	/* Enable module. */
	write(priv, mod->base + GAMMA_OUT_ENABLE_REG,
	      GAMMA_OUT_ENABLE_GAMMA_OUT_EN);

	return 0;
}

const struct rpp_module_ops rppx1_ga_ops = {
	.probe = rppx1_ga_probe,
	.start = rppx1_ga_start,
	.fill_params = rppx1_ga_fill_params,
};
