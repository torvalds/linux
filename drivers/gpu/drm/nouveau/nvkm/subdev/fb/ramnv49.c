/*
 * Copyright 2013 Red Hat Inc.
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
#include "ramnv40.h"

int
nv49_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	struct nvkm_device *device = fb->subdev.device;
	u32  size = nvkm_rd32(device, 0x10020c) & 0xff000000;
	u32 fb914 = nvkm_rd32(device, 0x100914);
	enum nvkm_ram_type type = NVKM_RAM_TYPE_UNKNOWN;
	int ret;

	switch (fb914 & 0x00000003) {
	case 0x00000000: type = NVKM_RAM_TYPE_DDR1 ; break;
	case 0x00000001: type = NVKM_RAM_TYPE_DDR2 ; break;
	case 0x00000002: type = NVKM_RAM_TYPE_GDDR3; break;
	case 0x00000003: break;
	}

	ret = nv40_ram_new_(fb, type, size, pram);
	if (ret)
		return ret;

	(*pram)->parts = (nvkm_rd32(device, 0x100200) & 0x00000003) + 1;
	return 0;
}
