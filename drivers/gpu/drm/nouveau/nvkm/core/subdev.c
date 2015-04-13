/*
 * Copyright 2012 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs
 */
#include <core/subdev.h>
#include <core/device.h>
#include <core/option.h>

struct nvkm_subdev *
nvkm_subdev(void *obj, int idx)
{
	struct nvkm_object *object = nv_object(obj);
	while (object && !nv_iclass(object, NV_SUBDEV_CLASS))
		object = object->parent;
	if (object == NULL || nv_subidx(nv_subdev(object)) != idx)
		object = nv_device(obj)->subdev[idx];
	return object ? nv_subdev(object) : NULL;
}

void
nvkm_subdev_reset(struct nvkm_object *subdev)
{
	nv_trace(subdev, "resetting...\n");
	nv_ofuncs(subdev)->fini(subdev, false);
	nv_debug(subdev, "reset\n");
}

int
nvkm_subdev_init(struct nvkm_subdev *subdev)
{
	int ret = nvkm_object_init(&subdev->object);
	if (ret)
		return ret;

	nvkm_subdev_reset(&subdev->object);
	return 0;
}

int
_nvkm_subdev_init(struct nvkm_object *object)
{
	return nvkm_subdev_init(nv_subdev(object));
}

int
nvkm_subdev_fini(struct nvkm_subdev *subdev, bool suspend)
{
	if (subdev->unit) {
		nv_mask(subdev, 0x000200, subdev->unit, 0x00000000);
		nv_mask(subdev, 0x000200, subdev->unit, subdev->unit);
	}

	return nvkm_object_fini(&subdev->object, suspend);
}

int
_nvkm_subdev_fini(struct nvkm_object *object, bool suspend)
{
	return nvkm_subdev_fini(nv_subdev(object), suspend);
}

void
nvkm_subdev_destroy(struct nvkm_subdev *subdev)
{
	int subidx = nv_hclass(subdev) & 0xff;
	nv_device(subdev)->subdev[subidx] = NULL;
	nvkm_object_destroy(&subdev->object);
}

void
_nvkm_subdev_dtor(struct nvkm_object *object)
{
	nvkm_subdev_destroy(nv_subdev(object));
}

int
nvkm_subdev_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, u32 pclass,
		    const char *subname, const char *sysname,
		    int size, void **pobject)
{
	struct nvkm_subdev *subdev;
	int ret;

	ret = nvkm_object_create_(parent, engine, oclass, pclass |
				  NV_SUBDEV_CLASS, size, pobject);
	subdev = *pobject;
	if (ret)
		return ret;

	__mutex_init(&subdev->mutex, subname, &oclass->lock_class_key);
	subdev->name = subname;

	if (parent) {
		struct nvkm_device *device = nv_device(parent);
		subdev->debug = nvkm_dbgopt(device->dbgopt, subname);
		subdev->mmio  = nv_subdev(device)->mmio;
	}

	return 0;
}
