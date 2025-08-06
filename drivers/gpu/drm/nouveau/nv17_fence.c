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
#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nv10_fence.h"

#include <nvif/push006c.h>

#include <nvif/class.h>
#include <nvif/cl0002.h>

#include <nvhw/class/cl176e.h>

int
nv17_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	struct nouveau_cli *cli = prev->cli;
	struct nv10_fence_priv *priv = cli->drm->fence;
	struct nv10_fence_chan *fctx = chan->fence;
	struct nvif_push *ppush = &prev->chan.push;
	struct nvif_push *npush = &chan->chan.push;
	u32 value;
	int ret;

	if (!mutex_trylock(&cli->mutex))
		return -EBUSY;

	spin_lock(&priv->lock);
	value = priv->sequence;
	priv->sequence += 2;
	spin_unlock(&priv->lock);

	ret = PUSH_WAIT(ppush, 5);
	if (!ret) {
		PUSH_MTHD(ppush, NV176E, SET_CONTEXT_DMA_SEMAPHORE, fctx->sema.handle,
					 SEMAPHORE_OFFSET, 0,
					 SEMAPHORE_ACQUIRE, value + 0,
					 SEMAPHORE_RELEASE, value + 1);
		PUSH_KICK(ppush);
	}

	if (!ret && !(ret = PUSH_WAIT(npush, 5))) {
		PUSH_MTHD(npush, NV176E, SET_CONTEXT_DMA_SEMAPHORE, fctx->sema.handle,
					 SEMAPHORE_OFFSET, 0,
					 SEMAPHORE_ACQUIRE, value + 1,
					 SEMAPHORE_RELEASE, value + 2);
		PUSH_KICK(npush);
	}

	mutex_unlock(&cli->mutex);
	return 0;
}

static int
nv17_fence_context_new(struct nouveau_channel *chan)
{
	struct nv10_fence_priv *priv = chan->cli->drm->fence;
	struct ttm_resource *reg = priv->bo->bo.resource;
	struct nv10_fence_chan *fctx;
	u32 start = reg->start * PAGE_SIZE;
	u32 limit = start + priv->bo->bo.base.size - 1;
	int ret = 0;

	fctx = chan->fence = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(chan, &fctx->base);
	fctx->base.emit = nv10_fence_emit;
	fctx->base.read = nv10_fence_read;
	fctx->base.sync = nv17_fence_sync;

	ret = nvif_object_ctor(&chan->user, "fenceCtxDma", NvSema,
			       NV_DMA_FROM_MEMORY,
			       &(struct nv_dma_v0) {
					.target = NV_DMA_V0_TARGET_VRAM,
					.access = NV_DMA_V0_ACCESS_RDWR,
					.start = start,
					.limit = limit,
			       }, sizeof(struct nv_dma_v0),
			       &fctx->sema);
	if (ret)
		nv10_fence_context_del(chan);
	return ret;
}

void
nv17_fence_resume(struct nouveau_drm *drm)
{
	struct nv10_fence_priv *priv = drm->fence;

	nouveau_bo_wr32(priv->bo, 0, priv->sequence);
}

int
nv17_fence_create(struct nouveau_drm *drm)
{
	struct nv10_fence_priv *priv;
	int ret = 0;

	priv = drm->fence = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nv10_fence_destroy;
	priv->base.resume = nv17_fence_resume;
	priv->base.context_new = nv17_fence_context_new;
	priv->base.context_del = nv10_fence_context_del;
	spin_lock_init(&priv->lock);

	ret = nouveau_bo_new_map(&drm->client, NOUVEAU_GEM_DOMAIN_VRAM, PAGE_SIZE, &priv->bo);
	if (ret) {
		nv10_fence_destroy(drm);
		return ret;
	}

	nouveau_bo_wr32(priv->bo, 0x000, 0x00000000);
	return ret;
}
