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
#include <core/falcon.h>
#include <core/firmware.h>
#include <subdev/acr.h>
#include <subdev/top.h>

static void *
nvkm_gsp_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_gsp *gsp = nvkm_gsp(subdev);
	nvkm_falcon_dtor(&gsp->falcon);
	return gsp;
}

static const struct nvkm_subdev_func
nvkm_gsp = {
	.dtor = nvkm_gsp_dtor,
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

	return nvkm_falcon_ctor(gsp->func->flcn, &gsp->subdev, gsp->subdev.name, 0, &gsp->falcon);
}
