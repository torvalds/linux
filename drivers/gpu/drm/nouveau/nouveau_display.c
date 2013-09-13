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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/ttm/ttm_execbuf_util.h>

#include "nouveau_fbcon.h"
#include "dispnv04/hw.h"
#include "nouveau_crtc.h"
#include "nouveau_dma.h"
#include "nouveau_gem.h"
#include "nouveau_connector.h"
#include "nv50_display.h"

#include "nouveau_fence.h"

#include <subdev/bios/gpio.h>
#include <subdev/gpio.h>
#include <engine/disp.h>

#include <core/class.h>

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
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_framebuffer *fb = &nv_fb->base;
	int ret;

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);
	nv_fb->nvbo = nvbo;

	if (nv_device(drm->device)->card_type >= NV_50) {
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
		case  8: nv_fb->r_format = 0x1e00; break;
		case 15: nv_fb->r_format = 0xe900; break;
		case 16: nv_fb->r_format = 0xe800; break;
		case 24:
		case 32: nv_fb->r_format = 0xcf00; break;
		case 30: nv_fb->r_format = 0xd100; break;
		default:
			 NV_ERROR(drm, "unknown depth %d\n", fb->depth);
			 return -EINVAL;
		}

		if (nvbo->tile_flags & NOUVEAU_GEM_TILE_NONCONTIG) {
			NV_ERROR(drm, "framebuffer requires contiguous bo\n");
			return -EINVAL;
		}

		if (nv_device(drm->device)->chipset == 0x50)
			nv_fb->r_format |= (tile_flags << 8);

		if (!tile_flags) {
			if (nv_device(drm->device)->card_type < NV_D0)
				nv_fb->r_pitch = 0x00100000 | fb->pitches[0];
			else
				nv_fb->r_pitch = 0x01000000 | fb->pitches[0];
		} else {
			u32 mode = nvbo->tile_mode;
			if (nv_device(drm->device)->card_type >= NV_C0)
				mode >>= 4;
			nv_fb->r_pitch = ((fb->pitches[0] / 4) << 4) | mode;
		}
	}

	ret = drm_framebuffer_init(dev, fb, &nouveau_framebuffer_funcs);
	if (ret) {
		return ret;
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
	int ret = -ENOMEM;

	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handles[0]);
	if (!gem)
		return ERR_PTR(-ENOENT);

	nouveau_fb = kzalloc(sizeof(struct nouveau_framebuffer), GFP_KERNEL);
	if (!nouveau_fb)
		goto err_unref;

	ret = nouveau_framebuffer_init(dev, nouveau_fb, mode_cmd, nouveau_gem_object(gem));
	if (ret)
		goto err;

	return &nouveau_fb->base;

err:
	kfree(nouveau_fb);
err_unref:
	drm_gem_object_unreference(gem);
	return ERR_PTR(ret);
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
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	struct drm_connector *connector;
	int ret;

	ret = disp->init(dev);
	if (ret)
		return ret;

	/* enable polling for external displays */
	drm_kms_helper_poll_enable(dev);

	/* enable hotplug interrupts */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		if (gpio && conn->hpd.func != DCB_GPIO_UNUSED) {
			nouveau_event_get(gpio->events, conn->hpd.line,
					 &conn->hpd_func);
		}
	}

	return ret;
}

void
nouveau_display_fini(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_display *disp = nouveau_display(dev);
	struct nouveau_gpio *gpio = nouveau_gpio(drm->device);
	struct drm_connector *connector;

	/* disable hotplug interrupts */
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		struct nouveau_connector *conn = nouveau_connector(connector);
		if (gpio && conn->hpd.func != DCB_GPIO_UNUSED) {
			nouveau_event_put(gpio->events, conn->hpd.line,
					 &conn->hpd_func);
		}
	}

	drm_kms_helper_poll_disable(dev);
	disp->fini(dev);
}

