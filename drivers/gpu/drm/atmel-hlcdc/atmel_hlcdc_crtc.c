/*
 * Copyright (C) 2014 Traphandler
 * Copyright (C) 2014 Free Electrons
 *
 * Author: Jean-Jacques Hiblot <jjhiblot@traphandler.com>
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drmP.h>

#include <video/videomode.h>

#include "atmel_hlcdc_dc.h"

/**
 * Atmel HLCDC CRTC structure
 *
 * @base: base DRM CRTC structure
 * @hlcdc: pointer to the atmel_hlcdc structure provided by the MFD device
 * @event: pointer to the current page flip event
 * @id: CRTC id (returned by drm_crtc_index)
 * @dpms: DPMS mode
 */
struct atmel_hlcdc_crtc {
	struct drm_crtc base;
	struct atmel_hlcdc_dc *dc;
	struct drm_pending_vblank_event *event;
	int id;
	int dpms;
};

static inline struct atmel_hlcdc_crtc *
drm_crtc_to_atmel_hlcdc_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct atmel_hlcdc_crtc, base);
}

static void atmel_hlcdc_crtc_dpms(struct drm_crtc *c, int mode)
{
	struct drm_device *dev = c->dev;
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;
	unsigned int status;

	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (crtc->dpms == mode)
		return;

	pm_runtime_get_sync(dev->dev);

	if (mode != DRM_MODE_DPMS_ON) {
		regmap_write(regmap, ATMEL_HLCDC_DIS, ATMEL_HLCDC_DISP);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       (status & ATMEL_HLCDC_DISP))
			cpu_relax();

		regmap_write(regmap, ATMEL_HLCDC_DIS, ATMEL_HLCDC_SYNC);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       (status & ATMEL_HLCDC_SYNC))
			cpu_relax();

		regmap_write(regmap, ATMEL_HLCDC_DIS, ATMEL_HLCDC_PIXEL_CLK);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       (status & ATMEL_HLCDC_PIXEL_CLK))
			cpu_relax();

		clk_disable_unprepare(crtc->dc->hlcdc->sys_clk);

		pm_runtime_allow(dev->dev);
	} else {
		pm_runtime_forbid(dev->dev);

		clk_prepare_enable(crtc->dc->hlcdc->sys_clk);

		regmap_write(regmap, ATMEL_HLCDC_EN, ATMEL_HLCDC_PIXEL_CLK);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       !(status & ATMEL_HLCDC_PIXEL_CLK))
			cpu_relax();


		regmap_write(regmap, ATMEL_HLCDC_EN, ATMEL_HLCDC_SYNC);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       !(status & ATMEL_HLCDC_SYNC))
			cpu_relax();

		regmap_write(regmap, ATMEL_HLCDC_EN, ATMEL_HLCDC_DISP);
		while (!regmap_read(regmap, ATMEL_HLCDC_SR, &status) &&
		       !(status & ATMEL_HLCDC_DISP))
			cpu_relax();
	}

	pm_runtime_put_sync(dev->dev);

	crtc->dpms = mode;
}

