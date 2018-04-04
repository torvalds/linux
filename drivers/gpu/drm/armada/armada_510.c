/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Armada 510 (aka Dove) variant support
 */
#include <linux/clk.h>
#include <linux/io.h>
#include <drm/drm_crtc_helper.h>
#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_hw.h"

static int armada510_crtc_init(struct armada_crtc *dcrtc, struct device *dev)
{
	struct clk *clk;

	clk = devm_clk_get(dev, "ext_ref_clk1");
	if (IS_ERR(clk))
		return PTR_ERR(clk) == -ENOENT ? -EPROBE_DEFER : PTR_ERR(clk);

	dcrtc->extclk[0] = clk;

	/* Lower the watermark so to eliminate jitter at higher bandwidths */
	armada_updatel(0x20, (1 << 11) | 0xff, dcrtc->base + LCD_CFG_RDREG4F);

	return 0;
}

/*
 * Armada510 specific SCLK register selection.
 * This gets called with sclk = NULL to test whether the mode is
 * supportable, and again with sclk != NULL to set the clocks up for
 * that.  The former can return an error, but the latter is expected
 * not to.
 *
 * We currently are pretty rudimentary here, always selecting
 * EXT_REF_CLK_1 for LCD0 and erroring LCD1.  This needs improvement!
 */
static int armada510_crtc_compute_clock(struct armada_crtc *dcrtc,
	const struct drm_display_mode *mode, uint32_t *sclk)
{
	struct clk *clk = dcrtc->extclk[0];
	int ret;

	if (dcrtc->num == 1)
		return -EINVAL;

	if (IS_ERR(clk))
		return PTR_ERR(clk);

	if (dcrtc->clk != clk) {
		ret = clk_prepare_enable(clk);
		if (ret)
			return ret;
		dcrtc->clk = clk;
	}

	if (sclk) {
		uint32_t rate, ref, div;

		rate = mode->clock * 1000;
		ref = clk_round_rate(clk, rate);
		div = DIV_ROUND_UP(ref, rate);
		if (div < 1)
			div = 1;

		clk_set_rate(clk, ref);
		*sclk = div | SCLK_510_EXTCLK1;
	}

	return 0;
}

const struct armada_variant armada510_ops = {
	.has_spu_adv_reg = true,
	.spu_adv_reg = ADV_HWC32ENABLE | ADV_HWC32ARGB | ADV_HWC32BLEND,
	.init = armada510_crtc_init,
	.compute_clock = armada510_crtc_compute_clock,
};
