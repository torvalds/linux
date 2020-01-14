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

#include <subdev/secboot.h>

void
nvkm_msgqueue_write_cmdline(struct nvkm_msgqueue *queue, void *buf)
{
	if (!queue || !queue->func || !queue->func->init_func)
		return;

	queue->func->init_func->gen_cmdline(queue, buf);
}

int
nvkm_msgqueue_new(u32 version, struct nvkm_falcon *falcon,
		  const struct nvkm_secboot *sb, struct nvkm_msgqueue **queue)
{
	const struct nvkm_subdev *subdev = falcon->owner;
	int ret = -EINVAL;

	switch (version) {
	case 0x0137c63d:
		ret = msgqueue_0137c63d_new(falcon, sb, queue);
		break;
	case 0x0137bca5:
		ret = msgqueue_0137bca5_new(falcon, sb, queue);
		break;
	case 0x0148cdec:
	case 0x015ccf3e:
	case 0x0167d263:
		ret = msgqueue_0148cdec_new(falcon, sb, queue);
		break;
	default:
		nvkm_error(subdev, "unhandled firmware version 0x%08x\n",
			   version);
		break;
	}

	if (ret == 0) {
		nvkm_debug(subdev, "firmware version: 0x%08x\n", version);
		(*queue)->fw_version = version;
	}

	return ret;
}

void
nvkm_msgqueue_del(struct nvkm_msgqueue **queue)
{
	if (*queue) {
		(*queue)->func->dtor(*queue);
		*queue = NULL;
	}
}

void
nvkm_msgqueue_recv(struct nvkm_msgqueue *queue)
{
	if (!queue->func || !queue->func->recv) {
		const struct nvkm_subdev *subdev = queue->falcon->owner;

		nvkm_warn(subdev, "missing msgqueue recv function\n");
		return;
	}

	queue->func->recv(queue);
}

int
nvkm_msgqueue_reinit(struct nvkm_msgqueue *queue)
{
	/* firmware not set yet... */
	if (!queue)
		return 0;

	queue->init_msg_received = false;
	return 0;
}

void
nvkm_msgqueue_ctor(const struct nvkm_msgqueue_func *func,
		   struct nvkm_falcon *falcon,
		   struct nvkm_msgqueue *queue)
{
	queue->func = func;
	queue->falcon = falcon;
}
