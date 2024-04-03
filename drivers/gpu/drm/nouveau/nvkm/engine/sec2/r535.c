/*
 * Copyright 2023 Red Hat Inc.
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

static void *
r535_sec2_dtor(struct nvkm_engine *engine)
{
	struct nvkm_sec2 *sec2 = nvkm_sec2(engine);

	nvkm_falcon_dtor(&sec2->falcon);
	return sec2;
}

static const struct nvkm_engine_func
r535_sec2 = {
	.dtor = r535_sec2_dtor,
};

int
r535_sec2_new(const struct nvkm_sec2_func *func, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, u32 addr, struct nvkm_sec2 **psec2)
{
	struct nvkm_sec2 *sec2;
	int ret;

	if (!(sec2 = *psec2 = kzalloc(sizeof(*sec2), GFP_KERNEL)))
		return -ENOMEM;

	ret = nvkm_engine_ctor(&r535_sec2, device, type, inst, true, &sec2->engine);
	if (ret)
		return ret;

	return nvkm_falcon_ctor(func->flcn, &sec2->engine.subdev, sec2->engine.subdev.name,
				addr, &sec2->falcon);
}
