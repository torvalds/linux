// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright Â© 2006-2011 Intel Corporation
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 */

#include <linux/delay.h>
#include <linux/i2c.h>

#include <drm/drm_plane_helper.h>

#include "framebuffer.h"
#include "gma_display.h"
#include "power.h"
#include "psb_drv.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"

#define INTEL_LIMIT_I9XX_SDVO_DAC   0
#define INTEL_LIMIT_I9XX_LVDS	    1

static const struct gma_limit_t psb_intel_limits[] = {
	{			/* INTEL_LIMIT_I9XX_SDVO_DAC */
	 .dot = {.min = 20000, .max = 400000},
	 .vco = {.min = 1400000, .max = 2800000},
	 .n = {.min = 1, .max = 6},
	 .m = {.min = 70, .max = 120},
	 .m1 = {.min = 8, .max = 18},
	 .m2 = {.min = 3, .max = 7},
	 .p = {.min = 5, .max = 80},
	 .p1 = {.min = 1, .max = 8},
	 .p2 = {.dot_limit = 200000, .p2_slow = 10, .p2_fast = 5},
	 .find_pll = gma_find_best_pll,
	 },
	{			/* INTEL_LIMIT_I9XX_LVDS */
	 .dot = {.min = 20000, .max = 400000},
	 .vco = {.min = 1400000, .max = 2800000},
	 .n = {.min = 1, .max = 6},
	 .m = {.min = 70, .max = 120},
	 .m1 = {.min = 8, .max = 18},
	 .m2 = {.min = 3, .max = 7},
	 .p = {.min = 7, .max = 98},
	 .p1 = {.min = 1, .max = 8},
	 /* The single-channel range is 25-112Mhz, and dual-channel
	  * is 80-224Mhz.  Prefer single channel as much as possible.
	  */
	 .p2 = {.dot_limit = 112000, .p2_slow = 14, .p2_fast = 7},
	 .find_pll = gma_find_best_pll,
	 },
};

static const struct gma_limit_t *psb_intel_limit(struct drm_crtc *crtc,
						 int refclk)
{
	const struct gma_limit_t *limit;

	if (gma_pipe_has_type(crtc, INTEL_OUTPUT_LVDS))
		limit = &psb_intel_limits[INTEL_LIMIT_I9XX_LVDS];
	else
		limit = &psb_intel_limits[INTEL_LIMIT_I9XX_SDVO_DAC];
	return limit;
}

static void psb_intel_clock(int refclk, struct gma_clock_t *clock)
{
	clock->m = 5 * (clock->m1 + 2) + (clock->m2 + 2);
	clock->p = clock->p1 * clock->p2;
	clock->vco = refclk * clock->m / (clock->n + 2);
	clock->dot = clock->vco / clock->p;
}

/**
 * Return the pipe currently connected to the panel fitter,
 * or -1 if the panel fitter is not present or not in use
 */
static int psb_intel_panel_fitter_pipe(struct drm_device *dev)
{
	u32 pfit_control;

	pfit_control = REG_READ(PFIT_CONTROL);

	/* See if the panel fitter is in use */
	if ((pfit_control & PFIT_ENABLE) == 0)
		return -1;
	/* Must be on PIPE 1 for PSB */
	return 1;
}

