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
#include <linux/pinctrl/consumer.h>

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
 * @enabled: CRTC state
 */
struct atmel_hlcdc_crtc {
	struct drm_crtc base;
	struct atmel_hlcdc_dc *dc;
	struct drm_pending_vblank_event *event;
	int id;
	bool enabled;
};

static inline struct atmel_hlcdc_crtc *
drm_crtc_to_atmel_hlcdc_crtc(struct drm_crtc *crtc)
{
	return container_of(crtc, struct atmel_hlcdc_crtc, base);
}

static void atmel_hlcdc_crtc_mode_set_nofb(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;
	struct drm_display_mode *adj = &c->state->adjusted_mode;
	unsigned long mode_rate;
	struct videomode vm;
	unsigned long prate;
	unsigned int cfg;
	int div;

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
	mode_rate = adj->crtc_clock * 1000;
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

	if (adj->flags & DRM_MODE_FLAG_NVSYNC)
		cfg |= ATMEL_HLCDC_VSPOL;

	if (adj->flags & DRM_MODE_FLAG_NHSYNC)
		cfg |= ATMEL_HLCDC_HSPOL;

	regmap_update_bits(regmap, ATMEL_HLCDC_CFG(5),
			   ATMEL_HLCDC_HSPOL | ATMEL_HLCDC_VSPOL |
			   ATMEL_HLCDC_VSPDLYS | ATMEL_HLCDC_VSPDLYE |
			   ATMEL_HLCDC_DISPPOL | ATMEL_HLCDC_DISPDLY |
			   ATMEL_HLCDC_VSPSU | ATMEL_HLCDC_VSPHO |
			   ATMEL_HLCDC_GUARDTIME_MASK,
			   cfg);
}

static void atmel_hlcdc_crtc_disable(struct drm_crtc *c)
{
	struct drm_device *dev = c->dev;
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;
	unsigned int status;

	if (!crtc->enabled)
		return;

	drm_crtc_vblank_off(c);

	pm_runtime_get_sync(dev->dev);

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
	pinctrl_pm_select_sleep_state(dev->dev);

	pm_runtime_allow(dev->dev);

	pm_runtime_put_sync(dev->dev);

	crtc->enabled = false;
}

static void atmel_hlcdc_crtc_enable(struct drm_crtc *c)
{
	struct drm_device *dev = c->dev;
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;
	unsigned int status;

	if (crtc->enabled)
		return;

	pm_runtime_get_sync(dev->dev);

	pm_runtime_forbid(dev->dev);

	pinctrl_pm_select_default_state(dev->dev);
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

	pm_runtime_put_sync(dev->dev);

	drm_crtc_vblank_on(c);

	crtc->enabled = true;
}

void atmel_hlcdc_crtc_suspend(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	if (crtc->enabled) {
		atmel_hlcdc_crtc_disable(c);
		/* save enable state for resume */
		crtc->enabled = true;
	}
}

void atmel_hlcdc_crtc_resume(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	if (crtc->enabled) {
		crtc->enabled = false;
		atmel_hlcdc_crtc_enable(c);
	}
}

static int atmel_hlcdc_crtc_atomic_check(struct drm_crtc *c,
					 struct drm_crtc_state *s)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	if (atmel_hlcdc_dc_mode_valid(crtc->dc, &s->adjusted_mode) != MODE_OK)
		return -EINVAL;

	return atmel_hlcdc_plane_prepare_disc_area(s);
}

static void atmel_hlcdc_crtc_atomic_begin(struct drm_crtc *c,
					  struct drm_crtc_state *old_s)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	if (c->state->event) {
		c->state->event->pipe = drm_crtc_index(c);

		WARN_ON(drm_crtc_vblank_get(c) != 0);

		crtc->event = c->state->event;
		c->state->event = NULL;
	}
}

static void atmel_hlcdc_crtc_atomic_flush(struct drm_crtc *crtc,
					  struct drm_crtc_state *old_s)
{
	/* TODO: write common plane control register if available */
}

static const struct drm_crtc_helper_funcs lcdc_crtc_helper_funcs = {
	.mode_set = drm_helper_crtc_mode_set,
	.mode_set_nofb = atmel_hlcdc_crtc_mode_set_nofb,
	.mode_set_base = drm_helper_crtc_mode_set_base,
	.disable = atmel_hlcdc_crtc_disable,
	.enable = atmel_hlcdc_crtc_enable,
	.atomic_check = atmel_hlcdc_crtc_atomic_check,
	.atomic_begin = atmel_hlcdc_crtc_atomic_begin,
	.atomic_flush = atmel_hlcdc_crtc_atomic_flush,
};

static void atmel_hlcdc_crtc_destroy(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	drm_crtc_cleanup(c);
	kfree(crtc);
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

static const struct drm_crtc_funcs atmel_hlcdc_crtc_funcs = {
	.page_flip = drm_atomic_helper_page_flip,
	.set_config = drm_atomic_helper_set_config,
	.destroy = atmel_hlcdc_crtc_destroy,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state =  drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
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

	crtc->dc = dc;

	ret = drm_crtc_init_with_planes(dev, &crtc->base,
				&planes->primary->base,
				planes->cursor ? &planes->cursor->base : NULL,
				&atmel_hlcdc_crtc_funcs, NULL);
	if (ret < 0)
		goto fail;

	crtc->id = drm_crtc_index(&crtc->base);

	if (planes->cursor)
		planes->cursor->base.possible_crtcs = 1 << crtc->id;

	for (i = 0; i < planes->noverlays; i++)
		planes->overlays[i]->base.possible_crtcs = 1 << crtc->id;

	drm_crtc_helper_add(&crtc->base, &lcdc_crtc_helper_funcs);
	drm_crtc_vblank_reset(&crtc->base);

	dc->crtc = &crtc->base;

	return 0;

fail:
	atmel_hlcdc_crtc_destroy(&crtc->base);
	return ret;
}
