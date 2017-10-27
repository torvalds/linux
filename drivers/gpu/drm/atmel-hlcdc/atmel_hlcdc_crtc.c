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
 * Atmel HLCDC CRTC state structure
 *
 * @base: base CRTC state
 * @output_mode: RGBXXX output mode
 */
struct atmel_hlcdc_crtc_state {
	struct drm_crtc_state base;
	unsigned int output_mode;
};

static inline struct atmel_hlcdc_crtc_state *
drm_crtc_state_to_atmel_hlcdc_crtc_state(struct drm_crtc_state *state)
{
	return container_of(state, struct atmel_hlcdc_crtc_state, base);
}

/**
 * Atmel HLCDC CRTC structure
 *
 * @base: base DRM CRTC structure
 * @hlcdc: pointer to the atmel_hlcdc structure provided by the MFD device
 * @event: pointer to the current page flip event
 * @id: CRTC id (returned by drm_crtc_index)
 */
struct atmel_hlcdc_crtc {
	struct drm_crtc base;
	struct atmel_hlcdc_dc *dc;
	struct drm_pending_vblank_event *event;
	int id;
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
	struct atmel_hlcdc_crtc_state *state;
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

	state = drm_crtc_state_to_atmel_hlcdc_crtc_state(c->state);
	cfg |= state->output_mode << 8;

	regmap_update_bits(regmap, ATMEL_HLCDC_CFG(5),
			   ATMEL_HLCDC_HSPOL | ATMEL_HLCDC_VSPOL |
			   ATMEL_HLCDC_VSPDLYS | ATMEL_HLCDC_VSPDLYE |
			   ATMEL_HLCDC_DISPPOL | ATMEL_HLCDC_DISPDLY |
			   ATMEL_HLCDC_VSPSU | ATMEL_HLCDC_VSPHO |
			   ATMEL_HLCDC_GUARDTIME_MASK | ATMEL_HLCDC_MODE_MASK,
			   cfg);
}

static enum drm_mode_status
atmel_hlcdc_crtc_mode_valid(struct drm_crtc *c,
			    const struct drm_display_mode *mode)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);

	return atmel_hlcdc_dc_mode_valid(crtc->dc, mode);
}

static void atmel_hlcdc_crtc_atomic_disable(struct drm_crtc *c,
					    struct drm_crtc_state *old_state)
{
	struct drm_device *dev = c->dev;
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;
	unsigned int status;

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
}

static void atmel_hlcdc_crtc_atomic_enable(struct drm_crtc *c,
					   struct drm_crtc_state *old_state)
{
	struct drm_device *dev = c->dev;
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;
	unsigned int status;

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
}

#define ATMEL_HLCDC_RGB444_OUTPUT	BIT(0)
#define ATMEL_HLCDC_RGB565_OUTPUT	BIT(1)
#define ATMEL_HLCDC_RGB666_OUTPUT	BIT(2)
#define ATMEL_HLCDC_RGB888_OUTPUT	BIT(3)
#define ATMEL_HLCDC_OUTPUT_MODE_MASK	GENMASK(3, 0)

static int atmel_hlcdc_crtc_select_output_mode(struct drm_crtc_state *state)
{
	unsigned int output_fmts = ATMEL_HLCDC_OUTPUT_MODE_MASK;
	struct atmel_hlcdc_crtc_state *hstate;
	struct drm_connector_state *cstate;
	struct drm_connector *connector;
	struct atmel_hlcdc_crtc *crtc;
	int i;

	crtc = drm_crtc_to_atmel_hlcdc_crtc(state->crtc);

	for_each_new_connector_in_state(state->state, connector, cstate, i) {
		struct drm_display_info *info = &connector->display_info;
		unsigned int supported_fmts = 0;
		int j;

		if (!cstate->crtc)
			continue;

		for (j = 0; j < info->num_bus_formats; j++) {
			switch (info->bus_formats[j]) {
			case MEDIA_BUS_FMT_RGB444_1X12:
				supported_fmts |= ATMEL_HLCDC_RGB444_OUTPUT;
				break;
			case MEDIA_BUS_FMT_RGB565_1X16:
				supported_fmts |= ATMEL_HLCDC_RGB565_OUTPUT;
				break;
			case MEDIA_BUS_FMT_RGB666_1X18:
				supported_fmts |= ATMEL_HLCDC_RGB666_OUTPUT;
				break;
			case MEDIA_BUS_FMT_RGB888_1X24:
				supported_fmts |= ATMEL_HLCDC_RGB888_OUTPUT;
				break;
			default:
				break;
			}
		}

		if (crtc->dc->desc->conflicting_output_formats)
			output_fmts &= supported_fmts;
		else
			output_fmts |= supported_fmts;
	}

	if (!output_fmts)
		return -EINVAL;

	hstate = drm_crtc_state_to_atmel_hlcdc_crtc_state(state);
	hstate->output_mode = fls(output_fmts) - 1;

	return 0;
}

static int atmel_hlcdc_crtc_atomic_check(struct drm_crtc *c,
					 struct drm_crtc_state *s)
{
	int ret;

	ret = atmel_hlcdc_crtc_select_output_mode(s);
	if (ret)
		return ret;

	ret = atmel_hlcdc_plane_prepare_disc_area(s);
	if (ret)
		return ret;

