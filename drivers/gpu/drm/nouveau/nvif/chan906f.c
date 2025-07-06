/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#include <nvif/chan.h>
#include <nvif/user.h>
#include <nvif/push906f.h>

#include <nvhw/class/cl906f.h>

/* Limits GPFIFO size to 1MiB, and "main" push buffer size to 64KiB. */
#define NVIF_CHAN906F_PBPTR_BITS  15
#define NVIF_CHAN906F_PBPTR_MASK  ((1 << NVIF_CHAN906F_PBPTR_BITS) - 1)

#define NVIF_CHAN906F_GPPTR_SHIFT NVIF_CHAN906F_PBPTR_BITS
#define NVIF_CHAN906F_GPPTR_BITS  (32 - NVIF_CHAN906F_PBPTR_BITS)
#define NVIF_CHAN906F_GPPTR_MASK  ((1 << NVIF_CHAN906F_GPPTR_BITS) - 1)

#define NVIF_CHAN906F_SEM_RELEASE_SIZE 5

static int
nvif_chan906f_sem_release(struct nvif_chan *chan, u64 addr, u32 data)
{
	struct nvif_push *push = &chan->push;
	int ret;

	ret = PUSH_WAIT(push, NVIF_CHAN906F_SEM_RELEASE_SIZE);
	if (ret)
		return ret;

	PUSH_MTHD(push, NV906F, SEMAPHOREA,
		  NVVAL(NV906F, SEMAPHOREA, OFFSET_UPPER, upper_32_bits(addr)),

				SEMAPHOREB, lower_32_bits(addr),

				SEMAPHOREC, data,

				SEMAPHORED,
		  NVDEF(NV906F, SEMAPHORED, OPERATION, RELEASE) |
		  NVDEF(NV906F, SEMAPHORED, RELEASE_WFI, DIS) |
		  NVDEF(NV906F, SEMAPHORED, RELEASE_SIZE, 16BYTE));

	return 0;
}

int
nvif_chan906f_gpfifo_post(struct nvif_chan *chan, u32 gpptr, u32 pbptr)
{
	return chan->func->sem.release(chan, chan->sema.addr,
				       (gpptr << NVIF_CHAN906F_GPPTR_SHIFT) | pbptr);
}

u32
nvif_chan906f_gpfifo_read_get(struct nvif_chan *chan)
{
	return nvif_rd32(&chan->sema, 0) >> NVIF_CHAN906F_GPPTR_SHIFT;
}

u32
nvif_chan906f_read_get(struct nvif_chan *chan)
{
	return nvif_rd32(&chan->sema, 0) & NVIF_CHAN906F_PBPTR_MASK;
}

static const struct nvif_chan_func
nvif_chan906f = {
	.push.read_get = nvif_chan906f_read_get,
	.gpfifo.read_get = nvif_chan906f_gpfifo_read_get,
	.gpfifo.push = nvif_chan506f_gpfifo_push,
	.gpfifo.kick = nvif_chan506f_gpfifo_kick,
	.gpfifo.post = nvif_chan906f_gpfifo_post,
	.gpfifo.post_size = NVIF_CHAN906F_SEM_RELEASE_SIZE,
	.sem.release = nvif_chan906f_sem_release,
};

int
nvif_chan906f_ctor_(const struct nvif_chan_func *func, void *userd, void *gpfifo, u32 gpfifo_size,
		    void *push, u64 push_addr, u32 push_size, void *sema, u64 sema_addr,
		    struct nvif_chan *chan)
{
	nvif_chan_gpfifo_ctor(func, userd, gpfifo, gpfifo_size, push, push_addr, push_size, chan);
	chan->sema.map.ptr = sema;
	chan->sema.addr = sema_addr;
	return 0;
}

int
nvif_chan906f_ctor(struct nvif_chan *chan, void *userd, void *gpfifo, u32 gpfifo_size,
		   void *push, u64 push_addr, u32 push_size, void *sema, u64 sema_addr)
{
	return nvif_chan906f_ctor_(&nvif_chan906f, userd, gpfifo, gpfifo_size,
				   push, push_addr, push_size, sema, sema_addr, chan);
}
