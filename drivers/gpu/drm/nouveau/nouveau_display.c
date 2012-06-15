/*
 * Copyright (C) 2008 Maarten Maathuis.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm_crtc_helper.h"
#include "nouveau_drv.h"
#include "nouveau_fb.h"
#include "nouveau_fbcon.h"
#include "nouveau_hw.h"
#include "nouveau_crtc.h"
#include "nouveau_dma.h"
#include "nouveau_connector.h"
#include "nouveau_software.h"
#include "nouveau_gpio.h"
#include "nouveau_fence.h"
#include "nv50_display.h"

static void
nouveau_user_framebuffer_destroy(struct drm_framebuffer *drm_fb)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);

	if (fb->nvbo)
		drm_gem_object_unreference_unlocked(fb->nvbo->gem);

	drm_framebuffer_cleanup(drm_fb);
	kfree(fb);
}

static int
nouveau_user_framebuffer_create_handle(struct drm_framebuffer *drm_fb,
				       struct drm_file *file_priv,
				       unsigned int *handle)
{
	struct nouveau_framebuffer *fb = nouveau_framebuffer(drm_fb);

	return drm_gem_handle_create(file_priv, fb->nvbo->gem, handle);
}

static const struct drm_framebuffer_funcs nouveau_framebuffer_funcs = {
	.destroy = nouveau_user_framebuffer_destroy,
	.create_handle = nouveau_user_framebuffer_create_handle,
};

int
nouveau_framebuffer_init(struct drm_device *dev,
			 struct nouveau_framebuffer *nv_fb,
			 struct drm_mode_fb_cmd2 *mode_cmd,
			 struct nouveau_bo *nvbo)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_framebuffer *fb = &nv_fb->base;
	int ret;

	ret = drm_framebuffer_init(dev, fb, &nouveau_framebuffer_funcs);
	if (ret) {
		return ret;
	}

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);
	nv_fb->nvbo = nvbo;

	if (dev_priv->card_type >= NV_50) {
		u32 tile_flags = nouveau_bo_tile_layout(nvbo);
		if (tile_flags == 0x7a00 ||
		    tile_flags == 0xfe00)
			nv_fb->r_dma = NvEvoFB32;
		else
		if (tile_flags == 0x7000)
			nv_fb->r_dma = NvEvoFB16;
		else
			nv_fb->r_dma = NvEvoVRAM_LP;

		switch (fb->depth) {
		case  8: nv_fb->r_format = NV50_EVO_CRTC_FB_DEPTH_8; break;
		case 15: nv_fb->r_format = NV50_EVO_CRTC_FB_DEPTH_15; break;
		case 16: nv_fb->r_format = NV50_EVO_CRTC_FB_DEPTH_16; break;
		case 24:
		case 32: nv_fb->r_format = NV50_EVO_CRTC_FB_DEPTH_24; break;
		case 30: nv_fb->r_format = NV50_EVO_CRTC_FB_DEPTH_30; break;
		default:
			 NV_ERROR(dev, "unknown depth %d\n", fb->depth);
			 return -EINVAL;
		}

		if (dev_priv->chipset == 0x50)
			nv_fb->r_format |= (tile_flags << 8);

		if (!tile_flags) {
			if (dev_priv->card_type < NV_D0)
				nv_fb->r_pitch = 0x00100000 | fb->pitches[0];
			else
				nv_fb->r_pitch = 0x01000000 | fb->pitches[0];
		} else {
			u32 mode = nvbo->tile_mode;
			if (dev_priv->card_type >= NV_C0)
				mode >>= 4;
			nv_fb->r_pitch = ((fb->pitches[0] / 4) << 4) | mode;
		}
	}

	return 0;
}

static struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct nouveau_framebuffer *nouveau_fb;
	struct drm_gem_object *gem;
	int ret;

	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (!gem)
		return ERR_PTR(-ENOENT);

	nouveau_fb = kzalloc(sizeof(struct nouveau_framebuffer), GFP_KERNEL);
	if (!nouveau_fb)
		return ERR_PTR(-ENOMEM);

	ret = nouveau_framebuffer_init(dev, nouveau_fb, mode_cmd, nouveau_gem_object(gem));
	if (ret) {
		drm_gem_object_unreference(gem);
		return ERR_PTR(ret);
	}

	return &nouveau_fb->base;
}

static const struct drm_mode_config_funcs nouveau_mode_config_funcs = {
	.fb_create = nouveau_user_framebuffer_create,
	.output_poll_changed = nouveau_fbcon_output_poll_changed,
};


struct nouveau_drm_prop_enum_list {
	u8 gen_mask;
	int type;
	char *name;
};

static struct nouveau_drm_prop_enum_list underscan[] = {
	{ 6, UNDERSCAN_AUTO, "auto" },
	{ 6, UNDERSCAN_OFF, "off" },
	{ 6, UNDERSCAN_ON, "on" },
	{}
};

static struct nouveau_drm_prop_enum_list dither_mode[] = {
	{ 7, DITHERING_MODE_AUTO, "auto" },
	{ 7, DITHERING_MODE_OFF, "off" },
	{ 1, DITHERING_MODE_ON, "on" },
	{ 6, DITHERING_MODE_STATIC2X2, "static 2x2" },
	{ 6, DITHERING_MODE_DYNAMIC2X2, "dynamic 2x2" },
	{ 4, DITHERING_MODE_TEMPORAL, "temporal" },
	{}
};

static struct nouveau_drm_prop_enum_list dither_depth[] = {
	{ 6, DITHERING_DEPTH_AUTO, "auto" },
	{ 6, DITHERING_DEPTH_6BPC, "6 bpc" },
	{ 6, DITHERING_DEPTH_8BPC, "8 bpc" },
	{}
};

#define PROP_ENUM(p,gen,n,list) do {                                           \
	struct nouveau_drm_prop_enum_list *l = (list);                         \
	int c = 0;                                                             \
	while (l->gen_mask) {                                                  \
		if (l->gen_mask & (1 << (gen)))                                \
			c++;                                                   \
		l++;                                                           \
	}                                                                      \
	if (c) {                                                               \
		p = drm_property_create(dev, DRM_MODE_PROP_ENUM, n, c);        \
		l = (list);                                                    \
		c = 0;                                                         \
		while (p && l->gen_mask) {                                     \
			if (l->gen_mask & (1 << (gen))) {                      \
				drm_property_add_enum(p, c, l->type, l->name); \
				c++;                                           \
			}                                                      \
			l++;                                                   \
		}                                                              \
	}                                                                      \
} while(0)

int
nouveau_display_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_display_engine *disp = &dev_priv->engine.display;
	struct drm_connector *connector;
	int ret;

	ret = disp->init(dev);
	if (ret)
		return ret;

	/* power on internal panel if it's not already.  the init tables of
	 * some vbios default this to off for some reason, causing the
	 * panel to not work after resume
	 */
	if (nouveau_gpio_func_get(dev, DCB_GPIO_PANEL_POWER) == 0) {
		nouveau_gpio_func_set(dev, DCB_GPIO_PANEL_POWER, true);
		msleep(300);
	}

	/* enable polling for external displays */
	drm_kms_helper_poll_enable(dev);

	/* enable hotplug interrupts */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		nouveau_gpio_irq(dev, 0, conn->hpd, 0xff, true);
	}

	return ret;
}

