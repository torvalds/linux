/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_fb.h"
#include "armada_gem.h"
#include "armada_hw.h"
#include <drm/armada_drm.h>
#include "armada_ioctlP.h"
#include "armada_trace.h"

struct armada_ovl_plane_properties {
	uint32_t colorkey_yr;
	uint32_t colorkey_ug;
	uint32_t colorkey_vb;
#define K2R(val) (((val) >> 0) & 0xff)
#define K2G(val) (((val) >> 8) & 0xff)
#define K2B(val) (((val) >> 16) & 0xff)
	int16_t  brightness;
	uint16_t contrast;
	uint16_t saturation;
	uint32_t colorkey_mode;
	uint32_t colorkey_enable;
};

struct armada_ovl_plane {
	struct armada_plane base;
	struct armada_ovl_plane_properties prop;
};
#define drm_to_armada_ovl_plane(p) \
	container_of(p, struct armada_ovl_plane, base.base)


static void
armada_ovl_update_attr(struct armada_ovl_plane_properties *prop,
	struct armada_crtc *dcrtc)
{
	writel_relaxed(prop->colorkey_yr, dcrtc->base + LCD_SPU_COLORKEY_Y);
	writel_relaxed(prop->colorkey_ug, dcrtc->base + LCD_SPU_COLORKEY_U);
	writel_relaxed(prop->colorkey_vb, dcrtc->base + LCD_SPU_COLORKEY_V);

	writel_relaxed(prop->brightness << 16 | prop->contrast,
		       dcrtc->base + LCD_SPU_CONTRAST);
	/* Docs say 15:0, but it seems to actually be 31:16 on Armada 510 */
	writel_relaxed(prop->saturation << 16,
		       dcrtc->base + LCD_SPU_SATURATION);
	writel_relaxed(0x00002000, dcrtc->base + LCD_SPU_CBSH_HUE);

	spin_lock_irq(&dcrtc->irq_lock);
	armada_updatel(prop->colorkey_mode,
		       CFG_CKMODE_MASK | CFG_ALPHAM_MASK | CFG_ALPHA_MASK,
		       dcrtc->base + LCD_SPU_DMA_CTRL1);
	if (dcrtc->variant->has_spu_adv_reg)
		armada_updatel(prop->colorkey_enable,
			       ADV_GRACOLORKEY | ADV_VIDCOLORKEY,
			       dcrtc->base + LCD_SPU_ADV_REG);
	spin_unlock_irq(&dcrtc->irq_lock);
}

/* === Plane support === */
static void armada_ovl_plane_work(struct armada_crtc *dcrtc,
	struct armada_plane_work *work)
{
	unsigned long flags;

	trace_armada_ovl_plane_work(&dcrtc->crtc, work->plane);

	spin_lock_irqsave(&dcrtc->irq_lock, flags);
	armada_drm_crtc_update_regs(dcrtc, work->regs);
	spin_unlock_irqrestore(&dcrtc->irq_lock, flags);
}

static void armada_ovl_plane_update_state(struct drm_plane_state *state,
	struct armada_regs *regs)
{
	struct armada_ovl_plane *dplane = drm_to_armada_ovl_plane(state->plane);
	struct armada_framebuffer *dfb = drm_fb_to_armada_fb(state->fb);
	const struct drm_format_info *format;
	unsigned int idx = 0;
	bool fb_changed;
	u32 val, ctrl0;
	u16 src_x, src_y;

	ctrl0 = CFG_DMA_FMT(dfb->fmt) | CFG_DMA_MOD(dfb->mod) | CFG_CBSH_ENA;
	if (state->visible)
		ctrl0 |= CFG_DMA_ENA;
	if (drm_rect_width(&state->src) >> 16 != drm_rect_width(&state->dst))
		ctrl0 |= CFG_DMA_HSMOOTH;

