/*
 * Copyright © 2006-2011 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *	Eric Anholt <eric@anholt.net>
 *	Patrik Jakobsson <patrik.r.jakobsson@gmail.com>
 */

#include <drm/drmP.h>
#include "gma_display.h"
#include "psb_intel_drv.h"
#include "psb_intel_reg.h"
#include "psb_drv.h"
#include "framebuffer.h"

/**
 * Returns whether any output on the specified pipe is of the specified type
 */
bool gma_pipe_has_type(struct drm_crtc *crtc, int type)
{
	struct drm_device *dev = crtc->dev;
	struct drm_mode_config *mode_config = &dev->mode_config;
	struct drm_connector *l_entry;

	list_for_each_entry(l_entry, &mode_config->connector_list, head) {
		if (l_entry->encoder && l_entry->encoder->crtc == crtc) {
			struct psb_intel_encoder *psb_intel_encoder =
					psb_intel_attached_encoder(l_entry);
			if (psb_intel_encoder->type == type)
				return true;
		}
	}

	return false;
}

void gma_wait_for_vblank(struct drm_device *dev)
{
	/* Wait for 20ms, i.e. one cycle at 50hz. */
	mdelay(20);
}

int gma_pipe_set_base(struct drm_crtc *crtc, int x, int y,
		      struct drm_framebuffer *old_fb)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	struct psb_framebuffer *psbfb = to_psb_fb(crtc->fb);
	int pipe = psb_intel_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	unsigned long start, offset;
	u32 dspcntr;
	int ret = 0;

	if (!gma_power_begin(dev, true))
		return 0;

	/* no fb bound */
	if (!crtc->fb) {
		dev_err(dev->dev, "No FB bound\n");
		goto gma_pipe_cleaner;
	}

	/* We are displaying this buffer, make sure it is actually loaded
	   into the GTT */
	ret = psb_gtt_pin(psbfb->gtt);
	if (ret < 0)
		goto gma_pipe_set_base_exit;
	start = psbfb->gtt->offset;
	offset = y * crtc->fb->pitches[0] + x * (crtc->fb->bits_per_pixel / 8);

	REG_WRITE(map->stride, crtc->fb->pitches[0]);

	dspcntr = REG_READ(map->cntr);
	dspcntr &= ~DISPPLANE_PIXFORMAT_MASK;

	switch (crtc->fb->bits_per_pixel) {
	case 8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case 16:
		if (crtc->fb->depth == 15)
			dspcntr |= DISPPLANE_15_16BPP;
		else
			dspcntr |= DISPPLANE_16BPP;
		break;
	case 24:
	case 32:
		dspcntr |= DISPPLANE_32BPP_NO_ALPHA;
		break;
	default:
		dev_err(dev->dev, "Unknown color depth\n");
		ret = -EINVAL;
		goto gma_pipe_set_base_exit;
	}
	REG_WRITE(map->cntr, dspcntr);

	dev_dbg(dev->dev,
		"Writing base %08lX %08lX %d %d\n", start, offset, x, y);

	/* FIXME: Investigate whether this really is the base for psb and why
		  the linear offset is named base for the other chips. map->surf
		  should be the base and map->linoff the offset for all chips */
	if (IS_PSB(dev)) {
		REG_WRITE(map->base, offset + start);
		REG_READ(map->base);
	} else {
		REG_WRITE(map->base, offset);
		REG_READ(map->base);
		REG_WRITE(map->surf, start);
		REG_READ(map->surf);
	}

gma_pipe_cleaner:
	/* If there was a previous display we can now unpin it */
	if (old_fb)
		psb_gtt_unpin(to_psb_fb(old_fb)->gtt);

gma_pipe_set_base_exit:
	gma_power_end(dev);
	return ret;
}

/* Loads the palette/gamma unit for the CRTC with the prepared values */
void gma_crtc_load_lut(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	const struct psb_offset *map = &dev_priv->regmap[psb_intel_crtc->pipe];
	int palreg = map->palette;
	int i;

	/* The clocks have to be on to load the palette. */
	if (!crtc->enabled)
		return;

	if (gma_power_begin(dev, false)) {
		for (i = 0; i < 256; i++) {
			REG_WRITE(palreg + 4 * i,
				  ((psb_intel_crtc->lut_r[i] +
				  psb_intel_crtc->lut_adj[i]) << 16) |
				  ((psb_intel_crtc->lut_g[i] +
				  psb_intel_crtc->lut_adj[i]) << 8) |
				  (psb_intel_crtc->lut_b[i] +
				  psb_intel_crtc->lut_adj[i]));
		}
		gma_power_end(dev);
	} else {
		for (i = 0; i < 256; i++) {
			/* FIXME: Why pipe[0] and not pipe[..._crtc->pipe]? */
			dev_priv->regs.pipe[0].palette[i] =
				  ((psb_intel_crtc->lut_r[i] +
				  psb_intel_crtc->lut_adj[i]) << 16) |
				  ((psb_intel_crtc->lut_g[i] +
				  psb_intel_crtc->lut_adj[i]) << 8) |
				  (psb_intel_crtc->lut_b[i] +
				  psb_intel_crtc->lut_adj[i]);
		}

	}
}

