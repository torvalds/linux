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

#include <core/firmware.h>
#include <subdev/mc.h>
#include <subdev/timer.h>

#include <nvfw/sec2.h>

static int
nvkm_sec2_finimsg(void *priv, struct nvfw_falcon_msg *hdr)
{
	struct nvkm_sec2 *sec2 = priv;

	atomic_set(&sec2->running, 0);
	return 0;
}

static int
nvkm_sec2_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	struct nvkm_subdev *subdev = &sec2->engine.subdev;
	struct nvkm_falcon *falcon = &sec2->falcon;
	struct nvkm_falcon_cmdq *cmdq = sec2->cmdq;
	struct nvfw_falcon_cmd cmd = {
		.unit_id = sec2->func->unit_unload,
		.size = sizeof(cmd),
	};
	int ret;

	if (!subdev->use.enabled)
		return 0;

	if (atomic_read(&sec2->initmsg) == 1) {
		ret = nvkm_falcon_cmdq_send(cmdq, &cmd, nvkm_sec2_finimsg, sec2,
					    msecs_to_jiffies(1000));
		WARN_ON(ret);

		nvkm_msec(subdev->device, 2000,
			if (nvkm_falcon_rd32(falcon, 0x100) & 0x00000010)
				break;
		);
	}

	nvkm_inth_block(&subdev->inth);

	nvkm_falcon_cmdq_fini(cmdq);
	falcon->func->disable(falcon);
	nvkm_falcon_put(falcon, subdev);
	return 0;
}

static int
nvkm_sec2_init(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	struct nvkm_subdev *subdev = &sec2->engine.subdev;
	struct nvkm_falcon *falcon = &sec2->falcon;
	int ret;

	ret = nvkm_falcon_get(falcon, subdev);
	if (ret)
		return ret;

	nvkm_falcon_wr32(falcon, 0x014, 0xffffffff);
	atomic_set(&sec2->initmsg, 0);
	atomic_set(&sec2->running, 1);
	nvkm_inth_allow(&subdev->inth);

	nvkm_falcon_start(falcon);
	return 0;
}

static int
nvkm_sec2_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	struct nvkm_subdev *subdev = &sec2->engine.subdev;
	struct nvkm_intr *intr = &sec2->engine.subdev.device->mc->intr;
	enum nvkm_intr_type type = NVKM_INTR_SUBDEV;

	if (sec2->func->intr_vector) {
		intr = sec2->func->intr_vector(sec2, &type);
		if (IS_ERR(intr))
			return PTR_ERR(intr);
	}

	return nvkm_inth_add(intr, type, NVKM_INTR_PRIO_NORMAL, subdev, sec2->func->intr,
			     &subdev->inth);
}

static void *
nvkm_sec2_dtor(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);

	nvkm_falcon_msgq_del(&sec2->msgq);
	nvkm_falcon_cmdq_del(&sec2->cmdq);
	nvkm_falcon_qmgr_del(&sec2->qmgr);
	nvkm_falcon_dtor(&sec2->falcon);
	return sec2;
}

static const struct nvkm_engine_func
nvkm_sec2 = {
	.dtor = nvkm_sec2_dtor,
	.oneinit = nvkm_sec2_oneinit,
	.init = nvkm_sec2_init,
	.fini = nvkm_sec2_fini,
};

int
nvkm_sec2_new_(const struct nvkm_sec2_fwif *fwif, struct nvkm_device *device,
	       enum nvkm_subdev_type type, int inst, u32 addr, struct nvkm_sec2 **psec2)
{
	struct nvkm_sec2 *sec2;
	int ret;

	if (!(sec2 = *psec2 = kzalloc(sizeof(*sec2), GFP_KERNEL)))
		return -ENOMEM;

	ret = nvkm_engine_ctor(&nvkm_sec2, device, type, inst, true, &sec2->engine);
	if (ret)
		return ret;

	fwif = nvkm_firmware_load(&sec2->engine.subdev, fwif, "Sec2", sec2);
	if (IS_ERR(fwif))
		return PTR_ERR(fwif);

	sec2->func = fwif->func;

	ret = nvkm_falcon_ctor(sec2->func->flcn, &sec2->engine.subdev,
			       sec2->engine.subdev.name, addr, &sec2->falcon);
	if (ret)
		return ret;

	if ((ret = nvkm_falcon_qmgr_new(&sec2->falcon, &sec2->qmgr)) ||
	    (ret = nvkm_falcon_cmdq_new(sec2->qmgr, "cmdq", &sec2->cmdq)) ||
	    (ret = nvkm_falcon_msgq_new(sec2->qmgr, "msgq", &sec2->msgq)))
		return ret;

	return 0;
};
