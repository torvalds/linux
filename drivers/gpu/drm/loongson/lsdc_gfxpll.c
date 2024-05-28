// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/delay.h>

#include <drm/drm_file.h>
#include <drm/drm_managed.h>
#include <drm/drm_print.h>

#include "lsdc_drv.h"

/*
 * GFX PLL is the PLL used by DC, GMC and GPU, the structure of the GFX PLL
 * may suffer from change across chip variants.
 *
 *
 *                                            +-------------+  sel_out_dc
 *                                       +----| / div_out_0 | _____/ _____ DC
 *                                       |    +-------------+
 * refclk   +---------+      +-------+   |    +-------------+  sel_out_gmc
 * ---+---> | div_ref | ---> | loopc | --+--> | / div_out_1 | _____/ _____ GMC
 *    |     +---------+      +-------+   |    +-------------+
 *    |          /               *       |    +-------------+  sel_out_gpu
 *    |                                  +----| / div_out_2 | _____/ _____ GPU
 *    |                                       +-------------+
 *    |                                                         ^
 *    |                                                         |
 *    +--------------------------- bypass ----------------------+
 */

struct loongson_gfxpll_bitmap {
	/* Byte 0 ~ Byte 3 */
	unsigned div_out_dc    : 7;  /*  6 : 0    DC output clock divider  */
	unsigned div_out_gmc   : 7;  /* 13 : 7    GMC output clock divider */
	unsigned div_out_gpu   : 7;  /* 20 : 14   GPU output clock divider */
	unsigned loopc         : 9;  /* 29 : 21   clock multiplier         */
	unsigned _reserved_1_  : 2;  /* 31 : 30                            */

	/* Byte 4 ~ Byte 7 */
	unsigned div_ref       : 7;   /* 38 : 32   Input clock divider    */
	unsigned locked        : 1;   /* 39        PLL locked indicator   */
	unsigned sel_out_dc    : 1;   /* 40        dc output clk enable   */
	unsigned sel_out_gmc   : 1;   /* 41        gmc output clk enable  */
	unsigned sel_out_gpu   : 1;   /* 42        gpu output clk enable  */
	unsigned set_param     : 1;   /* 43        Trigger the update     */
	unsigned bypass        : 1;   /* 44                               */
	unsigned powerdown     : 1;   /* 45                               */
	unsigned _reserved_2_  : 18;  /* 46 : 63   no use                 */
};

union loongson_gfxpll_reg_bitmap {
	struct loongson_gfxpll_bitmap bitmap;
	u32 w[2];
	u64 d;
};

static void __gfxpll_rreg(struct loongson_gfxpll *this,
			  union loongson_gfxpll_reg_bitmap *reg)
{
#if defined(CONFIG_64BIT)
	reg->d = readq(this->mmio);
#else
	reg->w[0] = readl(this->mmio);
	reg->w[1] = readl(this->mmio + 4);
#endif
}

/* Update new parameters to the hardware */

static int loongson_gfxpll_update(struct loongson_gfxpll * const this,
				  struct loongson_gfxpll_parms const *pin)
{
	/* None, TODO */

	return 0;
}

static void loongson_gfxpll_get_rates(struct loongson_gfxpll * const this,
				      unsigned int *dc,
				      unsigned int *gmc,
				      unsigned int *gpu)
{
	struct loongson_gfxpll_parms *pparms = &this->parms;
	union loongson_gfxpll_reg_bitmap gfxpll_reg;
	unsigned int pre_output;
	unsigned int dc_mhz;
	unsigned int gmc_mhz;
	unsigned int gpu_mhz;

	__gfxpll_rreg(this, &gfxpll_reg);

	pparms->div_ref = gfxpll_reg.bitmap.div_ref;
	pparms->loopc = gfxpll_reg.bitmap.loopc;

	pparms->div_out_dc = gfxpll_reg.bitmap.div_out_dc;
	pparms->div_out_gmc = gfxpll_reg.bitmap.div_out_gmc;
	pparms->div_out_gpu = gfxpll_reg.bitmap.div_out_gpu;

	pre_output = pparms->ref_clock / pparms->div_ref * pparms->loopc;

	dc_mhz = pre_output / pparms->div_out_dc / 1000;
	gmc_mhz = pre_output / pparms->div_out_gmc / 1000;
	gpu_mhz = pre_output / pparms->div_out_gpu / 1000;

	if (dc)
		*dc = dc_mhz;

	if (gmc)
		*gmc = gmc_mhz;

	if (gpu)
		*gpu = gpu_mhz;
}

static void loongson_gfxpll_print(struct loongson_gfxpll * const this,
				  struct drm_printer *p,
				  bool verbose)
{
	struct loongson_gfxpll_parms *parms = &this->parms;
	unsigned int dc, gmc, gpu;

	if (verbose) {
		drm_printf(p, "reference clock: %u\n", parms->ref_clock);
		drm_printf(p, "div_ref = %u\n", parms->div_ref);
		drm_printf(p, "loopc = %u\n", parms->loopc);

		drm_printf(p, "div_out_dc = %u\n", parms->div_out_dc);
		drm_printf(p, "div_out_gmc = %u\n", parms->div_out_gmc);
		drm_printf(p, "div_out_gpu = %u\n", parms->div_out_gpu);
	}

	this->funcs->get_rates(this, &dc, &gmc, &gpu);

	drm_printf(p, "dc: %uMHz, gmc: %uMHz, gpu: %uMHz\n", dc, gmc, gpu);
}

/* GFX (DC, GPU, GMC) PLL initialization and destroy function */

static void loongson_gfxpll_fini(struct drm_device *ddev, void *data)
{
	struct loongson_gfxpll *this = (struct loongson_gfxpll *)data;

	iounmap(this->mmio);

	kfree(this);
}

static int loongson_gfxpll_init(struct loongson_gfxpll * const this)
{
	struct loongson_gfxpll_parms *pparms = &this->parms;
	struct drm_printer printer = drm_info_printer(this->ddev->dev);

	pparms->ref_clock = LSDC_PLL_REF_CLK_KHZ;

	this->mmio = ioremap(this->reg_base, this->reg_size);
	if (IS_ERR_OR_NULL(this->mmio))
		return -ENOMEM;

	this->funcs->print(this, &printer, false);

	return 0;
}

static const struct loongson_gfxpll_funcs lsdc_gmc_gpu_funcs = {
	.init = loongson_gfxpll_init,
	.update = loongson_gfxpll_update,
	.get_rates = loongson_gfxpll_get_rates,
	.print = loongson_gfxpll_print,
};

int loongson_gfxpll_create(struct drm_device *ddev,
			   struct loongson_gfxpll **ppout)
{
	struct lsdc_device *ldev = to_lsdc(ddev);
	const struct loongson_gfx_desc *gfx = to_loongson_gfx(ldev->descp);
	struct loongson_gfxpll *this;
	int ret;

	this = kzalloc(sizeof(*this), GFP_KERNEL);
	if (IS_ERR_OR_NULL(this))
		return -ENOMEM;

	this->ddev = ddev;
	this->reg_size = gfx->gfxpll.reg_size;
	this->reg_base = gfx->conf_reg_base + gfx->gfxpll.reg_offset;
	this->funcs = &lsdc_gmc_gpu_funcs;

	ret = this->funcs->init(this);
	if (unlikely(ret)) {
		kfree(this);
		return ret;
	}

	*ppout = this;

	return drmm_add_action_or_reset(ddev, loongson_gfxpll_fini, this);
}
