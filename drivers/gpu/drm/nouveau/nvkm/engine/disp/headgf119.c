/*
 * Copyright 2017 Red Hat Inc.
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
#include "head.h"

static void
gf119_head_vblank_put(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;
	nvkm_mask(device, 0x6100c0 + hoff, 0x00000001, 0x00000000);
}

static void
gf119_head_vblank_get(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;
	nvkm_mask(device, 0x6100c0 + hoff, 0x00000001, 0x00000001);
}

static void
gf119_head_rgclk(struct nvkm_head *head, int div)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x612200 + (head->id * 0x800), 0x0000000f, div);
}

static void
gf119_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = (state == &head->asy) * 0x20000 + head->id * 0x300;
	u32 data;

	data = nvkm_rd32(device, 0x640414 + hoff);
	state->vtotal = (data & 0xffff0000) >> 16;
	state->htotal = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x640418 + hoff);
	state->vsynce = (data & 0xffff0000) >> 16;
	state->hsynce = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x64041c + hoff);
	state->vblanke = (data & 0xffff0000) >> 16;
	state->hblanke = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x640420 + hoff);
	state->vblanks = (data & 0xffff0000) >> 16;
	state->hblanks = (data & 0x0000ffff);
	state->hz = nvkm_rd32(device, 0x640450 + hoff);

	data = nvkm_rd32(device, 0x640404 + hoff);
	switch ((data & 0x000003c0) >> 6) {
	case 6: state->or.depth = 30; break;
	case 5: state->or.depth = 24; break;
	case 2: state->or.depth = 18; break;
	case 0: state->or.depth = 18; break; /*XXX: "default" */
	default:
		state->or.depth = 18;
		WARN_ON(1);
		break;
	}
}

static const struct nvkm_head_func
gf119_head = {
	.state = gf119_head_state,
	.rgpos = nv50_head_rgpos,
	.rgclk = gf119_head_rgclk,
	.vblank_get = gf119_head_vblank_get,
	.vblank_put = gf119_head_vblank_put,
};

int
gf119_head_new(struct nvkm_disp *disp, int id)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	if (!(nvkm_rd32(device, 0x612004) & (0x00000001 << id)))
		return 0;
	return nvkm_head_new_(&gf119_head, disp, id);
}
