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
#include "priv.h"

#include <core/memory.h>

void
nvkm_ltc_tags_clear(struct nvkm_device *device, u32 first, u32 count)
{
	struct nvkm_ltc *ltc = device->ltc;
	const u32 limit = first + count - 1;

	BUG_ON((first > limit) || (limit >= ltc->num_tags));

	mutex_lock(&ltc->subdev.mutex);
	ltc->func->cbc_clear(ltc, first, limit);
	ltc->func->cbc_wait(ltc);
	mutex_unlock(&ltc->subdev.mutex);
}

int
nvkm_ltc_zbc_color_get(struct nvkm_ltc *ltc, int index, const u32 color[4])
{
	memcpy(ltc->zbc_color[index], color, sizeof(ltc->zbc_color[index]));
	ltc->func->zbc_clear_color(ltc, index, color);
	return index;
}

int
nvkm_ltc_zbc_depth_get(struct nvkm_ltc *ltc, int index, const u32 depth)
{
	ltc->zbc_depth[index] = depth;
	ltc->func->zbc_clear_depth(ltc, index, depth);
	return index;
}

int
nvkm_ltc_zbc_stencil_get(struct nvkm_ltc *ltc, int index, const u32 stencil)
{
	ltc->zbc_stencil[index] = stencil;
	ltc->func->zbc_clear_stencil(ltc, index, stencil);
	return index;
}

void
nvkm_ltc_invalidate(struct nvkm_ltc *ltc)
{
	if (ltc->func->invalidate)
		ltc->func->invalidate(ltc);
}

void
nvkm_ltc_flush(struct nvkm_ltc *ltc)
{
	if (ltc->func->flush)
		ltc->func->flush(ltc);
}

static void
nvkm_ltc_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_ltc *ltc = nvkm_ltc(subdev);
	ltc->func->intr(ltc);
}

static int
nvkm_ltc_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_ltc *ltc = nvkm_ltc(subdev);
	return ltc->func->oneinit(ltc);
}

static int
nvkm_ltc_init(struct nvkm_subdev *subdev)
{
	struct nvkm_ltc *ltc = nvkm_ltc(subdev);
	int i;

	for (i = ltc->zbc_min; i <= ltc->zbc_max; i++) {
		ltc->func->zbc_clear_color(ltc, i, ltc->zbc_color[i]);
		ltc->func->zbc_clear_depth(ltc, i, ltc->zbc_depth[i]);
		if (ltc->func->zbc_clear_stencil)
			ltc->func->zbc_clear_stencil(ltc, i, ltc->zbc_stencil[i]);
	}

	ltc->func->init(ltc);
	return 0;
}

static void *
nvkm_ltc_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_ltc *ltc = nvkm_ltc(subdev);
	nvkm_memory_unref(&ltc->tag_ram);
	return ltc;
}

static const struct nvkm_subdev_func
nvkm_ltc = {
	.dtor = nvkm_ltc_dtor,
	.oneinit = nvkm_ltc_oneinit,
	.init = nvkm_ltc_init,
	.intr = nvkm_ltc_intr,
};

int
nvkm_ltc_new_(const struct nvkm_ltc_func *func, struct nvkm_device *device,
	      int index, struct nvkm_ltc **pltc)
{
	struct nvkm_ltc *ltc;

	if (!(ltc = *pltc = kzalloc(sizeof(*ltc), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_ltc, device, index, &ltc->subdev);
	ltc->func = func;
	ltc->zbc_min = 1; /* reserve 0 for disabled */
	ltc->zbc_max = min(func->zbc, NVKM_LTC_MAX_ZBC_CNT) - 1;
	return 0;
}
