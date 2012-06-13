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
#include "nouveau_ramht.h"
#include "nouveau_fence.h"

struct nv10_fence_chan {
	struct nouveau_fence_chan base;
};

struct nv10_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_bo *bo;
	spinlock_t lock;
	u32 sequence;
};

static int
nv10_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	int ret = RING_SPACE(chan, 2);
	if (ret == 0) {
		BEGIN_NV04(chan, 0, NV10_SUBCHAN_REF_CNT, 1);
		OUT_RING  (chan, fence->sequence);
		FIRE_RING (chan);
	}
	return ret;
}


static int
nv10_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	return -ENODEV;
}

static int
nv17_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	struct nv10_fence_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_FENCE);
	u32 value;
	int ret;

	if (!mutex_trylock(&prev->mutex))
		return -EBUSY;

	spin_lock(&priv->lock);
	value = priv->sequence;
	priv->sequence += 2;
	spin_unlock(&priv->lock);

	ret = RING_SPACE(prev, 5);
	if (!ret) {
		BEGIN_NV04(prev, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 4);
		OUT_RING  (prev, NvSema);
		OUT_RING  (prev, 0);
		OUT_RING  (prev, value + 0);
		OUT_RING  (prev, value + 1);
		FIRE_RING (prev);
	}

	if (!ret && !(ret = RING_SPACE(chan, 5))) {
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 4);
		OUT_RING  (chan, NvSema);
		OUT_RING  (chan, 0);
		OUT_RING  (chan, value + 1);
		OUT_RING  (chan, value + 2);
		FIRE_RING (chan);
	}

	mutex_unlock(&prev->mutex);
	return 0;
}

static u32
nv10_fence_read(struct nouveau_channel *chan)
{
	return nvchan_rd32(chan, 0x0048);
}

static void
nv10_fence_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv10_fence_chan *fctx = chan->engctx[engine];
	nouveau_fence_context_del(&fctx->base);
	chan->engctx[engine] = NULL;
	kfree(fctx);
}

static int
nv10_fence_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv10_fence_priv *priv = nv_engine(chan->dev, engine);
	struct nv10_fence_chan *fctx;
	struct nouveau_gpuobj *obj;
	int ret = 0;

	fctx = chan->engctx[engine] = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(&fctx->base);

	if (priv->bo) {
		struct ttm_mem_reg *mem = &priv->bo->bo.mem;

		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_FROM_MEMORY,
					     mem->start * PAGE_SIZE, mem->size,
					     NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_VRAM, &obj);
		if (!ret) {
			ret = nouveau_ramht_insert(chan, NvSema, obj);
			nouveau_gpuobj_ref(NULL, &obj);
		}
	}

	if (ret)
		nv10_fence_context_del(chan, engine);
	return ret;
}

static int
nv10_fence_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static int
nv10_fence_init(struct drm_device *dev, int engine)
{
	return 0;
}

static void
nv10_fence_destroy(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv10_fence_priv *priv = nv_engine(dev, engine);

	nouveau_bo_ref(NULL, &priv->bo);
	dev_priv->eng[engine] = NULL;
	kfree(priv);
}

int
nv10_fence_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv10_fence_priv *priv;
	int ret = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.engine.destroy = nv10_fence_destroy;
	priv->base.engine.init = nv10_fence_init;
	priv->base.engine.fini = nv10_fence_fini;
	priv->base.engine.context_new = nv10_fence_context_new;
	priv->base.engine.context_del = nv10_fence_context_del;
	priv->base.emit = nv10_fence_emit;
	priv->base.read = nv10_fence_read;
	priv->base.sync = nv10_fence_sync;
	dev_priv->eng[NVOBJ_ENGINE_FENCE] = &priv->base.engine;
	spin_lock_init(&priv->lock);

	if (dev_priv->chipset >= 0x17) {
		ret = nouveau_bo_new(dev, 4096, 0x1000, TTM_PL_FLAG_VRAM,
				     0, 0x0000, NULL, &priv->bo);
		if (!ret) {
			ret = nouveau_bo_pin(priv->bo, TTM_PL_FLAG_VRAM);
			if (!ret)
				ret = nouveau_bo_map(priv->bo);
			if (ret)
				nouveau_bo_ref(NULL, &priv->bo);
		}

		if (ret == 0) {
			nouveau_bo_wr32(priv->bo, 0x000, 0x00000000);
			priv->base.sync = nv17_fence_sync;
		}
	}

	if (ret)
		nv10_fence_destroy(dev, NVOBJ_ENGINE_FENCE);
	return ret;
}
