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
			 struct drm_mode_fb_cmd *mode_cmd,
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
				nv_fb->r_pitch = 0x00100000 | fb->pitch;
			else
				nv_fb->r_pitch = 0x01000000 | fb->pitch;
		} else {
			u32 mode = nvbo->tile_mode;
			if (dev_priv->card_type >= NV_C0)
				mode >>= 4;
			nv_fb->r_pitch = ((fb->pitch / 4) << 4) | mode;
		}
	}

	return 0;
}

static struct drm_framebuffer *
nouveau_user_framebuffer_create(struct drm_device *dev,
				struct drm_file *file_priv,
				struct drm_mode_fb_cmd *mode_cmd)
{
	struct nouveau_framebuffer *nouveau_fb;
	struct drm_gem_object *gem;
	int ret;

	gem = drm_gem_object_lookup(dev, file_priv, mode_cmd->handle);
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

const struct drm_mode_config_funcs nouveau_mode_config_funcs = {
	.fb_create = nouveau_user_framebuffer_create,
	.output_poll_changed = nouveau_fbcon_output_poll_changed,
};

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
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct drm_device *dev = chan->dev;
	unsigned long flags;
	int ret;

	/* Queue it to the pending list */
	spin_lock_irqsave(&dev->event_lock, flags);
	list_add_tail(&s->head, &chan->nvsw.flip);
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/* Synchronize with the old framebuffer */
	ret = nouveau_fence_sync(old_bo->bo.sync_obj, chan);
	if (ret)
		goto fail;

	/* Emit the pageflip */
	ret = RING_SPACE(chan, 2);
	if (ret)
		goto fail;

	if (dev_priv->card_type < NV_C0)
		BEGIN_RING(chan, NvSubSw, NV_SW_PAGE_FLIP, 1);
	else
		BEGIN_NVC0(chan, 2, NvSubM2MF, 0x0500, 1);
	OUT_RING  (chan, 0);
	FIRE_RING (chan);

	ret = nouveau_fence_new(chan, pfence, true);
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
	struct nouveau_channel *chan;
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
		  fb->bits_per_pixel, fb->pitch, crtc->x, crtc->y,
		  new_bo->bo.offset };

	/* Choose the channel the flip will be handled in */
	chan = nouveau_fence_channel(new_bo->bo.sync_obj);
	if (!chan)
		chan = nouveau_channel_get_unlocked(dev_priv->channel);
	mutex_lock(&chan->mutex);

	/* Emit a page flip */
	if (dev_priv->card_type >= NV_50) {
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
	struct drm_device *dev = chan->dev;
	struct nouveau_page_flip_state *s;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);

	if (list_empty(&chan->nvsw.flip)) {
		NV_ERROR(dev, "Unexpected pageflip in channel %d.\n", chan->id);
		spin_unlock_irqrestore(&dev->event_lock, flags);
		return -EINVAL;
	}

	s = list_first_entry(&chan->nvsw.flip,
			     struct nouveau_page_flip_state, head);
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
