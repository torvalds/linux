/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <drm/drmP.h>
#include <drm/drm_plane_helper.h>
#include "armada_crtc.h"
#include "armada_drm.h"
#include "armada_fb.h"
#include "armada_gem.h"
#include "armada_hw.h"
#include <drm/armada_drm.h>
#include "armada_ioctlP.h"

struct armada_plane_properties {
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
};

struct armada_plane {
	struct drm_plane base;
	spinlock_t lock;
	struct drm_framebuffer *old_fb;
	uint32_t src_hw;
	uint32_t dst_hw;
	uint32_t dst_yx;
	uint32_t ctrl0;
	struct {
		struct armada_vbl_event update;
		struct armada_regs regs[13];
		wait_queue_head_t wait;
	} vbl;
	struct armada_plane_properties prop;
};
#define drm_to_armada_plane(p) container_of(p, struct armada_plane, base)


static void
armada_ovl_update_attr(struct armada_plane_properties *prop,
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
	armada_updatel(prop->colorkey_mode | CFG_ALPHAM_GRA,
		     CFG_CKMODE_MASK | CFG_ALPHAM_MASK | CFG_ALPHA_MASK,
		     dcrtc->base + LCD_SPU_DMA_CTRL1);

	armada_updatel(ADV_GRACOLORKEY, 0, dcrtc->base + LCD_SPU_ADV_REG);
	spin_unlock_irq(&dcrtc->irq_lock);
}

/* === Plane support === */
static void armada_plane_vbl(struct armada_crtc *dcrtc, void *data)
{
	struct armada_plane *dplane = data;
	struct drm_framebuffer *fb;

	armada_drm_crtc_update_regs(dcrtc, dplane->vbl.regs);

	spin_lock(&dplane->lock);
	fb = dplane->old_fb;
	dplane->old_fb = NULL;
	spin_unlock(&dplane->lock);

	if (fb)
		armada_drm_queue_unref_work(dcrtc->crtc.dev, fb);

	wake_up(&dplane->vbl.wait);
}

