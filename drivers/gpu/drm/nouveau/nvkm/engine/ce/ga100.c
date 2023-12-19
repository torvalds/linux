/*
 * Copyright 2021 Red Hat Inc.
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
 */
#include "priv.h"

#include <subdev/gsp.h>
#include <subdev/vfn.h>

#include <nvif/class.h>

static irqreturn_t
ga100_ce_intr(struct nvkm_inth *inth)
{
	struct nvkm_subdev *subdev = container_of(inth, typeof(*subdev), inth);

	/*TODO*/
	nvkm_error(subdev, "intr\n");
	return IRQ_NONE;
}

int
ga100_ce_nonstall(struct nvkm_engine *engine)
{
	struct nvkm_subdev *subdev = &engine->subdev;
	struct nvkm_device *device = subdev->device;

	return nvkm_rd32(device, 0x104424 + (subdev->inst * 0x80)) & 0x00000fff;
}

int
ga100_ce_fini(struct nvkm_engine *engine, bool suspend)
{
	nvkm_inth_block(&engine->subdev.inth);
	return 0;
}

int
ga100_ce_init(struct nvkm_engine *engine)
{
	nvkm_inth_allow(&engine->subdev.inth);
	return 0;
}

int
ga100_ce_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_subdev *subdev = &engine->subdev;
	struct nvkm_device *device = subdev->device;
	u32 vector;

	vector = nvkm_rd32(device, 0x10442c + (subdev->inst * 0x80)) & 0x00000fff;

	return nvkm_inth_add(&device->vfn->intr, vector, NVKM_INTR_PRIO_NORMAL,
			     subdev, ga100_ce_intr, &subdev->inth);
}

static const struct nvkm_engine_func
ga100_ce = {
	.oneinit = ga100_ce_oneinit,
	.init = ga100_ce_init,
	.fini = ga100_ce_fini,
	.nonstall = ga100_ce_nonstall,
	.cclass = &gv100_ce_cclass,
	.sclass = {
		{ -1, -1, AMPERE_DMA_COPY_A },
		{}
	}
};

int
ga100_ce_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_engine **pengine)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_ce_new(&ga100_ce, device, type, inst, pengine);

	return nvkm_engine_new_(&ga100_ce, device, type, inst, true, pengine);
}
