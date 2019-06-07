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
#include "head.h"

#include <subdev/i2c.h>
#include <subdev/timer.h>

static void
nv50_pior_clock(struct nvkm_ior *pior)
{
	struct nvkm_device *device = pior->disp->engine.subdev.device;
	const u32 poff = nv50_ior_base(pior);
	nvkm_mask(device, 0x614380 + poff, 0x00000707, 0x00000001);
}

static int
nv50_pior_dp_links(struct nvkm_ior *pior, struct nvkm_i2c_aux *aux)
{
	int ret = nvkm_i2c_aux_lnk_ctl(aux, pior->dp.nr, pior->dp.bw,
					    pior->dp.ef);
	if (ret)
		return ret;
	return 1;
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

void
nv50_pior_depth(struct nvkm_ior *ior, struct nvkm_ior_state *state, u32 ctrl)
{
	/* GF119 moves this information to per-head methods, which is
	 * a lot more convenient, and where our shared code expect it.
	 */
	if (state->head && state == &ior->asy) {
		struct nvkm_head *head =
			nvkm_head_find(ior->disp, __ffs(state->head));
		if (!WARN_ON(!head)) {
			struct nvkm_head_state *state = &head->asy;
			switch ((ctrl & 0x000f0000) >> 16) {
			case 6: state->or.depth = 30; break;
			case 5: state->or.depth = 24; break;
			case 2: state->or.depth = 18; break;
			case 0: state->or.depth = 18; break; /*XXX*/
			default:
				state->or.depth = 18;
				WARN_ON(1);
				break;
			}
		}
	}
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
	nv50_pior_depth(pior, state, ctrl);
}

static const struct nvkm_ior_func
nv50_pior = {
	.state = nv50_pior_state,
	.power = nv50_pior_power,
	.clock = nv50_pior_clock,
	.dp = {
		.links = nv50_pior_dp_links,
	},
};

int
nv50_pior_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&nv50_pior, disp, PIOR, id);
}

int
nv50_pior_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = (nvkm_rd32(device, 0x610184) & 0x70000000) >> 28;
	return 3;
}
