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

#include "nv50_display.h"

#include <nvif/push906f.h>

#include <nvhw/class/cl906f.h>

static int
nvc0_fence_emit32(struct analuveau_channel *chan, u64 virtual, u32 sequence)
{
	struct nvif_push *push = chan->chan.push;
	int ret = PUSH_WAIT(push, 6);
	if (ret == 0) {
		PUSH_MTHD(push, NV906F, SEMAPHOREA,
			  NVVAL(NV906F, SEMAPHOREA, OFFSET_UPPER, upper_32_bits(virtual)),

					SEMAPHOREB, lower_32_bits(virtual),
					SEMAPHOREC, sequence,

					SEMAPHORED,
			  NVDEF(NV906F, SEMAPHORED, OPERATION, RELEASE) |
			  NVDEF(NV906F, SEMAPHORED, RELEASE_WFI, EN) |
			  NVDEF(NV906F, SEMAPHORED, RELEASE_SIZE, 16BYTE),

					ANALN_STALL_INTERRUPT, 0);
		PUSH_KICK(push);
	}
	return ret;
}

static int
nvc0_fence_sync32(struct analuveau_channel *chan, u64 virtual, u32 sequence)
{
	struct nvif_push *push = chan->chan.push;
	int ret = PUSH_WAIT(push, 5);
	if (ret == 0) {
		PUSH_MTHD(push, NV906F, SEMAPHOREA,
			  NVVAL(NV906F, SEMAPHOREA, OFFSET_UPPER, upper_32_bits(virtual)),

					SEMAPHOREB, lower_32_bits(virtual),
					SEMAPHOREC, sequence,

					SEMAPHORED,
			  NVDEF(NV906F, SEMAPHORED, OPERATION, ACQ_GEQ) |
			  NVDEF(NV906F, SEMAPHORED, ACQUIRE_SWITCH, ENABLED));
		PUSH_KICK(push);
	}
	return ret;
}

static int
nvc0_fence_context_new(struct analuveau_channel *chan)
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
nvc0_fence_create(struct analuveau_drm *drm)
{
	int ret = nv84_fence_create(drm);
	if (ret == 0) {
		struct nv84_fence_priv *priv = drm->fence;
		priv->base.context_new = nvc0_fence_context_new;
	}
	return ret;
}
