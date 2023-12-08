/*
 * Copyright 2018 Red Hat Inc.
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

#include <core/gpuobj.h>
#include <core/object.h>

#include <nvif/class.h>

static int
gv100_ce_cclass_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent, int align,
		     struct nvkm_gpuobj **pgpuobj)
{
	struct nvkm_device *device = object->engine->subdev.device;
	u32 size;

	/* Allocate fault method buffer (magics come from nvgpu). */
	size = nvkm_rd32(device, 0x104028); /* NV_PCE_PCE_MAP */
	size = 27 * 5 * (((9 + 1 + 3) * hweight32(size)) + 2);
	size = roundup(size, PAGE_SIZE);

	return nvkm_gpuobj_new(device, size, align, true, parent, pgpuobj);
}

const struct nvkm_object_func
gv100_ce_cclass = {
	.bind = gv100_ce_cclass_bind,
};

static const struct nvkm_engine_func
gv100_ce = {
	.intr = gp100_ce_intr,
	.cclass = &gv100_ce_cclass,
	.sclass = {
		{ -1, -1, VOLTA_DMA_COPY_A },
		{}
	}
};

int
gv100_ce_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_engine **pengine)
{
	return nvkm_engine_new_(&gv100_ce, device, type, inst, true, pengine);
}
