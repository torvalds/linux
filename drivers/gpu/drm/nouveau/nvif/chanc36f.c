/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <nvif/chan.h>
#include <nvif/user.h>

static void
nvif_chanc36f_gpfifo_kick(struct nvif_chan *chan)
{
	struct nvif_user *usermode = chan->usermode;

	nvif_wr32(&chan->userd, 0x8c, chan->gpfifo.cur);

	wmb(); /* ensure CPU writes are flushed to BAR1 */
	nvif_rd32(&chan->userd, 0); /* ensure BAR1 writes are flushed to vidmem */

	usermode->func->doorbell(usermode, chan->doorbell_token);
}

static const struct nvif_chan_func
nvif_chanc36f = {
	.push.read_get = nvif_chan506f_read_get,
	.gpfifo.read_get = nvif_chan506f_gpfifo_read_get,
	.gpfifo.push = nvif_chan506f_gpfifo_push,
	.gpfifo.kick = nvif_chanc36f_gpfifo_kick,
};

int
nvif_chanc36f_ctor(struct nvif_chan *chan, void *userd, void *gpfifo, u32 gpfifo_size,
		   void *push, u64 push_addr, u32 push_size,
		   struct nvif_user *usermode, u32 doorbell_token)
{
	nvif_chan_gpfifo_ctor(&nvif_chanc36f, userd, gpfifo, gpfifo_size,
			      push, push_addr, push_size, chan);
	chan->usermode = usermode;
	chan->doorbell_token = doorbell_token;
	return 0;
}
