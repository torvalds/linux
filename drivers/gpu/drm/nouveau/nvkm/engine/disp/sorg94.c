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

void
g94_sor_dp_watermark(struct nvkm_ior *sor, int head, u8 watermark)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 loff = nv50_sor_link(sor);
	nvkm_mask(device, 0x61c128 + loff, 0x0000003f, watermark);
}

void
g94_sor_dp_activesym(struct nvkm_ior *sor, int head,
		     u8 TU, u8 VTUa, u8 VTUf, u8 VTUi)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 loff = nv50_sor_link(sor);
	nvkm_mask(device, 0x61c10c + loff, 0x000001fc, TU << 2);
	nvkm_mask(device, 0x61c128 + loff, 0x010f7f00, VTUa << 24 |
						       VTUf << 16 |
						       VTUi << 8);
}

void
g94_sor_dp_audio_sym(struct nvkm_ior *sor, int head, u16 h, u32 v)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	nvkm_mask(device, 0x61c1e8 + soff, 0x0000ffff, h);
	nvkm_mask(device, 0x61c1ec + soff, 0x00ffffff, v);
}

void
g94_sor_dp_drive(struct nvkm_ior *sor, int ln, int pc, int dc, int pe, int pu)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32  loff = nv50_sor_link(sor);
	const u32 shift = sor->func->dp.lanes[ln] * 8;
	u32 data[3];

	data[0] = nvkm_rd32(device, 0x61c118 + loff) & ~(0x000000ff << shift);
	data[1] = nvkm_rd32(device, 0x61c120 + loff) & ~(0x000000ff << shift);
	data[2] = nvkm_rd32(device, 0x61c130 + loff);
	if ((data[2] & 0x0000ff00) < (pu << 8) || ln == 0)
		data[2] = (data[2] & ~0x0000ff00) | (pu << 8);
	nvkm_wr32(device, 0x61c118 + loff, data[0] | (dc << shift));
	nvkm_wr32(device, 0x61c120 + loff, data[1] | (pe << shift));
	nvkm_wr32(device, 0x61c130 + loff, data[2]);
}

void
g94_sor_dp_pattern(struct nvkm_ior *sor, int pattern)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 loff = nv50_sor_link(sor);
	nvkm_mask(device, 0x61c10c + loff, 0x0f000000, pattern << 24);
}

void
g94_sor_dp_power(struct nvkm_ior *sor, int nr)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	const u32 loff = nv50_sor_link(sor);
	u32 mask = 0, i;

	for (i = 0; i < nr; i++)
		mask |= 1 << sor->func->dp.lanes[i];

	nvkm_mask(device, 0x61c130 + loff, 0x0000000f, mask);
	nvkm_mask(device, 0x61c034 + soff, 0x80000000, 0x80000000);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x61c034 + soff) & 0x80000000))
			break;
	);
}

int
g94_sor_dp_links(struct nvkm_ior *sor, struct nvkm_i2c_aux *aux)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	const u32 loff = nv50_sor_link(sor);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;

	dpctrl |= ((1 << sor->dp.nr) - 1) << 16;
	if (sor->dp.ef)
		dpctrl |= 0x00004000;
	if (sor->dp.bw > 0x06)
		clksor |= 0x00040000;

	nvkm_mask(device, 0x614300 + soff, 0x000c0000, clksor);
	nvkm_mask(device, 0x61c10c + loff, 0x001f4000, dpctrl);
	return 0;
}

static bool
g94_sor_war_needed(struct nvkm_ior *sor)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	if (sor->asy.proto == TMDS) {
		switch (nvkm_rd32(device, 0x614300 + soff) & 0x00030000) {
		case 0x00000000:
		case 0x00030000:
			return true;
		default:
			break;
		}
	}
	return false;
}

static void
g94_sor_war_update_sppll1(struct nvkm_disp *disp)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	struct nvkm_ior *ior;
	bool used = false;
	u32 clksor;

	list_for_each_entry(ior, &disp->ior, head) {
		if (ior->type != SOR)
			continue;

		clksor = nvkm_rd32(device, 0x614300 + nv50_ior_base(ior));
		switch (clksor & 0x03000000) {
		case 0x02000000:
		case 0x03000000:
			used = true;
			break;
		default:
			break;
		}
	}

	if (used)
		return;

	nvkm_mask(device, 0x00e840, 0x80000000, 0x00000000);
}

