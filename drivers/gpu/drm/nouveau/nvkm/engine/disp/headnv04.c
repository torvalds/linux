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

int
nv04_head_new(struct nvkm_disp *disp, int id)
{
	return nvkm_head_new_(&nv04_head, disp, id);
}
