/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <nvif/chan.h>
#include <nvif/user.h>

#include <nvif/push906f.h>
#include <nvhw/class/clc36f.h>

static void
nvif_chanc36f_gpfifo_kick(struct nvif_chan *chan)
{
	struct nvif_user *usermode = chan->usermode;

	nvif_wr32(&chan->userd, 0x8c, chan->gpfifo.cur);

	wmb(); /* ensure CPU writes are flushed to BAR1 */
	nvif_rd32(&chan->userd, 0); /* ensure BAR1 writes are flushed to vidmem */

	usermode->func->doorbell(usermode, chan->doorbell_token);
}

#define NVIF_CHANC36F_SEM_RELEASE_SIZE 6

static int
nvif_chanc36f_sem_release(struct nvif_chan *chan, u64 addr, u32 data)
{
	struct nvif_push *push = &chan->push;
	int ret;

	ret = PUSH_WAIT(push, NVIF_CHANC36F_SEM_RELEASE_SIZE);
	if (ret)
		return ret;

	PUSH_MTHD(push, NVC36F, SEM_ADDR_LO, lower_32_bits(addr),

				SEM_ADDR_HI, upper_32_bits(addr),

				SEM_PAYLOAD_LO, data);

	PUSH_MTHD(push, NVC36F, SEM_EXECUTE,
		  NVDEF(NVC36F, SEM_EXECUTE, OPERATION, RELEASE) |
		  NVDEF(NVC36F, SEM_EXECUTE, RELEASE_WFI, DIS) |
		  NVDEF(NVC36F, SEM_EXECUTE, PAYLOAD_SIZE, 32BIT) |
		  NVDEF(NVC36F, SEM_EXECUTE, RELEASE_TIMESTAMP, DIS));

	return 0;
}

static const struct nvif_chan_func
nvif_chanc36f = {
	.push.read_get = nvif_chan906f_read_get,
	.gpfifo.read_get = nvif_chan906f_gpfifo_read_get,
	.gpfifo.push = nvif_chan506f_gpfifo_push,
	.gpfifo.kick = nvif_chanc36f_gpfifo_kick,
	.gpfifo.post = nvif_chan906f_gpfifo_post,
	.gpfifo.post_size = NVIF_CHANC36F_SEM_RELEASE_SIZE,
	.sem.release = nvif_chanc36f_sem_release,
};

int
nvif_chanc36f_ctor(struct nvif_chan *chan, void *userd, void *gpfifo, u32 gpfifo_size,
		   void *push, u64 push_addr, u32 push_size, void *sema, u64 sema_addr,
		   struct nvif_user *usermode, u32 doorbell_token)
{
	int ret;

	ret = nvif_chan906f_ctor_(&nvif_chanc36f, userd, gpfifo, gpfifo_size,
				  push, push_addr, push_size, sema, sema_addr, chan);
	if (ret)
		return ret;

	chan->usermode = usermode;
	chan->doorbell_token = doorbell_token;
	return 0;
}
