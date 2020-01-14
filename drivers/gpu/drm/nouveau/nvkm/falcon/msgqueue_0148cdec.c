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
#include <engine/sec2.h>
#include <subdev/secboot.h>

/*
 * This firmware runs on the SEC falcon. It only has one command and one
 * message queue, and uses a different command line and init message.
 */

struct msgqueue_0148cdec {
	struct nvkm_msgqueue base;
};
#define msgqueue_0148cdec(q) \
	container_of(q, struct msgqueue_0148cdec, base)

static void
msgqueue_0148cdec_process_msgs(struct nvkm_msgqueue *queue)
{
	nvkm_msgqueue_process_msgs(queue, queue->falcon->owner->device->sec2->msgq);
}


static void
init_gen_cmdline(struct nvkm_msgqueue *queue, void *buf)
{
	struct {
		u32 freq_hz;
		u32 falc_trace_size;
		u32 falc_trace_dma_base;
		u32 falc_trace_dma_idx;
		bool secure_mode;
	} *args = buf;

	args->secure_mode = false;
}

static const struct nvkm_msgqueue_init_func
msgqueue_0148cdec_init_func = {
	.gen_cmdline = init_gen_cmdline,
};


static void
msgqueue_0148cdec_dtor(struct nvkm_msgqueue *queue)
{
	kfree(msgqueue_0148cdec(queue));
}

const struct nvkm_msgqueue_func
msgqueue_0148cdec_func = {
	.init_func = &msgqueue_0148cdec_init_func,
	.recv = msgqueue_0148cdec_process_msgs,
	.dtor = msgqueue_0148cdec_dtor,
};

int
msgqueue_0148cdec_new(struct nvkm_falcon *falcon, const struct nvkm_secboot *sb,
		      struct nvkm_msgqueue **queue)
{
	struct msgqueue_0148cdec *ret;

	ret = kzalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	*queue = &ret->base;

	nvkm_msgqueue_ctor(&msgqueue_0148cdec_func, falcon, &ret->base);

	return 0;
}
