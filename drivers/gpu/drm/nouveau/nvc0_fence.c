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
 * Authors: Ben Skeggs
 */

#include <core/object.h>
#include <core/client.h>
#include <core/class.h>

#include <engine/fifo.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

#include "nv50_display.h"

struct nvc0_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_bo *bo;
	u32 *suspend;
};

struct nvc0_fence_chan {
	struct nouveau_fence_chan base;
	struct nouveau_vma vma;
	struct nouveau_vma dispc_vma[4];
};

u64
nvc0_fence_crtc(struct nouveau_channel *chan, int crtc)
{
	struct nvc0_fence_chan *fctx = chan->fence;
	return fctx->dispc_vma[crtc].offset;
}

static int
nvc0_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	struct nvc0_fence_chan *fctx = chan->fence;
	struct nouveau_fifo_chan *fifo = (void *)chan->object;
	u64 addr = fctx->vma.offset + fifo->chid * 16;
	int ret;

	ret = RING_SPACE(chan, 5);
	if (ret == 0) {
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr));
		OUT_RING  (chan, lower_32_bits(addr));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG);
		FIRE_RING (chan);
	}

	return ret;
}

static int
nvc0_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	struct nvc0_fence_chan *fctx = chan->fence;
	struct nouveau_fifo_chan *fifo = (void *)prev->object;
	u64 addr = fctx->vma.offset + fifo->chid * 16;
	int ret;

	ret = RING_SPACE(chan, 5);
	if (ret == 0) {
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(addr));
		OUT_RING  (chan, lower_32_bits(addr));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL |
				 NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD);
		FIRE_RING (chan);
	}

	return ret;
}

static u32
nvc0_fence_read(struct nouveau_channel *chan)
{
	struct nouveau_fifo_chan *fifo = (void *)chan->object;
	struct nvc0_fence_priv *priv = chan->drm->fence;
	return nouveau_bo_rd32(priv->bo, fifo->chid * 16/4);
}

static void
nvc0_fence_context_del(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->drm->dev;
	struct nvc0_fence_priv *priv = chan->drm->fence;
	struct nvc0_fence_chan *fctx = chan->fence;
	int i;

	if (nv_device(chan->drm->device)->card_type >= NV_D0) {
		for (i = 0; i < dev->mode_config.num_crtc; i++) {
			struct nouveau_bo *bo = nvd0_display_crtc_sema(dev, i);
			nouveau_bo_vma_del(bo, &fctx->dispc_vma[i]);
		}
	} else
	if (nv_device(chan->drm->device)->card_type >= NV_50) {
		for (i = 0; i < dev->mode_config.num_crtc; i++) {
			struct nouveau_bo *bo = nv50_display_crtc_sema(dev, i);
			nouveau_bo_vma_del(bo, &fctx->dispc_vma[i]);
		}
	}

	nouveau_bo_vma_del(priv->bo, &fctx->vma);
	nouveau_fence_context_del(&fctx->base);
	chan->fence = NULL;
	kfree(fctx);
}

static int
nvc0_fence_context_new(struct nouveau_channel *chan)
{
	struct nouveau_fifo_chan *fifo = (void *)chan->object;
	struct nouveau_client *client = nouveau_client(fifo);
	struct nvc0_fence_priv *priv = chan->drm->fence;
	struct nvc0_fence_chan *fctx;
	int ret, i;

	fctx = chan->fence = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(&fctx->base);

	ret = nouveau_bo_vma_add(priv->bo, client->vm, &fctx->vma);
	if (ret)
		nvc0_fence_context_del(chan);

	/* map display semaphore buffers into channel's vm */
	for (i = 0; !ret && i < chan->drm->dev->mode_config.num_crtc; i++) {
		struct nouveau_bo *bo;
		if (nv_device(chan->drm->device)->card_type >= NV_D0)
			bo = nvd0_display_crtc_sema(chan->drm->dev, i);
		else
			bo = nv50_display_crtc_sema(chan->drm->dev, i);

		ret = nouveau_bo_vma_add(bo, client->vm, &fctx->dispc_vma[i]);
	}

	nouveau_bo_wr32(priv->bo, fifo->chid * 16/4, 0x00000000);
	return ret;
}

static bool
nvc0_fence_suspend(struct nouveau_drm *drm)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(drm->device);
	struct nvc0_fence_priv *priv = drm->fence;
	int i;

	priv->suspend = vmalloc((pfifo->max + 1) * sizeof(u32));
	if (priv->suspend) {
		for (i = 0; i <= pfifo->max; i++)
			priv->suspend[i] = nouveau_bo_rd32(priv->bo, i);
	}

	return priv->suspend != NULL;
}

static void
nvc0_fence_resume(struct nouveau_drm *drm)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(drm->device);
	struct nvc0_fence_priv *priv = drm->fence;
	int i;

	if (priv->suspend) {
		for (i = 0; i <= pfifo->max; i++)
			nouveau_bo_wr32(priv->bo, i, priv->suspend[i]);
		vfree(priv->suspend);
		priv->suspend = NULL;
	}
}

static void
nvc0_fence_destroy(struct nouveau_drm *drm)
{
	struct nvc0_fence_priv *priv = drm->fence;
	nouveau_bo_unmap(priv->bo);
	nouveau_bo_ref(NULL, &priv->bo);
	drm->fence = NULL;
	kfree(priv);
}

int
nvc0_fence_create(struct nouveau_drm *drm)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(drm->device);
	struct nvc0_fence_priv *priv;
	int ret;

	priv = drm->fence = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nvc0_fence_destroy;
	priv->base.suspend = nvc0_fence_suspend;
	priv->base.resume = nvc0_fence_resume;
	priv->base.context_new = nvc0_fence_context_new;
	priv->base.context_del = nvc0_fence_context_del;
	priv->base.emit = nvc0_fence_emit;
	priv->base.sync = nvc0_fence_sync;
	priv->base.read = nvc0_fence_read;

	ret = nouveau_bo_new(drm->dev, 16 * (pfifo->max + 1), 0,
			     TTM_PL_FLAG_VRAM, 0, 0, NULL, &priv->bo);
	if (ret == 0) {
		ret = nouveau_bo_pin(priv->bo, TTM_PL_FLAG_VRAM);
		if (ret == 0)
			ret = nouveau_bo_map(priv->bo);
		if (ret)
			nouveau_bo_ref(NULL, &priv->bo);
	}

	if (ret)
		nvc0_fence_destroy(drm);
	return ret;
}
