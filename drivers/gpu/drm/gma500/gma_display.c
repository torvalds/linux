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
			struct gma_encoder *gma_encoder =
						gma_attached_encoder(l_entry);
			if (gma_encoder->type == type)
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
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	struct drm_framebuffer *fb = crtc->primary->fb;
	struct psb_framebuffer *psbfb = to_psb_fb(fb);
	int pipe = gma_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	unsigned long start, offset;
	u32 dspcntr;
	int ret = 0;

	if (!gma_power_begin(dev, true))
		return 0;

	/* no fb bound */
	if (!fb) {
		dev_err(dev->dev, "No FB bound\n");
		goto gma_pipe_cleaner;
	}

	/* We are displaying this buffer, make sure it is actually loaded
	   into the GTT */
	ret = psb_gtt_pin(psbfb->gtt);
	if (ret < 0)
		goto gma_pipe_set_base_exit;
	start = psbfb->gtt->offset;
	offset = y * fb->pitches[0] + x * fb->format->cpp[0];

	REG_WRITE(map->stride, fb->pitches[0]);

	dspcntr = REG_READ(map->cntr);
	dspcntr &= ~DISPPLANE_PIXFORMAT_MASK;

	switch (fb->format->cpp[0] * 8) {
	case 8:
		dspcntr |= DISPPLANE_8BPP;
		break;
	case 16:
		if (fb->format->depth == 15)
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
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	const struct psb_offset *map = &dev_priv->regmap[gma_crtc->pipe];
	int palreg = map->palette;
	int i;

	/* The clocks have to be on to load the palette. */
	if (!crtc->enabled)
		return;

	if (gma_power_begin(dev, false)) {
		for (i = 0; i < 256; i++) {
			REG_WRITE(palreg + 4 * i,
				  ((gma_crtc->lut_r[i] +
				  gma_crtc->lut_adj[i]) << 16) |
				  ((gma_crtc->lut_g[i] +
				  gma_crtc->lut_adj[i]) << 8) |
				  (gma_crtc->lut_b[i] +
				  gma_crtc->lut_adj[i]));
		}
		gma_power_end(dev);
	} else {
		for (i = 0; i < 256; i++) {
			/* FIXME: Why pipe[0] and not pipe[..._crtc->pipe]? */
			dev_priv->regs.pipe[0].palette[i] =
				  ((gma_crtc->lut_r[i] +
				  gma_crtc->lut_adj[i]) << 16) |
				  ((gma_crtc->lut_g[i] +
				  gma_crtc->lut_adj[i]) << 8) |
				  (gma_crtc->lut_b[i] +
				  gma_crtc->lut_adj[i]);
		}

	}
}

