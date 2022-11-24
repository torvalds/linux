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
#include "nouveau_vmm.h"

#include "nv50_display.h"

#include <nvif/push206e.h>

#include <nvhw/class/cl826f.h>

static int
nv84_fence_emit32(struct nouveau_channel *chan, u64 virtual, u32 sequence)
{
	struct nvif_push *push = chan->chan.push;
	int ret = PUSH_WAIT(push, 8);
	if (ret == 0) {
		PUSH_MTHD(push, NV826F, SET_CONTEXT_DMA_SEMAPHORE, chan->vram.handle);

		PUSH_MTHD(push, NV826F, SEMAPHOREA,
			  NVVAL(NV826F, SEMAPHOREA, OFFSET_UPPER, upper_32_bits(virtual)),

					SEMAPHOREB, lower_32_bits(virtual),
					SEMAPHOREC, sequence,

					SEMAPHORED,
			  NVDEF(NV826F, SEMAPHORED, OPERATION, RELEASE),

					NON_STALLED_INTERRUPT, 0);
		PUSH_KICK(push);
	}
	return ret;
}

static int
nv84_fence_sync32(struct nouveau_channel *chan, u64 virtual, u32 sequence)
{
	struct nvif_push *push = chan->chan.push;
	int ret = PUSH_WAIT(push, 7);
	if (ret == 0) {
		PUSH_MTHD(push, NV826F, SET_CONTEXT_DMA_SEMAPHORE, chan->vram.handle);

		PUSH_MTHD(push, NV826F, SEMAPHOREA,
			  NVVAL(NV826F, SEMAPHOREA, OFFSET_UPPER, upper_32_bits(virtual)),

					SEMAPHOREB, lower_32_bits(virtual),
					SEMAPHOREC, sequence,

					SEMAPHORED,
			  NVDEF(NV826F, SEMAPHORED, OPERATION, ACQ_GEQ));
		PUSH_KICK(push);
	}
	return ret;
}

static inline u32
nv84_fence_chid(struct nouveau_channel *chan)
{
	return chan->drm->runl[chan->runlist].chan_id_base + chan->chid;
}

static int
nv84_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	struct nv84_fence_chan *fctx = chan->fence;
	u64 addr = fctx->vma->addr + nv84_fence_chid(chan) * 16;

	return fctx->base.emit32(chan, addr, fence->base.seqno);
}

static int
nv84_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	struct nv84_fence_chan *fctx = chan->fence;
	u64 addr = fctx->vma->addr + nv84_fence_chid(prev) * 16;

	return fctx->base.sync32(chan, addr, fence->base.seqno);
}

static u32
nv84_fence_read(struct nouveau_channel *chan)
{
	struct nv84_fence_priv *priv = chan->drm->fence;
	return nouveau_bo_rd32(priv->bo, nv84_fence_chid(chan) * 16/4);
}

static void
nv84_fence_context_del(struct nouveau_channel *chan)
{
	struct nv84_fence_priv *priv = chan->drm->fence;
	struct nv84_fence_chan *fctx = chan->fence;

	nouveau_bo_wr32(priv->bo, nv84_fence_chid(chan) * 16 / 4, fctx->base.sequence);
	mutex_lock(&priv->mutex);
	nouveau_vma_del(&fctx->vma);
	mutex_unlock(&priv->mutex);
	nouveau_fence_context_del(&fctx->base);
	chan->fence = NULL;
	nouveau_fence_context_free(&fctx->base);
}

int
nv84_fence_context_new(struct nouveau_channel *chan)
{
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

	mutex_lock(&priv->mutex);
	ret = nouveau_vma_new(priv->bo, chan->vmm, &fctx->vma);
	mutex_unlock(&priv->mutex);

	if (ret)
		nv84_fence_context_del(chan);
	return ret;
}

static bool
nv84_fence_suspend(struct nouveau_drm *drm)
{
	struct nv84_fence_priv *priv = drm->fence;
	int i;

	priv->suspend = vmalloc(array_size(sizeof(u32), drm->chan_total));
	if (priv->suspend) {
		for (i = 0; i < drm->chan_total; i++)
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
		for (i = 0; i < drm->chan_total; i++)
			nouveau_bo_wr32(priv->bo, i*4, priv->suspend[i]);
		vfree(priv->suspend);
		priv->suspend = NULL;
	}
}

static void
nv84_fence_destroy(struct nouveau_drm *drm)
{
	struct nv84_fence_priv *priv = drm->fence;
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

	priv->base.uevent = true;

	mutex_init(&priv->mutex);

	/* Use VRAM if there is any ; otherwise fallback to system memory */
	domain = drm->client.device.info.ram_size != 0 ?
		NOUVEAU_GEM_DOMAIN_VRAM :
		 /*
		  * fences created in sysmem must be non-cached or we
		  * will lose CPU/GPU coherency!
		  */
		NOUVEAU_GEM_DOMAIN_GART | NOUVEAU_GEM_DOMAIN_COHERENT;
	ret = nouveau_bo_new(&drm->client, 16 * drm->chan_total, 0,
			     domain, 0, 0, NULL, NULL, &priv->bo);
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

	if (ret)
		nv84_fence_destroy(drm);
	return ret;
}
