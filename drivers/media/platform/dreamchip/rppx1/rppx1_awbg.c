// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define AWB_GAIN_VERSION_REG		0x0000

#define AWB_ENABLE_REG			0x0004
#define AWB_ENABLE_AWB_GAIN_EN		BIT(0)

#define AWB_GAIN_GR_REG			0x0008
#define AWB_GAIN_GB_REG			0x000c
#define AWB_GAIN_R_REG			0x0010
#define AWB_GAIN_B_REG			0x0014

static int rppx1_awbg_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, AWB_GAIN_VERSION_REG) != 3)
		return -EINVAL;

	return 0;
}

static int
rppx1_awbg_fill_params(struct rpp_module *mod,
		       const union rppx1_params_block *block,
		       rppx1_reg_write write, void *priv)
{
	const struct rppx1_awbg_params *cfg = &block->awbg;

	/* If the modules is disabled, simply bypass it. */
	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		write(priv, mod->base + AWB_ENABLE_REG, 0);
		return 0;
	}

	/*
	 * RPP gains are 18-bit with 12 bit fractional part and 0x1000 = 1.0,
	 * giving a possible range of 0.0 to 64.0. NOTE: RPP documentation is
	 * contradictory this is the register definition, the function
	 * description states 0x400 = 1.0 AND 18-bit with 12 fractional bits,
	 * which is not possible...
	 */

	write(priv, mod->base + AWB_GAIN_GR_REG, cfg->gain_green_r);
	write(priv, mod->base + AWB_GAIN_GB_REG, cfg->gain_green_b);
	write(priv, mod->base + AWB_GAIN_R_REG, cfg->gain_red);
	write(priv, mod->base + AWB_GAIN_B_REG, cfg->gain_blue);

	write(priv, mod->base + AWB_ENABLE_REG, AWB_ENABLE_AWB_GAIN_EN);

	return 0;
}

const struct rpp_module_ops rppx1_awbg_ops = {
	.probe = rppx1_awbg_probe,
	.fill_params = rppx1_awbg_fill_params,
};
