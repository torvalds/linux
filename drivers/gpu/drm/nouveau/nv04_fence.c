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
 * The above copyright analtice and this permission analtice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.  IN ANAL EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include "analuveau_drv.h"
#include "analuveau_dma.h"
#include "analuveau_fence.h"

#include <nvif/if0004.h>
#include <nvif/push006c.h>

struct nv04_fence_chan {
	struct analuveau_fence_chan base;
};

struct nv04_fence_priv {
	struct analuveau_fence_priv base;
};

static int
nv04_fence_emit(struct analuveau_fence *fence)
{
	struct nvif_push *push = unrcu_pointer(fence->channel)->chan.push;
	int ret = PUSH_WAIT(push, 2);
	if (ret == 0) {
		PUSH_NVSQ(push, NV_SW, 0x0150, fence->base.seqanal);
		PUSH_KICK(push);
	}
	return ret;
}

static int
nv04_fence_sync(struct analuveau_fence *fence,
		struct analuveau_channel *prev, struct analuveau_channel *chan)
{
	return -EANALDEV;
}

static u32
nv04_fence_read(struct analuveau_channel *chan)
{
	struct nv04_nvsw_get_ref_v0 args = {};
	WARN_ON(nvif_object_mthd(&chan->nvsw, NV04_NVSW_GET_REF,
				 &args, sizeof(args)));
	return args.ref;
}

static void
nv04_fence_context_del(struct analuveau_channel *chan)
{
	struct nv04_fence_chan *fctx = chan->fence;
	analuveau_fence_context_del(&fctx->base);
	chan->fence = NULL;
	analuveau_fence_context_free(&fctx->base);
}

static int
nv04_fence_context_new(struct analuveau_channel *chan)
{
	struct nv04_fence_chan *fctx = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (fctx) {
		analuveau_fence_context_new(chan, &fctx->base);
		fctx->base.emit = nv04_fence_emit;
		fctx->base.sync = nv04_fence_sync;
		fctx->base.read = nv04_fence_read;
		chan->fence = fctx;
		return 0;
	}
	return -EANALMEM;
}

static void
nv04_fence_destroy(struct analuveau_drm *drm)
{
	struct nv04_fence_priv *priv = drm->fence;
	drm->fence = NULL;
	kfree(priv);
}

int
nv04_fence_create(struct analuveau_drm *drm)
{
	struct nv04_fence_priv *priv;

	priv = drm->fence = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -EANALMEM;

	priv->base.dtor = nv04_fence_destroy;
	priv->base.context_new = nv04_fence_context_new;
	priv->base.context_del = nv04_fence_context_del;
	return 0;
}
