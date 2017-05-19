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
#include "dp.h"

#include <subdev/i2c.h>
#include <subdev/timer.h>

/******************************************************************************
 * TMDS
 *****************************************************************************/
static const struct nvkm_output_func
nv50_pior_output_func = {
};

int
nv50_pior_output_new(struct nvkm_disp *disp, int index,
		     struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_new_(&nv50_pior_output_func, disp,
				index, dcbE, poutp);
}

/******************************************************************************
 * DisplayPort
 *****************************************************************************/
static int
nv50_pior_dp_links(struct nvkm_ior *pior, struct nvkm_i2c_aux *aux)
{
	int ret = nvkm_i2c_aux_lnk_ctl(aux, pior->dp.nr, pior->dp.bw,
					    pior->dp.ef);
	if (ret)
		return ret;
	return 1;
}

static const struct nvkm_output_dp_func
nv50_pior_output_dp_func = {
};

int
nv50_pior_dp_new(struct nvkm_disp *disp, int index, struct dcb_output *dcbE,
		 struct nvkm_output **poutp)
{
	return nvkm_output_dp_new_(&nv50_pior_output_dp_func, disp,
				   index, dcbE, poutp);
}

static void
nv50_pior_power_wait(struct nvkm_device *device, u32 poff)
{
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61e004 + poff) & 0x80000000))
			break;
	);
}

static void
nv50_pior_power(struct nvkm_ior *pior, bool normal, bool pu,
	       bool data, bool vsync, bool hsync)
{
	struct nvkm_device *device = pior->disp->engine.subdev.device;
	const u32  poff = nv50_ior_base(pior);
	const u32 shift = normal ? 0 : 16;
	const u32 state = 0x80000000 | (0x00000001 * !!pu) << shift;
	const u32 field = 0x80000000 | (0x00000101 << shift);

	nv50_pior_power_wait(device, poff);
	nvkm_mask(device, 0x61e004 + poff, field, state);
	nv50_pior_power_wait(device, poff);
}

static void
nv50_pior_state(struct nvkm_ior *pior, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = pior->disp->engine.subdev.device;
	const u32 coff = pior->id * 8 + (state == &pior->arm) * 4;
	u32 ctrl = nvkm_rd32(device, 0x610b80 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	state->rgdiv = 1;
	switch (state->proto_evo) {
	case 0: state->proto = TMDS; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x00000003;
}

static const struct nvkm_ior_func
nv50_pior = {
	.state = nv50_pior_state,
	.power = nv50_pior_power,
	.dp = {
		.links = nv50_pior_dp_links,
	},
};

int
nv50_pior_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_pior, disp, PIOR, id);
}
