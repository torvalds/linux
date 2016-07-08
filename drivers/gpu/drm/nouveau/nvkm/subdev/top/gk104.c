/*
 * Copyright 2016 Red Hat Inc.
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

static int
gk104_top_oneinit(struct nvkm_top *top)
{
	struct nvkm_subdev *subdev = &top->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_top_device *info = NULL;
	u32 data, type;
	int i;

	for (i = 0; i < 64; i++) {
		if (!info) {
			if (!(info = nvkm_top_device_new(top)))
				return -ENOMEM;
			type = ~0;
		}

		data = nvkm_rd32(device, 0x022700 + (i * 0x04));
		nvkm_trace(subdev, "%02x: %08x\n", i, data);
		switch (data & 0x00000003) {
		case 0x00000000: /* NOT_VALID */
			continue;
		case 0x00000001: /* DATA */
			info->addr  = (data & 0x00fff000);
			info->fault = (data & 0x000000f8) >> 3;
			break;
		case 0x00000002: /* ENUM */
			if (data & 0x00000020)
				info->engine  = (data & 0x3c000000) >> 26;
			if (data & 0x00000010)
				info->runlist = (data & 0x01e00000) >> 21;
			if (data & 0x00000008)
				info->intr    = (data & 0x000f8000) >> 15;
			if (data & 0x00000004)
				info->reset   = (data & 0x00003e00) >> 9;
			break;
		case 0x00000003: /* ENGINE_TYPE */
			type = (data & 0x7ffffffc) >> 2;
			break;
		}

		if (data & 0x80000000)
			continue;

		/* Translate engine type to NVKM engine identifier. */
		switch (type) {
		case 0x00000000: info->index = NVKM_ENGINE_GR; break;
		case 0x00000001: info->index = NVKM_ENGINE_CE0; break;
		case 0x00000002: info->index = NVKM_ENGINE_CE1; break;
		case 0x00000003: info->index = NVKM_ENGINE_CE2; break;
		case 0x00000008: info->index = NVKM_ENGINE_MSPDEC; break;
		case 0x00000009: info->index = NVKM_ENGINE_MSPPP; break;
		case 0x0000000a: info->index = NVKM_ENGINE_MSVLD; break;
		case 0x0000000b: info->index = NVKM_ENGINE_MSENC; break;
		case 0x0000000c: info->index = NVKM_ENGINE_VIC; break;
		case 0x0000000d: info->index = NVKM_ENGINE_SEC; break;
		case 0x0000000e: info->index = NVKM_ENGINE_NVENC0; break;
		case 0x0000000f: info->index = NVKM_ENGINE_NVENC1; break;
		case 0x00000010: info->index = NVKM_ENGINE_NVDEC; break;
			break;
		default:
			break;
		}

		nvkm_debug(subdev, "%02x (%8s): addr %06x fault %2d engine %2d "
				   "runlist %2d intr %2d reset %2d\n", type,
			   info->index == NVKM_SUBDEV_NR ? NULL :
					  nvkm_subdev_name[info->index],
			   info->addr, info->fault, info->engine, info->runlist,
			   info->intr, info->reset);
		info = NULL;
	}

	return 0;
}

static const struct nvkm_top_func
gk104_top = {
	.oneinit = gk104_top_oneinit,
};

int
gk104_top_new(struct nvkm_device *device, int index, struct nvkm_top **ptop)
{
	return nvkm_top_new_(&gk104_top, device, index, ptop);
}
