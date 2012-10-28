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
#include <core/class.h>

#include <engine/fifo.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

#include "nv50_display.h"

struct nv84_fence_chan {
	struct nouveau_fence_chan base;
};

struct nv84_fence_priv {
	struct nouveau_fence_priv base;
	struct nouveau_gpuobj *mem;
};

static int
nv84_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	struct nouveau_fifo_chan *fifo = (void *)chan->object;
	int ret = RING_SPACE(chan, 7);
	if (ret == 0) {
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, NvSema);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(fifo->chid * 16));
		OUT_RING  (chan, lower_32_bits(fifo->chid * 16));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_WRITE_LONG);
		FIRE_RING (chan);
	}
	return ret;
}


static int
nv84_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	struct nouveau_fifo_chan *fifo = (void *)prev->object;
	int ret = RING_SPACE(chan, 7);
	if (ret == 0) {
		BEGIN_NV04(chan, 0, NV11_SUBCHAN_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, NvSema);
		BEGIN_NV04(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(fifo->chid * 16));
		OUT_RING  (chan, lower_32_bits(fifo->chid * 16));
		OUT_RING  (chan, fence->sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL);
		FIRE_RING (chan);
	}
	return ret;
}

static u32
nv84_fence_read(struct nouveau_channel *chan)
{
	struct nouveau_fifo_chan *fifo = (void *)chan->object;
	struct nv84_fence_priv *priv = chan->drm->fence;
	return nv_ro32(priv->mem, fifo->chid * 16);
}

static void
nv84_fence_context_del(struct nouveau_channel *chan)
{
	struct nv84_fence_chan *fctx = chan->fence;
	nouveau_fence_context_del(&fctx->base);
	chan->fence = NULL;
	kfree(fctx);
}

static int
nv84_fence_context_new(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->drm->dev;
	struct nouveau_fifo_chan *fifo = (void *)chan->object;
	struct nv84_fence_priv *priv = chan->drm->fence;
	struct nv84_fence_chan *fctx;
	struct nouveau_object *object;
	int ret, i;

	fctx = chan->fence = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	nouveau_fence_context_new(&fctx->base);

	ret = nouveau_object_new(nv_object(chan->cli), chan->handle,
				 NvSema, 0x0002,
				 &(struct nv_dma_class) {
					.flags = NV_DMA_TARGET_VRAM |
						 NV_DMA_ACCESS_RDWR,
					.start = priv->mem->addr,
					.limit = priv->mem->addr +
						 priv->mem->size - 1,
				 }, sizeof(struct nv_dma_class),
				 &object);

	/* dma objects for display sync channel semaphore blocks */
	for (i = 0; !ret && i < dev->mode_config.num_crtc; i++) {
		struct nouveau_bo *bo = nv50_display_crtc_sema(dev, i);

		ret = nouveau_object_new(nv_object(chan->cli), chan->handle,
					 NvEvoSema0 + i, 0x003d,
					 &(struct nv_dma_class) {
						.flags = NV_DMA_TARGET_VRAM |
							 NV_DMA_ACCESS_RDWR,
						.start = bo->bo.offset,
						.limit = bo->bo.offset + 0xfff,
					 }, sizeof(struct nv_dma_class),
					 &object);
	}

	if (ret)
		nv84_fence_context_del(chan);
	nv_wo32(priv->mem, fifo->chid * 16, 0x00000000);
	return ret;
}

static void
nv84_fence_destroy(struct nouveau_drm *drm)
{
	struct nv84_fence_priv *priv = drm->fence;
	nouveau_gpuobj_ref(NULL, &priv->mem);
	drm->fence = NULL;
	kfree(priv);
}

int
nv84_fence_create(struct nouveau_drm *drm)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(drm->device);
	struct nv84_fence_priv *priv;
	u32 chan = pfifo->max + 1;
	int ret;

	priv = drm->fence = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nv84_fence_destroy;
	priv->base.context_new = nv84_fence_context_new;
	priv->base.context_del = nv84_fence_context_del;
	priv->base.emit = nv84_fence_emit;
	priv->base.sync = nv84_fence_sync;
	priv->base.read = nv84_fence_read;

	ret = nouveau_gpuobj_new(drm->device, NULL, chan * 16, 0x1000, 0,
				&priv->mem);
	if (ret)
		nv84_fence_destroy(drm);
	return ret;
}
