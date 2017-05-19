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
nv50_sor_output_func = {
};

int
nv50_sor_output_new(struct nvkm_disp *disp, int index,
		    struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_new_(&nv50_sor_output_func, disp,
				index, dcbE, poutp);
}

int
nv50_sor_power(NV50_DISP_MTHD_V1)
{
	struct nvkm_device *device = disp->base.engine.subdev.device;
	union {
		struct nv50_disp_sor_pwr_v0 v0;
	} *args = data;
	const u32 soff = outp->or * 0x800;
	u32 stat;
	int ret = -ENOSYS;

	nvif_ioctl(object, "disp sor pwr size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "disp sor pwr vers %d state %d\n",
			   args->v0.version, args->v0.state);
		stat = !!args->v0.state;
	} else
		return ret;


	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c004 + soff) & 0x80000000))
			break;
	);
	nvkm_mask(device, 0x61c004 + soff, 0x80000001, 0x80000000 | stat);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c004 + soff) & 0x80000000))
			break;
	);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c030 + soff) & 0x10000000))
			break;
	);
	return 0;
}

void
nv50_sor_state(struct nvkm_ior *sor, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 coff = sor->id * 8 + (state == &sor->arm) * 4;
	u32 ctrl = nvkm_rd32(device, 0x610b70 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = LVDS; state->link = 1; break;
	case 1: state->proto = TMDS; state->link = 1; break;
	case 2: state->proto = TMDS; state->link = 2; break;
	case 5: state->proto = TMDS; state->link = 3; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x00000003;
}

static const struct nvkm_ior_func
nv50_sor = {
	.state = nv50_sor_state,
};

int
nv50_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_sor, disp, SOR, id);
}