void gma_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green, u16 *blue,
			u32 start, u32 size)
{
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int i;
	int end = (start + size > 256) ? 256 : start + size;

	for (i = start; i < end; i++) {
		psb_intel_crtc->lut_r[i] = red[i] >> 8;
		psb_intel_crtc->lut_g[i] = green[i] >> 8;
		psb_intel_crtc->lut_b[i] = blue[i] >> 8;
	}

	gma_crtc_load_lut(crtc);
}

/**
 * Sets the power management mode of the pipe and plane.
 *
 * This code should probably grow support for turning the cursor off and back
 * on appropriately at the same time as we're turning the pipe off/on.
 */
void gma_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);
	int pipe = psb_intel_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	u32 temp;

	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */

	/* FIXME: Uncomment this when we move cdv to generic dpms
	if (IS_CDV(dev))
		cdv_intel_disable_self_refresh(dev);
	*/

	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		if (psb_intel_crtc->active)
			break;

		psb_intel_crtc->active = true;

		/* Enable the DPLL */
		temp = REG_READ(map->dpll);
		if ((temp & DPLL_VCO_ENABLE) == 0) {
			REG_WRITE(map->dpll, temp);
			REG_READ(map->dpll);
			/* Wait for the clocks to stabilize. */
			udelay(150);
			REG_WRITE(map->dpll, temp | DPLL_VCO_ENABLE);
			REG_READ(map->dpll);
			/* Wait for the clocks to stabilize. */
			udelay(150);
			REG_WRITE(map->dpll, temp | DPLL_VCO_ENABLE);
			REG_READ(map->dpll);
			/* Wait for the clocks to stabilize. */
			udelay(150);
		}

		/* Enable the plane */
		temp = REG_READ(map->cntr);
		if ((temp & DISPLAY_PLANE_ENABLE) == 0) {
			REG_WRITE(map->cntr,
				  temp | DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			REG_WRITE(map->base, REG_READ(map->base));
		}

		udelay(150);

		/* Enable the pipe */
		temp = REG_READ(map->conf);
		if ((temp & PIPEACONF_ENABLE) == 0)
			REG_WRITE(map->conf, temp | PIPEACONF_ENABLE);

		temp = REG_READ(map->status);
		temp &= ~(0xFFFF);
		temp |= PIPE_FIFO_UNDERRUN;
		REG_WRITE(map->status, temp);
		REG_READ(map->status);

		gma_crtc_load_lut(crtc);

		/* Give the overlay scaler a chance to enable
		 * if it's on this pipe */
		/* psb_intel_crtc_dpms_video(crtc, true); TODO */
		break;
	case DRM_MODE_DPMS_OFF:
		if (!psb_intel_crtc->active)
			break;

		psb_intel_crtc->active = false;

		/* Give the overlay scaler a chance to disable
		 * if it's on this pipe */
		/* psb_intel_crtc_dpms_video(crtc, FALSE); TODO */

		/* Disable the VGA plane that we never use */
		REG_WRITE(VGACNTRL, VGA_DISP_DISABLE);

		/* Turn off vblank interrupts */
		drm_vblank_off(dev, pipe);

		/* Wait for vblank for the disable to take effect */
		gma_wait_for_vblank(dev);

		/* Disable plane */
		temp = REG_READ(map->cntr);
		if ((temp & DISPLAY_PLANE_ENABLE) != 0) {
			REG_WRITE(map->cntr,
				  temp & ~DISPLAY_PLANE_ENABLE);
			/* Flush the plane changes */
			REG_WRITE(map->base, REG_READ(map->base));
			REG_READ(map->base);
		}

		/* Disable pipe */
		temp = REG_READ(map->conf);
		if ((temp & PIPEACONF_ENABLE) != 0) {
			REG_WRITE(map->conf, temp & ~PIPEACONF_ENABLE);
			REG_READ(map->conf);
		}

		/* Wait for vblank for the disable to take effect. */
		gma_wait_for_vblank(dev);

		udelay(150);

		/* Disable DPLL */
		temp = REG_READ(map->dpll);
		if ((temp & DPLL_VCO_ENABLE) != 0) {
			REG_WRITE(map->dpll, temp & ~DPLL_VCO_ENABLE);
			REG_READ(map->dpll);
		}

		/* Wait for the clocks to turn off. */
		udelay(150);
		break;
	}

	/* FIXME: Uncomment this when we move cdv to generic dpms
	if (IS_CDV(dev))
		cdv_intel_update_watermark(dev, crtc);
	*/

	/* Set FIFO watermarks */
	REG_WRITE(DSPARB, 0x3F3E);
}

