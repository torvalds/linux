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
#define nvkm_uvfn(p) container_of((p), struct nvkm_uvfn, object)
#include "priv.h"

#include <core/object.h>

struct nvkm_uvfn {
	struct nvkm_object object;
	struct nvkm_vfn *vfn;
};

static int
nvkm_uvfn_map(struct nvkm_object *object, void *argv, u32 argc,
	      enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct nvkm_vfn *vfn = nvkm_uvfn(object)->vfn;
	struct nvkm_device *device = vfn->subdev.device;

	*addr = device->func->resource_addr(device, NVKM_BAR0_PRI) + vfn->addr.user;
	*size = vfn->func->user.size;
	*type = NVKM_OBJECT_MAP_IO;
	return 0;
}

static const struct nvkm_object_func
nvkm_uvfn = {
	.map = nvkm_uvfn_map,
};

int
nvkm_uvfn_new(struct nvkm_device *device, const struct nvkm_oclass *oclass,
	      void *argv, u32 argc, struct nvkm_object **pobject)
{
	struct nvkm_uvfn *uvfn;

	if (argc != 0)
		return -ENOSYS;

	if (!(uvfn = kzalloc(sizeof(*uvfn), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_object_ctor(&nvkm_uvfn, oclass, &uvfn->object);
	uvfn->vfn = device->vfn;

	*pobject = &uvfn->object;
	return 0;
}
