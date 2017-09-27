/*
 * rcar_du_lvdsenc.c  --  R-Car Display Unit LVDS Encoder
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_lvdsenc.h"
#include "rcar_lvds_regs.h"

struct rcar_du_lvdsenc {
	struct rcar_du_device *dev;

	unsigned int index;
	void __iomem *mmio;
	struct clk *clock;
	bool enabled;

	enum rcar_lvds_input input;
	enum rcar_lvds_mode mode;
};

static void rcar_lvds_write(struct rcar_du_lvdsenc *lvds, u32 reg, u32 data)
{
	iowrite32(data, lvds->mmio + reg);
}

static void rcar_du_lvdsenc_start_gen2(struct rcar_du_lvdsenc *lvds,
				       struct rcar_du_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.mode;
	unsigned int freq = mode->clock;
	u32 lvdcr0;
	u32 pllcr;

	/* PLL clock configuration */
	if (freq < 39000)
		pllcr = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_38M;
	else if (freq < 61000)
		pllcr = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_60M;
	else if (freq < 121000)
		pllcr = LVDPLLCR_CEEN | LVDPLLCR_COSEL | LVDPLLCR_PLLDLYCNT_121M;
	else
		pllcr = LVDPLLCR_PLLDLYCNT_150M;

	rcar_lvds_write(lvds, LVDPLLCR, pllcr);

	/*
	 * Select the input, hardcode mode 0, enable LVDS operation and turn
	 * bias circuitry on.
	 */
	lvdcr0 = (lvds->mode << LVDCR0_LVMD_SHIFT) | LVDCR0_BEN | LVDCR0_LVEN;
	if (rcrtc->index == 2)
		lvdcr0 |= LVDCR0_DUSEL;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds, LVDCR1,
			LVDCR1_CHSTBY_GEN2(3) | LVDCR1_CHSTBY_GEN2(2) |
			LVDCR1_CHSTBY_GEN2(1) | LVDCR1_CHSTBY_GEN2(0) |
			LVDCR1_CLKSTBY_GEN2);

	/*
	 * Turn the PLL on, wait for the startup delay, and turn the output
	 * on.
	 */
	lvdcr0 |= LVDCR0_PLLON;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	usleep_range(100, 150);

	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);
}

static void rcar_du_lvdsenc_start_gen3(struct rcar_du_lvdsenc *lvds,
				       struct rcar_du_crtc *rcrtc)
{
	const struct drm_display_mode *mode = &rcrtc->crtc.mode;
	unsigned int freq = mode->clock;
	u32 lvdcr0;
	u32 pllcr;

	/* PLL clock configuration */
	if (freq < 42000)
		pllcr = LVDPLLCR_PLLDIVCNT_42M;
	else if (freq < 85000)
		pllcr = LVDPLLCR_PLLDIVCNT_85M;
	else if (freq < 128000)
		pllcr = LVDPLLCR_PLLDIVCNT_128M;
	else
		pllcr = LVDPLLCR_PLLDIVCNT_148M;

	rcar_lvds_write(lvds, LVDPLLCR, pllcr);

	/* Turn all the channels on. */
	rcar_lvds_write(lvds, LVDCR1,
			LVDCR1_CHSTBY_GEN3(3) | LVDCR1_CHSTBY_GEN3(2) |
			LVDCR1_CHSTBY_GEN3(1) | LVDCR1_CHSTBY_GEN3(0) |
			LVDCR1_CLKSTBY_GEN3);

	/*
	 * Turn the PLL on, set it to LVDS normal mode, wait for the startup
	 * delay and turn the output on.
	 */
	lvdcr0 = (lvds->mode << LVDCR0_LVMD_SHIFT) | LVDCR0_PLLON;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	lvdcr0 |= LVDCR0_PWD;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);

	usleep_range(100, 150);

	lvdcr0 |= LVDCR0_LVRES;
	rcar_lvds_write(lvds, LVDCR0, lvdcr0);
}

static int rcar_du_lvdsenc_start(struct rcar_du_lvdsenc *lvds,
				 struct rcar_du_crtc *rcrtc)
{
	u32 lvdhcr;
	int ret;

	if (lvds->enabled)
		return 0;

	ret = clk_prepare_enable(lvds->clock);
	if (ret < 0)
		return ret;