	/*
	 * Shifting a YUV packed format image by one pixel causes the U/V
	 * planes to swap.  Compensate for it by also toggling the UV swap.
	 */
	format = dfb->fb.format;
	if (format->num_planes == 1 && state->src.x1 >> 16 & (format->hsub - 1))
		ctrl0 ^= CFG_DMA_MOD(CFG_SWAPUV);

	if (~dplane->base.state.ctrl0 & ctrl0 & CFG_DMA_ENA) {
		/* Power up the Y/U/V FIFOs on ENA 0->1 transitions */
		armada_reg_queue_mod(regs, idx,
				     0, CFG_PDWN16x66 | CFG_PDWN32x66,
				     LCD_SPU_SRAM_PARA1);
	}

	fb_changed = dplane->base.base.fb != &dfb->fb ||
		     dplane->base.state.src_x != state->src.x1 >> 16 ||
	             dplane->base.state.src_y != state->src.y1 >> 16;

	dplane->base.state.vsync_update = fb_changed;

	/* FIXME: overlay on an interlaced display */
	if (fb_changed) {
		u32 addrs[3];

		dplane->base.state.src_y = src_y = state->src.y1 >> 16;
		dplane->base.state.src_x = src_x = state->src.x1 >> 16;

		armada_drm_plane_calc_addrs(addrs, &dfb->fb, src_x, src_y);

		armada_reg_queue_set(regs, idx, addrs[0],
				     LCD_SPU_DMA_START_ADDR_Y0);
		armada_reg_queue_set(regs, idx, addrs[1],
				     LCD_SPU_DMA_START_ADDR_U0);
		armada_reg_queue_set(regs, idx, addrs[2],
				     LCD_SPU_DMA_START_ADDR_V0);
		armada_reg_queue_set(regs, idx, addrs[0],
				     LCD_SPU_DMA_START_ADDR_Y1);
		armada_reg_queue_set(regs, idx, addrs[1],
				     LCD_SPU_DMA_START_ADDR_U1);
		armada_reg_queue_set(regs, idx, addrs[2],
				     LCD_SPU_DMA_START_ADDR_V1);

		val = dfb->fb.pitches[0] << 16 | dfb->fb.pitches[0];
		armada_reg_queue_set(regs, idx, val,
				     LCD_SPU_DMA_PITCH_YC);
		val = dfb->fb.pitches[1] << 16 | dfb->fb.pitches[2];
		armada_reg_queue_set(regs, idx, val,
				     LCD_SPU_DMA_PITCH_UV);
	}

	val = (drm_rect_height(&state->src) & 0xffff0000) |
	       drm_rect_width(&state->src) >> 16;
	if (dplane->base.state.src_hw != val) {
		dplane->base.state.src_hw = val;
		armada_reg_queue_set(regs, idx, val,
				     LCD_SPU_DMA_HPXL_VLN);
	}

	val = drm_rect_height(&state->dst) << 16 | drm_rect_width(&state->dst);
	if (dplane->base.state.dst_hw != val) {
		dplane->base.state.dst_hw = val;
		armada_reg_queue_set(regs, idx, val,
				     LCD_SPU_DZM_HPXL_VLN);
	}

	val = state->dst.y1 << 16 | state->dst.x1;
	if (dplane->base.state.dst_yx != val) {
		dplane->base.state.dst_yx = val;
		armada_reg_queue_set(regs, idx, val,
				     LCD_SPU_DMA_OVSA_HPXL_VLN);
	}

	if (dplane->base.state.ctrl0 != ctrl0) {
		dplane->base.state.ctrl0 = ctrl0;
		armada_reg_queue_mod(regs, idx, ctrl0,
			CFG_CBSH_ENA | CFG_DMAFORMAT | CFG_DMA_FTOGGLE |
			CFG_DMA_HSMOOTH | CFG_DMA_TSTMODE |
			CFG_DMA_MOD(CFG_SWAPRB | CFG_SWAPUV | CFG_SWAPYU |
			CFG_YUV2RGB) | CFG_DMA_ENA,
			LCD_SPU_DMA_CTRL0);
		dplane->base.state.vsync_update = true;
	}

