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

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_ramht.h"
#include "nouveau_fence.h"

struct nvc0_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_bo *bo;
};

struct nvc0_fence_chan {
	struct nouveau_fence_chan base;
	struct nouveau_vma vma;
};

static int
nvc0_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	struct nvc0_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	u64 addr = fctx->vma.offset + chan->id * 16;
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
nvc0_fence_sync(struct nouveau_fence *fence, struct nouveau_channel *chan)
{
	struct nvc0_fence_chan *fctx = chan->engctx[NVOBJ_ENGINE_FENCE];
	u64 addr = fctx->vma.offset + fence->channel->id * 16;
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
	struct nvc0_fence_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_FENCE);
	return nouveau_bo_rd32(priv->bo, chan->id * 16/4);
}

static void
nvc0_fence_context_del(struct nouveau_channel *chan, int engine)
{
	struct nvc0_fence_priv *priv = nv_engine(chan->dev, engine);
	struct nvc0_fence_chan *fctx = chan->engctx[engine];

	nouveau_bo_vma_del(priv->bo, &fctx->vma);
	nouveau_fence_context_del(&fctx->base);
	chan->engctx[engine] = NULL;
	kfree(fctx);
}

static int
nvc0_fence_context_new(struct nouveau_channel *chan, int engine)
{
	struct nvc0_fence_priv *priv = nv_engine(chan->dev, engine);
	struct nvc0_fence_chan *fctx;
	int ret;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(&fctx->base);

	ret = nouveau_bo_vma_add(priv->bo, chan->vm, &fctx->vma);
	if (ret)
		nvc0_fence_context_del(chan, engine);

	nouveau_bo_wr32(priv->bo, chan->id * 16/4, 0x00000000);
	return ret;
}

static int
nvc0_fence_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static int
nvc0_fence_init(struct drm_device *dev, int engine)
{
	return 0;
}

static void
nvc0_fence_destroy(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_fence_priv *priv = nv_engine(dev, engine);

	nouveau_bo_unmap(priv->bo);
	nouveau_bo_ref(NULL, &priv->bo);
	dev_priv->eng[engine] = NULL;
	kfree(priv);
}

int
nvc0_fence_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nvc0_fence_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.engine.destroy = nvc0_fence_destroy;
	priv->base.engine.init = nvc0_fence_init;
	priv->base.engine.fini = nvc0_fence_fini;
	priv->base.engine.context_new = nvc0_fence_context_new;
	priv->base.engine.context_del = nvc0_fence_context_del;
	priv->base.emit = nvc0_fence_emit;
	priv->base.sync = nvc0_fence_sync;
	priv->base.read = nvc0_fence_read;
	dev_priv->eng[NVOBJ_ENGINE_FENCE] = &priv->base.engine;

	ret = nouveau_bo_new(dev, 16 * pfifo->channels, 0, TTM_PL_FLAG_VRAM,
			     0, 0, NULL, &priv->bo);
	if (ret == 0) {
		ret = nouveau_bo_pin(priv->bo, TTM_PL_FLAG_VRAM);
		if (ret == 0)
			ret = nouveau_bo_map(priv->bo);
		if (ret)
			nouveau_bo_ref(NULL, &priv->bo);
	}

	if (ret)
		nvc0_fence_destroy(dev, NVOBJ_ENGINE_FENCE);
	return ret;
}
