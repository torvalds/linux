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
#include "ior.h"

void
gf119_hda_device_entry(struct nvkm_ior *ior, int head)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 hoff = 0x800 * head;
	nvkm_mask(device, 0x616548 + hoff, 0x00000070, head << 4);
}

void
gf119_hda_eld(struct nvkm_ior *ior, int head, u8 *data, u8 size)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = 0x030 * ior->id + (head * 0x04);
	int i;

	for (i = 0; i < size; i++)
		nvkm_wr32(device, 0x10ec00 + soff, (i << 8) | data[i]);
	for (; i < 0x60; i++)
		nvkm_wr32(device, 0x10ec00 + soff, (i << 8));
	nvkm_mask(device, 0x10ec10 + soff, 0x80000002, 0x80000002);
}

void
gf119_hda_hpd(struct nvkm_ior *ior, int head, bool present)
{
	struct nvkm_device *device = ior->disp->engine.subdev.device;
	const u32 soff = 0x030 * ior->id + (head * 0x04);
	u32 data = 0x80000000;
	u32 mask = 0x80000001;
	if (present) {
		ior->func->hda.device_entry(ior, head);
		data |= 0x00000001;
	} else {
		mask |= 0x00000002;
	}
	nvkm_mask(device, 0x10ec10 + soff, mask, data);
}
