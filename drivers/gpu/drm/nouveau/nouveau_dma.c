/*
 * Copyright (C) 2007 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_dma.h"

void
nouveau_dma_pre_init(struct nouveau_channel *chan)
{
	chan->dma.max  = (chan->pushbuf_bo->bo.mem.size >> 2) - 2;
	chan->dma.put  = 0;
	chan->dma.cur  = chan->dma.put;
	chan->dma.free = chan->dma.max - chan->dma.cur;
}

int
nouveau_dma_init(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *m2mf = NULL;
	struct nouveau_gpuobj *nvsw = NULL;
	int ret, i;

	/* Create NV_MEMORY_TO_MEMORY_FORMAT for buffer moves */
	ret = nouveau_gpuobj_gr_new(chan, dev_priv->card_type < NV_50 ?
				    0x0039 : 0x5039, &m2mf);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_ref_add(dev, chan, NvM2MF, m2mf, NULL);
	if (ret)
		return ret;

	/* Create an NV_SW object for various sync purposes */
	ret = nouveau_gpuobj_sw_new(chan, NV_SW, &nvsw);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_ref_add(dev, chan, NvSw, nvsw, NULL);
	if (ret)
		return ret;

	/* NV_MEMORY_TO_MEMORY_FORMAT requires a notifier object */
	ret = nouveau_notifier_alloc(chan, NvNotify0, 32, &chan->m2mf_ntfy);
	if (ret)
		return ret;

	/* Map push buffer */
	ret = nouveau_bo_map(chan->pushbuf_bo);
	if (ret)
		return ret;

	/* Map M2MF notifier object - fbcon. */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = nouveau_bo_map(chan->notifier_bo);
		if (ret)
			return ret;
	}

	/* Insert NOPS for NOUVEAU_DMA_SKIPS */
	ret = RING_SPACE(chan, NOUVEAU_DMA_SKIPS);
	if (ret)
		return ret;

	for (i = 0; i < NOUVEAU_DMA_SKIPS; i++)
		OUT_RING(chan, 0);

	/* Initialise NV_MEMORY_TO_MEMORY_FORMAT */
	ret = RING_SPACE(chan, 4);
	if (ret)
		return ret;
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_NAME, 1);
	OUT_RING(chan, NvM2MF);
	BEGIN_RING(chan, NvSubM2MF, NV_MEMORY_TO_MEMORY_FORMAT_DMA_NOTIFY, 1);
	OUT_RING(chan, NvNotify0);

	/* Initialise NV_SW */
	ret = RING_SPACE(chan, 2);
	if (ret)
		return ret;
	BEGIN_RING(chan, NvSubSw, 0, 1);
	OUT_RING(chan, NvSw);

	/* Sit back and pray the channel works.. */
	FIRE_RING(chan);

	return 0;
}

void
OUT_RINGp(struct nouveau_channel *chan, const void *data, unsigned nr_dwords)
{
	bool is_iomem;
	u32 *mem = ttm_kmap_obj_virtual(&chan->pushbuf_bo->kmap, &is_iomem);
	mem = &mem[chan->dma.cur];
	if (is_iomem)
		memcpy_toio((void __force __iomem *)mem, data, nr_dwords * 4);
	else
		memcpy(mem, data, nr_dwords * 4);
	chan->dma.cur += nr_dwords;
}

static inline bool
READ_GET(struct nouveau_channel *chan, uint32_t *get)
{
	uint32_t val;

	val = nvchan_rd32(chan, chan->user_get);
	if (val < chan->pushbuf_base ||
	    val > chan->pushbuf_base + (chan->dma.max << 2)) {
		/* meaningless to dma_wait() except to know whether the
		 * GPU has stalled or not
		 */
		*get = val;
		return false;
	}

	*get = (val - chan->pushbuf_base) >> 2;
	return true;
}

int
nouveau_dma_wait(struct nouveau_channel *chan, int size)
{
	uint32_t get, prev_get = 0, cnt = 0;
	bool get_valid;

	while (chan->dma.free < size) {
		/* reset counter as long as GET is still advancing, this is
		 * to avoid misdetecting a GPU lockup if the GPU happens to
		 * just be processing an operation that takes a long time
		 */
		get_valid = READ_GET(chan, &get);
		if (get != prev_get) {
			prev_get = get;
			cnt = 0;
		}

		if ((++cnt & 0xff) == 0) {
			DRM_UDELAY(1);
			if (cnt > 100000)
				return -EBUSY;
		}

		/* loop until we have a usable GET pointer.  the value
		 * we read from the GPU may be outside the main ring if
		 * PFIFO is processing a buffer called from the main ring,
		 * discard these values until something sensible is seen.
		 *
		 * the other case we discard GET is while the GPU is fetching
		 * from the SKIPS area, so the code below doesn't have to deal
		 * with some fun corner cases.
		 */
		if (!get_valid || get < NOUVEAU_DMA_SKIPS)
			continue;

		if (get <= chan->dma.cur) {
			/* engine is fetching behind us, or is completely
			 * idle (GET == PUT) so we have free space up until
			 * the end of the push buffer
			 *
			 * we can only hit that path once per call due to
			 * looping back to the beginning of the push buffer,
			 * we'll hit the fetching-ahead-of-us path from that
			 * point on.
			 *
			 * the *one* exception to that rule is if we read
			 * GET==PUT, in which case the below conditional will
			 * always succeed and break us out of the wait loop.
			 */
			chan->dma.free = chan->dma.max - chan->dma.cur;
			if (chan->dma.free >= size)
				break;

			/* not enough space left at the end of the push buffer,
			 * instruct the GPU to jump back to the start right
			 * after processing the currently pending commands.
			 */
			OUT_RING(chan, chan->pushbuf_base | 0x20000000);
			WRITE_PUT(NOUVEAU_DMA_SKIPS);

			/* we're now submitting commands at the start of
			 * the push buffer.
			 */
			chan->dma.cur  =
			chan->dma.put  = NOUVEAU_DMA_SKIPS;
		}

		/* engine fetching ahead of us, we have space up until the
		 * current GET pointer.  the "- 1" is to ensure there's
		 * space left to emit a jump back to the beginning of the
		 * push buffer if we require it.  we can never get GET == PUT
		 * here, so this is safe.
		 */
		chan->dma.free = get - chan->dma.cur - 1;
	}

	return 0;
}

