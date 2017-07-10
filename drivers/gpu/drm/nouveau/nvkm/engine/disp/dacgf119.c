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
 */
#include "ior.h"

static void
gf119_dac_clock(struct nvkm_ior *dac)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 doff = nv50_ior_base(dac);
	nvkm_mask(device, 0x612280 + doff, 0x07070707, 0x00000000);
}

static void
gf119_dac_state(struct nvkm_ior *dac, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 coff = (state == &dac->asy) * 0x20000 + dac->id * 0x20;
	u32 ctrl = nvkm_rd32(device, 0x640180 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = CRT; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x0000000f;
}

static const struct nvkm_ior_func
gf119_dac = {
	.state = gf119_dac_state,
	.power = nv50_dac_power,
	.sense = nv50_dac_sense,
	.clock = gf119_dac_clock,
};

int
gf119_dac_new(struct nvkm_disp *disp, int id)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	if (!(nvkm_rd32(device, 0x612004) & (0x00000010 << id)))
		return 0;
	return nvkm_ior_new_(&gf119_dac, disp, DAC, id);
}
