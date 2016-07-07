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
#include <core/enum.h>

#include <nvif/class.h>

static const struct nvkm_enum
gk104_ce_launcherr_report[] = {
	{ 0x0, "NO_ERR" },
	{ 0x1, "2D_LAYER_EXCEEDS_DEPTH" },
	{ 0x2, "INVALID_ARGUMENT" },
	{ 0x3, "MEM2MEM_RECT_OUT_OF_BOUNDS" },
	{ 0x4, "SRC_LINE_EXCEEDS_PITCH" },
	{ 0x5, "SRC_LINE_EXCEEDS_NEG_PITCH" },
	{ 0x6, "DST_LINE_EXCEEDS_PITCH" },
	{ 0x7, "DST_LINE_EXCEEDS_NEG_PITCH" },
	{ 0x8, "BAD_SRC_PIXEL_COMP_REF" },
	{ 0x9, "INVALID_VALUE" },
	{ 0xa, "UNUSED_FIELD" },
	{ 0xb, "INVALID_OPERATION" },
	{}
};

static void
gk104_ce_intr_launcherr(struct nvkm_engine *ce, const u32 base)
{
	struct nvkm_subdev *subdev = &ce->subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x104f14 + base);
	const struct nvkm_enum *en =
		nvkm_enum_find(gk104_ce_launcherr_report, stat & 0x0000000f);
	nvkm_warn(subdev, "LAUNCHERR %08x [%s]\n", stat, en ? en->name : "");
	nvkm_wr32(device, 0x104f14 + base, 0x00000000);
}

void
gk104_ce_intr(struct nvkm_engine *ce)
{
	const u32 base = (ce->subdev.index - NVKM_ENGINE_CE0) * 0x1000;
	struct nvkm_subdev *subdev = &ce->subdev;
	struct nvkm_device *device = subdev->device;
	u32 mask = nvkm_rd32(device, 0x104904 + base);
	u32 intr = nvkm_rd32(device, 0x104908 + base) & mask;
	if (intr & 0x00000001) {
		nvkm_warn(subdev, "BLOCKPIPE\n");
		nvkm_wr32(device, 0x104908 + base, 0x00000001);
		intr &= ~0x00000001;
	}
	if (intr & 0x00000002) {
		nvkm_warn(subdev, "NONBLOCKPIPE\n");
		nvkm_wr32(device, 0x104908 + base, 0x00000002);
		intr &= ~0x00000002;
	}
	if (intr & 0x00000004) {
		gk104_ce_intr_launcherr(ce, base);
		nvkm_wr32(device, 0x104908 + base, 0x00000004);
		intr &= ~0x00000004;
	}
	if (intr) {
		nvkm_warn(subdev, "intr %08x\n", intr);
		nvkm_wr32(device, 0x104908 + base, intr);
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
	return nvkm_engine_new_(&gk104_ce, device, index, true, pengine);
}