static int
armada_plane_update(struct drm_plane *plane, struct drm_crtc *crtc,
	struct drm_framebuffer *fb,
	int crtc_x, int crtc_y, unsigned crtc_w, unsigned crtc_h,
	uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h)
{
	struct armada_plane *dplane = drm_to_armada_plane(plane);
	struct armada_crtc *dcrtc = drm_to_armada_crtc(crtc);
	struct drm_rect src = {
		.x1 = src_x,
		.y1 = src_y,
		.x2 = src_x + src_w,
		.y2 = src_y + src_h,
	};
	struct drm_rect dest = {
		.x1 = crtc_x,
		.y1 = crtc_y,
		.x2 = crtc_x + crtc_w,
		.y2 = crtc_y + crtc_h,
	};
	const struct drm_rect clip = {
		.x2 = crtc->mode.hdisplay,
		.y2 = crtc->mode.vdisplay,
	};
	uint32_t val, ctrl0;
	unsigned idx = 0;
	bool visible;
	int ret;

	ret = drm_plane_helper_check_update(plane, crtc, fb, &src, &dest, &clip,
					    0, INT_MAX, true, false, &visible);
	if (ret)
		return ret;

	ctrl0 = CFG_DMA_FMT(drm_fb_to_armada_fb(fb)->fmt) |
		CFG_DMA_MOD(drm_fb_to_armada_fb(fb)->mod) |
		CFG_CBSH_ENA | CFG_DMA_HSMOOTH | CFG_DMA_ENA;

	/* Does the position/size result in nothing to display? */
	if (!visible)
		ctrl0 &= ~CFG_DMA_ENA;

	if (!dcrtc->plane) {
		dcrtc->plane = plane;
		armada_ovl_update_attr(&dplane->prop, dcrtc);
	}

	/* FIXME: overlay on an interlaced display */
	/* Just updating the position/size? */
	if (plane->fb == fb && dplane->ctrl0 == ctrl0) {
		val = (drm_rect_height(&src) & 0xffff0000) |
		      drm_rect_width(&src) >> 16;
		dplane->src_hw = val;
		writel_relaxed(val, dcrtc->base + LCD_SPU_DMA_HPXL_VLN);

		val = drm_rect_height(&dest) << 16 | drm_rect_width(&dest);
		dplane->dst_hw = val;
		writel_relaxed(val, dcrtc->base + LCD_SPU_DZM_HPXL_VLN);

		val = dest.y1 << 16 | dest.x1;
		dplane->dst_yx = val;
		writel_relaxed(val, dcrtc->base + LCD_SPU_DMA_OVSA_HPXL_VLN);

		return 0;
	} else if (~dplane->ctrl0 & ctrl0 & CFG_DMA_ENA) {
		/* Power up the Y/U/V FIFOs on ENA 0->1 transitions */
		armada_updatel(0, CFG_PDWN16x66 | CFG_PDWN32x66,
			       dcrtc->base + LCD_SPU_SRAM_PARA1);
	}

	wait_event_timeout(dplane->vbl.wait,
			   list_empty(&dplane->vbl.update.node),
			   HZ/25);

	if (plane->fb != fb) {
		struct armada_gem_object *obj = drm_fb_obj(fb);
		uint32_t addr[3], pixel_format;
		int i, num_planes, hsub;

		/*
		 * Take a reference on the new framebuffer - we want to
		 * hold on to it while the hardware is displaying it.
		 */
		drm_framebuffer_reference(fb);

		if (plane->fb) {
			struct drm_framebuffer *older_fb;

			spin_lock_irq(&dplane->lock);
			older_fb = dplane->old_fb;
			dplane->old_fb = plane->fb;
			spin_unlock_irq(&dplane->lock);
			if (older_fb)
				armada_drm_queue_unref_work(dcrtc->crtc.dev,
							    older_fb);
		}

		src_y = src.y1 >> 16;
		src_x = src.x1 >> 16;

		pixel_format = fb->pixel_format;
		hsub = drm_format_horz_chroma_subsampling(pixel_format);
		num_planes = drm_format_num_planes(pixel_format);

		/*
		 * Annoyingly, shifting a YUYV-format image by one pixel
		 * causes the U/V planes to toggle.  Toggle the UV swap.
		 * (Unfortunately, this causes momentary colour flickering.)
		 */
		if (src_x & (hsub - 1) && num_planes == 1)
			ctrl0 ^= CFG_DMA_MOD(CFG_SWAPUV);

		for (i = 0; i < num_planes; i++)
			addr[i] = obj->dev_addr + fb->offsets[i] +
				  src_y * fb->pitches[i] +
				  src_x * drm_format_plane_cpp(pixel_format, i);
		for (; i < ARRAY_SIZE(addr); i++)
			addr[i] = 0;

		armada_reg_queue_set(dplane->vbl.regs, idx, addr[0],
				     LCD_SPU_DMA_START_ADDR_Y0);
		armada_reg_queue_set(dplane->vbl.regs, idx, addr[1],
				     LCD_SPU_DMA_START_ADDR_U0);
		armada_reg_queue_set(dplane->vbl.regs, idx, addr[2],
				     LCD_SPU_DMA_START_ADDR_V0);
		armada_reg_queue_set(dplane->vbl.regs, idx, addr[0],
				     LCD_SPU_DMA_START_ADDR_Y1);
		armada_reg_queue_set(dplane->vbl.regs, idx, addr[1],
				     LCD_SPU_DMA_START_ADDR_U1);
		armada_reg_queue_set(dplane->vbl.regs, idx, addr[2],
				     LCD_SPU_DMA_START_ADDR_V1);

		val = fb->pitches[0] << 16 | fb->pitches[0];
		armada_reg_queue_set(dplane->vbl.regs, idx, val,
				     LCD_SPU_DMA_PITCH_YC);
		val = fb->pitches[1] << 16 | fb->pitches[2];
		armada_reg_queue_set(dplane->vbl.regs, idx, val,
				     LCD_SPU_DMA_PITCH_UV);
	}

	val = (drm_rect_height(&src) & 0xffff0000) | drm_rect_width(&src) >> 16;
	if (dplane->src_hw != val) {
		dplane->src_hw = val;
		armada_reg_queue_set(dplane->vbl.regs, idx, val,
				     LCD_SPU_DMA_HPXL_VLN);
	}

	val = drm_rect_height(&dest) << 16 | drm_rect_width(&dest);
	if (dplane->dst_hw != val) {
		dplane->dst_hw = val;
		armada_reg_queue_set(dplane->vbl.regs, idx, val,
				     LCD_SPU_DZM_HPXL_VLN);
	}

	val = dest.y1 << 16 | dest.x1;
	if (dplane->dst_yx != val) {
		dplane->dst_yx = val;
		armada_reg_queue_set(dplane->vbl.regs, idx, val,
				     LCD_SPU_DMA_OVSA_HPXL_VLN);
	}

	if (dplane->ctrl0 != ctrl0) {
		dplane->ctrl0 = ctrl0;
		armada_reg_queue_mod(dplane->vbl.regs, idx, ctrl0,
			CFG_CBSH_ENA | CFG_DMAFORMAT | CFG_DMA_FTOGGLE |
			CFG_DMA_HSMOOTH | CFG_DMA_TSTMODE |
			CFG_DMA_MOD(CFG_SWAPRB | CFG_SWAPUV | CFG_SWAPYU |
			CFG_YUV2RGB) | CFG_DMA_ENA,
			LCD_SPU_DMA_CTRL0);
	}
	if (idx) {
		armada_reg_queue_end(dplane->vbl.regs, idx);
		armada_drm_vbl_event_add(dcrtc, &dplane->vbl.update);
	}
	return 0;
}