	/*
	 * Hardcode the channels and control signals routing for now.
	 *
	 * HSYNC -> CTRL0
	 * VSYNC -> CTRL1
	 * DISP  -> CTRL2
	 * 0     -> CTRL3
	 */
	rcar_lvds_write(lvds, LVDCTRCR, LVDCTRCR_CTR3SEL_ZERO |
			LVDCTRCR_CTR2SEL_DISP | LVDCTRCR_CTR1SEL_VSYNC |
			LVDCTRCR_CTR0SEL_HSYNC);

	if (rcar_du_needs(lvds->dev, RCAR_DU_QUIRK_LVDS_LANES))
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 3)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 1);
	else
		lvdhcr = LVDCHCR_CHSEL_CH(0, 0) | LVDCHCR_CHSEL_CH(1, 1)
		       | LVDCHCR_CHSEL_CH(2, 2) | LVDCHCR_CHSEL_CH(3, 3);

	rcar_lvds_write(lvds, LVDCHCR, lvdhcr);

	/* Perform generation-specific initialization. */
	if (lvds->dev->info->gen < 3)
		rcar_du_lvdsenc_start_gen2(lvds, rcrtc);
	else
		rcar_du_lvdsenc_start_gen3(lvds, rcrtc);

	lvds->enabled = true;

	return 0;
}

static void rcar_du_lvdsenc_stop(struct rcar_du_lvdsenc *lvds)
{
	if (!lvds->enabled)
		return;

	rcar_lvds_write(lvds, LVDCR0, 0);
	rcar_lvds_write(lvds, LVDCR1, 0);

	clk_disable_unprepare(lvds->clock);

	lvds->enabled = false;
}

int rcar_du_lvdsenc_enable(struct rcar_du_lvdsenc *lvds, struct drm_crtc *crtc,
			   bool enable)
{
	if (!enable) {
		rcar_du_lvdsenc_stop(lvds);
		return 0;
	} else if (crtc) {
		struct rcar_du_crtc *rcrtc = to_rcar_crtc(crtc);
		return rcar_du_lvdsenc_start(lvds, rcrtc);
	} else
		return -EINVAL;
}

void rcar_du_lvdsenc_atomic_check(struct rcar_du_lvdsenc *lvds,
				  struct drm_display_mode *mode)
{
	struct rcar_du_device *rcdu = lvds->dev;

	/*
	 * The internal LVDS encoder has a restricted clock frequency operating
	 * range (30MHz to 150MHz on Gen2, 25.175MHz to 148.5MHz on Gen3). Clamp
	 * the clock accordingly.
	 */
	if (rcdu->info->gen < 3)
		mode->clock = clamp(mode->clock, 30000, 150000);
	else
		mode->clock = clamp(mode->clock, 25175, 148500);
}

void rcar_du_lvdsenc_set_mode(struct rcar_du_lvdsenc *lvds,
			      enum rcar_lvds_mode mode)
{
	lvds->mode = mode;
}

static int rcar_du_lvdsenc_get_resources(struct rcar_du_lvdsenc *lvds,
					 struct platform_device *pdev)
{
	struct resource *mem;
	char name[7];

	sprintf(name, "lvds.%u", lvds->index);

	mem = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	lvds->mmio = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(lvds->mmio))
		return PTR_ERR(lvds->mmio);

	lvds->clock = devm_clk_get(&pdev->dev, name);
	if (IS_ERR(lvds->clock)) {
		dev_err(&pdev->dev, "failed to get clock for %s\n", name);
		return PTR_ERR(lvds->clock);
	}

	return 0;
}

int rcar_du_lvdsenc_init(struct rcar_du_device *rcdu)
{
	struct platform_device *pdev = to_platform_device(rcdu->dev);
	struct rcar_du_lvdsenc *lvds;
	unsigned int i;
	int ret;

	for (i = 0; i < rcdu->info->num_lvds; ++i) {
		lvds = devm_kzalloc(&pdev->dev, sizeof(*lvds), GFP_KERNEL);
		if (lvds == NULL)
			return -ENOMEM;

		lvds->dev = rcdu;
		lvds->index = i;
		lvds->input = i ? RCAR_LVDS_INPUT_DU1 : RCAR_LVDS_INPUT_DU0;
		lvds->enabled = false;

		ret = rcar_du_lvdsenc_get_resources(lvds, pdev);
		if (ret < 0)
			return ret;

		rcdu->lvds[i] = lvds;
	}

	return 0;
}