int gma_crtc_gamma_set(struct drm_crtc *crtc, u16 *red, u16 *green, u16 *blue,
		       u32 size)
{
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	int i;

	for (i = 0; i < size; i++) {
		gma_crtc->lut_r[i] = red[i] >> 8;
		gma_crtc->lut_g[i] = green[i] >> 8;
		gma_crtc->lut_b[i] = blue[i] >> 8;
	}

	gma_crtc_load_lut(crtc);

	return 0;
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
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	int pipe = gma_crtc->pipe;
	const struct psb_offset *map = &dev_priv->regmap[pipe];
	u32 temp;

	/* XXX: When our outputs are all unaware of DPMS modes other than off
	 * and on, we should map those modes to DRM_MODE_DPMS_OFF in the CRTC.
	 */

	if (IS_CDV(dev))
		dev_priv->ops->disable_sr(dev);

	switch (mode) {
	case DRM_MODE_DPMS_ON:
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
		if (gma_crtc->active)
			break;

		gma_crtc->active = true;

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
		if (!gma_crtc->active)
			break;

		gma_crtc->active = false;

		/* Give the overlay scaler a chance to disable
		 * if it's on this pipe */
		/* psb_intel_crtc_dpms_video(crtc, FALSE); TODO */

		/* Disable the VGA plane that we never use */
		REG_WRITE(VGACNTRL, VGA_DISP_DISABLE);

		/* Turn off vblank interrupts */
		drm_crtc_vblank_off(crtc);

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

	if (IS_CDV(dev))
		dev_priv->ops->update_wm(dev, crtc);

	/* Set FIFO watermarks */
	REG_WRITE(DSPARB, 0x3F3E);
}

int gma_crtc_cursor_set(struct drm_crtc *crtc,
			struct drm_file *file_priv,
			uint32_t handle,
			uint32_t width, uint32_t height)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	int pipe = gma_crtc->pipe;
	uint32_t control = (pipe == 0) ? CURACNTR : CURBCNTR;
	uint32_t base = (pipe == 0) ? CURABASE : CURBBASE;
	uint32_t temp;
	size_t addr = 0;
	struct gtt_range *gt;
	struct gtt_range *cursor_gt = gma_crtc->cursor_gt;
	struct drm_gem_object *obj;
	void *tmp_dst, *tmp_src;
	int ret = 0, i, cursor_pages;

	/* If we didn't get a handle then turn the cursor off */
	if (!handle) {
		temp = CURSOR_MODE_DISABLE;
		if (gma_power_begin(dev, false)) {
			REG_WRITE(control, temp);
			REG_WRITE(base, 0);
			gma_power_end(dev);
		}

		/* Unpin the old GEM object */
		if (gma_crtc->cursor_obj) {
			gt = container_of(gma_crtc->cursor_obj,
					  struct gtt_range, gem);
			psb_gtt_unpin(gt);
			drm_gem_object_unreference_unlocked(gma_crtc->cursor_obj);
			gma_crtc->cursor_obj = NULL;
		}
		return 0;
	}

	/* Currently we only support 64x64 cursors */
	if (width != 64 || height != 64) {
		dev_dbg(dev->dev, "We currently only support 64x64 cursors\n");
		return -EINVAL;
	}

	obj = drm_gem_object_lookup(file_priv, handle);
	if (!obj) {
		ret = -ENOENT;
		goto unlock;
	}

	if (obj->size < width * height * 4) {
		dev_dbg(dev->dev, "Buffer is too small\n");
		ret = -ENOMEM;
		goto unref_cursor;
	}

	gt = container_of(obj, struct gtt_range, gem);

	/* Pin the memory into the GTT */
	ret = psb_gtt_pin(gt);
	if (ret) {
		dev_err(dev->dev, "Can not pin down handle 0x%x\n", handle);
		goto unref_cursor;
	}

	if (dev_priv->ops->cursor_needs_phys) {
		if (cursor_gt == NULL) {
			dev_err(dev->dev, "No hardware cursor mem available");
			ret = -ENOMEM;
			goto unref_cursor;
		}

		/* Prevent overflow */
		if (gt->npage > 4)
			cursor_pages = 4;
		else
			cursor_pages = gt->npage;

		/* Copy the cursor to cursor mem */
		tmp_dst = dev_priv->vram_addr + cursor_gt->offset;
		for (i = 0; i < cursor_pages; i++) {
			tmp_src = kmap(gt->pages[i]);
			memcpy(tmp_dst, tmp_src, PAGE_SIZE);
			kunmap(gt->pages[i]);
			tmp_dst += PAGE_SIZE;
		}

		addr = gma_crtc->cursor_addr;
	} else {
		addr = gt->offset;
		gma_crtc->cursor_addr = addr;
	}

	temp = 0;
	/* set the pipe for the cursor */
	temp |= (pipe << 28);
	temp |= CURSOR_MODE_64_ARGB_AX | MCURSOR_GAMMA_ENABLE;

	if (gma_power_begin(dev, false)) {
		REG_WRITE(control, temp);
		REG_WRITE(base, addr);
		gma_power_end(dev);
	}

	/* unpin the old bo */
	if (gma_crtc->cursor_obj) {
		gt = container_of(gma_crtc->cursor_obj, struct gtt_range, gem);
		psb_gtt_unpin(gt);
		drm_gem_object_unreference_unlocked(gma_crtc->cursor_obj);
	}

	gma_crtc->cursor_obj = obj;
unlock:
	return ret;

unref_cursor:
	drm_gem_object_unreference_unlocked(obj);
	return ret;
}

