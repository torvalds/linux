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

static struct lock_class_key nvkm_subdev_lock_class[NVKM_SUBDEV_NR];

const char *
nvkm_subdev_name[NVKM_SUBDEV_NR] = {
	[NVKM_SUBDEV_BAR     ] = "bar",
	[NVKM_SUBDEV_VBIOS   ] = "bios",
	[NVKM_SUBDEV_BUS     ] = "bus",
	[NVKM_SUBDEV_CLK     ] = "clk",
	[NVKM_SUBDEV_DEVINIT ] = "devinit",
	[NVKM_SUBDEV_FAULT   ] = "fault",
	[NVKM_SUBDEV_FB      ] = "fb",
	[NVKM_SUBDEV_FUSE    ] = "fuse",
	[NVKM_SUBDEV_GPIO    ] = "gpio",
	[NVKM_SUBDEV_I2C     ] = "i2c",
	[NVKM_SUBDEV_IBUS    ] = "priv",
	[NVKM_SUBDEV_ICCSENSE] = "iccsense",
	[NVKM_SUBDEV_INSTMEM ] = "imem",
	[NVKM_SUBDEV_LTC     ] = "ltc",
	[NVKM_SUBDEV_MC      ] = "mc",
	[NVKM_SUBDEV_MMU     ] = "mmu",
	[NVKM_SUBDEV_MXM     ] = "mxm",
	[NVKM_SUBDEV_PCI     ] = "pci",
	[NVKM_SUBDEV_PMU     ] = "pmu",
	[NVKM_SUBDEV_SECBOOT ] = "secboot",
	[NVKM_SUBDEV_THERM   ] = "therm",
	[NVKM_SUBDEV_TIMER   ] = "tmr",
	[NVKM_SUBDEV_TOP     ] = "top",
	[NVKM_SUBDEV_VOLT    ] = "volt",
	[NVKM_ENGINE_BSP     ] = "bsp",
	[NVKM_ENGINE_CE0     ] = "ce0",
	[NVKM_ENGINE_CE1     ] = "ce1",
	[NVKM_ENGINE_CE2     ] = "ce2",
	[NVKM_ENGINE_CE3     ] = "ce3",
	[NVKM_ENGINE_CE4     ] = "ce4",
	[NVKM_ENGINE_CE5     ] = "ce5",
	[NVKM_ENGINE_CE6     ] = "ce6",
	[NVKM_ENGINE_CE7     ] = "ce7",
	[NVKM_ENGINE_CE8     ] = "ce8",
	[NVKM_ENGINE_CIPHER  ] = "cipher",
	[NVKM_ENGINE_DISP    ] = "disp",
	[NVKM_ENGINE_DMAOBJ  ] = "dma",
	[NVKM_ENGINE_FIFO    ] = "fifo",
	[NVKM_ENGINE_GR      ] = "gr",
	[NVKM_ENGINE_IFB     ] = "ifb",
	[NVKM_ENGINE_ME      ] = "me",
	[NVKM_ENGINE_MPEG    ] = "mpeg",
	[NVKM_ENGINE_MSENC   ] = "msenc",
	[NVKM_ENGINE_MSPDEC  ] = "mspdec",
	[NVKM_ENGINE_MSPPP   ] = "msppp",
	[NVKM_ENGINE_MSVLD   ] = "msvld",
	[NVKM_ENGINE_NVENC0  ] = "nvenc0",
	[NVKM_ENGINE_NVENC1  ] = "nvenc1",
	[NVKM_ENGINE_NVENC2  ] = "nvenc2",
	[NVKM_ENGINE_NVDEC   ] = "nvdec",
	[NVKM_ENGINE_PM      ] = "pm",
	[NVKM_ENGINE_SEC     ] = "sec",
	[NVKM_ENGINE_SEC2    ] = "sec2",
	[NVKM_ENGINE_SW      ] = "sw",
	[NVKM_ENGINE_VIC     ] = "vic",
	[NVKM_ENGINE_VP      ] = "vp",
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

	nvkm_mc_reset(device, subdev->index);

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

void
nvkm_subdev_ctor(const struct nvkm_subdev_func *func,
		 struct nvkm_device *device, int index,
		 struct nvkm_subdev *subdev)
{
	const char *name = nvkm_subdev_name[index];
	subdev->func = func;
	subdev->device = device;
	subdev->index = index;

	__mutex_init(&subdev->mutex, name, &nvkm_subdev_lock_class[index]);
	subdev->debug = nvkm_dbgopt(device->dbgopt, name);
}
