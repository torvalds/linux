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

#include <engine/falcon.h>

static int
nvkm_nvdec_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_nvdec *nvdec = nvkm_nvdec(engine);
	return nvkm_falcon_v1_new(&nvdec->engine.subdev, "NVDEC", 0x84000,
				  &nvdec->falcon);
}

static void *
nvkm_nvdec_dtor(struct nvkm_engine *engine)
{
	struct nvkm_nvdec *nvdec = nvkm_nvdec(engine);
	nvkm_falcon_del(&nvdec->falcon);
	return nvdec;
}

static const struct nvkm_engine_func
nvkm_nvdec = {
	.dtor = nvkm_nvdec_dtor,
	.oneinit = nvkm_nvdec_oneinit,
};

int
nvkm_nvdec_new_(struct nvkm_device *device, int index,
		struct nvkm_nvdec **pnvdec)
{
	struct nvkm_nvdec *nvdec;

	if (!(nvdec = *pnvdec = kzalloc(sizeof(*nvdec), GFP_KERNEL)))
		return -ENOMEM;

	return nvkm_engine_ctor(&nvkm_nvdec, device, index, true,
				&nvdec->engine);
};
