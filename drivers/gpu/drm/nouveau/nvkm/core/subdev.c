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
#include <subdev/mc.h>

const char *
nvkm_subdev_type[NVKM_SUBDEV_NR] = {
#define NVKM_LAYOUT_ONCE(type,data,ptr,...) [type] = #ptr,
#define NVKM_LAYOUT_INST(A...) NVKM_LAYOUT_ONCE(A)
#include <core/layout.h>
#undef NVKM_LAYOUT_ONCE
#undef NVKM_LAYOUT_INST
};

void
nvkm_subdev_intr(struct nvkm_subdev *subdev)
{
	if (subdev->func->intr)
		subdev->func->intr(subdev);
}

int
nvkm_subdev_info(struct nvkm_subdev *subdev, u64 mthd, u64 *data)
{
	if (subdev->func->info)
		return subdev->func->info(subdev, mthd, data);
	return -ENOSYS;
}

int
nvkm_subdev_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_device *device = subdev->device;
	const char *action = suspend ? "suspend" : "fini";
	s64 time;

	nvkm_trace(subdev, "%s running...\n", action);
	time = ktime_to_us(ktime_get());

	if (subdev->func->fini) {
		int ret = subdev->func->fini(subdev, suspend);
		if (ret) {
			nvkm_error(subdev, "%s failed, %d\n", action, ret);
			if (suspend)
				return ret;
		}
	}

	nvkm_mc_reset(device, subdev->type, subdev->inst);

	time = ktime_to_us(ktime_get()) - time;
	nvkm_trace(subdev, "%s completed in %lldus\n", action, time);
	return 0;
}

int
nvkm_subdev_preinit(struct nvkm_subdev *subdev)
{
	s64 time;

	nvkm_trace(subdev, "preinit running...\n");
	time = ktime_to_us(ktime_get());

	if (subdev->func->preinit) {
		int ret = subdev->func->preinit(subdev);
		if (ret) {
			nvkm_error(subdev, "preinit failed, %d\n", ret);
			return ret;
		}
	}

	time = ktime_to_us(ktime_get()) - time;
	nvkm_trace(subdev, "preinit completed in %lldus\n", time);
	return 0;
}

int
nvkm_subdev_init(struct nvkm_subdev *subdev)
{
	s64 time;
	int ret;

	nvkm_trace(subdev, "init running...\n");
	time = ktime_to_us(ktime_get());

	if (subdev->func->oneinit && !subdev->oneinit) {
		s64 time;
		nvkm_trace(subdev, "one-time init running...\n");
		time = ktime_to_us(ktime_get());
		ret = subdev->func->oneinit(subdev);
		if (ret) {
			nvkm_error(subdev, "one-time init failed, %d\n", ret);
			return ret;
		}

		subdev->oneinit = true;
		time = ktime_to_us(ktime_get()) - time;
		nvkm_trace(subdev, "one-time init completed in %lldus\n", time);
	}

	if (subdev->func->init) {
		ret = subdev->func->init(subdev);
		if (ret) {
			nvkm_error(subdev, "init failed, %d\n", ret);
			return ret;
		}
	}

	time = ktime_to_us(ktime_get()) - time;
	nvkm_trace(subdev, "init completed in %lldus\n", time);
	return 0;
}

void
nvkm_subdev_del(struct nvkm_subdev **psubdev)
{
	struct nvkm_subdev *subdev = *psubdev;
	s64 time;

	if (subdev && !WARN_ON(!subdev->func)) {
		nvkm_trace(subdev, "destroy running...\n");
		time = ktime_to_us(ktime_get());
		list_del(&subdev->head);
		if (subdev->func->dtor)
			*psubdev = subdev->func->dtor(subdev);
		time = ktime_to_us(ktime_get()) - time;
		nvkm_trace(subdev, "destroy completed in %lldus\n", time);
		kfree(*psubdev);
		*psubdev = NULL;
	}
}

void
nvkm_subdev_disable(struct nvkm_device *device, enum nvkm_subdev_type type, int inst)
{
	struct nvkm_subdev *subdev;
	list_for_each_entry(subdev, &device->subdev, head) {
		if (subdev->type == type && subdev->inst == inst) {
			*subdev->pself = NULL;
			nvkm_subdev_del(&subdev);
			break;
		}
	}
}

void
nvkm_subdev_ctor(const struct nvkm_subdev_func *func, struct nvkm_device *device,
		 enum nvkm_subdev_type type, int inst, struct nvkm_subdev *subdev)
{
	subdev->func = func;
	subdev->device = device;
	subdev->type = type;
	subdev->inst = inst < 0 ? 0 : inst;

	if (inst >= 0)
		snprintf(subdev->name, sizeof(subdev->name), "%s%d", nvkm_subdev_type[type], inst);
	else
		strscpy(subdev->name, nvkm_subdev_type[type], sizeof(subdev->name));
	subdev->debug = nvkm_dbgopt(device->dbgopt, subdev->name);
	list_add_tail(&subdev->head, &device->subdev);
}

int
nvkm_subdev_new_(const struct nvkm_subdev_func *func, struct nvkm_device *device,
		 enum nvkm_subdev_type type, int inst, struct nvkm_subdev **psubdev)
{
	if (!(*psubdev = kzalloc(sizeof(**psubdev), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(func, device, type, inst, *psubdev);
	return 0;
}
