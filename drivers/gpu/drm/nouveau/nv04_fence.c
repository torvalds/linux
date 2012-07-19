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
#include <core/ramht.h>
#include "nouveau_fence.h"

struct nv04_fence_chan {
	struct nouveau_fence_chan base;
	atomic_t sequence;
};

struct nv04_fence_priv {
	struct nouveau_fence_priv base;
};

static int
nv04_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	int ret = RING_SPACE(chan, 2);
	if (ret == 0) {
		BEGIN_NV04(chan, NvSubSw, 0x0150, 1);
		OUT_RING  (chan, fence->sequence);
		FIRE_RING (chan);
	}
	return ret;
}

static int
nv04_fence_sync(struct nouveau_fence *fence,
		struct nouveau_channel *prev, struct nouveau_channel *chan)
{
	return -ENODEV;
}

int
nv04_fence_mthd(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	struct nv04_fence_chan *fctx = chan->fence;
	atomic_set(&fctx->sequence, data);
	return 0;
}

static u32
nv04_fence_read(struct nouveau_channel *chan)
{
	struct nv04_fence_chan *fctx = chan->fence;
	return atomic_read(&fctx->sequence);
}

static void
nv04_fence_context_del(struct nouveau_channel *chan)
{
	struct nv04_fence_chan *fctx = chan->fence;
	nouveau_fence_context_del(&fctx->base);
	chan->fence = NULL;
	kfree(fctx);
}

static int
nv04_fence_context_new(struct nouveau_channel *chan)
{
	struct nv04_fence_chan *fctx = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (fctx) {
		nouveau_fence_context_new(&fctx->base);
		atomic_set(&fctx->sequence, 0);
		chan->fence = fctx;
		return 0;
	}
	return -ENOMEM;
}

static void
nv04_fence_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_fence_priv *priv = dev_priv->fence.func;

	dev_priv->fence.func = NULL;
	kfree(priv);
}

int
nv04_fence_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv04_fence_priv *priv;
	int ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nv04_fence_destroy;
	priv->base.context_new = nv04_fence_context_new;
	priv->base.context_del = nv04_fence_context_del;
	priv->base.emit = nv04_fence_emit;
	priv->base.sync = nv04_fence_sync;
	priv->base.read = nv04_fence_read;
	dev_priv->fence.func = &priv->base;
	return ret;
}
