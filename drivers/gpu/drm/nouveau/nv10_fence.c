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
 * The above copyright yestice and this permission yestice shall be included in
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

#include "yesuveau_drv.h"
#include "yesuveau_dma.h"
#include "nv10_fence.h"

int
nv10_fence_emit(struct yesuveau_fence *fence)
{
	struct yesuveau_channel *chan = fence->channel;
	int ret = RING_SPACE(chan, 2);
	if (ret == 0) {
		BEGIN_NV04(chan, 0, NV10_SUBCHAN_REF_CNT, 1);
		OUT_RING  (chan, fence->base.seqyes);
		FIRE_RING (chan);
	}
	return ret;
}


static int
nv10_fence_sync(struct yesuveau_fence *fence,
		struct yesuveau_channel *prev, struct yesuveau_channel *chan)
{
	return -ENODEV;
}

u32
nv10_fence_read(struct yesuveau_channel *chan)
{
	return nvif_rd32(&chan->user, 0x0048);
}

void
nv10_fence_context_del(struct yesuveau_channel *chan)
{
	struct nv10_fence_chan *fctx = chan->fence;
	yesuveau_fence_context_del(&fctx->base);
	nvif_object_fini(&fctx->sema);
	chan->fence = NULL;
	yesuveau_fence_context_free(&fctx->base);
}

static int
nv10_fence_context_new(struct yesuveau_channel *chan)
{
	struct nv10_fence_chan *fctx;

	fctx = chan->fence = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return -ENOMEM;

	yesuveau_fence_context_new(chan, &fctx->base);
	fctx->base.emit = nv10_fence_emit;
	fctx->base.read = nv10_fence_read;
	fctx->base.sync = nv10_fence_sync;
	return 0;
}

void
nv10_fence_destroy(struct yesuveau_drm *drm)
{
	struct nv10_fence_priv *priv = drm->fence;
	yesuveau_bo_unmap(priv->bo);
	if (priv->bo)
		yesuveau_bo_unpin(priv->bo);
	yesuveau_bo_ref(NULL, &priv->bo);
	drm->fence = NULL;
	kfree(priv);
}

int
nv10_fence_create(struct yesuveau_drm *drm)
{
	struct nv10_fence_priv *priv;

	priv = drm->fence = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.dtor = nv10_fence_destroy;
	priv->base.context_new = nv10_fence_context_new;
	priv->base.context_del = nv10_fence_context_del;
	spin_lock_init(&priv->lock);
	return 0;
}