static void
g94_sor_war_3(struct nvkm_ior *sor)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	u32 sorpwr;

	if (!g94_sor_war_needed(sor))
		return;

	sorpwr = nvkm_rd32(device, 0x61c004 + soff);
	if (sorpwr & 0x00000001) {
		u32 seqctl = nvkm_rd32(device, 0x61c030 + soff);
		u32  pd_pc = (seqctl & 0x00000f00) >> 8;
		u32  pu_pc =  seqctl & 0x0000000f;

		nvkm_wr32(device, 0x61c040 + soff + pd_pc * 4, 0x1f008000);

		nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x61c030 + soff) & 0x10000000))
				break;
		);
		nvkm_mask(device, 0x61c004 + soff, 0x80000001, 0x80000000);
		nvkm_msec(device, 2000,
			if (!(nvkm_rd32(device, 0x61c030 + soff) & 0x10000000))
				break;
		);

		nvkm_wr32(device, 0x61c040 + soff + pd_pc * 4, 0x00002000);
		nvkm_wr32(device, 0x61c040 + soff + pu_pc * 4, 0x1f000000);
	}

	nvkm_mask(device, 0x61c10c + soff, 0x00000001, 0x00000000);
	nvkm_mask(device, 0x614300 + soff, 0x03000000, 0x00000000);

	if (sorpwr & 0x00000001) {
		nvkm_mask(device, 0x61c004 + soff, 0x80000001, 0x80000001);
	}

	g94_sor_war_update_sppll1(sor->disp);
}

static void
g94_sor_war_2(struct nvkm_ior *sor)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);

	if (!g94_sor_war_needed(sor))
		return;

	nvkm_mask(device, 0x00e840, 0x80000000, 0x80000000);
	nvkm_mask(device, 0x614300 + soff, 0x03000000, 0x03000000);
	nvkm_mask(device, 0x61c10c + soff, 0x00000001, 0x00000001);

	nvkm_mask(device, 0x61c00c + soff, 0x0f000000, 0x00000000);
	nvkm_mask(device, 0x61c008 + soff, 0xff000000, 0x14000000);
	nvkm_usec(device, 400, NVKM_DELAY);
	nvkm_mask(device, 0x61c008 + soff, 0xff000000, 0x00000000);
	nvkm_mask(device, 0x61c00c + soff, 0x0f000000, 0x01000000);

	if (nvkm_rd32(device, 0x61c004 + soff) & 0x00000001) {
		u32 seqctl = nvkm_rd32(device, 0x61c030 + soff);
		u32  pu_pc = seqctl & 0x0000000f;
		nvkm_wr32(device, 0x61c040 + soff + pu_pc * 4, 0x1f008000);
	}
}

void
g94_sor_state(struct nvkm_ior *sor, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 coff = sor->id * 8 + (state == &sor->arm) * 4;
	u32 ctrl = nvkm_rd32(device, 0x610794 + coff);

	state->proto_evo = (ctrl & 0x00000f00) >> 8;
	switch (state->proto_evo) {
	case 0: state->proto = LVDS; state->link = 1; break;
	case 1: state->proto = TMDS; state->link = 1; break;
	case 2: state->proto = TMDS; state->link = 2; break;
	case 5: state->proto = TMDS; state->link = 3; break;
	case 8: state->proto =   DP; state->link = 1; break;
	case 9: state->proto =   DP; state->link = 2; break;
	default:
		state->proto = UNKNOWN;
		break;
	}

	state->head = ctrl & 0x00000003;
	nv50_pior_depth(sor, state, ctrl);
}

static const struct nvkm_ior_func
g94_sor = {
	.state = g94_sor_state,
	.power = nv50_sor_power,
	.clock = nv50_sor_clock,
	.war_2 = g94_sor_war_2,
	.war_3 = g94_sor_war_3,
	.dp = {
		.lanes = { 2, 1, 0, 3},
		.links = g94_sor_dp_links,
		.power = g94_sor_dp_power,
		.pattern = g94_sor_dp_pattern,
		.drive = g94_sor_dp_drive,
		.audio_sym = g94_sor_dp_audio_sym,
		.activesym = g94_sor_dp_activesym,
		.watermark = g94_sor_dp_watermark,
	},
};

int
g94_sor_new(struct nvkm_disp *disp, int id)
{
	return nv50_sor_new_(&g94_sor, disp, id);
}
