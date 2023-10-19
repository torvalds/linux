/*
 * Copyright 2021 Red Hat Inc.
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

static int
ga100_top_oneinit(struct nvkm_top *top)
{
	struct nvkm_subdev *subdev = &top->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_top_device *info = NULL;
	u32 data, type, inst;
	int i, n, size = nvkm_rd32(device, 0x0224fc) >> 20;

	for (i = 0, n = 0; i < size; i++) {
		if (!info) {
			if (!(info = nvkm_top_device_new(top)))
				return -ENOMEM;
			type = ~0;
			inst = 0;
		}

		data = nvkm_rd32(device, 0x022800 + (i * 0x04));
		nvkm_trace(subdev, "%02x: %08x\n", i, data);
		if (!data && n == 0)
			continue;

		switch (n++) {
		case 0:
			type	      = (data & 0x3f000000) >> 24;
			inst	      = (data & 0x000f0000) >> 16;
			info->fault   = (data & 0x0000007f);
			break;
		case 1:
			info->addr    = (data & 0x00fff000);
			info->reset   = (data & 0x0000001f);
			break;
		case 2:
			info->runlist = (data & 0x00fffc00);
			info->engine  = (data & 0x00000003);
			break;
		default:
			break;
		}

		if (data & 0x80000000)
			continue;
		n = 0;

		/* Translate engine type to NVKM engine identifier. */
#define I_(T,I) do { info->type = (T); info->inst = (I); } while(0)
#define O_(T,I) do { WARN_ON(inst); I_(T, I); } while (0)
		switch (type) {
		case 0x00000000: O_(NVKM_ENGINE_GR    ,    0); break;
		case 0x0000000d: O_(NVKM_ENGINE_SEC2  ,    0); break;
		case 0x0000000e: I_(NVKM_ENGINE_NVENC , inst); break;
		case 0x00000010: I_(NVKM_ENGINE_NVDEC , inst); break;
		case 0x00000012: I_(NVKM_SUBDEV_IOCTRL, inst); break;
		case 0x00000013: I_(NVKM_ENGINE_CE    , inst); break;
		case 0x00000014: O_(NVKM_SUBDEV_GSP   ,    0); break;
		case 0x00000015: O_(NVKM_ENGINE_NVJPG ,    0); break;
		case 0x00000016: O_(NVKM_ENGINE_OFA   ,    0); break;
		case 0x00000017: O_(NVKM_SUBDEV_FLA   ,    0); break;
			break;
		default:
			break;
		}

		nvkm_debug(subdev, "%02x.%d (%8s): addr %06x fault %2d "
				   "runlist %6x engine %2d reset %2d\n", type, inst,
			   info->type == NVKM_SUBDEV_NR ? "????????" : nvkm_subdev_type[info->type],
			   info->addr, info->fault, info->runlist < 0 ? 0 : info->runlist,
			   info->engine, info->reset);
		info = NULL;
	}

	return 0;
}

static const struct nvkm_top_func
ga100_top = {
	.oneinit = ga100_top_oneinit,
};

int
ga100_top_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_top **ptop)
{
	return nvkm_top_new_(&ga100_top, device, type, inst, ptop);
}
