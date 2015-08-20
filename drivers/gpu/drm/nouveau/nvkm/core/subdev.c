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

static struct lock_class_key nvkm_subdev_lock_class[NVDEV_SUBDEV_NR];

const char *
nvkm_subdev_name[64] = {
	[NVDEV_SUBDEV_BAR    ] = "bar",
	[NVDEV_SUBDEV_VBIOS  ] = "bios",
	[NVDEV_SUBDEV_BUS    ] = "bus",
	[NVDEV_SUBDEV_CLK    ] = "clk",
	[NVDEV_SUBDEV_DEVINIT] = "devinit",
	[NVDEV_SUBDEV_FB     ] = "fb",
	[NVDEV_SUBDEV_FUSE   ] = "fuse",
	[NVDEV_SUBDEV_GPIO   ] = "gpio",
	[NVDEV_SUBDEV_I2C    ] = "i2c",
	[NVDEV_SUBDEV_IBUS   ] = "priv",
	[NVDEV_SUBDEV_INSTMEM] = "imem",
	[NVDEV_SUBDEV_LTC    ] = "ltc",
	[NVDEV_SUBDEV_MC     ] = "mc",
	[NVDEV_SUBDEV_MMU    ] = "mmu",
	[NVDEV_SUBDEV_MXM    ] = "mxm",
	[NVDEV_SUBDEV_PMU    ] = "pmu",
	[NVDEV_SUBDEV_THERM  ] = "therm",
	[NVDEV_SUBDEV_TIMER  ] = "tmr",
	[NVDEV_SUBDEV_VOLT   ] = "volt",
	[NVDEV_ENGINE_BSP    ] = "bsp",
	[NVDEV_ENGINE_CE0    ] = "ce0",
	[NVDEV_ENGINE_CE1    ] = "ce1",
	[NVDEV_ENGINE_CE2    ] = "ce2",
	[NVDEV_ENGINE_CIPHER ] = "cipher",
	[NVDEV_ENGINE_DISP   ] = "disp",
	[NVDEV_ENGINE_DMAOBJ ] = "dma",
	[NVDEV_ENGINE_FIFO   ] = "fifo",
	[NVDEV_ENGINE_GR     ] = "gr",
	[NVDEV_ENGINE_IFB    ] = "ifb",
	[NVDEV_ENGINE_ME     ] = "me",
	[NVDEV_ENGINE_MPEG   ] = "mpeg",
	[NVDEV_ENGINE_MSENC  ] = "msenc",
	[NVDEV_ENGINE_MSPDEC ] = "mspdec",
	[NVDEV_ENGINE_MSPPP  ] = "msppp",
	[NVDEV_ENGINE_MSVLD  ] = "msvld",
	[NVDEV_ENGINE_PM     ] = "pm",
	[NVDEV_ENGINE_SEC    ] = "sec",
	[NVDEV_ENGINE_SW     ] = "sw",
	[NVDEV_ENGINE_VIC    ] = "vic",
	[NVDEV_ENGINE_VP     ] = "vp",
};

void
nvkm_subdev_intr(struct nvkm_subdev *subdev)
{
	if (subdev->func->intr)
		subdev->func->intr(subdev);
}

int
nvkm_subdev_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_device *device = subdev->device;
	const char *action = suspend ? "suspend" : "fini";
	u32 pmc_enable = subdev->pmc_enable;
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

	if (pmc_enable) {
		nvkm_mask(device, 0x000200, pmc_enable, 0x00000000);
		nvkm_mask(device, 0x000200, pmc_enable, pmc_enable);
		nvkm_rd32(device, 0x000200);
	}

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
		if (subdev->func->dtor)
			*psubdev = subdev->func->dtor(subdev);
		time = ktime_to_us(ktime_get()) - time;
		nvkm_trace(subdev, "destroy completed in %lldus\n", time);
		kfree(*psubdev);
		*psubdev = NULL;
	}
}

static const struct nvkm_object_func
nvkm_subdev_func = {
};

void
nvkm_subdev_ctor(const struct nvkm_subdev_func *func,
		 struct nvkm_device *device, int index, u32 pmc_enable,
		 struct nvkm_subdev *subdev)
{
	const char *name = nvkm_subdev_name[index];
	struct nvkm_oclass hack = {};
	nvkm_object_ctor(&nvkm_subdev_func, &hack, &subdev->object);
	subdev->func = func;
	subdev->device = device;
	subdev->index = index;
	subdev->pmc_enable = pmc_enable;

	__mutex_init(&subdev->mutex, name, &nvkm_subdev_lock_class[index]);
	subdev->debug = nvkm_dbgopt(device->dbgopt, name);
}

struct nvkm_subdev *
nvkm_subdev(void *obj, int idx)
{
	struct nvkm_object *object = nv_object(obj);
	while (object && !nv_iclass(object, NV_SUBDEV_CLASS))
		object = object->parent;
	if (object == NULL || !object->parent || nv_subidx(nv_subdev(object)) != idx)
		object = nv_device(obj)->subdev[idx];
	return object ? nv_subdev(object) : NULL;
}

void
nvkm_subdev_reset(struct nvkm_object *obj)
{
	struct nvkm_subdev *subdev = container_of(obj, typeof(*subdev), object);
	nvkm_trace(subdev, "resetting...\n");
	nvkm_object_fini(&subdev->object, false);
	nvkm_trace(subdev, "reset\n");
}

int
nvkm_subdev_init_old(struct nvkm_subdev *subdev)
{
	int ret = _nvkm_object_init(&subdev->object);
	if (ret)
		return ret;

	nvkm_subdev_reset(&subdev->object);
	return 0;
}

int
_nvkm_subdev_init(struct nvkm_object *object)
{
	struct nvkm_subdev *subdev = (void *)object;
	return nvkm_subdev_init_old(subdev);
}

int
nvkm_subdev_fini_old(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_device *device = subdev->device;

	if (subdev->unit) {
		nvkm_mask(device, 0x000200, subdev->unit, 0x00000000);
		nvkm_mask(device, 0x000200, subdev->unit, subdev->unit);
	}

	return _nvkm_object_fini(&subdev->object, suspend);
}

int
_nvkm_subdev_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_subdev *subdev = (void *)object;
	return nvkm_subdev_fini_old(subdev, suspend);
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
	const int subidx = oclass->handle & 0xff;
	const char *name = nvkm_subdev_name[subidx];
	struct nvkm_subdev *subdev;
	int ret;

	ret = nvkm_object_create_(parent, engine, oclass, pclass |
				  NV_SUBDEV_CLASS, size, pobject);
	subdev = *pobject;
	if (ret)
		return ret;

	__mutex_init(&subdev->mutex, name, &nvkm_subdev_lock_class[subidx]);
	subdev->index = subidx;

	if (parent) {
		struct nvkm_device *device = nv_device(parent);
		subdev->debug = nvkm_dbgopt(device->dbgopt, name);
		subdev->device = device;
	} else {
		subdev->device = nv_device(subdev);
	}

	return 0;
}