void
nouveau_display_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_display_engine *disp = &dev_priv->engine.display;
	struct drm_connector *connector;

	/* disable hotplug interrupts */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		nouveau_gpio_irq(dev, 0, conn->hpd, 0xff, false);
	}

	drm_kms_helper_poll_disable(dev);
	disp->fini(dev);
}

int
nouveau_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_display_engine *disp = &dev_priv->engine.display;
	int ret, gen;

	drm_mode_config_init(dev);
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dvi_i_properties(dev);

	if (dev_priv->card_type < NV_50)
		gen = 0;
	else
	if (dev_priv->card_type < NV_D0)
		gen = 1;
	else
		gen = 2;

	PROP_ENUM(disp->dithering_mode, gen, "dithering mode", dither_mode);
	PROP_ENUM(disp->dithering_depth, gen, "dithering depth", dither_depth);
	PROP_ENUM(disp->underscan_property, gen, "underscan", underscan);

	disp->underscan_hborder_property =
		drm_property_create_range(dev, 0, "underscan hborder", 0, 128);

	disp->underscan_vborder_property =
		drm_property_create_range(dev, 0, "underscan vborder", 0, 128);

	if (gen == 1) {
		disp->vibrant_hue_property =
			drm_property_create(dev, DRM_MODE_PROP_RANGE,
					    "vibrant hue", 2);
		disp->vibrant_hue_property->values[0] = 0;
		disp->vibrant_hue_property->values[1] = 180; /* -90..+90 */

		disp->color_vibrance_property =
			drm_property_create(dev, DRM_MODE_PROP_RANGE,
					    "color vibrance", 2);
		disp->color_vibrance_property->values[0] = 0;
		disp->color_vibrance_property->values[1] = 200; /* -100..+100 */
	}

	dev->mode_config.funcs = &nouveau_mode_config_funcs;
	dev->mode_config.fb_base = pci_resource_start(dev->pdev, 1);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	if (dev_priv->card_type < NV_10) {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	} else
	if (dev_priv->card_type < NV_50) {
		dev->mode_config.max_width = 4096;
		dev->mode_config.max_height = 4096;
	} else {
		dev->mode_config.max_width = 8192;
		dev->mode_config.max_height = 8192;
	}

	dev->mode_config.preferred_depth = 24;
	dev->mode_config.prefer_shadow = 1;

	drm_kms_helper_poll_init(dev);
	drm_kms_helper_poll_disable(dev);

	ret = disp->create(dev);
	if (ret)
		goto disp_create_err;

	if (dev->mode_config.num_crtc) {
		ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
		if (ret)
			goto vblank_err;
	}

	return 0;