static int psb_intel_crtc_mode_set(struct drm_crtc *crtc,
			       struct drm_display_mode *mode,
			       struct drm_display_mode *adjusted_mode,
			       int x, int y,
			       struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	const struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	int pipe = gma_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	int refclk;
	struct gma_clock_t clock;
	u32 dpll = 0, fp = 0, dspcntr, pipeconf;
	bool ok, is_sdvo = false;
	bool is_lvds = false, is_tv = false;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *connector;
	const struct gma_limit_t *limit;

	/* No scan out no play */
	if (crtc->primary->fb == NULL) {
		crtc_funcs->mode_set_base(crtc, x, y, old_fb);
		return 0;
	}

	list_for_each_entry(connector, &mode_config->connector_list, head) {
		struct gma_encoder *gma_encoder = gma_attached_encoder(connector);

		if (!connector->encoder
		    || connector->encoder->crtc != crtc)
			continue;

		switch (gma_encoder->type) {
		case INTEL_OUTPUT_LVDS:
			is_lvds = true;
			break;
		case INTEL_OUTPUT_SDVO:
			is_sdvo = true;
			break;
		case INTEL_OUTPUT_TVOUT:
			is_tv = true;
			break;
		}
	}

	refclk = 96000;

	limit = gma_crtc->clock_funcs->limit(crtc, refclk);

	ok = limit->find_pll(limit, crtc, adjusted_mode->clock, refclk,
				 &clock);
	if (!ok) {
		DRM_ERROR("Couldn't find PLL settings for mode! target: %d, actual: %d",
			  adjusted_mode->clock, clock.dot);
		return 0;
	}

	fp = clock.n << 16 | clock.m1 << 8 | clock.m2;

	dpll = DPLL_VGA_MODE_DIS;
	if (is_lvds) {
		dpll |= DPLLB_MODE_LVDS;
		dpll |= DPLL_DVO_HIGH_SPEED;
	} else
		dpll |= DPLLB_MODE_DAC_SERIAL;
	if (is_sdvo) {
		int sdvo_pixel_multiply =
			    adjusted_mode->clock / mode->clock;
		dpll |= DPLL_DVO_HIGH_SPEED;
		dpll |=
		    (sdvo_pixel_multiply - 1) << SDVO_MULTIPLIER_SHIFT_HIRES;
	}

	/* compute bitmask from p1 value */
	dpll |= (1 << (clock.p1 - 1)) << 16;
	switch (clock.p2) {
	case 5:
		dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_5;
		break;
	case 7:
		dpll |= DPLLB_LVDS_P2_CLOCK_DIV_7;
		break;
	case 10:
		dpll |= DPLL_DAC_SERIAL_P2_CLOCK_DIV_10;
		break;
	case 14:
		dpll |= DPLLB_LVDS_P2_CLOCK_DIV_14;
		break;
	}

	if (is_tv) {
		/* XXX: just matching BIOS for now */
/*	dpll |= PLL_REF_INPUT_TVCLKINBC; */
		dpll |= 3;
	}
	dpll |= PLL_REF_INPUT_DREFCLK;

	/* setup pipeconf */
	pipeconf = REG_READ(map->conf);

	/* Set up the display plane register */
	dspcntr = DISPPLANE_GAMMA_ENABLE;

	if (pipe == 0)
		dspcntr |= DISPPLANE_SEL_PIPE_A;
	else
		dspcntr |= DISPPLANE_SEL_PIPE_B;

	dspcntr |= DISPLAY_PLANE_ENABLE;
	pipeconf |= PIPEACONF_ENABLE;
	dpll |= DPLL_VCO_ENABLE;


	/* Disable the panel fitter if it was on our pipe */
	if (psb_intel_panel_fitter_pipe(dev) == pipe)
		REG_WRITE(PFIT_CONTROL, 0);

	drm_mode_debug_printmodeline(mode);

	if (dpll & DPLL_VCO_ENABLE) {
		REG_WRITE(map->fp0, fp);
		REG_WRITE(map->dpll, dpll & ~DPLL_VCO_ENABLE);
		REG_READ(map->dpll);
		udelay(150);
	}

	/* The LVDS pin pair needs to be on before the DPLLs are enabled.
	 * This is an exception to the general rule that mode_set doesn't turn
	 * things on.
	 */
	if (is_lvds) {
		u32 lvds = REG_READ(LVDS);

		lvds &= ~LVDS_PIPEB_SELECT;
		if (pipe == 1)
			lvds |= LVDS_PIPEB_SELECT;

		lvds |= LVDS_PORT_EN | LVDS_A0A2_CLKA_POWER_UP;
		/* Set the B0-B3 data pairs corresponding to
		 * whether we're going to
		 * set the DPLLs for dual-channel mode or not.
		 */
		lvds &= ~(LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP);
		if (clock.p2 == 7)
			lvds |= LVDS_B0B3_POWER_UP | LVDS_CLKB_POWER_UP;

		/* It would be nice to set 24 vs 18-bit mode (LVDS_A3_POWER_UP)
		 * appropriately here, but we need to look more
		 * thoroughly into how panels behave in the two modes.
		 */

		REG_WRITE(LVDS, lvds);
		REG_READ(LVDS);
	}

	REG_WRITE(map->fp0, fp);
	REG_WRITE(map->dpll, dpll);
	REG_READ(map->dpll);
	/* Wait for the clocks to stabilize. */
	udelay(150);

	/* write it again -- the BIOS does, after all */
	REG_WRITE(map->dpll, dpll);

	REG_READ(map->dpll);
	/* Wait for the clocks to stabilize. */
	udelay(150);

	REG_WRITE(map->htotal, (adjusted_mode->crtc_hdisplay - 1) |
		  ((adjusted_mode->crtc_htotal - 1) << 16));
	REG_WRITE(map->hblank, (adjusted_mode->crtc_hblank_start - 1) |
		  ((adjusted_mode->crtc_hblank_end - 1) << 16));
	REG_WRITE(map->hsync, (adjusted_mode->crtc_hsync_start - 1) |
		  ((adjusted_mode->crtc_hsync_end - 1) << 16));
	REG_WRITE(map->vtotal, (adjusted_mode->crtc_vdisplay - 1) |
		  ((adjusted_mode->crtc_vtotal - 1) << 16));
	REG_WRITE(map->vblank, (adjusted_mode->crtc_vblank_start - 1) |
		  ((adjusted_mode->crtc_vblank_end - 1) << 16));
	REG_WRITE(map->vsync, (adjusted_mode->crtc_vsync_start - 1) |
		  ((adjusted_mode->crtc_vsync_end - 1) << 16));
	/* pipesrc and dspsize control the size that is scaled from,
	 * which should always be the user's requested size.
	 */
	REG_WRITE(map->size,
		  ((mode->vdisplay - 1) << 16) | (mode->hdisplay - 1));
	REG_WRITE(map->pos, 0);
	REG_WRITE(map->src,
		  ((mode->hdisplay - 1) << 16) | (mode->vdisplay - 1));
	REG_WRITE(map->conf, pipeconf);
	REG_READ(map->conf);

	gma_wait_for_vblank(dev);

	REG_WRITE(map->cntr, dspcntr);

	/* Flush the plane changes */
	crtc_funcs->mode_set_base(crtc, x, y, old_fb);

	gma_wait_for_vblank(dev);

	return 0;
}

