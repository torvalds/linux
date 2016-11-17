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

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

#include "nv50_display.h"

static int
nv84_fence_emit32(struct nouveau_channel *chan, u64 virtual, u32 sequence)
{
	int ret = RING_SPACE(chan, 8);
	if (ret == 0) {
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, chan->vram.handle);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 5);
		OUT_RING  (chan, upper_32_bits(virtual));
		OUT_RING  (chan, lower_32_bits(virtual));
		OUT_RING  (chan, sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG);
		OUT_RING  (chan, 0x00000000);
		FIRE_RING (chan);
	}
	return ret;
}

static int
nv84_fence_sync32(struct nouveau_channel *chan, u64 virtual, u32 sequence)
{
	int ret = RING_SPACE(chan, 7);
	if (ret == 0) {
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, chan->vram.handle);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(virtual));
		OUT_RING  (chan, lower_32_bits(virtual));
		OUT_RING  (chan, sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL);
		FIRE_RING (chan);
	}
	return ret;
}

static int
nv84_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	struct nv84_fence_chan *fctx = chan->fence;
	u64 addr = chan->chid * 16;

	if (fence->sysmem)
		addr += fctx->vma_gart.offset;
	else
		addr += fctx->vma.offset;

	return fctx->base.emit32(chan, addr, fence->base.seqno);
}

static int
nv84_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	struct nv84_fence_chan *fctx = chan->fence;
	u64 addr = prev->chid * 16;

	if (fence->sysmem)
		addr += fctx->vma_gart.offset;
	else
		addr += fctx->vma.offset;

	return fctx->base.sync32(chan, addr, fence->base.seqno);
}

static u32
nv84_fence_read(struct nouveau_channel *chan)
{
	struct nv84_fence_priv *priv = chan->drm->fence;
	return nouveau_bo_rd32(priv->bo, chan->chid * 16/4);
}

static void
nv84_fence_context_del(struct nouveau_channel *chan)
{
	struct nv84_fence_priv *priv = chan->drm->fence;
	struct nv84_fence_chan *fctx = chan->fence;

	nouveau_bo_wr32(priv->bo, chan->chid * 16 / 4, fctx->base.sequence);
	nouveau_bo_vma_del(priv->bo, &fctx->vma_gart);
	nouveau_bo_vma_del(priv->bo, &fctx->vma);
	nouveau_fence_context_del(&fctx->base);
	chan->fence = NULL;
	nouveau_fence_context_free(&fctx->base);
}

int
nv84_fence_context_new(struct nouveau_channel *chan)
{
	struct nouveau_cli *cli = (void *)chan->user.client;
	struct nv84_fence_priv *priv = chan->drm->fence;
	struct nv84_fence_chan *fctx;
	int ret;

	fctx = chan->fence = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(chan, &fctx->base);
	fctx->base.emit = nv84_fence_emit;
	fctx->base.sync = nv84_fence_sync;
	fctx->base.read = nv84_fence_read;
	fctx->base.emit32 = nv84_fence_emit32;
	fctx->base.sync32 = nv84_fence_sync32;
	fctx->base.sequence = nv84_fence_read(chan);

	ret = nouveau_bo_vma_add(priv->bo, cli->vm, &fctx->vma);
	if (ret == 0) {
		ret = nouveau_bo_vma_add(priv->bo_gart, cli->vm,
					&fctx->vma_gart);
	}

	if (ret)
		nv84_fence_context_del(chan);
	return ret;
}

static bool
nv84_fence_suspend(struct nouveau_drm *drm)
{
	struct nv84_fence_priv *priv = drm->fence;
	int i;

	priv->suspend = vmalloc(priv->base.contexts * sizeof(u32));
	if (priv->suspend) {
		for (i = 0; i < priv->base.contexts; i++)
			priv->suspend[i] = nouveau_bo_rd32(priv->bo, i*4);
	}

	return priv->suspend != NULL;
}

static void
nv84_fence_resume(struct nouveau_drm *drm)
{
	struct nv84_fence_priv *priv = drm->fence;
	int i;

	if (priv->suspend) {
		for (i = 0; i < priv->base.contexts; i++)
			nouveau_bo_wr32(priv->bo, i*4, priv->suspend[i]);
		vfree(priv->suspend);
		priv->suspend = NULL;
	}
}

static void
nv84_fence_destroy(struct nouveau_drm *drm)
{
	struct nv84_fence_priv *priv = drm->fence;
	nouveau_bo_unmap(priv->bo_gart);
	if (priv->bo_gart)
		nouveau_bo_unpin(priv->bo_gart);
	nouveau_bo_ref(NULL, &priv->bo_gart);
	nouveau_bo_unmap(priv->bo);
	if (priv->bo)
		nouveau_bo_unpin(priv->bo);
	nouveau_bo_ref(NULL, &priv->bo);
	drm->fence = NULL;
	kfree(priv);
}

int
nv84_fence_create(struct nouveau_drm *drm)
{
	struct nvkm_fifo *fifo = nvxx_fifo(&drm->device);
	struct nv84_fence_priv *priv;
	u32 domain;
	int ret;

	priv = drm->fence = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nv84_fence_destroy;
	priv->base.suspend = nv84_fence_suspend;
	priv->base.resume = nv84_fence_resume;
	priv->base.context_new = nv84_fence_context_new;
	priv->base.context_del = nv84_fence_context_del;

	priv->base.contexts = fifo->nr;
	priv->base.context_base = dma_fence_context_alloc(priv->base.contexts);
	priv->base.uevent = true;

	/* Use VRAM if there is any ; otherwise fallback to system memory */
	domain = drm->device.info.ram_size != 0 ? TTM_PL_FLAG_VRAM :
			 /*
			  * fences created in sysmem must be non-cached or we
			  * will lose CPU/GPU coherency!
			  */
			 TTM_PL_FLAG_TT | TTM_PL_FLAG_UNCACHED;
	ret = nouveau_bo_new(drm->dev, 16 * priv->base.contexts, 0, domain, 0,
			     0, NULL, NULL, &priv->bo);
	if (ret == 0) {
		ret = nouveau_bo_pin(priv->bo, domain, false);
		if (ret == 0) {
			ret = nouveau_bo_map(priv->bo);
			if (ret)
				nouveau_bo_unpin(priv->bo);
		}
		if (ret)
			nouveau_bo_ref(NULL, &priv->bo);
	}

	if (ret == 0)
		ret = nouveau_bo_new(drm->dev, 16 * priv->base.contexts, 0,
				     TTM_PL_FLAG_TT | TTM_PL_FLAG_UNCACHED, 0,
				     0, NULL, NULL, &priv->bo_gart);
	if (ret == 0) {
		ret = nouveau_bo_pin(priv->bo_gart, TTM_PL_FLAG_TT, false);
		if (ret == 0) {
			ret = nouveau_bo_map(priv->bo_gart);
			if (ret)
				nouveau_bo_unpin(priv->bo_gart);
		}
		if (ret)
			nouveau_bo_ref(NULL, &priv->bo_gart);
	}

	if (ret)
		nv84_fence_destroy(drm);
	return ret;
}