static int armada_plane_disable(struct drm_plane *plane)
{
	struct armada_plane *dplane = drm_to_armada_plane(plane);
	struct drm_framebuffer *fb;
	struct armada_crtc *dcrtc;

	if (!dplane->base.crtc)
		return 0;

	dcrtc = drm_to_armada_crtc(dplane->base.crtc);
	dcrtc->plane = NULL;

	spin_lock_irq(&dcrtc->irq_lock);
	armada_drm_vbl_event_remove(dcrtc, &dplane->vbl.update);
	armada_updatel(0, CFG_DMA_ENA, dcrtc->base + LCD_SPU_DMA_CTRL0);
	dplane->ctrl0 = 0;
	spin_unlock_irq(&dcrtc->irq_lock);

	/* Power down the Y/U/V FIFOs */
	armada_updatel(CFG_PDWN16x66 | CFG_PDWN32x66, 0,
		       dcrtc->base + LCD_SPU_SRAM_PARA1);

	if (plane->fb)
		drm_framebuffer_unreference(plane->fb);

	spin_lock_irq(&dplane->lock);
	fb = dplane->old_fb;
	dplane->old_fb = NULL;
	spin_unlock_irq(&dplane->lock);
	if (fb)
		drm_framebuffer_unreference(fb);

	return 0;
}

static void armada_plane_destroy(struct drm_plane *plane)
{
	struct armada_plane *dplane = drm_to_armada_plane(plane);

	drm_plane_cleanup(plane);

	kfree(dplane);
}

static int armada_plane_set_property(struct drm_plane *plane,
	struct drm_property *property, uint64_t val)
{
	struct armada_private *priv = plane->dev->dev_private;
	struct armada_plane *dplane = drm_to_armada_plane(plane);
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
		dplane->prop.colorkey_mode &= ~CFG_CKMODE_MASK;
		dplane->prop.colorkey_mode |= CFG_CKMODE(val);
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

	if (update_attr && dplane->base.crtc)
		armada_ovl_update_attr(&dplane->prop,
				       drm_to_armada_crtc(dplane->base.crtc));

	return 0;
}

static const struct drm_plane_funcs armada_plane_funcs = {
	.update_plane	= armada_plane_update,
	.disable_plane	= armada_plane_disable,
	.destroy	= armada_plane_destroy,
	.set_property	= armada_plane_set_property,
};

static const uint32_t armada_formats[] = {
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

static struct drm_prop_enum_list armada_drm_colorkey_enum_list[] = {
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
	struct armada_plane *dplane;
	int ret;

	ret = armada_overlay_create_properties(dev);
	if (ret)
		return ret;

	dplane = kzalloc(sizeof(*dplane), GFP_KERNEL);
	if (!dplane)
		return -ENOMEM;

	spin_lock_init(&dplane->lock);
	init_waitqueue_head(&dplane->vbl.wait);
	armada_drm_vbl_event_init(&dplane->vbl.update, armada_plane_vbl,
				  dplane);

	drm_plane_init(dev, &dplane->base, crtcs, &armada_plane_funcs,
		       armada_formats, ARRAY_SIZE(armada_formats), false);

	dplane->prop.colorkey_yr = 0xfefefe00;
	dplane->prop.colorkey_ug = 0x01010100;
	dplane->prop.colorkey_vb = 0x01010100;
	dplane->prop.colorkey_mode = CFG_CKMODE(CKMODE_RGB);
	dplane->prop.brightness = 0;
	dplane->prop.contrast = 0x4000;
	dplane->prop.saturation = 0x4000;

	mobj = &dplane->base.base;
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