/* Returns the clock of the currently programmed mode of the given pipe. */
static int psb_intel_crtc_clock_get(struct drm_device *dev,
				struct drm_crtc *crtc)
{
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	struct drm_psb_private *dev_priv = dev->dev_private;
	int pipe = gma_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	u32 dpll;
	u32 fp;
	struct gma_clock_t clock;
	bool is_lvds;
	struct psb_pipe *p = &dev_priv->regs.pipe[pipe];

	if (gma_power_begin(dev, false)) {
		dpll = REG_READ(map->dpll);
		if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
			fp = REG_READ(map->fp0);
		else
			fp = REG_READ(map->fp1);
		is_lvds = (pipe == 1) && (REG_READ(LVDS) & LVDS_PORT_EN);
		gma_power_end(dev);
	} else {
		dpll = p->dpll;

		if ((dpll & DISPLAY_RATE_SELECT_FPA1) == 0)
			fp = p->fp0;
		else
		        fp = p->fp1;

		is_lvds = (pipe == 1) && (dev_priv->regs.psb.saveLVDS &
								LVDS_PORT_EN);
	}

	clock.m1 = (fp & FP_M1_DIV_MASK) >> FP_M1_DIV_SHIFT;
	clock.m2 = (fp & FP_M2_DIV_MASK) >> FP_M2_DIV_SHIFT;
	clock.n = (fp & FP_N_DIV_MASK) >> FP_N_DIV_SHIFT;