	dplane->base.state.changed = idx != 0;

	armada_reg_queue_end(regs, idx);
}

static int
armada_ovl_plane_update(struct drm_plane *plane, struct drm_crtc *crtc,
	struct drm_framebuffer *fb,
	int crtc_x, int crtc_y, unsigned crtc_w, unsigned crtc_h,
	uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h,
	struct drm_modeset_acquire_ctx *ctx)
{
	struct armada_ovl_plane *dplane = drm_to_armada_ovl_plane(plane);
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct armada_plane_work *work;
	struct drm_plane_state state = {
		.plane = plane,
		.crtc = crtc,
		.fb = fb,
		.src_x = src_x,
		.src_y = src_y,
		.src_w = src_w,
		.src_h = src_h,
		.crtc_x = crtc_x,
		.crtc_y = crtc_y,
		.crtc_w = crtc_w,
		.crtc_h = crtc_h,
		.rotation = DRM_MODE_ROTATE_0,
	};
	struct drm_crtc_state crtc_state = {
		.crtc = crtc,
		.enable = crtc->enabled,
		.mode = crtc->mode,
	};
	int ret;

	trace_armada_ovl_plane_update(plane, crtc, fb,
				 crtc_x, crtc_y, crtc_w, crtc_h,
				 src_x, src_y, src_w, src_h);

	ret = drm_atomic_helper_check_plane_state(&state, &crtc_state, 0,
						  INT_MAX, true, false);
	if (ret)
		return ret;

	work = &dplane->base.works[dplane->base.next_work];

	if (plane->fb != fb) {
		/*
		 * Take a reference on the new framebuffer - we want to
		 * hold on to it while the hardware is displaying it.
		 */
		drm_framebuffer_reference(fb);

		work->old_fb = plane->fb;
	} else {
		work->old_fb = NULL;
	}

	armada_ovl_plane_update_state(&state, work->regs);

	if (!dplane->base.state.changed)
		return 0;

	/* Wait for pending work to complete */
	if (armada_drm_plane_work_wait(&dplane->base, HZ / 25) == 0)
		armada_drm_plane_work_cancel(dcrtc, &dplane->base);

	/* Just updating the position/size? */
	if (!dplane->base.state.vsync_update) {
		armada_ovl_plane_work(dcrtc, work);
		return 0;
	}

	if (!dcrtc->plane) {
		dcrtc->plane = plane;
		armada_ovl_update_attr(&dplane->prop, dcrtc);
	}

	/* Queue it for update on the next interrupt if we are enabled */
	ret = armada_drm_plane_work_queue(dcrtc, work);
	if (ret)
		DRM_ERROR("failed to queue plane work: %d\n", ret);

	dplane->base.next_work = !dplane->base.next_work;

	return 0;
}

static void armada_ovl_plane_destroy(struct drm_plane *plane)
{
	struct armada_ovl_plane *dplane = drm_to_armada_ovl_plane(plane);

	drm_plane_cleanup(plane);

	kfree(dplane);
}

static int armada_ovl_plane_set_property(struct drm_plane *plane,
	struct drm_property *property, uint64_t val)
{
	struct armada_private *priv = plane->dev->dev_private;
	struct armada_ovl_plane *dplane = drm_to_armada_ovl_plane(plane);
	bool update_attr = false;