	return atmel_hlcdc_plane_prepare_ahb_routing(s);
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
	.mode_valid = atmel_hlcdc_crtc_mode_valid,
	.mode_set = drm_helper_crtc_mode_set,
	.mode_set_nofb = atmel_hlcdc_crtc_mode_set_nofb,
	.mode_set_base = drm_helper_crtc_mode_set_base,
	.atomic_check = atmel_hlcdc_crtc_atomic_check,
	.atomic_begin = atmel_hlcdc_crtc_atomic_begin,
	.atomic_flush = atmel_hlcdc_crtc_atomic_flush,
	.atomic_enable = atmel_hlcdc_crtc_atomic_enable,
	.atomic_disable = atmel_hlcdc_crtc_atomic_disable,
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
		drm_crtc_send_vblank_event(&crtc->base, crtc->event);
		drm_crtc_vblank_put(&crtc->base);
		crtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

void atmel_hlcdc_crtc_irq(struct drm_crtc *c)
{
	drm_crtc_handle_vblank(c);
	atmel_hlcdc_crtc_finish_page_flip(drm_crtc_to_atmel_hlcdc_crtc(c));
}

static void atmel_hlcdc_crtc_reset(struct drm_crtc *crtc)
{
	struct atmel_hlcdc_crtc_state *state;

	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state(crtc->state);
		state = drm_crtc_state_to_atmel_hlcdc_crtc_state(crtc->state);
		kfree(state);
		crtc->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		crtc->state = &state->base;
		crtc->state->crtc = crtc;
	}
}

static struct drm_crtc_state *
atmel_hlcdc_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct atmel_hlcdc_crtc_state *state, *cur;

	if (WARN_ON(!crtc->state))
		return NULL;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;
	__drm_atomic_helper_crtc_duplicate_state(crtc, &state->base);

	cur = drm_crtc_state_to_atmel_hlcdc_crtc_state(crtc->state);
	state->output_mode = cur->output_mode;

	return &state->base;
}

static void atmel_hlcdc_crtc_destroy_state(struct drm_crtc *crtc,
					   struct drm_crtc_state *s)
{
	struct atmel_hlcdc_crtc_state *state;

	state = drm_crtc_state_to_atmel_hlcdc_crtc_state(s);
	__drm_atomic_helper_crtc_destroy_state(s);
	kfree(state);
}

static int atmel_hlcdc_crtc_enable_vblank(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;

	/* Enable SOF (Start Of Frame) interrupt for vblank counting */
	regmap_write(regmap, ATMEL_HLCDC_IER, ATMEL_HLCDC_SOF);

	return 0;
}

static void atmel_hlcdc_crtc_disable_vblank(struct drm_crtc *c)
{
	struct atmel_hlcdc_crtc *crtc = drm_crtc_to_atmel_hlcdc_crtc(c);
	struct regmap *regmap = crtc->dc->hlcdc->regmap;

	regmap_write(regmap, ATMEL_HLCDC_IDR, ATMEL_HLCDC_SOF);
}

static const struct drm_crtc_funcs atmel_hlcdc_crtc_funcs = {
	.page_flip = drm_atomic_helper_page_flip,
	.set_config = drm_atomic_helper_set_config,
	.destroy = atmel_hlcdc_crtc_destroy,
	.reset = atmel_hlcdc_crtc_reset,
	.atomic_duplicate_state =  atmel_hlcdc_crtc_duplicate_state,
	.atomic_destroy_state = atmel_hlcdc_crtc_destroy_state,
	.enable_vblank = atmel_hlcdc_crtc_enable_vblank,
	.disable_vblank = atmel_hlcdc_crtc_disable_vblank,
	.gamma_set = drm_atomic_helper_legacy_gamma_set,
};

int atmel_hlcdc_crtc_create(struct drm_device *dev)
{
	struct atmel_hlcdc_plane *primary = NULL, *cursor = NULL;
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_crtc *crtc;
	int ret;
	int i;

	crtc = kzalloc(sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	crtc->dc = dc;

	for (i = 0; i < ATMEL_HLCDC_MAX_LAYERS; i++) {
		if (!dc->layers[i])
			continue;

		switch (dc->layers[i]->desc->type) {
		case ATMEL_HLCDC_BASE_LAYER:
			primary = atmel_hlcdc_layer_to_plane(dc->layers[i]);
			break;

		case ATMEL_HLCDC_CURSOR_LAYER:
			cursor = atmel_hlcdc_layer_to_plane(dc->layers[i]);
			break;

		default:
			break;
		}
	}

	ret = drm_crtc_init_with_planes(dev, &crtc->base, &primary->base,
					&cursor->base, &atmel_hlcdc_crtc_funcs,
					NULL);
	if (ret < 0)
		goto fail;

	crtc->id = drm_crtc_index(&crtc->base);

	for (i = 0; i < ATMEL_HLCDC_MAX_LAYERS; i++) {
		struct atmel_hlcdc_plane *overlay;

		if (dc->layers[i] &&
		    dc->layers[i]->desc->type == ATMEL_HLCDC_OVERLAY_LAYER) {
			overlay = atmel_hlcdc_layer_to_plane(dc->layers[i]);
			overlay->base.possible_crtcs = 1 << crtc->id;
		}
	}

	drm_crtc_helper_add(&crtc->base, &lcdc_crtc_helper_funcs);
	drm_crtc_vblank_reset(&crtc->base);

	drm_mode_crtc_set_gamma_size(&crtc->base, ATMEL_HLCDC_CLUT_SIZE);
	drm_crtc_enable_color_mgmt(&crtc->base, 0, false,
				   ATMEL_HLCDC_CLUT_SIZE);

	dc->crtc = &crtc->base;

	return 0;

fail:
	atmel_hlcdc_crtc_destroy(&crtc->base);
	return ret;
}
