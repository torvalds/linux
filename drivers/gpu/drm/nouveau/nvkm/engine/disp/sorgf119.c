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

void
gf119_sor_dp_vcpi(struct nvkm_output_dp *outp, int head, u8 slot,
		  u8 slot_nr, u16 pbn, u16 aligned)
{
	struct nvkm_device *device = outp->base.disp->engine.subdev.device;
	const u32 hoff = head * 0x800;

	nvkm_mask(device, 0x616588 + hoff, 0x00003f3f, (slot_nr << 8) | slot);
	nvkm_mask(device, 0x61658c + hoff, 0xffffffff, (aligned << 16) | pbn);
}

void
gf119_sor_dp_drive(struct nvkm_ior *sor, int ln, int pc, int dc, int pe, int pu)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32  loff = nv50_sor_link(sor);
	const u32 shift = sor->func->dp.lanes[ln] * 8;
	u32 data[4];

	data[0] = nvkm_rd32(device, 0x61c118 + loff) & ~(0x000000ff << shift);
	data[1] = nvkm_rd32(device, 0x61c120 + loff) & ~(0x000000ff << shift);
	data[2] = nvkm_rd32(device, 0x61c130 + loff);
	if ((data[2] & 0x0000ff00) < (pu << 8) || ln == 0)
		data[2] = (data[2] & ~0x0000ff00) | (pu << 8);
	nvkm_wr32(device, 0x61c118 + loff, data[0] | (dc << shift));
	nvkm_wr32(device, 0x61c120 + loff, data[1] | (pe << shift));
	nvkm_wr32(device, 0x61c130 + loff, data[2]);
	data[3] = nvkm_rd32(device, 0x61c13c + loff) & ~(0x000000ff << shift);
	nvkm_wr32(device, 0x61c13c + loff, data[3] | (pc << shift));
}

void
gf119_sor_dp_pattern(struct nvkm_ior *sor, int pattern)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	nvkm_mask(device, 0x61c110 + soff, 0x0f0f0f0f, 0x01010101 * pattern);
}

int
gf119_sor_dp_links(struct nvkm_ior *sor, struct nvkm_i2c_aux *aux)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 soff = nv50_ior_base(sor);
	const u32 loff = nv50_sor_link(sor);
	u32 dpctrl = 0x00000000;
	u32 clksor = 0x00000000;

	clksor |= sor->dp.bw << 18;
	dpctrl |= ((1 << sor->dp.nr) - 1) << 16;
	if (sor->dp.mst)
		dpctrl |= 0x40000000;
	if (sor->dp.ef)
		dpctrl |= 0x00004000;

	nvkm_mask(device, 0x612300 + soff, 0x007c0000, clksor);
	nvkm_mask(device, 0x61c10c + loff, 0x401f4000, dpctrl);
	return 0;
}

static const struct nvkm_output_dp_func
gf119_sor_dp_func = {
	.vcpi = gf119_sor_dp_vcpi,
};

int
gf119_sor_dp_new(struct nvkm_disp *disp, int index,
		 struct dcb_output *dcbE, struct nvkm_output **poutp)
{
	return nvkm_output_dp_new_(&gf119_sor_dp_func, disp, index, dcbE, poutp);
}

void
gf119_sor_state(struct nvkm_ior *sor, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 coff = (state == &sor->asy) * 0x20000 + sor->id * 0x20;
	u32 ctrl = nvkm_rd32(device, 0x640200 + coff);

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

	state->head = ctrl & 0x0000000f;
}

static const struct nvkm_ior_func
gf119_sor = {
	.state = gf119_sor_state,
	.power = nv50_sor_power,
	.hdmi = {
		.ctrl = gf119_hdmi_ctrl,
	},
	.dp = {
		.lanes = { 2, 1, 0, 3 },
		.links = gf119_sor_dp_links,
		.power = g94_sor_dp_power,
		.pattern = gf119_sor_dp_pattern,
	},
};

int
gf119_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gf119_sor, disp, SOR, id);
}