static int atmel_hlcdc_crtc_mode_set(struct drm_crtc *c,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adj,
				     int x, int y,
				     struct drm_framebuffer *old_fb)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;
	struct drm_plane *plane = c->primary;
	struct drm_framebuffer *fb;
	unsigned long mode_rate;
	struct videomode vm;
	unsigned long prate;
	unsigned int cfg;
	int div;

	if (atmel_hlcdc_dc_mode_valid(crtc->dc, adj) != MODE_OK)
		return -EINVAL;

	vm.vfront_porch = adj->crtc_vsync_start - adj->crtc_vdisplay;
	vm.vback_porch = adj->crtc_vtotal - adj->crtc_vsync_end;
	vm.vsync_len = adj->crtc_vsync_end - adj->crtc_vsync_start;
	vm.hfront_porch = adj->crtc_hsync_start - adj->crtc_hdisplay;
	vm.hback_porch = adj->crtc_htotal - adj->crtc_hsync_end;
	vm.hsync_len = adj->crtc_hsync_end - adj->crtc_hsync_start;

	regmap_write(regmap, ATMEL_HLCDC_CFG(1),
		     (vm.hsync_len - 1) | ((vm.vsync_len - 1) << 16));

	regmap_write(regmap, ATMEL_HLCDC_CFG(2),
		     (vm.vfront_porch - 1) | (vm.vback_porch << 16));

	regmap_write(regmap, ATMEL_HLCDC_CFG(3),
		     (vm.hfront_porch - 1) | ((vm.hback_porch - 1) << 16));

	regmap_write(regmap, ATMEL_HLCDC_CFG(4),
		     (adj->crtc_hdisplay - 1) |
		     ((adj->crtc_vdisplay - 1) << 16));

	cfg = 0;

	prate = clk_get_rate(crtc->dc->hlcdc->sys_clk);
	mode_rate = mode->crtc_clock * 1000;
	if ((prate / 2) < mode_rate) {
		prate *= 2;
		cfg |= ATMEL_HLCDC_CLKSEL;
	}

	div = DIV_ROUND_UP(prate, mode_rate);
	if (div < 2)
		div = 2;

	cfg |= ATMEL_HLCDC_CLKDIV(div);

	regmap_update_bits(regmap, ATMEL_HLCDC_CFG(0),
			   ATMEL_HLCDC_CLKSEL | ATMEL_HLCDC_CLKDIV_MASK |
			   ATMEL_HLCDC_CLKPOL, cfg);

	cfg = 0;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		cfg |= ATMEL_HLCDC_VSPOL;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		cfg |= ATMEL_HLCDC_HSPOL;

	regmap_update_bits(regmap, ATMEL_HLCDC_CFG(5),
			   ATMEL_HLCDC_HSPOL | ATMEL_HLCDC_VSPOL |
			   ATMEL_HLCDC_VSPDLYS | ATMEL_HLCDC_VSPDLYE |
			   ATMEL_HLCDC_DISPPOL | ATMEL_HLCDC_DISPDLY |
			   ATMEL_HLCDC_VSPSU | ATMEL_HLCDC_VSPHO |
			   ATMEL_HLCDC_GUARDTIME_MASK,
			   cfg);

	fb = plane->fb;
	plane->fb = old_fb;

	return atmel_hlcdc_plane_update_with_mode(plane, c, fb, 0, 0,
						  adj->hdisplay, adj->vdisplay,
						  x << 16, y << 16,
						  adj->hdisplay << 16,
						  adj->vdisplay << 16,
						  adj);
}

int atmel_hlcdc_crtc_mode_set_base(struct drm_crtc *c, int x, int y,
				   struct drm_framebuffer *old_fb)
{
	struct drm_plane *plane = c->primary;
	struct drm_framebuffer *fb = plane->fb;
	struct drm_display_mode *mode = &c->hwmode;

	plane->fb = old_fb;

	return plane->funcs->update_plane(plane, c, fb,
					  0, 0,
					  mode->hdisplay,
					  mode->vdisplay,
					  x << 16, y << 16,
					  mode->hdisplay << 16,
					  mode->vdisplay << 16);
}

