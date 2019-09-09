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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

#include <core/msgqueue.h>
#include <subdev/top.h>
#include <engine/falcon.h>

static void *
nvkm_sec2_dtor(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	nvkm_msgqueue_del(&sec2->queue);
	nvkm_falcon_del(&sec2->falcon);
	return sec2;
}

static void
nvkm_sec2_intr(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	struct nvkm_subdev *subdev = &engine->subdev;
	struct nvkm_device *device = subdev->device;
	u32 disp = nvkm_rd32(device, sec2->addr + 0x01c);
	u32 intr = nvkm_rd32(device, sec2->addr + 0x008) & disp & ~(disp >> 16);

	if (intr & 0x00000040) {
		schedule_work(&sec2->work);
		nvkm_wr32(device, sec2->addr + 0x004, 0x00000040);
		intr &= ~0x00000040;
	}

	if (intr) {
		nvkm_error(subdev, "unhandled intr %08x\n", intr);
		nvkm_wr32(device, sec2->addr + 0x004, intr);

	}
}

static void
nvkm_sec2_recv(struct work_struct *work)
{
	struct nvkm_sec2 *sec2 = container_of(work, typeof(*sec2), work);

	if (!sec2->queue) {
		nvkm_warn(&sec2->engine.subdev,
			  "recv function called while no firmware set!\n");
		return;
	}

	nvkm_msgqueue_recv(sec2->queue);
}


static int
nvkm_sec2_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	struct nvkm_subdev *subdev = &sec2->engine.subdev;

	if (!sec2->addr) {
		sec2->addr = nvkm_top_addr(subdev->device, subdev->index);
		if (WARN_ON(!sec2->addr))
			return -EINVAL;
	}

	return nvkm_falcon_v1_new(subdev, "SEC2", sec2->addr, &sec2->falcon);
}

static int
nvkm_sec2_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	flush_work(&sec2->work);
	return 0;
}

static const struct nvkm_engine_func
nvkm_sec2 = {
	.dtor = nvkm_sec2_dtor,
	.oneinit = nvkm_sec2_oneinit,
	.fini = nvkm_sec2_fini,
	.intr = nvkm_sec2_intr,
};

int
nvkm_sec2_new_(struct nvkm_device *device, int index, u32 addr,
	       struct nvkm_sec2 **psec2)
{
	struct nvkm_sec2 *sec2;

	if (!(sec2 = *psec2 = kzalloc(sizeof(*sec2), GFP_KERNEL)))
		return -ENOMEM;
	sec2->addr = addr;
	INIT_WORK(&sec2->work, nvkm_sec2_recv);

	return nvkm_engine_ctor(&nvkm_sec2, device, index, true, &sec2->engine);
};
