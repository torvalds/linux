/*
 * Copyright 2019 Red Hat Inc.
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

int
nvkm_gsp_intr_nonstall(struct nvkm_gsp *gsp, enum nvkm_subdev_type type, int inst)
{
	for (int i = 0; i < gsp->intr_nr; i++) {
		if (gsp->intr[i].type == type && gsp->intr[i].inst == inst) {
			if (gsp->intr[i].nonstall != ~0)
				return gsp->intr[i].nonstall;

			return -EINVAL;
		}
	}

	return -ENOENT;
}

int
nvkm_gsp_intr_stall(struct nvkm_gsp *gsp, enum nvkm_subdev_type type, int inst)
{
	for (int i = 0; i < gsp->intr_nr; i++) {
		if (gsp->intr[i].type == type && gsp->intr[i].inst == inst) {
			if (gsp->intr[i].stall != ~0)
				return gsp->intr[i].stall;

			return -EINVAL;
		}
	}

	return -ENOENT;
}

static int
nvkm_gsp_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_gsp *gsp = nvkm_gsp(subdev);

	if (!gsp->func->fini)
		return 0;

	return gsp->func->fini(gsp, suspend);
}

static int
nvkm_gsp_init(struct nvkm_subdev *subdev)
{
	struct nvkm_gsp *gsp = nvkm_gsp(subdev);

	if (!gsp->func->init)
		return 0;

	return gsp->func->init(gsp);
}

static int
nvkm_gsp_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_gsp *gsp = nvkm_gsp(subdev);

	if (!gsp->func->oneinit)
		return 0;

	return gsp->func->oneinit(gsp);
}

static void *
nvkm_gsp_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_gsp *gsp = nvkm_gsp(subdev);

	if (gsp->func && gsp->func->dtor)
		gsp->func->dtor(gsp);

	nvkm_falcon_dtor(&gsp->falcon);
	return gsp;
}

static const struct nvkm_subdev_func
nvkm_gsp = {
	.dtor = nvkm_gsp_dtor,
	.oneinit = nvkm_gsp_oneinit,
	.init = nvkm_gsp_init,
	.fini = nvkm_gsp_fini,
};

int
nvkm_gsp_new_(const struct nvkm_gsp_fwif *fwif, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_gsp **pgsp)
{
	struct nvkm_gsp *gsp;

	if (!(gsp = *pgsp = kzalloc(sizeof(*gsp), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_gsp, device, type, inst, &gsp->subdev);

	fwif = nvkm_firmware_load(&gsp->subdev, fwif, "Gsp", gsp);
	if (IS_ERR(fwif))
		return PTR_ERR(fwif);

	gsp->func = fwif->func;
	gsp->rm = gsp->func->rm;

	return nvkm_falcon_ctor(gsp->func->flcn, &gsp->subdev, gsp->subdev.name, 0x110000,
				&gsp->falcon);
}