vblank_err:
	disp->destroy(dev);
disp_create_err:
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	return ret;
}

void
nouveau_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_display_engine *disp = &dev_priv->engine.display;

	drm_vblank_cleanup(dev);

	disp->destroy(dev);

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
}

int
nouveau_vblank_enable(struct drm_device *dev, int crtc)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		nv_mask(dev, NV50_PDISPLAY_INTR_EN_1, 0,
			NV50_PDISPLAY_INTR_EN_1_VBLANK_CRTC_(crtc));
	else
		NVWriteCRTC(dev, crtc, NV_PCRTC_INTR_EN_0,
			    NV_PCRTC_INTR_0_VBLANK);

	return 0;
}

void
nouveau_vblank_disable(struct drm_device *dev, int crtc)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->card_type >= NV_50)
		nv_mask(dev, NV50_PDISPLAY_INTR_EN_1,
			NV50_PDISPLAY_INTR_EN_1_VBLANK_CRTC_(crtc), 0);
	else
		NVWriteCRTC(dev, crtc, NV_PCRTC_INTR_EN_0, 0);
}

static int
nouveau_page_flip_reserve(struct nouveau_bo *old_bo,
			  struct nouveau_bo *new_bo)
{
	int ret;

	ret = nouveau_bo_pin(new_bo, TTM_PL_FLAG_VRAM);
	if (ret)
		return ret;

	ret = ttm_bo_reserve(&new_bo->bo, false, false, false, 0);
	if (ret)
		goto fail;

	ret = ttm_bo_reserve(&old_bo->bo, false, false, false, 0);
	if (ret)
		goto fail_unreserve;

	return 0;

fail_unreserve:
	ttm_bo_unreserve(&new_bo->bo);
fail:
	nouveau_bo_unpin(new_bo);
	return ret;
}

static void
nouveau_page_flip_unreserve(struct nouveau_bo *old_bo,
			    struct nouveau_bo *new_bo,
			    struct nouveau_fence *fence)
{
	nouveau_bo_fence(new_bo, fence);
	ttm_bo_unreserve(&new_bo->bo);

	nouveau_bo_fence(old_bo, fence);
	ttm_bo_unreserve(&old_bo->bo);

	nouveau_bo_unpin(old_bo);
}

static int
nouveau_page_flip_emit(struct nouveau_channel *chan,
		       struct nouveau_bo *old_bo,
		       struct nouveau_bo *new_bo,
		       struct nouveau_page_flip_state *s,
		       struct nouveau_fence **pfence)
{
	struct nouveau_software_chan *swch = chan->engctx[NVOBJ_ENGINE_SW];
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_device *dev = chan->dev;
	unsigned long flags;
	int ret;

	/* Queue it to the pending list */
	spin_lock_irqsave(&dev->event_lock, flags);
	list_add_tail(&s->head, &swch->flip);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* Synchronize with the old framebuffer */
	ret = nouveau_fence_sync(old_bo->bo.sync_obj, chan);
	if (ret)
		goto fail;

	/* Emit the pageflip */
	ret = RING_SPACE(chan, 3);
	if (ret)
		goto fail;

	if (dev_priv->card_type < NV_C0) {
		BEGIN_NV04(chan, NvSubSw, NV_SW_PAGE_FLIP, 1);
		OUT_RING  (chan, 0x00000000);
		OUT_RING  (chan, 0x00000000);
	} else {
		BEGIN_NVC0(chan, 0, NV10_SUBCHAN_REF_CNT, 1);
		OUT_RING  (chan, 0);
		BEGIN_IMC0(chan, 0, NVSW_SUBCHAN_PAGE_FLIP, 0x0000);
	}
	FIRE_RING (chan);

	ret = nouveau_fence_new(chan, pfence);
	if (ret)
		goto fail;

	return 0;
fail:
	spin_lock_irqsave(&dev->event_lock, flags);
	list_del(&s->head);
	spin_unlock_irqrestore(&dev->event_lock, flags);
	return ret;
}

