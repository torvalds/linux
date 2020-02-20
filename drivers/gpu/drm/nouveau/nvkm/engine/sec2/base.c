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
#include <subdev/top.h>

static void
nvkm_sec2_recv(struct work_struct *work)
{
	struct nvkm_sec2 *sec2 = container_of(work, typeof(*sec2), work);

	if (!sec2->initmsg_received) {
		int ret = sec2->func->initmsg(sec2);
		if (ret) {
			nvkm_error(&sec2->engine.subdev,
				   "error parsing init message: %d\n", ret);
			return;
		}

		sec2->initmsg_received = true;
	}

	nvkm_falcon_msgq_recv(sec2->msgq);
}

static void
nvkm_sec2_intr(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);
	sec2->func->intr(sec2);
}

static int
nvkm_sec2_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);

	flush_work(&sec2->work);

	if (suspend) {
		nvkm_falcon_cmdq_fini(sec2->cmdq);
		sec2->initmsg_received = false;
	}

	return 0;
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
	.fini = nvkm_sec2_fini,
	.intr = nvkm_sec2_intr,
};

int
nvkm_sec2_new_(const struct nvkm_sec2_fwif *fwif, struct nvkm_device *device,
	       int index, u32 addr, struct nvkm_sec2 **psec2)
{
	struct nvkm_sec2 *sec2;
	int ret;

	if (!(sec2 = *psec2 = kzalloc(sizeof(*sec2), GFP_KERNEL)))
		return -ENOMEM;

	ret = nvkm_engine_ctor(&nvkm_sec2, device, index, true, &sec2->engine);
	if (ret)
		return ret;

	fwif = nvkm_firmware_load(&sec2->engine.subdev, fwif, "Sec2", sec2);
	if (IS_ERR(fwif))
		return PTR_ERR(fwif);

	sec2->func = fwif->func;

	ret = nvkm_falcon_ctor(sec2->func->flcn, &sec2->engine.subdev,
			       nvkm_subdev_name[index], addr, &sec2->falcon);
	if (ret)
		return ret;

	if ((ret = nvkm_falcon_qmgr_new(&sec2->falcon, &sec2->qmgr)) ||
	    (ret = nvkm_falcon_cmdq_new(sec2->qmgr, "cmdq", &sec2->cmdq)) ||
	    (ret = nvkm_falcon_msgq_new(sec2->qmgr, "msgq", &sec2->msgq)))
		return ret;

	INIT_WORK(&sec2->work, nvkm_sec2_recv);
	return 0;
};
