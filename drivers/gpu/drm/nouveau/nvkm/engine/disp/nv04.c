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
#include "head.h"

#include <nvif/class.h>

static void
nv04_head_vblank_put(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_wr32(device, 0x600140 + (head->id * 0x2000) , 0x00000000);
}

static void
nv04_head_vblank_get(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_wr32(device, 0x600140 + (head->id * 0x2000) , 0x00000001);
}

static void
nv04_head_rgpos(struct nvkm_head *head, u16 *hline, u16 *vline)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	u32 data = nvkm_rd32(device, 0x600868 + (head->id * 0x2000));
	*hline = (data & 0xffff0000) >> 16;
	*vline = (data & 0x0000ffff);
}

static void
nv04_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x0200;
	state->vblanks = nvkm_rd32(device, 0x680800 + hoff) & 0x0000ffff;
	state->vtotal  = nvkm_rd32(device, 0x680804 + hoff) & 0x0000ffff;
	state->vblanke = state->vtotal - 1;
	state->hblanks = nvkm_rd32(device, 0x680820 + hoff) & 0x0000ffff;
	state->htotal  = nvkm_rd32(device, 0x680824 + hoff) & 0x0000ffff;
	state->hblanke = state->htotal - 1;
}

static const struct nvkm_head_func
nv04_head = {
	.state = nv04_head_state,
	.rgpos = nv04_head_rgpos,
	.vblank_get = nv04_head_vblank_get,
	.vblank_put = nv04_head_vblank_put,
};

static int
nv04_head_new(struct nvkm_disp *disp, int id)
{
	return nvkm_head_new_(&nv04_head, disp, id);
}

static void
nv04_disp_intr(struct nvkm_disp *disp)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 crtc0 = nvkm_rd32(device, 0x600100);
	u32 crtc1 = nvkm_rd32(device, 0x602100);
	u32 pvideo;

	if (crtc0 & 0x00000001) {
		nvkm_disp_vblank(disp, 0);
		nvkm_wr32(device, 0x600100, 0x00000001);
	}

	if (crtc1 & 0x00000001) {
		nvkm_disp_vblank(disp, 1);
		nvkm_wr32(device, 0x602100, 0x00000001);
	}

	if (device->chipset >= 0x10 && device->chipset <= 0x40) {
		pvideo = nvkm_rd32(device, 0x8100);
		if (pvideo & ~0x11)
			nvkm_info(subdev, "PVIDEO intr: %08x\n", pvideo);
		nvkm_wr32(device, 0x8100, pvideo);
	}
}

static const struct nvkm_disp_func
nv04_disp = {
	.intr = nv04_disp_intr,
	.root = { 0, 0, NV04_DISP },
	.user = { {} },
};

int
nv04_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_disp **pdisp)
{
	int ret, i;

	ret = nvkm_disp_new_(&nv04_disp, device, type, inst, pdisp);
	if (ret)
		return ret;

	for (i = 0; i < 2; i++) {
		ret = nv04_head_new(*pdisp, i);
		if (ret)
			return ret;
	}

	return 0;
}