	if (property == priv->colorkey_prop) {
#define CCC(v) ((v) << 24 | (v) << 16 | (v) << 8)
		dplane->prop.colorkey_yr = CCC(K2R(val));
		dplane->prop.colorkey_ug = CCC(K2G(val));
		dplane->prop.colorkey_vb = CCC(K2B(val));
#undef CCC
		update_attr = true;
	} else if (property == priv->colorkey_min_prop) {
		dplane->prop.colorkey_yr &= ~0x00ff0000;
		dplane->prop.colorkey_yr |= K2R(val) << 16;
		dplane->prop.colorkey_ug &= ~0x00ff0000;
		dplane->prop.colorkey_ug |= K2G(val) << 16;
		dplane->prop.colorkey_vb &= ~0x00ff0000;
		dplane->prop.colorkey_vb |= K2B(val) << 16;
		update_attr = true;
	} else if (property == priv->colorkey_max_prop) {
		dplane->prop.colorkey_yr &= ~0xff000000;
		dplane->prop.colorkey_yr |= K2R(val) << 24;
		dplane->prop.colorkey_ug &= ~0xff000000;
		dplane->prop.colorkey_ug |= K2G(val) << 24;
		dplane->prop.colorkey_vb &= ~0xff000000;
		dplane->prop.colorkey_vb |= K2B(val) << 24;
		update_attr = true;
	} else if (property == priv->colorkey_val_prop) {
		dplane->prop.colorkey_yr &= ~0x0000ff00;
		dplane->prop.colorkey_yr |= K2R(val) << 8;
		dplane->prop.colorkey_ug &= ~0x0000ff00;
		dplane->prop.colorkey_ug |= K2G(val) << 8;
		dplane->prop.colorkey_vb &= ~0x0000ff00;
		dplane->prop.colorkey_vb |= K2B(val) << 8;
		update_attr = true;
	} else if (property == priv->colorkey_alpha_prop) {
		dplane->prop.colorkey_yr &= ~0x000000ff;
		dplane->prop.colorkey_yr |= K2R(val);
		dplane->prop.colorkey_ug &= ~0x000000ff;
		dplane->prop.colorkey_ug |= K2G(val);
		dplane->prop.colorkey_vb &= ~0x000000ff;
		dplane->prop.colorkey_vb |= K2B(val);
		update_attr = true;
	} else if (property == priv->colorkey_mode_prop) {
		if (val == CKMODE_DISABLE) {
			dplane->prop.colorkey_mode =
				CFG_CKMODE(CKMODE_DISABLE) |
				CFG_ALPHAM_CFG | CFG_ALPHA(255);
			dplane->prop.colorkey_enable = 0;
		} else {
			dplane->prop.colorkey_mode =
				CFG_CKMODE(val) |
				CFG_ALPHAM_GRA | CFG_ALPHA(0);
			dplane->prop.colorkey_enable = ADV_GRACOLORKEY;
		}
		update_attr = true;
	} else if (property == priv->brightness_prop) {
		dplane->prop.brightness = val - 256;
		update_attr = true;
	} else if (property == priv->contrast_prop) {
		dplane->prop.contrast = val;
		update_attr = true;
	} else if (property == priv->saturation_prop) {
		dplane->prop.saturation = val;
		update_attr = true;
	}

	if (update_attr && dplane->base.base.crtc)
		armada_ovl_update_attr(&dplane->prop,
				       drm_to_armada_crtc(dplane->base.base.crtc));

	return 0;
}

static const struct drm_plane_funcs armada_ovl_plane_funcs = {
	.update_plane	= armada_ovl_plane_update,
	.disable_plane	= armada_drm_plane_disable,
	.destroy	= armada_ovl_plane_destroy,
	.set_property	= armada_ovl_plane_set_property,
};

static const uint32_t armada_ovl_formats[] = {
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_BGR565,
};

static const struct drm_prop_enum_list armada_drm_colorkey_enum_list[] = {
	{ CKMODE_DISABLE, "disabled" },
	{ CKMODE_Y,       "Y component" },
	{ CKMODE_U,       "U component" },
	{ CKMODE_V,       "V component" },
	{ CKMODE_RGB,     "RGB" },
	{ CKMODE_R,       "R component" },
	{ CKMODE_G,       "G component" },
	{ CKMODE_B,       "B component" },
};

