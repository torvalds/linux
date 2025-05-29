/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <nvif/chan.h>

void
nvif_chan506f_gpfifo_kick(struct nvif_chan *chan)
{
	wmb();
	nvif_wr32(&chan->userd, 0x8c, chan->gpfifo.cur);
}

void
nvif_chan506f_gpfifo_push(struct nvif_chan *chan, bool main, u64 addr, u32 size, bool no_prefetch)
{
	u32 gpptr = chan->gpfifo.cur << 3;

	if (WARN_ON(!chan->gpfifo.free))
		return;

	nvif_wr32(&chan->gpfifo, gpptr + 0, lower_32_bits(addr));
	nvif_wr32(&chan->gpfifo, gpptr + 4, upper_32_bits(addr) |
					    (main ? 0 : BIT(9)) |
					    (size >> 2) << 10 |
					    (no_prefetch ? BIT(31) : 0));

	chan->gpfifo.cur = (chan->gpfifo.cur + 1) & chan->gpfifo.max;
	chan->gpfifo.free--;
	if (!chan->gpfifo.free)
		chan->push.end = chan->push.cur;
}

static u32
nvif_chan506f_gpfifo_read_get(struct nvif_chan *chan)
{
	return nvif_rd32(&chan->userd, 0x88);
}

static u32
nvif_chan506f_read_get(struct nvif_chan *chan)
{
	u32 tlgetlo = nvif_rd32(&chan->userd, 0x58);
	u32 tlgethi = nvif_rd32(&chan->userd, 0x5c);
	struct nvif_push *push = &chan->push;

	/* Update cached GET pointer if TOP_LEVEL_GET is valid. */
	if (tlgethi & BIT(31)) {
		u64 tlget = ((u64)(tlgethi & 0xff) << 32) | tlgetlo;

		push->hw.get = (tlget - push->addr) >> 2;
	}

	return push->hw.get;
}

static const struct nvif_chan_func
nvif_chan506f = {
	.push.read_get = nvif_chan506f_read_get,
	.gpfifo.read_get = nvif_chan506f_gpfifo_read_get,
	.gpfifo.push = nvif_chan506f_gpfifo_push,
	.gpfifo.kick = nvif_chan506f_gpfifo_kick,
};

int
nvif_chan506f_ctor(struct nvif_chan *chan, void *userd, void *gpfifo, u32 gpfifo_size,
		   void *push, u64 push_addr, u32 push_size)
{
	nvif_chan_gpfifo_ctor(&nvif_chan506f, userd, gpfifo, gpfifo_size,
			      push, push_addr, push_size, chan);
	return 0;
}