int
nouveau_display_create(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_display *disp;
	u32 pclass = dev->pdev->class >> 8;
	int ret, gen;

	disp = drm->display = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;

	drm_mode_config_init(dev);
	drm_mode_create_scaling_mode_property(dev);
	drm_mode_create_dvi_i_properties(dev);

	if (nv_device(drm->device)->card_type < NV_50)
		gen = 0;
	else
	if (nv_device(drm->device)->card_type < NV_D0)
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

	if (gen >= 1) {
		/* -90..+90 */
		disp->vibrant_hue_property =
			drm_property_create_range(dev, 0, "vibrant hue", 0, 180);

		/* -100..+100 */
		disp->color_vibrance_property =
			drm_property_create_range(dev, 0, "color vibrance", 0, 200);
	}

	dev->mode_config.funcs = &nouveau_mode_config_funcs;
	dev->mode_config.fb_base = pci_resource_start(dev->pdev, 1);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	if (nv_device(drm->device)->card_type < NV_10) {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	} else
	if (nv_device(drm->device)->card_type < NV_50) {
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

	if (nouveau_modeset == 1 ||
	    (nouveau_modeset < 0 && pclass == PCI_CLASS_DISPLAY_VGA)) {
		if (drm->vbios.dcb.entries) {
			if (nv_device(drm->device)->card_type < NV_50)
				ret = nv04_display_create(dev);
			else
				ret = nv50_display_create(dev);
		} else {
			ret = 0;
		}

		if (ret)
			goto disp_create_err;

		if (dev->mode_config.num_crtc) {
			ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
			if (ret)
				goto vblank_err;
		}

		nouveau_backlight_init(dev);
	}

	return 0;

vblank_err:
	disp->dtor(dev);
disp_create_err:
	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);
	return ret;
}

void
nouveau_display_destroy(struct drm_device *dev)
{
	struct nouveau_display *disp = nouveau_display(dev);

	nouveau_backlight_exit(dev);
	drm_vblank_cleanup(dev);

	drm_kms_helper_poll_fini(dev);
	drm_mode_config_cleanup(dev);

	if (disp->dtor)
		disp->dtor(dev);

	nouveau_drm(dev)->display = NULL;
	kfree(disp);
}

int
nouveau_display_suspend(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_crtc *crtc;

	nouveau_display_fini(dev);

	NV_SUSPEND(drm, "unpinning framebuffer(s)...\n");
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_framebuffer *nouveau_fb;

		nouveau_fb = nouveau_framebuffer(crtc->fb);
		if (!nouveau_fb || !nouveau_fb->nvbo)
			continue;

		nouveau_bo_unpin(nouveau_fb->nvbo);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		nouveau_bo_unmap(nv_crtc->cursor.nvbo);
		nouveau_bo_unpin(nv_crtc->cursor.nvbo);
	}

	return 0;
}

void
nouveau_display_repin(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct drm_crtc *crtc;
	int ret;

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_framebuffer *nouveau_fb;

		nouveau_fb = nouveau_framebuffer(crtc->fb);
		if (!nouveau_fb || !nouveau_fb->nvbo)
			continue;

		nouveau_bo_pin(nouveau_fb->nvbo, TTM_PL_FLAG_VRAM);
	}

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		ret = nouveau_bo_pin(nv_crtc->cursor.nvbo, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(nv_crtc->cursor.nvbo);
		if (ret)
			NV_ERROR(drm, "Could not pin/map cursor.\n");
	}
}

void
nouveau_display_resume(struct drm_device *dev)
{
	struct drm_crtc *crtc;
	nouveau_display_init(dev);

	/* Force CLUT to get re-loaded during modeset */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);

		nv_crtc->lut.depth = 0;
	}

	drm_helper_resume_force_mode(dev);

	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
		u32 offset = nv_crtc->cursor.nvbo->bo.offset;

		nv_crtc->cursor.set_offset(nv_crtc, offset);
		nv_crtc->cursor.set_pos(nv_crtc, nv_crtc->cursor_saved_x,
						 nv_crtc->cursor_saved_y);
	}
}

