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
	u32 data, type, inst;
	int i;

	for (i = 0; i < 64; i++) {
		if (!info) {
			if (!(info = nvkm_top_device_new(top)))
				return -ENOMEM;
			type = ~0;
			inst = 0;
		}

		data = nvkm_rd32(device, 0x022700 + (i * 0x04));
		nvkm_trace(subdev, "%02x: %08x\n", i, data);
		switch (data & 0x00000003) {
		case 0x00000000: /* NOT_VALID */
			continue;
		case 0x00000001: /* DATA */
			inst        = (data & 0x3c000000) >> 26;
			info->addr  = (data & 0x00fff000);
			if (data & 0x00000004)
				info->fault = (data & 0x000003f8) >> 3;
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
#define A_(A) if (inst == 0) info->index = NVKM_ENGINE_##A
#define B_(A) if (inst + NVKM_ENGINE_##A##0 < NVKM_ENGINE_##A##_LAST + 1)      \
		info->index = NVKM_ENGINE_##A##0 + inst
#define C_(A) if (inst == 0) info->index = NVKM_SUBDEV_##A
		switch (type) {
		case 0x00000000: A_(GR    ); break;
		case 0x00000001: A_(CE0   ); break;
		case 0x00000002: A_(CE1   ); break;
		case 0x00000003: A_(CE2   ); break;
		case 0x00000008: A_(MSPDEC); break;
		case 0x00000009: A_(MSPPP ); break;
		case 0x0000000a: A_(MSVLD ); break;
		case 0x0000000b: A_(MSENC ); break;
		case 0x0000000c: A_(VIC   ); break;
		case 0x0000000d: A_(SEC2  ); break;
		case 0x0000000e: B_(NVENC ); break;
		case 0x0000000f: A_(NVENC1); break;
		case 0x00000010: B_(NVDEC ); break;
		case 0x00000013: B_(CE    ); break;
		case 0x00000014: C_(GSP   ); break;
		default:
			break;
		}

		nvkm_debug(subdev, "%02x.%d (%8s): addr %06x fault %2d "
				   "engine %2d runlist %2d intr %2d "
				   "reset %2d\n", type, inst,
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
