/*
 * Copyright 2020 Red Hat Inc.
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
#define gv100_disp_caps(p) container_of((p), struct gv100_disp_caps, object)
#include "rootnv50.h"

struct gv100_disp_caps {
	struct nvkm_object object;
	struct nv50_disp *disp;
};

static int
gv100_disp_caps_map(struct nvkm_object *object, void *argv, u32 argc,
		    enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct gv100_disp_caps *caps = gv100_disp_caps(object);
	struct nvkm_device *device = caps->disp->base.engine.subdev.device;
	*type = NVKM_OBJECT_MAP_IO;
	*addr = 0x640000 + device->func->resource_addr(device, 0);
	*size = 0x1000;
	return 0;
}

static const struct nvkm_object_func
gv100_disp_caps = {
	.map = gv100_disp_caps_map,
};

int
gv100_disp_caps_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nv50_disp *disp, struct nvkm_object **pobject)
{
	struct gv100_disp_caps *caps;

	if (!(caps = kzalloc(sizeof(*caps), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &caps->object;

	nvkm_object_ctor(&gv100_disp_caps, oclass, &caps->object);
	caps->disp = disp;
	return 0;
}