static int armada_overlay_create_properties(struct drm_device *dev)
{
	struct armada_private *priv = dev->dev_private;

	if (priv->colorkey_prop)
		return 0;

	priv->colorkey_prop = drm_property_create_range(dev, 0,
				"colorkey", 0, 0xffffff);
	priv->colorkey_min_prop = drm_property_create_range(dev, 0,
				"colorkey_min", 0, 0xffffff);
	priv->colorkey_max_prop = drm_property_create_range(dev, 0,
				"colorkey_max", 0, 0xffffff);
	priv->colorkey_val_prop = drm_property_create_range(dev, 0,
				"colorkey_val", 0, 0xffffff);
	priv->colorkey_alpha_prop = drm_property_create_range(dev, 0,
				"colorkey_alpha", 0, 0xffffff);
	priv->colorkey_mode_prop = drm_property_create_enum(dev, 0,
				"colorkey_mode",
				armada_drm_colorkey_enum_list,
				ARRAY_SIZE(armada_drm_colorkey_enum_list));
	priv->brightness_prop = drm_property_create_range(dev, 0,
				"brightness", 0, 256 + 255);
	priv->contrast_prop = drm_property_create_range(dev, 0,
				"contrast", 0, 0x7fff);
	priv->saturation_prop = drm_property_create_range(dev, 0,
				"saturation", 0, 0x7fff);

	if (!priv->colorkey_prop)
		return -ENOMEM;

	return 0;
}

int armada_overlay_plane_create(struct drm_device *dev, unsigned long crtcs)
{
	struct armada_private *priv = dev->dev_private;
	struct drm_mode_object *mobj;
	struct armada_ovl_plane *dplane;
	int ret;

	ret = armada_overlay_create_properties(dev);
	if (ret)
		return ret;

	dplane = kzalloc(sizeof(*dplane), GFP_KERNEL);
	if (!dplane)
		return -ENOMEM;

	ret = armada_drm_plane_init(&dplane->base);
	if (ret) {
		kfree(dplane);
		return ret;
	}

	dplane->base.works[0].fn = armada_ovl_plane_work;
	dplane->base.works[1].fn = armada_ovl_plane_work;

	ret = drm_universal_plane_init(dev, &dplane->base.base, crtcs,
				       &armada_ovl_plane_funcs,
				       armada_ovl_formats,
				       ARRAY_SIZE(armada_ovl_formats),
				       NULL,
				       DRM_PLANE_TYPE_OVERLAY, NULL);
	if (ret) {
		kfree(dplane);
		return ret;
	}

	dplane->prop.colorkey_yr = 0xfefefe00;
	dplane->prop.colorkey_ug = 0x01010100;
	dplane->prop.colorkey_vb = 0x01010100;
	dplane->prop.colorkey_mode = CFG_CKMODE(CKMODE_RGB) |
				     CFG_ALPHAM_GRA | CFG_ALPHA(0);
	dplane->prop.colorkey_enable = ADV_GRACOLORKEY;
	dplane->prop.brightness = 0;
	dplane->prop.contrast = 0x4000;
	dplane->prop.saturation = 0x4000;

	mobj = &dplane->base.base.base;
	drm_object_attach_property(mobj, priv->colorkey_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_min_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_max_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_val_prop,
				   0x0101fe);
	drm_object_attach_property(mobj, priv->colorkey_alpha_prop,
				   0x000000);
	drm_object_attach_property(mobj, priv->colorkey_mode_prop,
				   CKMODE_RGB);
	drm_object_attach_property(mobj, priv->brightness_prop, 256);
	drm_object_attach_property(mobj, priv->contrast_prop,
				   dplane->prop.contrast);
	drm_object_attach_property(mobj, priv->saturation_prop,
				   dplane->prop.saturation);

	return 0;
}