	if (is_lvds) {
		clock.p1 =
		    ffs((dpll &
			 DPLL_FPA01_P1_POST_DIV_MASK_I830_LVDS) >>
			DPLL_FPA01_P1_POST_DIV_SHIFT);
		clock.p2 = 14;

		if ((dpll & PLL_REF_INPUT_MASK) ==
		    PLLB_REF_INPUT_SPREADSPECTRUMIN) {
			/* XXX: might not be 66MHz */
			psb_intel_clock(66000, &clock);
		} else
			psb_intel_clock(48000, &clock);
	} else {
		if (dpll & PLL_P1_DIVIDE_BY_TWO)
			clock.p1 = 2;
		else {
			clock.p1 =
			    ((dpll &
			      DPLL_FPA01_P1_POST_DIV_MASK_I830) >>
			     DPLL_FPA01_P1_POST_DIV_SHIFT) + 2;
		}
		if (dpll & PLL_P2_DIVIDE_BY_4)
			clock.p2 = 4;
		else
			clock.p2 = 2;

		psb_intel_clock(48000, &clock);
	}

	/* XXX: It would be nice to validate the clocks, but we can't reuse
	 * i830PllIsValid() because it relies on the xf86_config connector
	 * configuration being accurate, which it isn't necessarily.
	 */

	return clock.dot;
}

/** Returns the currently programmed mode of the given pipe. */
struct drm_display_mode *psb_intel_crtc_mode_get(struct drm_device *dev,
					     struct drm_crtc *crtc)
{
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	int pipe = gma_crtc->pipe;
	struct drm_display_mode *mode;
	int htot;
	int hsync;
	int vtot;
	int vsync;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_pipe *p = &dev_priv->regs.pipe[pipe];
	const struct psb_offset *map = &dev_priv->regmap[pipe];

	if (gma_power_begin(dev, false)) {
		htot = REG_READ(map->htotal);
		hsync = REG_READ(map->hsync);
		vtot = REG_READ(map->vtotal);
		vsync = REG_READ(map->vsync);
		gma_power_end(dev);
	} else {
		htot = p->htotal;
		hsync = p->hsync;
		vtot = p->vtotal;
		vsync = p->vsync;
	}

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return NULL;

	mode->clock = psb_intel_crtc_clock_get(dev, crtc);
	mode->hdisplay = (htot & 0xffff) + 1;
	mode->htotal = ((htot & 0xffff0000) >> 16) + 1;
	mode->hsync_start = (hsync & 0xffff) + 1;
	mode->hsync_end = ((hsync & 0xffff0000) >> 16) + 1;
	mode->vdisplay = (vtot & 0xffff) + 1;
	mode->vtotal = ((vtot & 0xffff0000) >> 16) + 1;
	mode->vsync_start = (vsync & 0xffff) + 1;
	mode->vsync_end = ((vsync & 0xffff0000) >> 16) + 1;

	drm_mode_set_name(mode);
	drm_mode_set_crtcinfo(mode, 0);

	return mode;
}

const struct drm_crtc_helper_funcs psb_intel_helper_funcs = {
	.dpms = gma_crtc_dpms,
	.mode_set = psb_intel_crtc_mode_set,
	.mode_set_base = gma_pipe_set_base,
	.prepare = gma_crtc_prepare,
	.commit = gma_crtc_commit,
	.disable = gma_crtc_disable,
};

const struct drm_crtc_funcs psb_intel_crtc_funcs = {
	.cursor_set = gma_crtc_cursor_set,
	.cursor_move = gma_crtc_cursor_move,
	.gamma_set = gma_crtc_gamma_set,
	.set_config = gma_crtc_set_config,
	.destroy = gma_crtc_destroy,
	.page_flip = gma_crtc_page_flip,
	.enable_vblank = psb_enable_vblank,
	.disable_vblank = psb_disable_vblank,
	.get_vblank_counter = psb_get_vblank_counter,
};

const struct gma_clock_funcs psb_clock_funcs = {
	.clock = psb_intel_clock,
	.limit = psb_intel_limit,
	.pll_is_valid = gma_pll_is_valid,
};

/*
 * Set the default value of cursor control and base register
 * to zero. This is a workaround for h/w defect on Oaktrail
 */
