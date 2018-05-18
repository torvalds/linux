/*
 * Copyright 2018 Red Hat Inc.
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
#include "head.h"

static void
gv100_head_vblank_put(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x611d80 + (head->id * 4), 0x00000004, 0x00000000);
}

static void
gv100_head_vblank_get(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x611d80 + (head->id * 4), 0x00000004, 0x00000004);
}

static void
gv100_head_rgpos(struct nvkm_head *head, u16 *hline, u16 *vline)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;
	/* vline read locks hline. */
	*vline = nvkm_rd32(device, 0x616330 + hoff) & 0x0000ffff;
	*hline = nvkm_rd32(device, 0x616334 + hoff) & 0x0000ffff;
}

static void
gv100_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = (state == &head->arm) * 0x8000 + head->id * 0x400;
	u32 data;

	data = nvkm_rd32(device, 0x682064 + hoff);
	state->vtotal = (data & 0xffff0000) >> 16;
	state->htotal = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x682068 + hoff);
	state->vsynce = (data & 0xffff0000) >> 16;
	state->hsynce = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x68206c + hoff);
	state->vblanke = (data & 0xffff0000) >> 16;
	state->hblanke = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x682070 + hoff);
	state->vblanks = (data & 0xffff0000) >> 16;
	state->hblanks = (data & 0x0000ffff);
	state->hz = nvkm_rd32(device, 0x68200c + hoff);

	data = nvkm_rd32(device, 0x682004 + hoff);
	switch ((data & 0x000000f0) >> 4) {
	case 5: state->or.depth = 30; break;
	case 4: state->or.depth = 24; break;
	case 1: state->or.depth = 18; break;
	default:
		state->or.depth = 18;
		WARN_ON(1);
		break;
	}
}

static const struct nvkm_head_func
gv100_head = {
	.state = gv100_head_state,
	.rgpos = gv100_head_rgpos,
	.rgclk = gf119_head_rgclk,
	.vblank_get = gv100_head_vblank_get,
	.vblank_put = gv100_head_vblank_put,
};

int
gv100_head_new(struct nvkm_disp *disp, int id)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	if (!(nvkm_rd32(device, 0x610060) & (0x00000001 << id)))
		return 0;
	return nvkm_head_new_(&gv100_head, disp, id);
}

int
gv100_head_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = nvkm_rd32(device, 0x610060) & 0x000000ff;
	return nvkm_rd32(device, 0x610074) & 0x0000000f;
}
