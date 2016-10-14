/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <nvif/os.h>
#include <nvif/class.h>
#include <nvif/cl0002.h>

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nv10_fence.h"

#include "nv50_display.h"

static int
nv50_fence_context_new(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->drm->dev;
	struct nv10_fence_priv *priv = chan->drm->fence;
	struct nv10_fence_chan *fctx;
	struct ttm_mem_reg *mem = &priv->bo->bo.mem;
	u32 start = mem->start * PAGE_SIZE;
	u32 limit = start + mem->size - 1;
	int ret, i;

	fctx = chan->fence = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(chan, &fctx->base);
	fctx->base.emit = nv10_fence_emit;
	fctx->base.read = nv10_fence_read;
	fctx->base.sync = nv17_fence_sync;

	ret = nvif_object_init(&chan->user, NvSema, NV_DMA_IN_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_VRAM,
					.access = NV_DMA_V0_ACCESS_RDWR,
					.start = start,
					.limit = limit,
			       }, sizeof(struct nv_dma_v0),
			       &fctx->sema);

	/* dma objects for display sync channel semaphore blocks */
	for (i = 0; !ret && i < dev->mode_config.num_crtc; i++) {
		struct nouveau_bo *bo = nv50_display_crtc_sema(dev, i);
		u32 start = bo->bo.mem.start * PAGE_SIZE;
		u32 limit = start + bo->bo.mem.size - 1;

		ret = nvif_object_init(&chan->user, NvEvoSema0 + i,
				       NV_DMA_IN_MEMORY, &(struct nv_dma_v0) {
						.target = NV_DMA_V0_TARGET_VRAM,
						.access = NV_DMA_V0_ACCESS_RDWR,
						.start = start,
						.limit = limit,
				       }, sizeof(struct nv_dma_v0),
				       &fctx->head[i]);
	}

	if (ret)
		nv10_fence_context_del(chan);
	return ret;
}

int
nv50_fence_create(struct nouveau_drm *drm)
{
	struct nv10_fence_priv *priv;
	int ret = 0;

	priv = drm->fence = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nv10_fence_destroy;
	priv->base.resume = nv17_fence_resume;
	priv->base.context_new = nv50_fence_context_new;
	priv->base.context_del = nv10_fence_context_del;
	priv->base.contexts = 127;
	priv->base.context_base = fence_context_alloc(priv->base.contexts);
	spin_lock_init(&priv->lock);

	ret = nouveau_bo_new(drm->dev, 4096, 0x1000, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, NULL, &priv->bo);
	if (!ret) {
		ret = nouveau_bo_pin(priv->bo, TTM_PL_FLAG_VRAM, false);
		if (!ret) {
			ret = nouveau_bo_map(priv->bo);
			if (ret)
				nouveau_bo_unpin(priv->bo);
		}
		if (ret)
			nouveau_bo_ref(NULL, &priv->bo);
	}

	if (ret) {
		nv10_fence_destroy(drm);
		return ret;
	}

	nouveau_bo_wr32(priv->bo, 0x000, 0x00000000);
	return ret;
}
