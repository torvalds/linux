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

struct nvkm_vm *
nvkm_bar_kmap(struct nvkm_bar *bar)
{
	/* disallow kmap() until after vm has been bootstrapped */
	if (bar && bar->func->kmap && bar->subdev.oneinit)
		return bar->func->kmap(bar);
	return NULL;
}

int
nvkm_bar_umap(struct nvkm_bar *bar, u64 size, int type, struct nvkm_vma *vma)
{
	return bar->func->umap(bar, size, type, vma);
}

static int
nvkm_bar_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_bar *bar = nvkm_bar(subdev);
	return bar->func->oneinit(bar);
}

static int
nvkm_bar_init(struct nvkm_subdev *subdev)
{
	struct nvkm_bar *bar = nvkm_bar(subdev);
	return bar->func->init(bar);
}

static void *
nvkm_bar_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_bar *bar = nvkm_bar(subdev);
	return bar->func->dtor(bar);
}

static const struct nvkm_subdev_func
nvkm_bar = {
	.dtor = nvkm_bar_dtor,
	.oneinit = nvkm_bar_oneinit,
	.init = nvkm_bar_init,
};

void
nvkm_bar_ctor(const struct nvkm_bar_func *func, struct nvkm_device *device,
	      int index, struct nvkm_bar *bar)
{
	nvkm_subdev_ctor(&nvkm_bar, device, index, 0, &bar->subdev);
	bar->func = func;
	spin_lock_init(&bar->lock);
}
