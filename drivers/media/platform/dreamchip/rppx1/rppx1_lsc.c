// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Renesas Electronics Corp.
 * Copyright (C) 2026 Ideas on Board Oy
 * Copyright (C) 2026 Ragnatech AB
 */

#include "rpp_module.h"

#define LSC_VERSION_REG		0x0000

#define LSC_CTRL_REG		0x0004
#define LSC_CTRL_LSC_EN		BIT(0)

#define LSC_R_TABLE_ADDR_REG	0x0008
#define LSC_GR_TABLE_ADDR_REG	0x000c
#define LSC_B_TABLE_ADDR_REG	0x0010
#define LSC_GB_TABLE_ADDR_REG	0x0014
#define LSC_R_TABLE_DATA_REG	0x0018
#define LSC_GR_TABLE_DATA_REG	0x001c
#define LSC_B_TABLE_DATA_REG	0x0020
#define LSC_GB_TABLE_DATA_REG	0x0024
#define LSC_XGRAD_01_REG	0x0028
#define LSC_XGRAD_23_REG	0x002c
#define LSC_XGRAD_45_REG	0x0030
#define LSC_XGRAD_67_REG	0x0034
#define LSC_XGRAD_89_REG	0x0038
#define LSC_XGRAD_1011_REG	0x003c
#define LSC_XGRAD_1213_REG	0x0040
#define LSC_XGRAD_1415_REG	0x0044
#define LSC_YGRAD_01_REG	0x0048
#define LSC_YGRAD_23_REG	0x004c
#define LSC_YGRAD_45_REG	0x0050
#define LSC_YGRAD_67_REG	0x0054
#define LSC_YGRAD_89_REG	0x0058
#define LSC_YGRAD_1011_REG	0x005c
#define LSC_YGRAD_1213_REG	0x0060
#define LSC_YGRAD_1415_REG	0x0064
#define LSC_XSIZE_01_REG	0x0068
#define LSC_XSIZE_23_REG	0x006c
#define LSC_XSIZE_45_REG	0x0070
#define LSC_XSIZE_67_REG	0x0074
#define LSC_XSIZE_89_REG	0x0078
#define LSC_XSIZE_1011_REG	0x007c
#define LSC_XSIZE_1213_REG	0x0080
#define LSC_XSIZE_1415_REG	0x0084
#define LSC_YSIZE_01_REG	0x0088
#define LSC_YSIZE_23_REG	0x008c
#define LSC_YSIZE_45_REG	0x0090
#define LSC_YSIZE_67_REG	0x0094
#define LSC_YSIZE_89_REG	0x0098
#define LSC_YSIZE_1011_REG	0x009c
#define LSC_YSIZE_1213_REG	0x00a0
#define LSC_YSIZE_1415_REG	0x00a4
#define LSC_TABLE_SEL_REG	0x00a8
#define LSC_STATUS_REG		0x00ac

#define LSC_R_TABLE_DATA_VALUE(v1, v2) (((v1) & 0xfff) | (((v2) & 0xfff) << 12))
#define LSC_GRAD_VALUE(v1, v2) (((v1) & 0xfff) | (((v2) & 0xfff) << 16))
#define LSC_SIZE_VALUE(v1, v2) (((v1) & 0x1ff) | (((v2) & 0x1ff) << 16))

static int rppx1_lsc_probe(struct rpp_module *mod)
{
	/* Version check. */
	if (rpp_module_read(mod, LSC_VERSION_REG) != 0x04)
		return -EINVAL;

	return 0;
}

