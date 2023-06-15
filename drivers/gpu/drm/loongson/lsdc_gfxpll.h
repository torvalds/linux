/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#ifndef __LSDC_GFXPLL_H__
#define __LSDC_GFXPLL_H__

#include <drm/drm_device.h>

struct loongson_gfxpll;

struct loongson_gfxpll_parms {
	unsigned int ref_clock;
	unsigned int div_ref;
	unsigned int loopc;
	unsigned int div_out_dc;
	unsigned int div_out_gmc;
	unsigned int div_out_gpu;
};

struct loongson_gfxpll_funcs {
	int (*init)(struct loongson_gfxpll * const this);

	int (*update)(struct loongson_gfxpll * const this,
		      struct loongson_gfxpll_parms const *pin);

	void (*get_rates)(struct loongson_gfxpll * const this,
			  unsigned int *dc, unsigned int *gmc, unsigned int *gpu);

	void (*print)(struct loongson_gfxpll * const this,
		      struct drm_printer *printer, bool verbose);
};

struct loongson_gfxpll {
	struct drm_device *ddev;
	void __iomem *mmio;

	/* PLL register offset */
	u32 reg_base;
	/* PLL register size in bytes */
	u32 reg_size;

	const struct loongson_gfxpll_funcs *funcs;

	struct loongson_gfxpll_parms parms;
};

int loongson_gfxpll_create(struct drm_device *ddev,
			   struct loongson_gfxpll **ppout);

#endif
