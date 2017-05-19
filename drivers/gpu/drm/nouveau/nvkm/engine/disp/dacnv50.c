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
#include "nv50.h"
#include "outp.h"

#include <core/client.h>
#include <subdev/timer.h>

#include <nvif/cl5070.h>
#include <nvif/unpack.h>

static const struct nvkm_output_func
nv50_dac_output_func = {
};

int
nv50_dac_output_new(struct nvkm_disp *disp, int index,
		    struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_new_(&nv50_dac_output_func, disp,
				index, dcbE, poutp);
}

int
nv50_dac_sense(NV50_DISP_MTHD_V1)
{
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	union {
		struct nv50_disp_dac_load_v0 v0;
	} *args = data;
	const u32 doff = outp->or * 0x800;
	u32 loadval;
	int ret = -ENOSYS;

	nvif_ioctl(object, "disp dac load size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "disp dac load vers %d data %08x\n",
			   args->v0.version, args->v0.data);
		if (args->v0.data & 0xfff00000)
			return -EINVAL;
		loadval = args->v0.data;
	} else
		return ret;

	nvkm_mask(device, 0x61a004 + doff, 0x807f0000, 0x80150000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);

	nvkm_wr32(device, 0x61a00c + doff, 0x00100000 | loadval);
	mdelay(9);
	udelay(500);
	loadval = nvkm_mask(device, 0x61a00c + doff, 0xffffffff, 0x00000000);

	nvkm_mask(device, 0x61a004 + doff, 0x807f0000, 0x80550000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61a004 + doff) & 0x80000000))
			break;
	);

	nvkm_debug(subdev, "DAC%d sense: %08x\n", outp->or, loadval);
	if (!(loadval & 0x80000000))
		return -ETIMEDOUT;

	args->v0.load = (loadval & 0x38000000) >> 27;
	return 0;
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
};

int
nv50_dac_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_dac, disp, DAC, id);
}
