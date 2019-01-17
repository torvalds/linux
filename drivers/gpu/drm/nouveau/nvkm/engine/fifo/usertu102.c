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
#include "user.h"

static int
tu102_fifo_user_map(struct nvkm_object *object, void *argv, u32 argc,
		    enum nvkm_object_map *type, u64 *addr, u64 *size)
{
	struct nvkm_device *device = object->engine->subdev.device;
	*addr = 0xbb0000 + device->func->resource_addr(device, 0);
	*size = 0x010000;
	*type = NVKM_OBJECT_MAP_IO;
	return 0;
}

static const struct nvkm_object_func
tu102_fifo_user = {
	.map = tu102_fifo_user_map,
};

int
tu102_fifo_user_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	return nvkm_object_new_(&tu102_fifo_user, oclass, argv, argc, pobject);
}