static int
nouveau_page_flip_emit(struct nouveau_channel *chan,
		       struct nouveau_bo *old_bo,
		       struct nouveau_bo *new_bo,
		       struct nouveau_page_flip_state *s,
		       struct nouveau_fence **pfence)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_drm *drm = chan->drm;
	struct drm_device *dev = drm->dev;
	unsigned long flags;
	int ret;

	/* Queue it to the pending list */
	spin_lock_irqsave(&dev->event_lock, flags);
	list_add_tail(&s->head, &fctx->flip);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* Synchronize with the old framebuffer */
	ret = nouveau_fence_sync(old_bo->bo.sync_obj, chan);
	if (ret)
		goto fail;

	/* Emit the pageflip */
	ret = RING_SPACE(chan, 3);
	if (ret)
		goto fail;

	if (nv_device(drm->device)->card_type < NV_C0) {
		BEGIN_NV04(chan, NvSubSw, NV_SW_PAGE_FLIP, 1);
		OUT_RING  (chan, 0x00000000);
		OUT_RING  (chan, 0x00000000);
	} else {
		BEGIN_NVC0(chan, 0, NV10_SUBCHAN_REF_CNT, 1);
		OUT_RING  (chan, 0);
		BEGIN_IMC0(chan, 0, NVSW_SUBCHAN_PAGE_FLIP, 0x0000);
	}
	FIRE_RING (chan);

	ret = nouveau_fence_new(chan, false, pfence);
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
		       struct drm_pending_vblank_event *event,
		       uint32_t page_flip_flags)
{
	struct drm_device *dev = crtc->dev;
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_bo *old_bo = nouveau_framebuffer(crtc->fb)->nvbo;
	struct nouveau_bo *new_bo = nouveau_framebuffer(fb)->nvbo;
	struct nouveau_page_flip_state *s;
	struct nouveau_channel *chan = NULL;
	struct nouveau_fence *fence;
	struct ttm_validate_buffer resv[2] = {
		{ .bo = &old_bo->bo },
		{ .bo = &new_bo->bo },
	};
	struct ww_acquire_ctx ticket;
	LIST_HEAD(res);
	int ret;

	if (!drm->channel)
		return -ENODEV;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	/* Choose the channel the flip will be handled in */
	spin_lock(&old_bo->bo.bdev->fence_lock);
	fence = new_bo->bo.sync_obj;
	if (fence)
		chan = fence->channel;
	if (!chan)
		chan = drm->channel;
	spin_unlock(&old_bo->bo.bdev->fence_lock);

	if (new_bo != old_bo) {
		ret = nouveau_bo_pin(new_bo, TTM_PL_FLAG_VRAM);
		if (ret)
			goto fail_free;

		list_add(&resv[1].head, &res);
	}
	list_add(&resv[0].head, &res);

	mutex_lock(&chan->cli->mutex);
	ret = ttm_eu_reserve_buffers(&ticket, &res);
	if (ret)
		goto fail_unpin;

	/* Initialize a page flip struct */
	*s = (struct nouveau_page_flip_state)
		{ { }, event, nouveau_crtc(crtc)->index,
		  fb->bits_per_pixel, fb->pitches[0], crtc->x, crtc->y,
		  new_bo->bo.offset };

	/* Emit a page flip */
	if (nv_device(drm->device)->card_type >= NV_50) {
		ret = nv50_display_flip_next(crtc, fb, chan, 0);
		if (ret)
			goto fail_unreserve;
	} else {
		struct nv04_display *dispnv04 = nv04_display(dev);
		nouveau_bo_ref(new_bo, &dispnv04->image[nouveau_crtc(crtc)->index]);
	}

	ret = nouveau_page_flip_emit(chan, old_bo, new_bo, s, &fence);
	mutex_unlock(&chan->cli->mutex);
	if (ret)
		goto fail_unreserve;

	/* Update the crtc struct and cleanup */
	crtc->fb = fb;

	ttm_eu_fence_buffer_objects(&ticket, &res, fence);
	if (old_bo != new_bo)
		nouveau_bo_unpin(old_bo);
	nouveau_fence_unref(&fence);
	return 0;

fail_unreserve:
	ttm_eu_backoff_reservation(&ticket, &res);
fail_unpin:
	mutex_unlock(&chan->cli->mutex);
	if (old_bo != new_bo)
		nouveau_bo_unpin(new_bo);
fail_free:
	kfree(s);
	return ret;
}

int
nouveau_finish_page_flip(struct nouveau_channel *chan,
			 struct nouveau_page_flip_state *ps)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_drm *drm = chan->drm;
	struct drm_device *dev = drm->dev;
	struct nouveau_page_flip_state *s;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (list_empty(&fctx->flip)) {
		NV_ERROR(drm, "unexpected pageflip\n");
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EINVAL;
	}

	s = list_first_entry(&fctx->flip, struct nouveau_page_flip_state, head);
	if (s->event)
		drm_send_vblank_event(dev, -1, s->event);

	list_del(&s->head);
	if (ps)
		*ps = *s;
	kfree(s);

	spin_unlock_irqrestore(&dev->event_lock, flags);
	return 0;
}

int
nouveau_flip_complete(void *data)
{
	struct nouveau_channel *chan = data;
	struct nouveau_drm *drm = chan->drm;
	struct nouveau_page_flip_state state;

	if (!nouveau_finish_page_flip(chan, &state)) {
		if (nv_device(drm->device)->card_type < NV_50) {
			nv_set_crtc_base(drm->dev, state.crtc, state.offset +
					 state.y * state.pitch +
					 state.x * state.bpp / 8);
		}
	}

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

	ret = nouveau_gem_new(dev, args->size, 0, NOUVEAU_GEM_DOMAIN_VRAM, 0, 0, &bo);
	if (ret)
		return ret;

	ret = drm_gem_handle_create(file_priv, bo->gem, &args->handle);
	drm_gem_object_unreference_unlocked(bo->gem);
	return ret;
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
		*poffset = drm_vma_node_offset_addr(&bo->bo.vma_node);
		drm_gem_object_unreference_unlocked(gem);
		return 0;
	}

	return -ENOENT;
}
