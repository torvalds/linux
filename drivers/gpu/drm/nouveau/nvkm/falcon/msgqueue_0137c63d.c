/*
 * Copyright (c) 2017, NVIDIA CORPORATION. All rights reserved.
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
 */
#include "msgqueue.h"
#include <engine/falcon.h>
#include <subdev/pmu.h>
#include <subdev/secboot.h>

struct msgqueue_0137c63d {
	struct nvkm_msgqueue base;
};
#define msgqueue_0137c63d(q) \
	container_of(q, struct msgqueue_0137c63d, base)

struct msgqueue_0137bca5 {
	struct msgqueue_0137c63d base;

	u64 wpr_addr;
};
#define msgqueue_0137bca5(q) \
	container_of(container_of(q, struct msgqueue_0137c63d, base), \
		     struct msgqueue_0137bca5, base);

/* Init unit */
static void
init_gen_cmdline(struct nvkm_msgqueue *queue, void *buf)
{
	struct {
		u32 reserved;
		u32 freq_hz;
		u32 trace_size;
		u32 trace_dma_base;
		u16 trace_dma_base1;
		u8 trace_dma_offset;
		u32 trace_dma_idx;
		bool secure_mode;
		bool raise_priv_sec;
		struct {
			u32 dma_base;
			u16 dma_base1;
			u8 dma_offset;
			u16 fb_size;
			u8 dma_idx;
		} gc6_ctx;
		u8 pad;
	} *args = buf;

	args->secure_mode = 1;
}

static const struct nvkm_msgqueue_init_func
msgqueue_0137c63d_init_func = {
	.gen_cmdline = init_gen_cmdline,
};

static void
msgqueue_0137c63d_dtor(struct nvkm_msgqueue *queue)
{
	kfree(msgqueue_0137c63d(queue));
}

static const struct nvkm_msgqueue_func
msgqueue_0137c63d_func = {
	.init_func = &msgqueue_0137c63d_init_func,
	.dtor = msgqueue_0137c63d_dtor,
};

int
msgqueue_0137c63d_new(struct nvkm_falcon *falcon, const struct nvkm_secboot *sb,
		      struct nvkm_msgqueue **queue)
{
	struct msgqueue_0137c63d *ret;

	ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	*queue = &ret->base;

	nvkm_msgqueue_ctor(&msgqueue_0137c63d_func, falcon, &ret->base);

	return 0;
}

static const struct nvkm_msgqueue_func
msgqueue_0137bca5_func = {
	.init_func = &msgqueue_0137c63d_init_func,
	.dtor = msgqueue_0137c63d_dtor,
};

int
msgqueue_0137bca5_new(struct nvkm_falcon *falcon, const struct nvkm_secboot *sb,
		      struct nvkm_msgqueue **queue)
{
	struct msgqueue_0137bca5 *ret;

	ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	*queue = &ret->base.base;

	/*
	 * FIXME this must be set to the address of a *GPU* mapping within the
	 * ACR address space!
	 */
	/* ret->wpr_addr = sb->wpr_addr; */

	nvkm_msgqueue_ctor(&msgqueue_0137bca5_func, falcon, &ret->base.base);

	return 0;
}
