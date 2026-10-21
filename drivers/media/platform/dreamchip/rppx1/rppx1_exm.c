// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define EXM_VERSION_REG			0x0000
#define EXM_START_REG			0x0004

#define EXM_CTRL_REG			0x0008
#define EXM_CTRL_EXM_AUTOSTOP		BIT(1) /* HW doc says not supported. */
#define EXM_CTRL_EXM_UPDATE_ENABLE	BIT(0)

#define EXM_MODE_REG			0x000c

#define EXM_CHANNEL_SEL_REG		0x0010
#define EXM_CHANNEL_SEL_CHANNEL_SELECT_MASK	GENMASK(2, 0)

#define EXM_LAST_MEAS_LINE_REG		0x0014
#define EXM_COEFF_R_REG			0x0018
#define EXM_COEFF_G_GR_REG		0x001c
#define EXM_COEFF_B_REG			0x0020
#define EXM_COEFF_GB_REG		0x0024
#define EXM_H_OFFS_REG			0x0028
#define EXM_V_OFFS_REG			0x002c
#define EXM_H_SIZE_REG			0x0030
#define EXM_V_SIZE_REG			0x0034
#define EXM_FORCED_UPD_START_LINE_REG	0x0038
#define EXM_VSTART_STATUS_REG		0x003c

#define EXM_MEAN_REG(n)			(0x0040 + (4 * (n)))

static int rppx1_exm_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, EXM_VERSION_REG)) {
	case 1:
		/* 8-bit. */
		break;
	case 3:
		/* 20-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
rppx1_exm_fill_params(struct rpp_module *mod,
		      const union rppx1_params_block *block,
		      rppx1_reg_write write, void *priv)
{
	const struct rppx1_exm_params *cfg = &block->exm;
	u32 h_offs, v_offs, h_size, v_size;

	/* If the modules is disabled, simply bypass it. */
	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		write(priv, mod->base + EXM_MODE_REG, 0);
		return 0;
	}

	switch (cfg->mode) {
	case RPPX1_EXP_MEASURING_MODE_RGB:
	case RPPX1_EXP_MEASURING_MODE_BAYER:
		write(priv, mod->base + EXM_MODE_REG, cfg->mode);
		break;
	default:
		write(priv, mod->base + EXM_MODE_REG, 0);
		return 0;
	}

	write(priv, mod->base + EXM_COEFF_R_REG, cfg->coeff_r);
	write(priv, mod->base + EXM_COEFF_G_GR_REG, cfg->coeff_g_gr);
	write(priv, mod->base + EXM_COEFF_GB_REG, cfg->coeff_gb);
	write(priv, mod->base + EXM_COEFF_B_REG, cfg->coeff_b);

	/* Select sample point */
	write(priv, mod->base + EXM_CHANNEL_SEL_REG,
	      cfg->channel_sel & EXM_CHANNEL_SEL_CHANNEL_SELECT_MASK);

	/*
	 * Adjust and set measurement window,
	 * - Offsets must be even.
	 * - Width and height must be even and divisible in 5 windows.
	 */
	h_offs = cfg->wnd.h_offs & 0x1ffe;
	v_offs = cfg->wnd.v_offs & 0x1ffe;
	h_size = (cfg->wnd.h_size - 1) - ((cfg->wnd.h_size - 1) % 10);
	v_size = (cfg->wnd.v_size - 1) - ((cfg->wnd.v_size - 1) % 10);

	write(priv, mod->base + EXM_H_OFFS_REG, h_offs);
	write(priv, mod->base + EXM_V_OFFS_REG, v_offs);
	write(priv, mod->base + EXM_H_SIZE_REG, h_size / 5);
	write(priv, mod->base + EXM_V_SIZE_REG, v_size / 5);

	/*
	 * Set last measurement line for ready interrupt. Ignore the value
	 * from the parameters as it is only useful for fast-channel switching.
	 */
	write(priv, mod->base + EXM_LAST_MEAS_LINE_REG, v_offs + v_size + 1);

	write(priv, mod->base + EXM_START_REG, 1);

	return 0;
}

static int rppx1_exm_fill_stats(struct rpp_module *mod,
				union rppx1_stats_block *block)
{
	struct rppx1_exm_stats *stats = &block->exm;

	for (unsigned int i = 0; i < RPPX1_EXM_NUM_WIN; i++)
		stats->exp_mean[i] = rpp_module_read(mod, EXM_MEAN_REG(i));

	return 0;
}

const struct rpp_module_ops rppx1_exm_ops = {
	.probe = rppx1_exm_probe,
	.fill_params = rppx1_exm_fill_params,
	.fill_stats = rppx1_exm_fill_stats,
};
