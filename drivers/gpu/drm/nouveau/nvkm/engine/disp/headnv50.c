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
nv50_head_vblank_put(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x61002c, (4 << head->id), 0);
}

static void
nv50_head_vblank_get(struct nvkm_head *head)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x61002c, (4 << head->id), (4 << head->id));
}

static void
nv50_head_rgclk(struct nvkm_head *head, int div)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	nvkm_mask(device, 0x614200 + (head->id * 0x800), 0x0000000f, div);
}

void
nv50_head_rgpos(struct nvkm_head *head, u16 *hline, u16 *vline)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x800;
	/* vline read locks hline. */
	*vline = nvkm_rd32(device, 0x616340 + hoff) & 0x0000ffff;
	*hline = nvkm_rd32(device, 0x616344 + hoff) & 0x0000ffff;
}

static void
nv50_head_state(struct nvkm_head *head, struct nvkm_head_state *state)
{
	struct nvkm_device *device = head->disp->engine.subdev.device;
	const u32 hoff = head->id * 0x540 + (state == &head->arm) * 4;
	u32 data;

	data = nvkm_rd32(device, 0x610ae8 + hoff);
	state->vblanke = (data & 0xffff0000) >> 16;
	state->hblanke = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x610af0 + hoff);
	state->vblanks = (data & 0xffff0000) >> 16;
	state->hblanks = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x610af8 + hoff);
	state->vtotal = (data & 0xffff0000) >> 16;
	state->htotal = (data & 0x0000ffff);
	data = nvkm_rd32(device, 0x610b00 + hoff);
	state->vsynce = (data & 0xffff0000) >> 16;
	state->hsynce = (data & 0x0000ffff);
	state->hz = (nvkm_rd32(device, 0x610ad0 + hoff) & 0x003fffff) * 1000;
}

static const struct nvkm_head_func
nv50_head = {
	.state = nv50_head_state,
	.rgpos = nv50_head_rgpos,
	.rgclk = nv50_head_rgclk,
	.vblank_get = nv50_head_vblank_get,
	.vblank_put = nv50_head_vblank_put,
};

int
nv50_head_new(struct nvkm_disp *disp, int id)
{
	return nvkm_head_new_(&nv50_head, disp, id);
}