static int
rppx1_lsc_fill_params(struct rpp_module *mod,
		      const union rppx1_params_block *block,
		      rppx1_reg_write write, void *priv)
{
	const struct rppx1_lsc_params *cfg = &block->lsc;
	const __u16 *v;

	/* Always disable module as it needs be disabled before configuring. */
	write(priv, mod->base + LSC_CTRL_REG, 0);
	if (cfg->header.flags & V4L2_ISP_PARAMS_FL_BLOCK_DISABLE)
		return 0;

	/*
	 * Program the color correction sectors.
	 *
	 * There are two tables to one can program and switch between. As the
	 * RPPX1 supports preparing a buffer of commands to be applied later
	 * only use table 0. This works as long as the ISP is not used in
	 * inline-mode.
	 *
	 * For inline-mode support using DMA for configuration is not possible
	 * so this is not an issue, but needs to be address if inline-mode
	 * support is added to the driver.
	 */

	/* Start writing at beginning of table 0. */
	write(priv, mod->base + LSC_R_TABLE_ADDR_REG, 0);
	write(priv, mod->base + LSC_GR_TABLE_ADDR_REG, 0);
	write(priv, mod->base + LSC_B_TABLE_ADDR_REG, 0);
	write(priv, mod->base + LSC_GB_TABLE_ADDR_REG, 0);

	/* Program data tables. */
	for (unsigned int i = 0; i < RPPX1_LSC_SAMPLES_MAX; i++) {
		const __u16 *r = cfg->r_data[i];
		const __u16 *gr = cfg->gr_data[i];
		const __u16 *b = cfg->b_data[i];
		const __u16 *gb = cfg->gb_data[i];
		unsigned int j;

		for (j = 0; j < RPPX1_LSC_SAMPLES_MAX - 1; j += 2) {
			write(priv, mod->base + LSC_R_TABLE_DATA_REG,
			      LSC_R_TABLE_DATA_VALUE(r[j], r[j + 1]));
			write(priv, mod->base + LSC_GR_TABLE_DATA_REG,
			      LSC_R_TABLE_DATA_VALUE(gr[j], gr[j + 1]));
			write(priv, mod->base + LSC_B_TABLE_DATA_REG,
			      LSC_R_TABLE_DATA_VALUE(b[j], b[j + 1]));
			write(priv, mod->base + LSC_GB_TABLE_DATA_REG,
			      LSC_R_TABLE_DATA_VALUE(gb[j], gb[j + 1]));
		}

		write(priv, mod->base + LSC_R_TABLE_DATA_REG,
		      LSC_R_TABLE_DATA_VALUE(r[j], 0));
		write(priv, mod->base + LSC_GR_TABLE_DATA_REG,
		      LSC_R_TABLE_DATA_VALUE(gr[j], 0));
		write(priv, mod->base + LSC_B_TABLE_DATA_REG,
		      LSC_R_TABLE_DATA_VALUE(b[j], 0));
		write(priv, mod->base + LSC_GB_TABLE_DATA_REG,
		      LSC_R_TABLE_DATA_VALUE(gb[j], 0));
	}

	/* Activate table 0. */
	write(priv, mod->base + LSC_TABLE_SEL_REG, 0);

	/*
	 * Program X- and Y- sizes, and gradients.
	 */

	v = cfg->x_grad;
	write(priv, mod->base + LSC_XGRAD_01_REG, LSC_GRAD_VALUE(v[0], v[1]));
	write(priv, mod->base + LSC_XGRAD_23_REG, LSC_GRAD_VALUE(v[2], v[3]));
	write(priv, mod->base + LSC_XGRAD_45_REG, LSC_GRAD_VALUE(v[4], v[5]));
	write(priv, mod->base + LSC_XGRAD_67_REG, LSC_GRAD_VALUE(v[6], v[7]));
	write(priv, mod->base + LSC_XGRAD_89_REG, LSC_GRAD_VALUE(v[8], v[9]));
	write(priv, mod->base + LSC_XGRAD_1011_REG, LSC_GRAD_VALUE(v[10], v[11]));
	write(priv, mod->base + LSC_XGRAD_1213_REG, LSC_GRAD_VALUE(v[12], v[13]));
	write(priv, mod->base + LSC_XGRAD_1415_REG, LSC_GRAD_VALUE(v[14], v[15]));

	v = cfg->y_grad;
	write(priv, mod->base + LSC_YGRAD_01_REG, LSC_GRAD_VALUE(v[0], v[1]));
	write(priv, mod->base + LSC_YGRAD_23_REG, LSC_GRAD_VALUE(v[2], v[3]));
	write(priv, mod->base + LSC_YGRAD_45_REG, LSC_GRAD_VALUE(v[4], v[5]));
	write(priv, mod->base + LSC_YGRAD_67_REG, LSC_GRAD_VALUE(v[6], v[7]));
	write(priv, mod->base + LSC_YGRAD_89_REG, LSC_GRAD_VALUE(v[8], v[9]));
	write(priv, mod->base + LSC_YGRAD_1011_REG, LSC_GRAD_VALUE(v[10], v[11]));
	write(priv, mod->base + LSC_YGRAD_1213_REG, LSC_GRAD_VALUE(v[12], v[13]));
	write(priv, mod->base + LSC_YGRAD_1415_REG, LSC_GRAD_VALUE(v[14], v[15]));

	v = cfg->x_sect_size;
	write(priv, mod->base + LSC_XSIZE_01_REG, LSC_GRAD_VALUE(v[0], v[1]));
	write(priv, mod->base + LSC_XSIZE_23_REG, LSC_GRAD_VALUE(v[2], v[3]));
	write(priv, mod->base + LSC_XSIZE_45_REG, LSC_GRAD_VALUE(v[4], v[5]));
	write(priv, mod->base + LSC_XSIZE_67_REG, LSC_GRAD_VALUE(v[6], v[7]));
	write(priv, mod->base + LSC_XSIZE_89_REG, LSC_GRAD_VALUE(v[8], v[9]));
	write(priv, mod->base + LSC_XSIZE_1011_REG, LSC_GRAD_VALUE(v[10], v[11]));
	write(priv, mod->base + LSC_XSIZE_1213_REG, LSC_GRAD_VALUE(v[12], v[13]));
	write(priv, mod->base + LSC_XSIZE_1415_REG, LSC_GRAD_VALUE(v[14], v[15]));

	v = cfg->y_sect_size;
	write(priv, mod->base + LSC_YSIZE_01_REG, LSC_GRAD_VALUE(v[0], v[1]));
	write(priv, mod->base + LSC_YSIZE_23_REG, LSC_GRAD_VALUE(v[2], v[3]));
	write(priv, mod->base + LSC_YSIZE_45_REG, LSC_GRAD_VALUE(v[4], v[5]));
	write(priv, mod->base + LSC_YSIZE_67_REG, LSC_GRAD_VALUE(v[6], v[7]));
	write(priv, mod->base + LSC_YSIZE_89_REG, LSC_GRAD_VALUE(v[8], v[9]));
	write(priv, mod->base + LSC_YSIZE_1011_REG, LSC_GRAD_VALUE(v[10], v[11]));
	write(priv, mod->base + LSC_YSIZE_1213_REG, LSC_GRAD_VALUE(v[12], v[13]));
	write(priv, mod->base + LSC_YSIZE_1415_REG, LSC_GRAD_VALUE(v[14], v[15]));

	/* Enable module. */
	write(priv, mod->base + LSC_CTRL_REG, LSC_CTRL_LSC_EN);

	return 0;
}

const struct rpp_module_ops rppx1_lsc_ops = {
	.probe = rppx1_lsc_probe,
	.fill_params = rppx1_lsc_fill_params,
};
