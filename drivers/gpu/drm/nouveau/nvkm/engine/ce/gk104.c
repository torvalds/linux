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

#include <nvif/class.h>

void
gk104_ce_intr(struct nvkm_engine *ce)
{
	const u32 base = (ce->subdev.index - NVKM_ENGINE_CE0) * 0x1000;
	struct nvkm_subdev *subdev = &ce->subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x104908 + base);
	if (stat) {
		nvkm_warn(subdev, "intr %08x\n", stat);
		nvkm_wr32(device, 0x104908 + base, stat);
	}
}

static const struct nvkm_engine_func
gk104_ce = {
	.intr = gk104_ce_intr,
	.sclass = {
		{ -1, -1, KEPLER_DMA_COPY_A },
		{}
	}
};

int
gk104_ce_new(struct nvkm_device *device, int index,
	     struct nvkm_engine **pengine)
{
	if (index == NVKM_ENGINE_CE0) {
		return nvkm_engine_new_(&gk104_ce, device, index,
					0x00000040, true, pengine);
	} else
	if (index == NVKM_ENGINE_CE1) {
		return nvkm_engine_new_(&gk104_ce, device, index,
					0x00000080, true, pengine);
	} else
	if (index == NVKM_ENGINE_CE2) {
		return nvkm_engine_new_(&gk104_ce, device, index,
					0x00200000, true, pengine);
	}
	return -ENODEV;
}