int gma_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct drm_device *dev = crtc->dev;
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	int pipe = gma_crtc->pipe;
	uint32_t temp = 0;
	uint32_t addr;

	if (x < 0) {
		temp |= (CURSOR_POS_SIGN << CURSOR_X_SHIFT);
		x = -x;
	}
	if (y < 0) {
		temp |= (CURSOR_POS_SIGN << CURSOR_Y_SHIFT);
		y = -y;
	}

	temp |= ((x & CURSOR_POS_MASK) << CURSOR_X_SHIFT);
	temp |= ((y & CURSOR_POS_MASK) << CURSOR_Y_SHIFT);

	addr = gma_crtc->cursor_addr;

	if (gma_power_begin(dev, false)) {
		REG_WRITE((pipe == 0) ? CURAPOS : CURBPOS, temp);
		REG_WRITE((pipe == 0) ? CURABASE : CURBBASE, addr);
		gma_power_end(dev);
	}
	return 0;
}

void gma_crtc_prepare(struct drm_crtc *crtc)
{
	const struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);
}

void gma_crtc_commit(struct drm_crtc *crtc)
{
	const struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;
	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_ON);
}

void gma_crtc_disable(struct drm_crtc *crtc)
{
	struct gtt_range *gt;
	const struct drm_crtc_helper_funcs *crtc_funcs = crtc->helper_private;

	crtc_funcs->dpms(crtc, DRM_MODE_DPMS_OFF);

	if (crtc->primary->fb) {
		gt = to_psb_fb(crtc->primary->fb)->gtt;
		psb_gtt_unpin(gt);
	}
}

void gma_crtc_destroy(struct drm_crtc *crtc)
{
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);

	kfree(gma_crtc->crtc_state);
	drm_crtc_cleanup(crtc);
	kfree(gma_crtc);
}

int gma_crtc_set_config(struct drm_mode_set *set)
{
	struct drm_device *dev = set->crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	int ret;

	if (!dev_priv->rpm_enabled)
		return drm_crtc_helper_set_config(set);

	pm_runtime_forbid(&dev->pdev->dev);
	ret = drm_crtc_helper_set_config(set);
	pm_runtime_allow(&dev->pdev->dev);

	return ret;
}

/**
 * Save HW states of given crtc
 */
void gma_crtc_save(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_crtc *gma_crtc = to_gma_crtc(crtc);
	struct psb_intel_crtc_state *crtc_state = gma_crtc->crtc_state;
	const struct psb_offset *map = &dev_priv->regmap[gma_crtc->pipe];
	uint32_t palette_reg;
	int i;

	if (!crtc_state) {
		dev_err(dev->dev, "No CRTC state found\n");
		return;
	}

	crtc_state->saveDSPCNTR = REG_READ(map->cntr);
	crtc_state->savePIPECONF = REG_READ(map->conf);
	crtc_state->savePIPESRC = REG_READ(map->src);
	crtc_state->saveFP0 = REG_READ(map->fp0);
	crtc_state->saveFP1 = REG_READ(map->fp1);
	crtc_state->saveDPLL = REG_READ(map->dpll);
	crtc_state->saveHTOTAL = REG_READ(map->htotal);
	crtc_state->saveHBLANK = REG_READ(map->hblank);
	crtc_state->saveHSYNC = REG_READ(map->hsync);
	crtc_state->saveVTOTAL = REG_READ(map->vtotal);
	crtc_state->saveVBLANK = REG_READ(map->vblank);
	crtc_state->saveVSYNC = REG_READ(map->vsync);
	crtc_state->saveDSPSTRIDE = REG_READ(map->stride);

	/* NOTE: DSPSIZE DSPPOS only for psb */
	crtc_state->saveDSPSIZE = REG_READ(map->size);
	crtc_state->saveDSPPOS = REG_READ(map->pos);

	crtc_state->saveDSPBASE = REG_READ(map->base);

	palette_reg = map->palette;
	for (i = 0; i < 256; ++i)
		crtc_state->savePalette[i] = REG_READ(palette_reg + (i << 2));
}

