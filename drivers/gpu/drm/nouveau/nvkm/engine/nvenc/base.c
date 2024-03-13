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

#include <core/firmware.h>

static void *
nvkm_nvenc_dtor(struct nvkm_engine *engine)
{
	struct nvkm_nvenc *nvenc = nvkm_nvenc(engine);
	nvkm_falcon_dtor(&nvenc->falcon);
	return nvenc;
}

static const struct nvkm_engine_func
nvkm_nvenc = {
	.dtor = nvkm_nvenc_dtor,
};

int
nvkm_nvenc_new_(const struct nvkm_nvenc_fwif *fwif, struct nvkm_device *device,
		enum nvkm_subdev_type type, int inst, struct nvkm_nvenc **pnvenc)
{
	struct nvkm_nvenc *nvenc;
	int ret;

	if (!(nvenc = *pnvenc = kzalloc(sizeof(*nvenc), GFP_KERNEL)))
		return -ENOMEM;

	ret = nvkm_engine_ctor(&nvkm_nvenc, device, type, inst, true,
			       &nvenc->engine);
	if (ret)
		return ret;

	fwif = nvkm_firmware_load(&nvenc->engine.subdev, fwif, "Nvenc", nvenc);
	if (IS_ERR(fwif))
		return -ENODEV;

	nvenc->func = fwif->func;

	return nvkm_falcon_ctor(nvenc->func->flcn, &nvenc->engine.subdev,
				nvenc->engine.subdev.name, 0, &nvenc->falcon);
};
