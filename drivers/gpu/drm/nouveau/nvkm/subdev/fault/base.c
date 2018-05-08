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

#include <core/memory.h>
#include <core/notify.h>
#include <subdev/bar.h>
#include <subdev/mmu.h>

static void
nvkm_fault_ntfy_fini(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fault *fault = container_of(event, typeof(*fault), event);
	fault->func->buffer.fini(fault->buffer[index]);
}

static void
nvkm_fault_ntfy_init(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fault *fault = container_of(event, typeof(*fault), event);
	fault->func->buffer.init(fault->buffer[index]);
}

static int
nvkm_fault_ntfy_ctor(struct nvkm_object *object, void *argv, u32 argc,
		     struct nvkm_notify *notify)
{
	struct nvkm_fault_buffer *buffer = nvkm_fault_buffer(object);
	if (argc == 0) {
		notify->size  = 0;
		notify->types = 1;
		notify->index = buffer->id;
		return 0;
	}
	return -ENOSYS;
}

static const struct nvkm_event_func
nvkm_fault_ntfy = {
	.ctor = nvkm_fault_ntfy_ctor,
	.init = nvkm_fault_ntfy_init,
	.fini = nvkm_fault_ntfy_fini,
};

static void
nvkm_fault_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_fault *fault = nvkm_fault(subdev);
	return fault->func->intr(fault);
}

static int
nvkm_fault_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_fault *fault = nvkm_fault(subdev);
	if (fault->func->fini)
		fault->func->fini(fault);
	return 0;
}

static int
nvkm_fault_init(struct nvkm_subdev *subdev)
{
	struct nvkm_fault *fault = nvkm_fault(subdev);
	if (fault->func->init)
		fault->func->init(fault);
	return 0;
}

static int
nvkm_fault_oneinit_buffer(struct nvkm_fault *fault, int id)
{
	struct nvkm_subdev *subdev = &fault->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_vmm *bar2 = nvkm_bar_bar2_vmm(device);
	struct nvkm_fault_buffer *buffer;
	int ret;

	if (!(buffer = kzalloc(sizeof(*buffer), GFP_KERNEL)))
		return -ENOMEM;
	buffer->fault = fault;
	buffer->id = id;
	buffer->entries = fault->func->buffer.entries(buffer);
	fault->buffer[id] = buffer;

	nvkm_debug(subdev, "buffer %d: %d entries\n", id, buffer->entries);

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, buffer->entries *
			      fault->func->buffer.entry_size, 0x1000, true,
			      &buffer->mem);
	if (ret)
		return ret;

	ret = nvkm_vmm_get(bar2, 12, nvkm_memory_size(buffer->mem),
			   &buffer->vma);
	if (ret)
		return ret;

	return nvkm_memory_map(buffer->mem, 0, bar2, buffer->vma, NULL, 0);
}

static int
nvkm_fault_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_fault *fault = nvkm_fault(subdev);
	int ret, i;

	for (i = 0; i < ARRAY_SIZE(fault->buffer); i++) {
		if (i < fault->func->buffer.nr) {
			ret = nvkm_fault_oneinit_buffer(fault, i);
			if (ret)
				return ret;
			fault->buffer_nr = i + 1;
		}
	}

	return nvkm_event_init(&nvkm_fault_ntfy, 1, fault->buffer_nr,
			       &fault->event);
}

static void *
nvkm_fault_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_vmm *bar2 = nvkm_bar_bar2_vmm(subdev->device);
	struct nvkm_fault *fault = nvkm_fault(subdev);
	int i;

	nvkm_event_fini(&fault->event);

	for (i = 0; i < fault->buffer_nr; i++) {
		if (fault->buffer[i]) {
			nvkm_vmm_put(bar2, &fault->buffer[i]->vma);
			nvkm_memory_unref(&fault->buffer[i]->mem);
			kfree(fault->buffer[i]);
		}
	}

	return fault;
}

static const struct nvkm_subdev_func
nvkm_fault = {
	.dtor = nvkm_fault_dtor,
	.oneinit = nvkm_fault_oneinit,
	.init = nvkm_fault_init,
	.fini = nvkm_fault_fini,
	.intr = nvkm_fault_intr,
};

int
nvkm_fault_new_(const struct nvkm_fault_func *func, struct nvkm_device *device,
		int index, struct nvkm_fault **pfault)
{
	struct nvkm_fault *fault;
	if (!(fault = *pfault = kzalloc(sizeof(*fault), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_fault, device, index, &fault->subdev);
	fault->func = func;
	return 0;
}
