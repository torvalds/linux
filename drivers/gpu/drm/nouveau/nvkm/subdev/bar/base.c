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
#include "priv.h"

void
nvkm_bar_flush(struct nvkm_bar *bar)
{
	if (bar && bar->func->flush)
		bar->func->flush(bar);
}

struct nvkm_vmm *
nvkm_bar_bar1_vmm(struct nvkm_device *device)
{
	return device->bar->func->bar1.vmm(device->bar);
}

void
nvkm_bar_bar1_reset(struct nvkm_device *device)
{
	struct nvkm_bar *bar = device->bar;
	if (bar) {
		bar->func->bar1.init(bar);
		bar->func->bar1.wait(bar);
	}
}

struct nvkm_vmm *
nvkm_bar_bar2_vmm(struct nvkm_device *device)
{
	/* Denies access to BAR2 when it's not initialised, used by INSTMEM
	 * to know when object access needs to go through the BAR0 window.
	 */
	struct nvkm_bar *bar = device->bar;
	if (bar && bar->bar2)
		return bar->func->bar2.vmm(bar);
	return NULL;
}

void
nvkm_bar_bar2_reset(struct nvkm_device *device)
{
	struct nvkm_bar *bar = device->bar;
	if (bar && bar->bar2) {
		bar->func->bar2.init(bar);
		bar->func->bar2.wait(bar);
	}
}

void
nvkm_bar_bar2_fini(struct nvkm_device *device)
{
	struct nvkm_bar *bar = device->bar;
	if (bar && bar->bar2) {
		bar->func->bar2.fini(bar);
		bar->bar2 = false;
	}
}

void
nvkm_bar_bar2_init(struct nvkm_device *device)
{
	struct nvkm_bar *bar = device->bar;
	if (bar && bar->subdev.oneinit && !bar->bar2 && bar->func->bar2.init) {
		bar->func->bar2.init(bar);
		bar->func->bar2.wait(bar);
		bar->bar2 = true;
	}
}

static int
nvkm_bar_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_bar *bar = nvkm_bar(subdev);
	if (bar->func->bar1.fini)
		bar->func->bar1.fini(bar);
	return 0;
}

static int
nvkm_bar_init(struct nvkm_subdev *subdev)
{
	struct nvkm_bar *bar = nvkm_bar(subdev);
	bar->func->bar1.init(bar);
	bar->func->bar1.wait(bar);
	if (bar->func->init)
		bar->func->init(bar);
	return 0;
}

static int
nvkm_bar_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_bar *bar = nvkm_bar(subdev);
	return bar->func->oneinit(bar);
}

static void *
nvkm_bar_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_bar *bar = nvkm_bar(subdev);
	nvkm_bar_bar2_fini(subdev->device);
	return bar->func->dtor(bar);
}

static const struct nvkm_subdev_func
nvkm_bar = {
	.dtor = nvkm_bar_dtor,
	.oneinit = nvkm_bar_oneinit,
	.init = nvkm_bar_init,
	.fini = nvkm_bar_fini,
};

void
nvkm_bar_ctor(const struct nvkm_bar_func *func, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_bar *bar)
{
	nvkm_subdev_ctor(&nvkm_bar, device, type, inst, &bar->subdev);
	bar->func = func;
	spin_lock_init(&bar->lock);
}
