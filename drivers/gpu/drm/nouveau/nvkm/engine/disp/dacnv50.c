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

#include <subdev/timer.h>

static void
nv50_dac_clock(struct nvkm_ior *dac)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 doff = nv50_ior_base(dac);
	nvkm_mask(device, 0x614280 + doff, 0x07070707, 0x00000000);
}

int
nv50_dac_sense(struct nvkm_ior *dac, u32 loadval)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 doff = nv50_ior_base(dac);

	dac->func->power(dac, false, true, false, false, false);

	nvkm_wr32(device, 0x61a00c + doff, 0x00100000 | loadval);
	mdelay(9);
	udelay(500);
	loadval = nvkm_mask(device, 0x61a00c + doff, 0xffffffff, 0x00000000);

	dac->func->power(dac, false, false, false, false, false);
	if (!(loadval & 0x80000000))
		return -ETIMEDOUT;

	return (loadval & 0x38000000) >> 27;
}

static void
nv50_dac_power_wait(struct nvkm_device *device, const u32 doff)
{
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);
}

void
nv50_dac_power(struct nvkm_ior *dac, bool normal, bool pu,
	       bool data, bool vsync, bool hsync)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32  doff = nv50_ior_base(dac);
	const u32 shift = normal ? 0 : 16;
	const u32 state = 0x80000000 | (0x00000040 * !    pu |
					0x00000010 * !  data |
					0x00000004 * ! vsync |
					0x00000001 * ! hsync) << shift;
	const u32 field = 0xc0000000 | (0x00000055 << shift);

	nv50_dac_power_wait(device, doff);
	nvkm_mask(device, 0x61a004 + doff, field, state);
	nv50_dac_power_wait(device, doff);
}

static void
nv50_dac_state(struct nvkm_ior *dac, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = dac->disp->engine.subdev.device;
	const u32 coff = dac->id * 8 + (state == &dac->arm) * 4;
	u32 ctrl = nvkm_rd32(device, 0x610b58 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = CRT; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x00000003;
}

static const struct nvkm_ior_func
nv50_dac = {
	.state = nv50_dac_state,
	.power = nv50_dac_power,
	.sense = nv50_dac_sense,
	.clock = nv50_dac_clock,
};

int
nv50_dac_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_dac, disp, DAC, id);
}

int
nv50_dac_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = (nvkm_rd32(device, 0x610184) & 0x00700000) >> 20;
	return 3;
}
