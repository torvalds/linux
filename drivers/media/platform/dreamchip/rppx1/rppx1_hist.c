// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#include <linux/bitfield.h>

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
#define HIST_SUBSAMPLING_V_STEPSIZE(x)		(((x) & 0x7f) << 24)
#define HIST_SUBSAMPLING_H_STEP_INC(x)		(((x) & 0x1ffff))

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

#define RPPX1_HIST_WEIGHT(v0, v1, v2, v3) \
	(((v0) & 0x1f) | (((v1) & 0x1f) << 8)  | \
	(((v2) & 0x1f) << 16) | \
	(((v3) & 0x1f) << 24))

static int rppx1_hist_fill_params(struct rpp_module *mod,
				  const union rppx1_params_block *block,
				  rppx1_reg_write write, void *priv)
{
	const struct rppx1_hist_params *cfg = &block->hist;
	u32 h_offs, v_offs, h_size, v_size;

	/* If the modules is disabled, simply bypass it. */
	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		write(priv, mod->base + HIST_MODE_REG,
		      HIST_MODE_HIST_MODE_DISABLE);
		return 0;
	}

	/* Select sample point */
	write(priv, mod->base + HIST_CHANNEL_SEL_REG,
	      cfg->channel_sel & HIST_CHANNEL_SEL_CHANNEL_SELECT_MASK);

	/*
	 * Configure the input subsampling.
	 *
	 * In Bayer mode the vertical and horizontal subsampling counters are
	 * only incremented for color channels selected by hist_mode.
	 */
	write(priv, mod->base + HIST_SUBSAMPLING_REG,
	      HIST_SUBSAMPLING_V_STEPSIZE(cfg->v_stepsize) |
	      HIST_SUBSAMPLING_H_STEP_INC(cfg->h_step_inc));

	/*
	 * Adjust and set measurement window to hardware limitations,
	 * - Offsets must be even.
	 * - Width and height must be even and divisible in 5 windows.
	 */
	h_offs = cfg->wnd.h_offs & 0x1ffe;
	v_offs = cfg->wnd.v_offs & 0x1ffe;
	h_size = cfg->wnd.h_size - cfg->wnd.h_size % 10;
	v_size = cfg->wnd.v_size - cfg->wnd.v_size % 10;

	write(priv, mod->base + HIST_H_OFFS_REG, h_offs);
	write(priv, mod->base + HIST_V_OFFS_REG, v_offs);
	write(priv, mod->base + HIST_H_SIZE_REG, h_size / 5);
	write(priv, mod->base + HIST_V_SIZE_REG, v_size / 5);

	/*
	 * Set last measurement line for ready interrupt. Ignore the value
	 * from the parameters as it is only useful for fast-channel switching.
	 */
	write(priv, mod->base + HIST_LAST_MEAS_LINE_REG, v_offs + v_size + 1);

	/* Set measurement window weights. */
	write(priv, mod->base + HIST_WEIGHT_00TO30_REG,
	      RPPX1_HIST_WEIGHT(cfg->weights[0], cfg->weights[1],
				cfg->weights[2], cfg->weights[3]));
	write(priv, mod->base + HIST_WEIGHT_40TO21_REG,
	      RPPX1_HIST_WEIGHT(cfg->weights[4], cfg->weights[5],
				cfg->weights[6], cfg->weights[7]));
	write(priv, mod->base + HIST_WEIGHT_31TO12_REG,
	      RPPX1_HIST_WEIGHT(cfg->weights[8], cfg->weights[9],
				cfg->weights[10], cfg->weights[11]));
	write(priv, mod->base + HIST_WEIGHT_22TO03_REG,
	      RPPX1_HIST_WEIGHT(cfg->weights[12], cfg->weights[13],
				cfg->weights[14], cfg->weights[15]));
	write(priv, mod->base + HIST_WEIGHT_13TO43_REG,
	      RPPX1_HIST_WEIGHT(cfg->weights[16], cfg->weights[17],
				cfg->weights[18], cfg->weights[19]));
	write(priv, mod->base + HIST_WEIGHT_04TO34_REG,
	      RPPX1_HIST_WEIGHT(cfg->weights[20], cfg->weights[21],
				cfg->weights[22], cfg->weights[23]));
	write(priv, mod->base + HIST_WEIGHT_44_REG,
	      RPPX1_HIST_WEIGHT(cfg->weights[24], 0, 0, 0));

	write(priv, mod->base + HIST_MODE_REG, cfg->mode);
	write(priv, mod->base + HIST_COEFF_R_REG, cfg->coeff[0]);
	write(priv, mod->base + HIST_COEFF_G_REG, cfg->coeff[1]);
	write(priv, mod->base + HIST_COEFF_B_REG, cfg->coeff[2]);

	u32 sample_reg = FIELD_PREP(HIST_SAMPLE_RANGE_SAMPLE_SHIFT_MASK,
				    cfg->sample_shift) |
			 FIELD_PREP(HIST_SAMPLE_RANGE_SAMPLE_OFFSET_MASK,
				    cfg->sample_offs);
	write(priv, mod->base + HIST_SAMPLE_RANGE_REG, sample_reg);

	write(priv, mod->base + HIST_FORCED_UPDATE_REG, 1);

	return 0;
}

static int rppx1_hist_fill_stats(struct rpp_module *mod,
				 union rppx1_stats_block *block)
{
	struct rppx1_hist_stats *stats = &block->hist;

	for (unsigned int i = 0; i < RPPX1_HIST_NUM_BINS; i++)
		stats->hist_bins[i] = rpp_module_read(mod, HIST_BIN_REG(i));

	return 0;
}

const struct rpp_module_ops rppx1_hist_ops = {
	.probe = rppx1_hist_probe,
	.fill_params = rppx1_hist_fill_params,
	.fill_stats = rppx1_hist_fill_stats,
};