bool gma_crtc_mode_fixup(struct drm_crtc *crtc,
			 const struct drm_display_mode *mode,
			 struct drm_display_mode *adjusted_mode)
{
	return true;
}

void gma_crtc_prepare(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
}

void gma_crtc_commit(struct drm_crtc *crtc)
{
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
}

void gma_crtc_disable(struct drm_crtc *crtc)
{
	struct gtt_range *gt;
	struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;

	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);

	if (crtc->fb) {
		gt = to_psb_fb(crtc->fb)->gtt;
		psb_gtt_unpin(gt);
	}
}

void gma_crtc_destroy(struct drm_crtc *crtc)
{
	struct psb_intel_crtc *psb_intel_crtc = to_psb_intel_crtc(crtc);

	kfree(psb_intel_crtc->crtc_state);
	drm_crtc_cleanup(crtc);
	kfree(psb_intel_crtc);
}

#define GMA_PLL_INVALID(s) { /* DRM_ERROR(s); */ return false; }

bool gma_pll_is_valid(struct drm_crtc *crtc,
		      const struct gma_limit_t *limit,
		      struct gma_clock_t *clock)
{
	if (clock->p1 < limit->p1.min || limit->p1.max < clock->p1)
		GMA_PLL_INVALID("p1 out of range");
	if (clock->p < limit->p.min || limit->p.max < clock->p)
		GMA_PLL_INVALID("p out of range");
	if (clock->m2 < limit->m2.min || limit->m2.max < clock->m2)
		GMA_PLL_INVALID("m2 out of range");
	if (clock->m1 < limit->m1.min || limit->m1.max < clock->m1)
		GMA_PLL_INVALID("m1 out of range");
	/* On CDV m1 is always 0 */
	if (clock->m1 <= clock->m2 && clock->m1 != 0)
		GMA_PLL_INVALID("m1 <= m2 && m1 != 0");
	if (clock->m < limit->m.min || limit->m.max < clock->m)
		GMA_PLL_INVALID("m out of range");
	if (clock->n < limit->n.min || limit->n.max < clock->n)
		GMA_PLL_INVALID("n out of range");
	if (clock->vco < limit->vco.min || limit->vco.max < clock->vco)
		GMA_PLL_INVALID("vco out of range");
	/* XXX: We may need to be checking "Dot clock"
	 * depending on the multiplier, connector, etc.,
	 * rather than just a single range.
	 */
	if (clock->dot < limit->dot.min || limit->dot.max < clock->dot)
		GMA_PLL_INVALID("dot out of range");

	return true;
}

bool gma_find_best_pll(const struct gma_limit_t *limit,
		       struct drm_crtc *crtc, int target, int refclk,
		       struct gma_clock_t *best_clock)
{
	struct drm_device *dev = crtc->dev;
	const struct gma_clock_funcs *clock_funcs =
					to_psb_intel_crtc(crtc)->clock_funcs;
	struct gma_clock_t clock;
	int err = target;

	if (gma_pipe_has_type(crtc, INTEL_OUTPUT_LVDS) &&
	    (REG_READ(LVDS) & LVDS_PORT_EN) != 0) {
		/*
		 * For LVDS, if the panel is on, just rely on its current
		 * settings for dual-channel.  We haven't figured out how to
		 * reliably set up different single/dual channel state, if we
		 * even can.
		 */
		if ((REG_READ(LVDS) & LVDS_CLKB_POWER_MASK) ==
		    LVDS_CLKB_POWER_UP)
			clock.p2 = limit->p2.p2_fast;
		else
			clock.p2 = limit->p2.p2_slow;
	} else {
		if (target < limit->p2.dot_limit)
			clock.p2 = limit->p2.p2_slow;
		else
			clock.p2 = limit->p2.p2_fast;
	}

	memset(best_clock, 0, sizeof(*best_clock));

	/* m1 is always 0 on CDV so the outmost loop will run just once */
	for (clock.m1 = limit->m1.min; clock.m1 <= limit->m1.max; clock.m1++) {
		for (clock.m2 = limit->m2.min;
		     (clock.m2 < clock.m1 || clock.m1 == 0) &&
		      clock.m2 <= limit->m2.max; clock.m2++) {
			for (clock.n = limit->n.min;
			     clock.n <= limit->n.max; clock.n++) {
				for (clock.p1 = limit->p1.min;
				     clock.p1 <= limit->p1.max;
				     clock.p1++) {
					int this_err;

					clock_funcs->clock(refclk, &clock);

					if (!clock_funcs->pll_is_valid(crtc,
								limit, &clock))
						continue;

					this_err = abs(clock.dot - target);
					if (this_err < err) {
						*best_clock = clock;
						err = this_err;
					}
				}
			}
		}
	}

	return err != target;
}
