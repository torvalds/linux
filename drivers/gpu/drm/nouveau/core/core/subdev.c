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

#include <core/object.h>
#include <core/subdev.h>
#include <core/device.h>
#include <core/option.h>

void
nouveau_subdev_reset(struct nouveau_object *subdev)
{
	nv_trace(subdev, "resetting...\n");
	nv_ofuncs(subdev)->fini(subdev, false);
	nv_debug(subdev, "reset\n");
}

int
nouveau_subdev_init(struct nouveau_subdev *subdev)
{
	int ret = nouveau_object_init(&subdev->base);
	if (ret)
		return ret;

	nouveau_subdev_reset(&subdev->base);
	return 0;
}

int
_nouveau_subdev_init(struct nouveau_object *object)
{
	return nouveau_subdev_init(nv_subdev(object));
}

int
nouveau_subdev_fini(struct nouveau_subdev *subdev, bool suspend)
{
	if (subdev->unit) {
		nv_mask(subdev, 0x000200, subdev->unit, 0x00000000);
		nv_mask(subdev, 0x000200, subdev->unit, subdev->unit);
	}

	return nouveau_object_fini(&subdev->base, suspend);
}

int
_nouveau_subdev_fini(struct nouveau_object *object, bool suspend)
{
	return nouveau_subdev_fini(nv_subdev(object), suspend);
}

void
nouveau_subdev_destroy(struct nouveau_subdev *subdev)
{
	int subidx = nv_hclass(subdev) & 0xff;
	nv_device(subdev)->subdev[subidx] = NULL;
	nouveau_object_destroy(&subdev->base);
}

void
_nouveau_subdev_dtor(struct nouveau_object *object)
{
	nouveau_subdev_destroy(nv_subdev(object));
}

int
nouveau_subdev_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, u32 pclass,
		       const char *subname, const char *sysname,
		       int size, void **pobject)
{
	struct nouveau_subdev *subdev;
	int ret;

	ret = nouveau_object_create_(parent, engine, oclass, pclass |
				     NV_SUBDEV_CLASS, size, pobject);
	subdev = *pobject;
	if (ret)
		return ret;

	__mutex_init(&subdev->mutex, subname, &oclass->lock_class_key);
	subdev->name = subname;

	if (parent) {
		struct nouveau_device *device = nv_device(parent);
		subdev->debug = nouveau_dbgopt(device->dbgopt, subname);
		subdev->mmio  = nv_subdev(device)->mmio;
	}

	return 0;
}
