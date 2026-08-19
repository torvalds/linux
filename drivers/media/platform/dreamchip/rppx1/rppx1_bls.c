// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"
#include "rppx1.h"

#define BLS_VERSION_REG				0x0000

#define BLS_CTRL_REG				0x0004
#define BLS_CTRL_BLS_WIN2			BIT(3)
#define BLS_CTRL_BLS_WIN1			BIT(2)
#define BLS_CTRL_BLS_MODE_MEASURED		BIT(1)
#define BLS_CTRL_BLS_EN				BIT(0)

#define BLS_SAMPLES_REG				0x0008
#define BLS_H1_START_REG			0x000c
#define BLS_H1_STOP_REG				0x0010
#define BLS_V1_START_REG			0x0014
#define BLS_V1_STOP_REG				0x0018
#define BLS_H2_START_REG			0x001c
#define BLS_H2_STOP_REG				0x0020
#define BLS_V2_START_REG			0x0024
#define BLS_V2_STOP_REG				0x0028
#define BLS_A_FIXED_REG				0x002c
#define BLS_B_FIXED_REG				0x0030
#define BLS_C_FIXED_REG				0x0034
#define BLS_D_FIXED_REG				0x0038
#define BLS_A_MEASURED_REG			0x003c
#define BLS_B_MEASURED_REG			0x0040
#define BLS_C_MEASURED_REG			0x0044
#define BLS_D_MEASURED_REG			0x0048
#define BLS_PRE1_FIXED_MASK			GENMASK(24, 0)
#define BLS_PRE2_FIXED_MASK			GENMASK(12, 0)

static int rppx1_bls_probe(struct rpp_module *mod)
{
	/* Version check. */
	switch (rpp_module_read(mod, BLS_VERSION_REG)) {
	case 3:
	case 5:
		/* 12-bit. */
		break;
	case 2:
	case 4:
		/* 20-bit. */
		break;
	case 6:
		/* 24-bit. */
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void
rppx1_bls_swap_regs(struct rpp_module *mod, const u32 input[4], u32 output[4])
{
	static const unsigned int swap[4][4] = {
		[RPP_RGGB] = { 0, 1, 2, 3 },
		[RPP_GRBG] = { 1, 0, 3, 2 },
		[RPP_GBRG] = { 2, 3, 0, 1 },
		[RPP_BGGR] = { 3, 2, 1, 0 },
	};

	/* Swap to pattern used in our path, PRE1 or PRE2. */
	struct rpp_module *acq = mod == &mod->rpp->pre1.bls ?
		&mod->rpp->pre1.acq : &mod->rpp->pre2.bls;
	enum rpp_raw_pattern pattern = acq->info.acq.raw_pattern;

	for (unsigned int i = 0; i < 4; ++i)
		output[i] = input[swap[pattern][i]];
}

static int
rppx1_bls_fill_params(struct rpp_module *mod,
		      const union rppx1_params_block *block,
		      rppx1_reg_write write, void *priv)
{
	const struct rppx1_bls_params *cfg = &block->bls;

	/* If the modules is disabled, simply bypass it. */
	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE) {
		write(priv, mod->base + BLS_CTRL_REG, 0);
		return 0;
	}

	u32 ctrl = BLS_CTRL_BLS_EN;

	if (cfg->mode == RPPX1_BLS_MODE_FIXED) {
		static const u32 regs[] = {
			BLS_A_FIXED_REG,
			BLS_B_FIXED_REG,
			BLS_C_FIXED_REG,
			BLS_D_FIXED_REG,
		};
		u32 swapped[4];

		rppx1_bls_swap_regs(mod, regs, swapped);

		/*
		 * The PRE1 pipe fixed values are 24-bits + 1 sign bit, while
		 * the PRE2 pipe values are 12-bits + 1 sign bit.
		 */
		u32 mask;

		switch (cfg->header.type) {
		case RPPX1_PARAMS_BLOCK_TYPE_BLS_PRE1:
			mask = BLS_PRE1_FIXED_MASK;
			break;
		case RPPX1_PARAMS_BLOCK_TYPE_BLS_PRE2:
			mask = BLS_PRE2_FIXED_MASK;
			break;
		default:
			return -EINVAL;
		}

		write(priv, mod->base + swapped[0], cfg->fixed.a & mask);
		write(priv, mod->base + swapped[1], cfg->fixed.b & mask);
		write(priv, mod->base + swapped[2], cfg->fixed.c & mask);
		write(priv, mod->base + swapped[3], cfg->fixed.d & mask);
	} else {
		write(priv, mod->base + BLS_SAMPLES_REG, cfg->samples);

		if (cfg->en_windows & RPPX1_BLS_WIN_EN_WIN1) {
			write(priv, mod->base + BLS_H1_START_REG, cfg->window1.h_offs);
			write(priv, mod->base + BLS_H1_STOP_REG, cfg->window1.h_size);
			write(priv, mod->base + BLS_V1_START_REG, cfg->window1.v_offs);
			write(priv, mod->base + BLS_V1_STOP_REG, cfg->window1.v_size);
			ctrl |= BLS_CTRL_BLS_WIN1;
		}

		if (cfg->en_windows & RPPX1_BLS_WIN_EN_WIN2) {
			write(priv, mod->base + BLS_H2_START_REG, cfg->window2.h_offs);
			write(priv, mod->base + BLS_H2_STOP_REG, cfg->window2.h_size);
			write(priv, mod->base + BLS_V2_START_REG, cfg->window2.v_offs);
			write(priv, mod->base + BLS_V2_STOP_REG, cfg->window2.v_size);
			ctrl |= BLS_CTRL_BLS_WIN2;
		}

		ctrl |= BLS_CTRL_BLS_MODE_MEASURED;
	}

	write(priv, mod->base + BLS_CTRL_REG, ctrl);

	return 0;
}

const struct rpp_module_ops rppx1_bls_ops = {
	.probe = rppx1_bls_probe,
	.fill_params = rppx1_bls_fill_params,
};