static void psb_intel_cursor_init(struct drm_device *dev,
				  struct gma_crtc *gma_crtc)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	u32 control[3] = { CURACNTR, CURBCNTR, CURCCNTR };
	u32 base[3] = { CURABASE, CURBBASE, CURCBASE };
	struct gtt_range *cursor_gt;

	if (dev_priv->ops->cursor_needs_phys) {
		/* Allocate 4 pages of stolen mem for a hardware cursor. That
		 * is enough for the 64 x 64 ARGB cursors we support.
		 */
		cursor_gt = psb_gtt_alloc_range(dev, 4 * PAGE_SIZE, "cursor", 1,
						PAGE_SIZE);
		if (!cursor_gt) {
			gma_crtc->cursor_gt = NULL;
			goto out;
		}
		gma_crtc->cursor_gt = cursor_gt;
		gma_crtc->cursor_addr = dev_priv->stolen_base +
							cursor_gt->offset;
	} else {
		gma_crtc->cursor_gt = NULL;
	}

out:
	REG_WRITE(control[gma_crtc->pipe], 0);
	REG_WRITE(base[gma_crtc->pipe], 0);
}

void psb_intel_crtc_init(struct drm_device *dev, int pipe,
		     struct psb_intel_mode_device *mode_dev)
{
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_crtc *gma_crtc;
	int i;

	/* We allocate a extra array of drm_connector pointers
	 * for fbdev after the crtc */
	gma_crtc = kzalloc(sizeof(struct gma_crtc) +
			(INTELFB_CONN_LIMIT * sizeof(struct drm_connector *)),
			GFP_KERNEL);
	if (gma_crtc == NULL)
		return;

	gma_crtc->crtc_state =
		kzalloc(sizeof(struct psb_intel_crtc_state), GFP_KERNEL);
	if (!gma_crtc->crtc_state) {
		dev_err(dev->dev, "Crtc state error: No memory\n");
		kfree(gma_crtc);
		return;
	}

	/* Set the CRTC operations from the chip specific data */
	drm_crtc_init(dev, &gma_crtc->base, dev_priv->ops->crtc_funcs);

	/* Set the CRTC clock functions from chip specific data */
	gma_crtc->clock_funcs = dev_priv->ops->clock_funcs;

	drm_mode_crtc_set_gamma_size(&gma_crtc->base, 256);
	gma_crtc->pipe = pipe;
	gma_crtc->plane = pipe;

	for (i = 0; i < 256; i++)
		gma_crtc->lut_adj[i] = 0;

	gma_crtc->mode_dev = mode_dev;
	gma_crtc->cursor_addr = 0;

	drm_crtc_helper_add(&gma_crtc->base,
						dev_priv->ops->crtc_helper);

	/* Setup the array of drm_connector pointer array */
	gma_crtc->mode_set.crtc = &gma_crtc->base;
	BUG_ON(pipe >= ARRAY_SIZE(dev_priv->plane_to_crtc_mapping) ||
	       dev_priv->plane_to_crtc_mapping[gma_crtc->plane] != NULL);
	dev_priv->plane_to_crtc_mapping[gma_crtc->plane] = &gma_crtc->base;
	dev_priv->pipe_to_crtc_mapping[gma_crtc->pipe] = &gma_crtc->base;
	gma_crtc->mode_set.connectors = (struct drm_connector **)(gma_crtc + 1);
	gma_crtc->mode_set.num_connectors = 0;
	psb_intel_cursor_init(dev, gma_crtc);

	/* Set to true so that the pipe is forced off on initial config. */
	gma_crtc->active = true;
}

struct drm_crtc *psb_intel_get_crtc_from_pipe(struct drm_device *dev, int pipe)
{
	struct drm_crtc *crtc = NULL;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
		if (gma_crtc->pipe == pipe)
			break;
	}
	return crtc;
}

int gma_connector_clones(struct drm_device *dev, int type_mask)
{
	int index_mask = 0;
	struct drm_connector *connector;
	int entry = 0;

	list_for_each_entry(connector, &dev->mode_config.connector_list,
			    head) {
		struct gma_encoder *gma_encoder = gma_attached_encoder(connector);
		if (type_mask & (1 << gma_encoder->type))
			index_mask |= (1 << entry);
		entry++;
	}
	return index_mask;
}
