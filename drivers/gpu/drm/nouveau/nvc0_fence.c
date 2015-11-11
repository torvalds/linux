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

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

#include "nv50_display.h"

static int
nvc0_fence_emit32(struct nouveau_channel *chan, u64 virtual, u32 sequence)
{
	int ret = RING_SPACE(chan, 6);
	if (ret == 0) {
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 5);
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
nvc0_fence_sync32(struct nouveau_channel *chan, u64 virtual, u32 sequence)
{
	int ret = RING_SPACE(chan, 5);
	if (ret == 0) {
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(virtual));
		OUT_RING  (chan, lower_32_bits(virtual));
		OUT_RING  (chan, sequence);
		OUT_RING  (chan, NV84_SUBCHAN_SEMAPHORE_TRIGGER_ACQUIRE_GEQUAL |
				 NVC0_SUBCHAN_SEMAPHORE_TRIGGER_YIELD);
		FIRE_RING (chan);
	}
	return ret;
}

static int
nvc0_fence_context_new(struct nouveau_channel *chan)
{
	int ret = nv84_fence_context_new(chan);
	if (ret == 0) {
		struct nv84_fence_chan *fctx = chan->fence;
		fctx->base.emit32 = nvc0_fence_emit32;
		fctx->base.sync32 = nvc0_fence_sync32;
	}
	return ret;
}

int
nvc0_fence_create(struct nouveau_drm *drm)
{
	int ret = nv84_fence_create(drm);
	if (ret == 0) {
		struct nv84_fence_priv *priv = drm->fence;
		priv->base.context_new = nvc0_fence_context_new;
	}
	return ret;
}
