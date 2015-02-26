/*
 * Copyright 2014 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */

#include <nvif/device.h>

void
nvif_device_fini(struct nvif_device *device)
{
	nvif_object_fini(&device->base);
}

int
nvif_device_init(struct nvif_object *parent, void (*dtor)(struct nvif_device *),
		 u32 handle, u32 oclass, void *data, u32 size,
		 struct nvif_device *device)
{
	int ret = nvif_object_init(parent, (void *)dtor, handle, oclass,
				   data, size, &device->base);
	if (ret == 0) {
		device->object = &device->base;
		device->info.version = 0;
		ret = nvif_object_mthd(&device->base, NV_DEVICE_V0_INFO,
				       &device->info, sizeof(device->info));
	}
	return ret;
}

static void
nvif_device_del(struct nvif_device *device)
{
	nvif_device_fini(device);
	kfree(device);
}

int
nvif_device_new(struct nvif_object *parent, u32 handle, u32 oclass,
		void *data, u32 size, struct nvif_device **pdevice)
{
	struct nvif_device *device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (device) {
		int ret = nvif_device_init(parent, nvif_device_del, handle,
					   oclass, data, size, device);
		if (ret) {
			kfree(device);
			device = NULL;
		}
		*pdevice = device;
		return ret;
	}
	return -ENOMEM;
}

void
nvif_device_ref(struct nvif_device *device, struct nvif_device **pdevice)
{
	nvif_object_ref(&device->base, (struct nvif_object **)pdevice);
}