/**
 * Restore HW states of given crtc
 */
void gma_crtc_restore(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct drm_psb_private *dev_priv = dev->dev_private;
	struct gma_crtc *gma_crtc =  to_gma_crtc(crtc);
	struct psb_intel_crtc_state *crtc_state = gma_crtc->crtc_state;
	const struct psb_offset *map = &dev_priv->regmap[gma_crtc->pipe];
	uint32_t palette_reg;
	int i;

	if (!crtc_state) {
		dev_err(dev->dev, "No crtc state\n");
		return;
	}

	if (crtc_state->saveDPLL & DPLL_VCO_ENABLE) {
		REG_WRITE(map->dpll,
			crtc_state->saveDPLL & ~DPLL_VCO_ENABLE);
		REG_READ(map->dpll);
		udelay(150);
	}

	REG_WRITE(map->fp0, crtc_state->saveFP0);
	REG_READ(map->fp0);

	REG_WRITE(map->fp1, crtc_state->saveFP1);
	REG_READ(map->fp1);

	REG_WRITE(map->dpll, crtc_state->saveDPLL);
	REG_READ(map->dpll);
	udelay(150);

	REG_WRITE(map->htotal, crtc_state->saveHTOTAL);
	REG_WRITE(map->hblank, crtc_state->saveHBLANK);
	REG_WRITE(map->hsync, crtc_state->saveHSYNC);
	REG_WRITE(map->vtotal, crtc_state->saveVTOTAL);
	REG_WRITE(map->vblank, crtc_state->saveVBLANK);
	REG_WRITE(map->vsync, crtc_state->saveVSYNC);
	REG_WRITE(map->stride, crtc_state->saveDSPSTRIDE);

	REG_WRITE(map->size, crtc_state->saveDSPSIZE);
	REG_WRITE(map->pos, crtc_state->saveDSPPOS);

	REG_WRITE(map->src, crtc_state->savePIPESRC);
	REG_WRITE(map->base, crtc_state->saveDSPBASE);
	REG_WRITE(map->conf, crtc_state->savePIPECONF);

	gma_wait_for_vblank(dev);

	REG_WRITE(map->cntr, crtc_state->saveDSPCNTR);
	REG_WRITE(map->base, crtc_state->saveDSPBASE);

	gma_wait_for_vblank(dev);

	palette_reg = map->palette;
	for (i = 0; i < 256; ++i)
		REG_WRITE(palette_reg + (i << 2), crtc_state->savePalette[i]);
}

void gma_encoder_prepare(struct drm_encoder *encoder)
{
	const struct drm_encoder_helper_funcs *encoder_funcs =
	    encoder->helper_private;
	/* lvds has its own version of prepare see psb_intel_lvds_prepare */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_OFF);
}

void gma_encoder_commit(struct drm_encoder *encoder)
{
	const struct drm_encoder_helper_funcs *encoder_funcs =
	    encoder->helper_private;
	/* lvds has its own version of commit see psb_intel_lvds_commit */
	encoder_funcs->dpms(encoder, DRM_MODE_DPMS_ON);
}

void gma_encoder_destroy(struct drm_encoder *encoder)
{
	struct gma_encoder *intel_encoder = to_gma_encoder(encoder);

	drm_encoder_cleanup(encoder);
	kfree(intel_encoder);
}

/* Currently there is only a 1:1 mapping of encoders and connectors */
struct drm_encoder *gma_best_encoder(struct drm_connector *connector)
{
	struct gma_encoder *gma_encoder = gma_attached_encoder(connector);

	return &gma_encoder->base;
}

void gma_connector_attach_encoder(struct gma_connector *connector,
				  struct gma_encoder *encoder)
{
	connector->encoder = encoder;
	drm_mode_connector_attach_encoder(&connector->base,
					  &encoder->base);
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
						to_gma_crtc(crtc)->clock_funcs;
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