static void atmel_hlcdc_crtc_prepare(struct drm_crtc *crtc)
{
	atmel_hlcdc_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void atmel_hlcdc_crtc_commit(struct drm_crtc *crtc)
{
	atmel_hlcdc_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static bool atmel_hlcdc_crtc_mode_fixup(struct drm_crtc *crtc,
					const struct drm_display_mode *mode,
					struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void atmel_hlcdc_crtc_disable(struct drm_crtc *crtc)
{
	struct drm_plane *plane;

	atmel_hlcdc_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
	crtc->primary->funcs->disable_plane(crtc->primary);

	drm_for_each_legacy_plane(plane, &crtc->dev->mode_config.plane_list) {
		if (plane->crtc != crtc)
			continue;

		plane->funcs->disable_plane(crtc->primary);
		plane->crtc = NULL;
	}
}

static const struct drm_crtc_helper_funcs lcdc_crtc_helper_funcs = {
	.mode_fixup = atmel_hlcdc_crtc_mode_fixup,
	.dpms = atmel_hlcdc_crtc_dpms,
	.mode_set = atmel_hlcdc_crtc_mode_set,
	.mode_set_base = atmel_hlcdc_crtc_mode_set_base,
	.prepare = atmel_hlcdc_crtc_prepare,
	.commit = atmel_hlcdc_crtc_commit,
	.disable = atmel_hlcdc_crtc_disable,
};

static void atmel_hlcdc_crtc_destroy(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	drm_crtc_cleanup(c);
	kfree(crtc);
}

void atmel_hlcdc_crtc_cancel_page_flip(struct drm_crtc *c,
				       struct drm_file *file)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct drm_pending_vblank_event *event;
	struct drm_device *dev = c->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = crtc->event;
	if (event && event->base.file_priv == file) {
		event->base.destroy(&event->base);
		drm_vblank_put(dev, crtc->id);
		crtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void atmel_hlcdc_crtc_finish_page_flip(struct atmel_hlcdc_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (crtc->event) {
		drm_send_vblank_event(dev, crtc->id, crtc->event);
		drm_vblank_put(dev, crtc->id);
		crtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

void atmel_hlcdc_crtc_irq(struct drm_crtc *c)
{
	drm_handle_vblank(c->dev, 0);
	atmel_hlcdc_crtc_finish_page_flip(drm_crtc_to_atmel_hlcdc_crtc(c));
}

static int atmel_hlcdc_crtc_page_flip(struct drm_crtc *c,
				      struct drm_framebuffer *fb,
				      struct drm_pending_vblank_event *event,
				      uint32_t page_flip_flags)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct atmel_hlcdc_plane_update_req req;
	struct drm_plane *plane = c->primary;
	struct drm_device *dev = c->dev;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (crtc->event)
		ret = -EBUSY;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (ret)
		return ret;

	memset(&req, 0, sizeof(req));
	req.crtc_x = 0;
	req.crtc_y = 0;
	req.crtc_h = c->mode.crtc_vdisplay;
	req.crtc_w = c->mode.crtc_hdisplay;
	req.src_x = c->x << 16;
	req.src_y = c->y << 16;
	req.src_w = req.crtc_w << 16;
	req.src_h = req.crtc_h << 16;
	req.fb = fb;

	ret = atmel_hlcdc_plane_prepare_update_req(plane, &req, &c->hwmode);
	if (ret)
		return ret;

	if (event) {
		drm_vblank_get(c->dev, crtc->id);
		spin_lock_irqsave(&dev->event_lock, flags);
		crtc->event = event;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	ret = atmel_hlcdc_plane_apply_update_req(plane, &req);
	if (ret)
		crtc->event = NULL;
	else
		plane->fb = fb;

	return ret;
}

static const struct drm_crtc_funcs atmel_hlcdc_crtc_funcs = {
	.page_flip = atmel_hlcdc_crtc_page_flip,
	.set_config = drm_crtc_helper_set_config,
	.destroy = atmel_hlcdc_crtc_destroy,
};

int atmel_hlcdc_crtc_create(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_planes *planes = dc->planes;
	struct atmel_hlcdc_crtc *crtc;
	int ret;
	int i;

	crtc = kzalloc(sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	crtc->dpms = DRM_MODE_DPMS_OFF;
	crtc->dc = dc;

	ret = drm_crtc_init_with_planes(dev, &crtc->base,
				&planes->primary->base,
				planes->cursor ? &planes->cursor->base : NULL,
				&atmel_hlcdc_crtc_funcs);
	if (ret < 0)
		goto fail;

	crtc->id = drm_crtc_index(&crtc->base);

	if (planes->cursor)
		planes->cursor->base.possible_crtcs = 1 << crtc->id;

	for (i = 0; i < planes->noverlays; i++)
		planes->overlays[i]->base.possible_crtcs = 1 << crtc->id;

	drm_crtc_helper_add(&crtc->base, &lcdc_crtc_helper_funcs);

	dc->crtc = &crtc->base;

	return 0;

fail:
	atmel_hlcdc_crtc_destroy(&crtc->base);
	return ret;
}

