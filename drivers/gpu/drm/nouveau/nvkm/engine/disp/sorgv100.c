/*
 * Copyright 2018 Red Hat Inc.
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

#include <subdev/timer.h>

void
gv100_sor_dp_watermark(struct nvkm_ior *sor, int head, u8 watermark)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;
	nvkm_mask(device, 0x616550 + hoff, 0x0c00003f, 0x08000000 | watermark);
}

void
gv100_sor_dp_audio_sym(struct nvkm_ior *sor, int head, u16 h, u32 v)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = head * 0x800;
	nvkm_mask(device, 0x616568 + hoff, 0x0000ffff, h);
	nvkm_mask(device, 0x61656c + hoff, 0x00ffffff, v);
}

void
gv100_sor_dp_audio(struct nvkm_ior *sor, int head, bool enable)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 hoff = 0x800 * head;
	const u32 data = 0x80000000 | (0x00000001 * enable);
	const u32 mask = 0x8000000d;
	nvkm_mask(device, 0x616560 + hoff, mask, data);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x616560 + hoff) & 0x80000000))
			break;
	);
}

void
gv100_sor_state(struct nvkm_ior *sor, struct nvkm_ior_state *state)
{
	struct nvkm_device *device = sor->disp->engine.subdev.device;
	const u32 coff = (state == &sor->arm) * 0x8000 + sor->id * 0x20;
	u32 ctrl = nvkm_rd32(device, 0x680300 + coff);

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

	state->head = ctrl & 0x000000ff;
}

static const struct nvkm_ior_func
gv100_sor = {
	.route = {
		.get = gm200_sor_route_get,
		.set = gm200_sor_route_set,
	},
	.state = gv100_sor_state,
	.power = nv50_sor_power,
	.clock = gf119_sor_clock,
	.hdmi = {
		.ctrl = gv100_hdmi_ctrl,
		.scdc = gm200_hdmi_scdc,
	},
	.dp = {
		.lanes = { 0, 1, 2, 3 },
		.links = gf119_sor_dp_links,
		.power = g94_sor_dp_power,
		.pattern = gm107_sor_dp_pattern,
		.drive = gm200_sor_dp_drive,
		.audio = gv100_sor_dp_audio,
		.audio_sym = gv100_sor_dp_audio_sym,
		.watermark = gv100_sor_dp_watermark,
	},
	.hda = {
		.hpd = gf119_hda_hpd,
		.eld = gf119_hda_eld,
		.device_entry = gf119_hda_device_entry,
	},
};

int
gv100_sor_new(struct nvkm_disp *disp, int id)
{
	return nvkm_ior_new_(&gv100_sor, disp, SOR, id);
}

int
gv100_sor_cnt(struct nvkm_disp *disp, unsigned long *pmask)
{
	struct nvkm_device *device = disp->engine.subdev.device;
	*pmask = (nvkm_rd32(device, 0x610060) & 0x0000ff00) >> 8;
	return (nvkm_rd32(device, 0x610074) & 0x00000f00) >> 8;
}