int
nouveau_crtc_page_flip(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct drm_pending_vblank_event *event)
{
	struct drm_device *dev = crtc->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_bo *old_bo = nouveau_framebuffer(crtc->fb)->nvbo;
	struct nouveau_bo *new_bo = nouveau_framebuffer(fb)->nvbo;
	struct nouveau_page_flip_state *s;
	struct nouveau_channel *chan = NULL;
	struct nouveau_fence *fence;
	int ret;

	if (!dev_priv->channel)
		return -ENODEV;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	/* Don't let the buffers go away while we flip */
	ret = nouveau_page_flip_reserve(old_bo, new_bo);
	if (ret)
		goto fail_free;

	/* Initialize a page flip struct */
	*s = (struct nouveau_page_flip_state)
		{ { }, event, nouveau_crtc(crtc)->index,
		  fb->bits_per_pixel, fb->pitches[0], crtc->x, crtc->y,
		  new_bo->bo.offset };

	/* Choose the channel the flip will be handled in */
	fence = new_bo->bo.sync_obj;
	if (fence)
		chan = nouveau_channel_get_unlocked(fence->channel);
	if (!chan)
		chan = nouveau_channel_get_unlocked(dev_priv->channel);
	mutex_lock(&chan->mutex);

	/* Emit a page flip */
	if (dev_priv->card_type >= NV_50) {
		if (dev_priv->card_type >= NV_D0)
			ret = nvd0_display_flip_next(crtc, fb, chan, 0);
		else
			ret = nv50_display_flip_next(crtc, fb, chan);
		if (ret) {
			nouveau_channel_put(&chan);
			goto fail_unreserve;
		}
	}

	ret = nouveau_page_flip_emit(chan, old_bo, new_bo, s, &fence);
	nouveau_channel_put(&chan);
	if (ret)
		goto fail_unreserve;

	/* Update the crtc struct and cleanup */
	crtc->fb = fb;

	nouveau_page_flip_unreserve(old_bo, new_bo, fence);
	nouveau_fence_unref(&fence);
	return 0;

fail_unreserve:
	nouveau_page_flip_unreserve(old_bo, new_bo, NULL);
fail_free:
	kfree(s);
	return ret;
}

int
nouveau_finish_page_flip(struct nouveau_channel *chan,
			 struct nouveau_page_flip_state *ps)
{
	struct nouveau_software_chan *swch = chan->engctx[NVOBJ_ENGINE_SW];
	struct drm_device *dev = chan->dev;
	struct nouveau_page_flip_state *s;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (list_empty(&swch->flip)) {
		NV_ERROR(dev, "Unexpected pageflip in channel %d.\n", chan->id);
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EINVAL;
	}

	s = list_first_entry(&swch->flip, struct nouveau_page_flip_state, head);
	if (s->event) {
		struct drm_pending_vblank_event *e = s->event;
		struct timeval now;

		do_gettimeofday(&now);
		e->event.sequence = 0;
		e->event.tv_sec = now.tv_sec;
		e->event.tv_usec = now.tv_usec;
		list_add_tail(&e->base.link, &e->base.file_priv->event_list);
		wake_up_interruptible(&e->base.file_priv->event_wait);
	}

	list_del(&s->head);
	if (ps)
		*ps = *s;
	kfree(s);

	spin_unlock_irqrestore(&dev->event_lock, flags);
	return 0;
}

int
nouveau_display_dumb_create(struct drm_file *file_priv, struct drm_device *dev,
			    struct drm_mode_create_dumb *args)
{
	struct nouveau_bo *bo;
	int ret;

	args->pitch = roundup(args->width * (args->bpp / 8), 256);
	args->size = args->pitch * args->height;
	args->size = roundup(args->size, PAGE_SIZE);

	ret = nouveau_gem_new(dev, args->size, 0, TTM_PL_FLAG_VRAM, 0, 0, &bo);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file_priv, bo->gem, &args->handle);
	drm_gem_object_unreference_unlocked(bo->gem);
	return ret;
}

int
nouveau_display_dumb_destroy(struct drm_file *file_priv, struct drm_device *dev,
			     uint32_t handle)
{
	return drm_gem_handle_delete(file_priv, handle);
}

int
nouveau_display_dumb_map_offset(struct drm_file *file_priv,
				struct drm_device *dev,
				uint32_t handle, uint64_t *poffset)
{
	struct drm_gem_object *gem;

	gem = drm_gem_object_lookup(dev, file_priv, handle);
	if (gem) {
		struct nouveau_bo *bo = gem->driver_private;
		*poffset = bo->bo.addr_space_offset;
		drm_gem_object_unreference_unlocked(gem);
		return 0;
	}

	return -ENOENT;
}
