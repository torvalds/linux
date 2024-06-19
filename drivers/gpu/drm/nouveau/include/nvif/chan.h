/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025, NVIDIA CORPORATION. All rights reserved.
 */
#ifndef __NVIF_CHAN_H__
#define __NVIF_CHAN_H__
#include "push.h"

struct nvif_chan {
	const struct nvif_chan_func {
		struct {
			u32 (*read_get)(struct nvif_chan *);
		} push;

		struct {
			u32 (*read_get)(struct nvif_chan *);
			void (*push)(struct nvif_chan *, bool main, u64 addr, u32 size,
				     bool no_prefetch);
			void (*kick)(struct nvif_chan *);
		} gpfifo;
	} *func;

	struct {
		struct nvif_map map;
	} userd;

	struct {
		struct nvif_map map;
		u32 cur;
		u32 max;
		int free;
	} gpfifo;

	struct nvif_push push;

	struct nvif_user *usermode;
	u32 doorbell_token;
};

int nvif_chan_dma_wait(struct nvif_chan *, u32 push_nr);

void nvif_chan_gpfifo_ctor(const struct nvif_chan_func *, void *userd, void *gpfifo, u32 gpfifo_size,
			   void *push, u64 push_addr, u32 push_size, struct nvif_chan *);
int nvif_chan_gpfifo_wait(struct nvif_chan *, u32 gpfifo_nr, u32 push_nr);
void nvif_chan_gpfifo_push(struct nvif_chan *, u64 addr, u32 size, bool no_prefetch);

int nvif_chan506f_ctor(struct nvif_chan *, void *userd, void *gpfifo, u32 gpfifo_size,
		       void *push, u64 push_addr, u32 push_size);
u32 nvif_chan506f_read_get(struct nvif_chan *);
u32 nvif_chan506f_gpfifo_read_get(struct nvif_chan *);
void nvif_chan506f_gpfifo_push(struct nvif_chan *, bool main, u64 addr, u32 size, bool no_prefetch);

int nvif_chanc36f_ctor(struct nvif_chan *, void *userd, void *gpfifo, u32 gpfifo_size,
		       void *push, u64 push_addr, u32 push_size,
		       struct nvif_user *usermode, u32 doorbell_token);
#endif
