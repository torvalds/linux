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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include <core/ramht.h>
#include "nouveau_fence.h"
#include "nv50_display.h"

struct nv50_fence_chan {
	struct nouveau_fence_chan base;
};

struct nv50_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_bo *bo;
	spinlock_t lock;
	u32 sequence;
};

static int
nv50_fence_context_new(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nv50_fence_priv *priv = dev_priv->fence.func;
	struct nv50_fence_chan *fctx;
	struct ttm_mem_reg *mem = &priv->bo->bo.mem;
	struct nouveau_gpuobj *obj;
	int ret = 0, i;

	fctx = chan->fence = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(&fctx->base);

	ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_FROM_MEMORY,
				     mem->start * PAGE_SIZE, mem->size,
				     NV_MEM_ACCESS_RW,
				     NV_MEM_TARGET_VRAM, &obj);
	if (!ret) {
		ret = nouveau_ramht_insert(chan, NvSema, obj);
		nouveau_gpuobj_ref(NULL, &obj);
	}

	/* dma objects for display sync channel semaphore blocks */
	for (i = 0; i < chan->dev->mode_config.num_crtc; i++) {
		struct nv50_display *pdisp = nv50_display(chan->dev);
		struct nv50_display_crtc *dispc = &pdisp->crtc[i];
		struct nouveau_gpuobj *obj = NULL;

		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     dispc->sem.bo->bo.offset, 0x1000,
					     NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_VRAM, &obj);
		if (ret)
			break;

		ret = nouveau_ramht_insert(chan, NvEvoSema0 + i, obj);
		nouveau_gpuobj_ref(NULL, &obj);
	}

	if (ret)
		nv10_fence_context_del(chan);
	return ret;
}

int
nv50_fence_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_fence_priv *priv;
	int ret = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nv10_fence_destroy;
	priv->base.context_new = nv50_fence_context_new;
	priv->base.context_del = nv10_fence_context_del;
	priv->base.emit = nv10_fence_emit;
	priv->base.read = nv10_fence_read;
	priv->base.sync = nv17_fence_sync;
	dev_priv->fence.func = &priv->base;
	spin_lock_init(&priv->lock);

	ret = nouveau_bo_new(dev, 4096, 0x1000, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &priv->bo);
	if (!ret) {
		ret = nouveau_bo_pin(priv->bo, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(priv->bo);
		if (ret)
			nouveau_bo_ref(NULL, &priv->bo);
	}

	if (ret == 0)
		nouveau_bo_wr32(priv->bo, 0x000, 0x00000000);
	else
		nv10_fence_destroy(dev);
	return ret;
}
