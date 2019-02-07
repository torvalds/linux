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

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_vmm.h"

#include <nvif/user.h>

void
OUT_RINGp(struct nouveau_channel *chan, const void *data, unsigned nr_dwords)
{
	bool is_iomem;
	u32 *mem = ttm_kmap_obj_virtual(&chan->push.buffer->kmap, &is_iomem);
	mem = &mem[chan->dma.cur];
	if (is_iomem)
		memcpy_toio((void __force __iomem *)mem, data, nr_dwords * 4);
	else
		memcpy(mem, data, nr_dwords * 4);
	chan->dma.cur += nr_dwords;
}

/* Fetch and adjust GPU GET pointer
 *
 * Returns:
 *  value >= 0, the adjusted GET pointer
 *  -EINVAL if GET pointer currently outside main push buffer
 *  -EBUSY if timeout exceeded
 */
static inline int
READ_GET(struct nouveau_channel *chan, uint64_t *prev_get, int *timeout)
{
	uint64_t val;

	val = nvif_rd32(&chan->user, chan->user_get);
        if (chan->user_get_hi)
                val |= (uint64_t)nvif_rd32(&chan->user, chan->user_get_hi) << 32;

	/* reset counter as long as GET is still advancing, this is
	 * to avoid misdetecting a GPU lockup if the GPU happens to
	 * just be processing an operation that takes a long time
	 */
	if (val != *prev_get) {
		*prev_get = val;
		*timeout = 0;
	}

	if ((++*timeout & 0xff) == 0) {
		udelay(1);
		if (*timeout > 100000)
			return -EBUSY;
	}

	if (val < chan->push.addr ||
	    val > chan->push.addr + (chan->dma.max << 2))
		return -EINVAL;

	return (val - chan->push.addr) >> 2;
}

void
nv50_dma_push(struct nouveau_channel *chan, u64 offset, int length)
{
	struct nvif_user *user = &chan->drm->client.device.user;
	struct nouveau_bo *pb = chan->push.buffer;
	int ip = (chan->dma.ib_put * 2) + chan->dma.ib_base;

	BUG_ON(chan->dma.ib_free < 1);

	nouveau_bo_wr32(pb, ip++, lower_32_bits(offset));
	nouveau_bo_wr32(pb, ip++, upper_32_bits(offset) | length << 8);

	chan->dma.ib_put = (chan->dma.ib_put + 1) & chan->dma.ib_max;

	mb();
	/* Flush writes. */
	nouveau_bo_rd32(pb, 0);

	nvif_wr32(&chan->user, 0x8c, chan->dma.ib_put);
	if (user->func && user->func->doorbell)
		user->func->doorbell(user, chan->token);
	chan->dma.ib_free--;
}

static int
nv50_dma_push_wait(struct nouveau_channel *chan, int count)
{
	uint32_t cnt = 0, prev_get = 0;

	while (chan->dma.ib_free < count) {
		uint32_t get = nvif_rd32(&chan->user, 0x88);
		if (get != prev_get) {
			prev_get = get;
			cnt = 0;
		}

		if ((++cnt & 0xff) == 0) {
			DRM_UDELAY(1);
			if (cnt > 100000)
				return -EBUSY;
		}

		chan->dma.ib_free = get - chan->dma.ib_put;
		if (chan->dma.ib_free <= 0)
			chan->dma.ib_free += chan->dma.ib_max;
	}

	return 0;
}

static int
nv50_dma_wait(struct nouveau_channel *chan, int slots, int count)
{
	uint64_t prev_get = 0;
	int ret, cnt = 0;

	ret = nv50_dma_push_wait(chan, slots + 1);
	if (unlikely(ret))
		return ret;

	while (chan->dma.free < count) {
		int get = READ_GET(chan, &prev_get, &cnt);
		if (unlikely(get < 0)) {
			if (get == -EINVAL)
				continue;

			return get;
		}

		if (get <= chan->dma.cur) {
			chan->dma.free = chan->dma.max - chan->dma.cur;
			if (chan->dma.free >= count)
				break;

			FIRE_RING(chan);
			do {
				get = READ_GET(chan, &prev_get, &cnt);
				if (unlikely(get < 0)) {
					if (get == -EINVAL)
						continue;
					return get;
				}
			} while (get == 0);
			chan->dma.cur = 0;
			chan->dma.put = 0;
		}

		chan->dma.free = get - chan->dma.cur - 1;
	}

	return 0;
}

int
nouveau_dma_wait(struct nouveau_channel *chan, int slots, int size)
{
	uint64_t prev_get = 0;
	int cnt = 0, get;

	if (chan->dma.ib_max)
		return nv50_dma_wait(chan, slots, size);

	while (chan->dma.free < size) {
		get = READ_GET(chan, &prev_get, &cnt);
		if (unlikely(get == -EBUSY))
			return -EBUSY;

		/* loop until we have a usable GET pointer.  the value
		 * we read from the GPU may be outside the main ring if
		 * PFIFO is processing a buffer called from the main ring,
		 * discard these values until something sensible is seen.
		 *
		 * the other case we discard GET is while the GPU is fetching
		 * from the SKIPS area, so the code below doesn't have to deal
		 * with some fun corner cases.
		 */
		if (unlikely(get == -EINVAL) || get < NOUVEAU_DMA_SKIPS)
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
			OUT_RING(chan, chan->push.addr | 0x20000000);

			/* wait for GET to depart from the skips area.
			 * prevents writing GET==PUT and causing a race
			 * condition that causes us to think the GPU is
			 * idle when it's not.
			 */
			do {
				get = READ_GET(chan, &prev_get, &cnt);
				if (unlikely(get == -EBUSY))
					return -EBUSY;
				if (unlikely(get == -EINVAL))
					continue;
			} while (get <= NOUVEAU_DMA_SKIPS);
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

